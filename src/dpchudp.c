/* DPCHAOS.C - Chaos link implementation (over Ethernet or using UDP)
*/
/*  Copyright © 2005, 2018 Björn Victor and Kenneth L. Harrenstien
**  All Rights Reserved
**
**  This file may become part of the KLH10 Distribution.  Use, modification, and
**  re-distribution is permitted subject to the terms in the file
**  named "LICENSE", which contains the full text of the legal notices
**  and should always accompany this Distribution.
**
**  This software is provided "AS IS" with NO WARRANTY OF ANY KIND.
**
**  This notice (including the copyright and warranty disclaimer)
**  must be included in all copies or derivations of this software.
*/
/* This is based on DPIMP.C, to some extent.
   Some things are possibly irrelevant inheritage from dpimp, and  could be cleaned up... */
/*
This is a program intended to be run as a child of the KLH10
PDP-10 emulator, in order to provide a Chaosnet link-layer implementation.

It can do that in two ways:

Firstly, by implementing Chaosnet over Ethernet (protocol nr 0x0804) and handling
ARP for that protocol (cf RFC 826). This uses one of the packet filtering
implementations (currently only pcap) provided by osdnet. (Only Ethernet II
headers are supported, not 802.3.)
No routing is handled, that's done by ITS - this code uses the
Chaosnet trailer provided by ITS to decide where to send pkts. 
(On Ethernet, no Chaosnet trailer is included.)

Secondly, it can do it by using a Chaos-over-UDP tunnel.
Given a Chaos packet and a mapping between Chaosnet and IP addresses,
it simply sends the packet encapsulated in a UDP datagram. The packet is
sent in LSB first order, rather than network (MSB first) order.

The protocol is very similar to the original Chaosnet link protocol,
with the addition of a four-byte protocol header:

	| V  | F  | A1 | A2 |
where
	V is the CHUDP protocol version (currently 1)
	F is the function code (initially only CHUDP_PKT)
	A1, A2 are arguments for the function

For F = CHUDP_PKT, A1 and A2 are zero (ignored), followed by the Chaos packet,
and a trailer
	| D1 | D2 | S1 | S2 |
	| C1 | C2 |
where D1,D2 are the destination Chaos address (subnet, address)
      S1,S2         source
      C1,C2 are the two bytes of Chaosnet checksum
            (for the Chaos packet + trailer)
            *note* that this is (currently) the standard Internet
            checksum, not the original Chaosnet checksum

	"Messages" to and from the CH11 are sent over the standard DP
shared memory mechanism; these are composed of 8-bit bytes in exactly
the same order as if a CH11 was sending or receiving them.  Packets to
and from the NET are Ethernet or CHUDP packets.

	There are actually two processes active, one which pumps data
from the NET to the CH11-output buffer, and another that pumps in the
opposite direction from an input buffer to the NET.  Generally they
are completely independent.
*/
// TODO (some day):
// Make sure short construction by | is from known network order, otherwise use casting.
// Try to check ARP best practices (e.g retrans intervals?)

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>

#include <arpa/inet.h>

#include "klh10.h"	/* Get config params */

/* This must precede any other OSD includes to ensure that DECOSF gets
   the right flavor sockaddr (sigh)
*/
#define OSN_USE_IPONLY 0	/* Need more than IP stuff */ 
#include "osdnet.h"		/* OSD net defs, shared with DPNI20 and DPIMP */

#include <sys/resource.h>	/* For setpriority() */
#include <sys/mman.h>		/* For mlockall() */

#include "dpchudp.h"	/* DPCHAOS specific defs, grabs DPSUP if needed */


/* Globals */

struct dp_s dp;			/* Device-Process struct for DP ops */

int cpupid;			/* PID of superior CPU process */
int chpid;			/* PID of child (R proc) */
int swstatus = TRUE;
int chudpsock;			/* UDP socket */
struct osnpf npf;
struct pfdata pfdata;		/* Packet-Filter state */

int myaddr;			/* My chaos address */
struct in_addr ihost_ip;	/* My host IP addr, net order */


#define ARP_PKTSIZ (sizeof(struct ether_header)	+ sizeof(struct ether_arp))
static u_char eth_brd[ETHER_ADDR_LEN] = {255,255,255,255,255,255};

/* Debug flag reference.  Use DBGFLG within functions that have "dpchaos";
 * all others must use DP_DBGFLG.  Both refer to the same location.
 * Caching a local copy of the debug flag is a no-no because it lives
 * in a shared memory location that may change at any time.
 */
#define DBGFLG (dpchaos->dpchaos_dpc.dpc_debug)
#define DP_DBGFLG (((struct dpchaos_s *)dp.dp_adr)->dpchaos_dpc.dpc_debug)

/* Local predeclarations */

void chaostohost(struct dpchaos_s *);
void hosttochaos(struct dpchaos_s *);

void net_init(struct dpchaos_s *);
void dumppkt(unsigned char *, int);
void dumppkt_raw(unsigned char *ucp, int cnt);

void ihl_hhsend(register struct dpchaos_s *dpchaos, int cnt, register unsigned char *pp);
void ip_write(struct in_addr *ipa, in_port_t ipport, unsigned char *buf, int len, struct dpchaos_s *dpchaos);
int hi_iproute(struct in_addr *ipa, in_port_t *ipport, unsigned char *lp, int cnt, struct dpchaos_s *dpchaos);

char *ch_adrsprint(char *cp, unsigned char *ca);
void send_chaos_arp_reply(u_short dest_chaddr, u_char *dest_eth);
void send_chaos_arp_request(u_short chaddr);
void send_chaos_packet(unsigned char *ea, unsigned char *buf, int cnt);
u_char *find_arp_entry(u_short daddr);
void print_arp_table(void);


/* Error and diagnostic output */

static const char progname_i[] = "dpchaos";
static const char progname_r[] = "dpchaos-R";
static const char progname_w[] = "dpchaos-W";
static const char *progname = progname_i;

static void efatal(int num, char *fmt, ...)
{
    fprintf(stderr, "\r\n[%s: Fatal error: ", progname);
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fputs("]\r\n", stderr);

    /* DP automatically kills any child as well. */
    dp_exit(&dp, num);
}

static void esfatal(int num, char *fmt, ...)
{
    fprintf(stderr, "\r\n[%s: ", progname);
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fprintf(stderr, " - %s]\r\n", dp_strerror(errno));

    /* DP automatically kills any child as well. */
    dp_exit(&dp, num);
}

static void dbprint(char *fmt, ...)
{
    fprintf(stderr, "[%s: ", progname);
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fputs("]", stderr);
}

static void dbprintln(char *fmt, ...)
{
    fprintf(stderr, "[%s: ", progname);
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fputs("]\r\n", stderr);
}

static void error(char *fmt, ...)
{
    fprintf(stderr, "\r\n[%s: ", progname);
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fputs("]\r\n", stderr);
}

static void syserr(int num, char *fmt, ...)
{
    fprintf(stderr, "\r\n[%s: ", progname);
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fprintf(stderr, " - %s]\r\n", dp_strerror(num));
}

int initdebug = 0;

void
htons_buf(u_short *ibuf, u_short *obuf, int len)
{
  int i;
  for (i = 0; i < len; i += 2)
    *obuf++ = htons(*ibuf++);
}
void
ntohs_buf(u_short *ibuf, u_short *obuf, int len)
{
  int i;
  for (i = 0; i < len; i += 2)
    *obuf++ = ntohs(*ibuf++);
}

int
main(int argc, char **argv)
{
    register struct dpchaos_s *dpchaos;	/* Ptr to shared memory area */

    /* Search for a "-debug" command-line argument so that we can start
       debug output ASAP if necessary.
    */
    if (argc > 1) {
	int i;
	for (i = 1; i < argc; ++i) {
	    if (strcmp(argv[i], "-debug") == 0) {
		initdebug = TRUE;
		break;
	    }
	}
    }
    if (initdebug)
	dbprint("Starting");

    /* Right off the bat attempt to get the highest scheduling priority
    ** we can, since a slow response will cause the 10 monitor to declare
    ** the interface dead.
    */
#if CENV_SYS_SOLARIS || CENV_SYS_DECOSF || CENV_SYS_XBSD || CENV_SYS_LINUX
    if (setpriority(PRIO_PROCESS, 0, -20) < 0)
	syserr(errno, "Warning - cannot set high priority");
#elif CENV_SYS_UNIX		/* Try old generic Unix call */
    if (nice(-20) == -1)
	syserr(errno, "Warning - cannot set high priority");
#else
    error("Warning - cannot set high priority");
#endif

    /* Next priority is to quickly close the vulnerability window;
       disable TTY cruft to ensure that any TTY hacking done by superior
       process doesn't inadvertently kill us off.
    */
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    if (initdebug)
	dbprint("Started");

    /* General initialization */
    if (!dp_main(&dp, argc, argv)) {
	efatal(1, "DP init failed!");
    }
    dpchaos = (struct dpchaos_s *)dp.dp_adr;	/* Make for easier refs */

    /* Verify that the structure version is compatible */
    if (dpchaos->dpchaos_ver != DPCHAOS_VERSION) {
	efatal(1, "Wrong version of DPCHAOS: ch11=%0lx dpchaos=%0lx",
	       (long)dpchaos->dpchaos_ver, (long)DPCHAOS_VERSION);
    }

    /* Now can access DP args!
       From here on we can use DBGFLG, which is actually a shared
       memory reference that dpchaos points to.  Check here to accomodate the
       case where it's not already set but "-debug" was given as a command
       arg; leave it alone if already set since the exact bits have
       significance.
    */
    if (initdebug && !DBGFLG)
	DBGFLG = 1;
    if (DBGFLG)
	dbprint("DP inited");

    /* Always attempt to lock memory since the DP processes are fairly
    ** small, must respond quickly, and SU mode is more or less guaranteed.
    ** Skip it only if dp_main() already did it for us.
    */
#if CENV_SYS_DECOSF || CENV_SYS_SOLARIS || CENV_SYS_LINUX
    if (!(dpchaos->dpchaos_dpc.dpc_flags & DPCF_MEMLOCK)) {
	if (mlockall(MCL_CURRENT|MCL_FUTURE) != 0) {
	    dbprintln("Warning - cannot lock memory");
	}
    }
#endif

    /* Now set up legacy variables based on parameters passed through
       shared DP area.
    */
    if ((myaddr = dpchaos->dpchaos_myaddr) == 0) /* My chaos address */
	efatal(1, "no CHAOS address specified");

    /* Initialize various network info */
    net_init(dpchaos);

    /* Make this a status (rather than debug) printout? */
    if (swstatus) {
      int i;
      char ipbuf[OSN_IPSTRSIZ];

      dbprintln("ifc \"%s\" => chaos %lo",
		dpchaos->dpchaos_ifnam, (long)myaddr);
      for (i = 0; i < dpchaos->dpchaos_chip_tlen; i++)
	dbprintln(" chaos %6o => ip %s:%d",
		  dpchaos->dpchaos_chip_tbl[i].dpchaos_chip_chaddr,
		  ip_adrsprint(ipbuf,
			       (unsigned char *)&dpchaos->dpchaos_chip_tbl[i].dpchaos_chip_ipaddr),
		  dpchaos->dpchaos_chip_tbl[i].dpchaos_chip_ipport);
    }

    /* Now start up a child process to handle input */
    if (DBGFLG)
	dbprint("Forking R process");
    if ((chpid = fork()) < 0)
	esfatal(1, "fork failed");
    if (chpid == 0) {
	/* Child process.
	** Child inherits signal handlers, which is what we want here.
	*/
	/* Fix up xfer mechanism so ACK of DP input goes to correct proc */
	dp.dp_adr->dpc_frdp.dpx_donpid = getpid();

	/* And ensure its memory is locked too, since the lockage isn't
	** inherited over a fork().  Don't bother warning if it fails.
	*/
#if CENV_SYS_DECOSF || CENV_SYS_SOLARIS || CENV_SYS_LINUX
	(void) mlockall(MCL_CURRENT|MCL_FUTURE);
#endif
	progname = progname_r;	/* Reset progname to indicate identity */
	chaostohost(dpchaos);	/* Child process handles input */
    }
    progname = progname_w;	/* Reset progname to indicate identity */

    hosttochaos(dpchaos);		/* Parent process handles output */

    return 1;			/* Never returns, but placate compiler */
}

/* NET_INIT - Initialize net-related variables,
**	given network interface we'll use.
*/
struct ifent *osn_iflookup(char *ifnam);

void net_init_pf(struct dpchaos_s *dpchaos)
{
  // based on dpni20.c
  /* Set up packet filter.  This also returns in "ihost_ea"
     the ethernet address for the selected interface.
  */

  npf.osnpf_ifnam = dpchaos->dpchaos_ifnam;
  npf.osnpf_ifmeth = dpchaos->dpchaos_ifmeth;
  npf.osnpf_dedic = dpchaos->dpchaos_dedic;
  //npf.osnpf_rdtmo = dpchaos->dpchaos_rdtmo;
  npf.osnpf_backlog = dpchaos->dpchaos_backlog;
  //npf.osnpf_ip.ia_addr = ehost_ip;
  //npf.osnpf_tun.ia_addr = tun_ip;


  // Tell dvch11 at what offset in the shared buffer our data is.
  dpchaos->dpchaos_inoff = sizeof(struct ether_header);
  dpchaos->dpchaos_outoff = dpchaos->dpchaos_inoff; // DPCHUDP_DATAOFFSET


  (void)osn_iftab_init();

  // finding the interface (and its ethernet address) is necessary for the more clever packet filter,
  // which isn't enabled for now, so...
  if (npf.osnpf_ifnam == NULL || npf.osnpf_ifnam[0] == '\0') {
    // requires ip4 address, but better than nothing?
    struct ifent *ife = osn_ipdefault();
    if (ife && (strlen(ife->ife_name) < sizeof(dpchaos->dpchaos_ifnam))) {
      dbprintln("Using default interface '%s'", ife->ife_name);
      strcpy(dpchaos->dpchaos_ifnam, ife->ife_name);
    } else
      if (DBGFLG) dbprintln("Can't find default interface.");
  } else
    if (DBGFLG) dbprintln("Interface '%s' specified.", npf.osnpf_ifnam);

  /* Ether addr is both a potential arg and a returned value;
     the packetfilter open may use and/or change it.
  */
  if ((memcmp(dpchaos->dpchaos_eth,"\0\0\0\0\0\0", ETHER_ADDR_LEN) == 0) && npf.osnpf_ifnam[0] != '\0') {
    // we need the ea for the pf, so find it already here
    /* Now get our interface's ethernet address. */
    if (osn_ifealookup(npf.osnpf_ifnam, (unsigned char *) &npf.osnpf_ea) == 0) {
      struct ifent *ife = (struct ifent *)osn_iflookup(npf.osnpf_ifnam);
      dbprintln("Can't find EA for \"%s\"", npf.osnpf_ifnam);
      if (ife) {
	dbprintln("Found interface '%s', ea %s", ife->ife_name,
		  (ife->ife_ea != NULL && memcmp(ife->ife_ea,"\0\0\0\0\0\0", ETHER_ADDR_LEN) == 0) ? "found" : "not found");
      } else
	dbprintln("Can't find interface '%s'!", npf.osnpf_ifnam);
    } else {
      if (DP_DBGFLG) dbprintln("Found EA for \"%s\"", npf.osnpf_ifnam);
    }
  } else
    ea_set(&npf.osnpf_ea, dpchaos->dpchaos_eth);	/* Set requested ea if any */
  if (DP_DBGFLG) {
    char eastr[OSN_EASTRSIZ];

    dbprintln("EN addr for \"%s\" = %s",
	      npf.osnpf_ifnam, eth_adrsprint(eastr, (unsigned char *)&npf.osnpf_ea));
  }
  ea_set(dpchaos->dpchaos_eth, (char *)&npf.osnpf_ea);	/* Copy ether addr (so pfbuild can use it)*/
  // pfdata, osnpf, pfarg; pfbuild(pfarg, ehost_ip)
  osn_pfinit(&pfdata, &npf, (void *)dpchaos);	/* Will abort if fails */
  ea_set(dpchaos->dpchaos_eth, &npf.osnpf_ea);		/* Copy actual ea */

  /* Now set any return info values in shared struct.
   */
#if 0 // already done above
  ea_set(dpchaos->dpchaos_eth, (char *)&ihost_ea);	/* Copy ether addr */
#endif
}

void net_init_chudp(struct dpchaos_s *dpchaos)
{
  struct sockaddr_in mysin;

  // Tell dvch11 at what offset in the shared buffer our data is.
  dpchaos->dpchaos_inoff = DPCHAOS_CHUDP_DATAOFFSET;
  dpchaos->dpchaos_outoff = dpchaos->dpchaos_inoff; // DPCHAOS_CHUDP_DATAOFFSET

  if ((chudpsock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    efatal(1,"socket failed: %s", dp_strerror(errno));

  mysin.sin_family = AF_INET;
  mysin.sin_port = htons(dpchaos->dpchaos_port);
  mysin.sin_addr.s_addr = INADDR_ANY;
#if 0
  if (dpchaos->dpchaos_ifnam[0] && ihost_ip.s_addr != 0)
    mysin.sin_addr.s_addr = ihost_ip.s_addr;
#endif

  if (bind(chudpsock, (struct sockaddr *)&mysin, sizeof(mysin)) < 0)
    efatal(1,"bind failed: %s", dp_strerror(errno));
}

void
net_init(register struct dpchaos_s *dpchaos)
{
    struct ifreq ifr;

    /* Ensure network device name, if specified, isn't too long */
    if (dpchaos->dpchaos_ifnam[0] && (strlen(dpchaos->dpchaos_ifnam)
		>= sizeof(ifr.ifr_name))) {
	esfatal(0, "interface name \"%s\" too long - max %d",
		dpchaos->dpchaos_ifnam, (int)sizeof(ifr.ifr_name));
    }

    /* Set up appropriate net fd.
    */
    if (dpchaos->dpchaos_ifmeth_chudp) {
      // CHUDP case
      net_init_chudp(dpchaos);
    } else {
      // Ethernet case
      net_init_pf(dpchaos);
    }
}

/* CHAOSTOHOST - Child-process main loop for pumping packets from CHAOS to HOST.
**	Reads packets from net and feeds CHAOS packets to DP superior process.
*/

// #define NINBUFSIZ (DPCHUDP_DATAOFFSET+MAXETHERLEN)

/* Duplicated from dvch11.c - should go in dpsup.c */
static unsigned int
ch_checksum(const unsigned char *addr, int count)
{
  /* RFC1071 */
  /* Compute Internet Checksum for "count" bytes
   *         beginning at location "addr".
   */
  register long sum = 0;

  while( count > 1 )  {
    /*  This is the inner loop */
    sum += *(addr)<<8 | *(addr+1);
    addr += 2;
    count -= 2;
  }

  /*  Add left-over byte, if any */
  if( count > 0 )
    sum += * (unsigned char *) addr;

  /*  Fold 32-bit sum to 16 bits */
  while (sum>>16)
    sum = (sum & 0xffff) + (sum >> 16);

  return (~sum) & 0xffff;
}

void describe_eth_pkt_hdr(unsigned char *buffp)
{
  char ethstr1[OSN_EASTRSIZ],ethstr2[OSN_EASTRSIZ];
  dbprintln("Ether header:\r\n"
	    " dest %s\r\n"
	    " src  %s\r\n"
	    " prot 0x%04x",
	    eth_adrsprint(ethstr1, buffp), eth_adrsprint(ethstr2, &buffp[ETHER_ADDR_LEN]),
	    (buffp[ETHER_ADDR_LEN*2] << 8) | buffp[ETHER_ADDR_LEN*2+1]);
}

void describe_arp_pkt(struct arphdr *arp, unsigned char *buffp) 
{
  char ethstr[OSN_EASTRSIZ];
  char prostr[OSN_IPSTRSIZ];

  dbprintln("ARP message, protocol 0x%04x (%s)",
	    ntohs(arp->ar_pro), (arp->ar_pro == htons(ETHERTYPE_IP) ? "IPv4" :
				 (arp->ar_pro == htons(ETHERTYPE_CHAOS) ? "Chaos" : "?")));
  dbprintln(" HW addr len %d\r\n Proto addr len %d\r\n ARP command %d (%s)",
	    arp->ar_hln, arp->ar_pln, ntohs(arp->ar_op),
	    arp->ar_op == htons(ARPOP_REQUEST) ? "Request" :
	    (arp->ar_op == htons(ARPOP_REPLY) ? "Reply" :
	     (arp->ar_op == htons(ARPOP_RREQUEST) ? "Reverse request" :
	      (arp->ar_op == htons(ARPOP_RREPLY) ? "Reverse reply" : "?"))));
  dbprintln(" src: HW %s proto %s", eth_adrsprint(ethstr, &buffp[sizeof(struct arphdr)]),
	    (arp->ar_pro == htons(ETHERTYPE_IP) ?
	     ip_adrsprint(prostr, &buffp[sizeof(struct arphdr)+arp->ar_hln]) :
	     (arp->ar_pro == htons(ETHERTYPE_CHAOS) ?
	      ch_adrsprint(prostr, &buffp[sizeof(struct arphdr)+arp->ar_hln]) :
	      "?whatevs?")));
  dbprintln(" dst: HW %s proto %s", eth_adrsprint(ethstr, &buffp[sizeof(struct arphdr)+arp->ar_hln+arp->ar_pln]),
	    (arp->ar_pro == htons(ETHERTYPE_IP) ?
	     ip_adrsprint(prostr, &buffp[sizeof(struct arphdr)+arp->ar_hln+arp->ar_pln+arp->ar_hln]) :
	     (arp->ar_pro == htons(ETHERTYPE_CHAOS) ?
	      ch_adrsprint(prostr, &buffp[sizeof(struct arphdr)+arp->ar_hln+arp->ar_pln+arp->ar_hln]) :
	      "?whatevs?")));
}

void maybe_add_to_arp(struct dpchaos_s *dpchaos, u_short cha, u_char *eth) 
{
  int i, found = 0;
  for (i = 0; i < dpchaos->charp_len && i < CHARP_MAX; i++)
    if (dpchaos->charp_list[i].charp_chaddr == cha) {
      found = 1;
      dpchaos->charp_list[i].charp_age = time(NULL);  // update age
      if (memcmp(&dpchaos->charp_list[i].charp_eaddr, eth, ETHER_ADDR_LEN) != 0) {
	memcpy(&dpchaos->charp_list[i].charp_eaddr, eth, ETHER_ADDR_LEN);
	if (DBGFLG & 4) {
	  dbprintln("ARP: Changed MAC addr for %#o", cha);
	  // print_arp_table();
	}
      } else
	if (DBGFLG & 4) {
	  dbprintln("ARP: Updated age for %#o", cha);
	  // print_arp_table();
	}
      break;
    }
  /* It's not in the list already, is there room? */
  // @@@@ should reuse outdated entries
  if (!found) {
    if (dpchaos->charp_len < CHARP_MAX) {
      if (DBGFLG & 4) dbprintln("ARP: Adding new entry for Chaos %#o", cha);
      dpchaos->charp_list[dpchaos->charp_len].charp_chaddr = cha;
      dpchaos->charp_list[dpchaos->charp_len].charp_age = time(NULL);
      memcpy(&dpchaos->charp_list[dpchaos->charp_len].charp_eaddr, eth, ETHER_ADDR_LEN);
      dpchaos->charp_len++;
      if (DBGFLG & 4) print_arp_table();
    } else {
      dbprintln("ARP: table full! Please increase size from %d and/or implement GC", CHARP_MAX);
    }
  }
}

// handle an ARP pkt
void chaostohost_pf_arp(struct dpchaos_s *dpchaos, unsigned char *buffp, int cnt)
{
  struct arphdr *arp = (struct arphdr *)buffp;
  if (DBGFLG & 4) describe_arp_pkt(arp, buffp);

  if (arp->ar_pro != htons(ETHERTYPE_CHAOS)) {
    if (DBGFLG)
      dbprintln("Unexpected ARP protocol %#x received", ntohs(arp->ar_pro));
    return;
  }

  u_short schad = ntohs((buffp[sizeof(struct arphdr)+arp->ar_hln]<<8) |
			buffp[sizeof(struct arphdr)+arp->ar_hln+1]);
  u_char *sead = &buffp[sizeof(struct arphdr)];
  u_short dchad =  ntohs((buffp[sizeof(struct arphdr)+arp->ar_hln+arp->ar_hln+arp->ar_pln]<<8) |
			 buffp[sizeof(struct arphdr)+arp->ar_hln+arp->ar_hln+arp->ar_pln+1]);

  // don't create a storm
  if (memcmp(sead, eth_brd, ETHER_ADDR_LEN) == 0)
    return;

  if (arp->ar_op == htons(ARPOP_REQUEST)) {
    if (dchad == dpchaos->dpchaos_myaddr) {
      if (DBGFLG & 4) dbprintln("ARP: Sending reply for %#o (me) to %#o", dchad, schad);
      send_chaos_arp_reply(schad, sead); /* Yep. */
    }
    //return; // no, maybe update table first, also for requests
  }
  /* Now see if we should add this to our Chaos ARP list */
  maybe_add_to_arp(dpchaos, schad, sead);
}

void chaostohost_pf_ether(struct dpchaos_s *dpchaos, unsigned char *buffp, int cnt) 
{

  u_char *pp = buffp+sizeof(struct ether_header);
  int pcnt = cnt - sizeof(struct ether_header);
  if ((pcnt - CHAOS_HEADERSIZE) < 0) {
    // not even a Chaosnet header was read
    if (DBGFLG)
      dbprintln("Less than a Chaos header read!");
    return;
  }
  // Chaos data length
  int ccnt = ((pp[3] & 0xf) << 4) | pp[2];  /* un-swapped */

  if (DBGFLG & 1) {
    dbprintln("Read Chaos pkt len %d (incl EH), offset %d, %d (pkt len), %d (Chaos data len)",
	      cnt, dpchaos->dpchaos_inoff, pcnt, ccnt);
  }
  if (pcnt != (CHAOS_HEADERSIZE + ccnt + (ccnt%2) + CHAOS_HW_TRAILERSIZE)) {
    // @@@@ I still don't know why this happens, but it does - pcap packets are often longer
    if (DBGFLG & 0x2) {
      dbprintln("Expected %d+%d+%d = %d bytes, got %d",
		CHAOS_HEADERSIZE, ccnt+(ccnt%2), CHAOS_HW_TRAILERSIZE,
		CHAOS_HEADERSIZE + ccnt+(ccnt%2) + CHAOS_HW_TRAILERSIZE,
		pcnt);
      dumppkt_raw(buffp, cnt);
    }

  }
  if (pcnt < (CHAOS_HEADERSIZE + ccnt)) {
    dbprintln("Incomplete Chaosnet packet read: %d but expected at least %d+%d=%d (ignoring pkt)",
	      pcnt, CHAOS_HEADERSIZE, ccnt, CHAOS_HEADERSIZE+ccnt);
    return;
  }
      
  // ITS expects (and delivers) the Chaosnet hardware trailer (destination, source, checksum).
  // On Ethernet, this is not needed since it is replaced by the
  // Ethernet packet data (e.g. Ether addresses, CRC.)
  // (LambdaDelta and real Symbolics hardware do not include the Chaosnet trailer
  // when sending Chaosnet pkts on Ethernet.)

  // Even if ITS doesn't really use the contents of the trailer, it
  // needs to be there, so why not fill it in as best as we can.

  // Clear last byte (and add to length) before adding trailer (cf CHSRC6 in ITS).
  if ((ccnt % 2) == 1) {
    if (DBGFLG & 1)
      dbprintln("Odd Chaos data len %d, rounding up and zeroing extra byte", ccnt);
    // @@@@ make sure this zeros the correct byte - this is OK on little-endian
    buffp[sizeof(struct ether_header)+CHAOS_HEADERSIZE+ccnt] = '\0';
  }
  if (DBGFLG & 1)
    dbprintln("Clearing/re-creating hw trailer at index %d, resetting size to %d",
	      (sizeof(struct ether_header))+CHAOS_HEADERSIZE+ccnt+(ccnt%2),
	      (CHAOS_HEADERSIZE + ccnt+(ccnt%2) + CHAOS_HW_TRAILERSIZE) + sizeof(struct ether_header));
  memset(buffp+(sizeof(struct ether_header))+CHAOS_HEADERSIZE+ccnt+(ccnt%2), 0, CHAOS_HW_TRAILERSIZE);
  // reset sizes
  cnt = (CHAOS_HEADERSIZE + ccnt+(ccnt%2) + CHAOS_HW_TRAILERSIZE) + sizeof(struct ether_header);
  pcnt = (CHAOS_HEADERSIZE + ccnt+(ccnt%2) + CHAOS_HW_TRAILERSIZE);

  // Construct a trailer. Could find the actual hw sender from the Ethernet sender + ARP table,
  // but ITS doesn't care.
  u_short *hddest = (u_short *)&pp[4];
  u_short *hdsrc = (u_short *)&pp[8];
  u_short *hdd = (u_short *)&pp[pcnt-CHAOS_HW_TRAILERSIZE]; *hdd = *hddest;
  u_short *hds = (u_short *)&pp[pcnt-CHAOS_HW_TRAILERSIZE+2]; *hds = *hdsrc;
      
  u_short cks = ch_checksum(pp,pcnt-2);
  u_short *hdc = (u_short *)&pp[pcnt-CHAOS_HW_TRAILERSIZE+4]; *hdc = htons(cks);

  // byte swap to net order
  htons_buf((u_short *)pp, (u_short *)pp, pcnt);

  if (DBGFLG & 0x2) {
    dumppkt(buffp,cnt);
  }
  // "assume we're already in shared buffer" (so buffp arg is not used)
  ihl_hhsend(dpchaos, cnt, buffp);

  return;
}

void chaostohost_pf(struct dpchaos_s *dpchaos, unsigned char *buffp)
{
  register int cnt;
  // @@@@ check destination address (mine or broadcast)
  // @@@@ in case not using pcap_compile, and interface is promisc 

  /* OK, now do a blocking read on packetfilter input! */
  cnt = osn_pfread(&pfdata, buffp, DPCHAOS_MAXLEN);

  if (DBGFLG)
    dbprintln("Read=%d", cnt);

  if (cnt == 0)
    return;

  if (((struct ether_header *)buffp)->ether_type == htons(ETHERTYPE_ARP)) {
    if (DBGFLG & 4)
      dbprintln("Got ARP");
    chaostohost_pf_arp(dpchaos, buffp+(sizeof(struct ether_header)), cnt-sizeof(struct ether_header));

    return;
  } else if (((struct ether_header *)buffp)->ether_type == htons(ETHERTYPE_CHAOS)) {
    if (cnt <= sizeof(sizeof(struct ether_header))) {
      // Not even an Ethernet header read
#if 0
      /* If call timed out, should return 0 */
      if (cnt == 0 && dpchaos->dpchaos_rdtmo)
	return;		/* Just try again */
#endif
      if (DBGFLG)
	dbprintln("ERead=%d, Err=%d", cnt, errno);

      if (cnt >= 0) {
	dbprintln("Eread = %d, %s", cnt,
		  (cnt > 0) ? "no ether data" : "no packet");
	return;
      }

      /* System call error of some kind */
      if (errno == EINTR)		/* Ignore spurious signals */
	return;

      syserr(errno, "Eread = %d, errno %d", cnt, errno);
      return;		/* For now... */
    }
    // At least an ethernet header was read!
    chaostohost_pf_ether(dpchaos, buffp, cnt);
  } else {
    if (DBGFLG) {
      dbprintln("Unexpected ether type %#x", ntohs(((struct ether_header *)buffp)->ether_type));
      (void) dumppkt_raw(buffp, (cnt > 8*8) ? 8*8 : cnt);
    }
  }
}

void chaostohost_chudp(struct dpchaos_s *dpchaos, unsigned char *buffp)
{
  struct sockaddr_in ip_sender;
  socklen_t iplen;
  register int cnt;

  /* OK, now do a blocking read on UDP socket! */
  errno = 0;		/* Clear to make sure it's the actual error */
  memset(&ip_sender, 0, sizeof(ip_sender));
  iplen = sizeof(ip_sender); /* Supply size of ip_sender, and get actual stored length */
  cnt = recvfrom(chudpsock, buffp, DPCHAOS_MAXLEN, 0, (struct sockaddr *)&ip_sender, &iplen);
  // buff now has a CHUDP pkt
  if (cnt <= DPCHAOS_CHUDP_DATAOFFSET) {
    if (DBGFLG)
      fprintf(stderr, "[dpchaos-R: ERead=%d, Err=%d]\r\n",
	      cnt, errno);

    if (cnt < 0 && (errno == EINTR))	/* Ignore spurious signals */
      return;

    /* Error of some kind */
    fprintf(stderr, "[dpchaos-R: Eread = %d, ", cnt);
    if (cnt < 0) {
      fprintf(stderr, "errno %d = %s]\r\n",
	      errno, dp_strerror(errno));
    } else if (cnt > 0)
      fprintf(stderr, "no chudp data]\r\n");
    else fprintf(stderr, "no packet]\r\n");

    return;		/* For now... */
  }
  if (DBGFLG) {
    if (DBGFLG & 2) {
      fprintf(stderr, "\r\n[dpchaos-R: Read=%d\r\n", cnt);
      dumppkt(buffp, cnt);
      fprintf(stderr, "]");
    }
    else
      fprintf(stderr, "[dpchaos-R: Read=%d]", cnt);
  }

  /* Have packet, now dispatch it to host */

  if (((struct chudp_header *)buffp)->chudp_version != CHUDP_VERSION) {
    if (DBGFLG) 
      error("wrong protocol version %d",
	    ((struct chudp_header *)buffp)->chudp_version);
    return;
  }
  switch (((struct chudp_header *)buffp)->chudp_function) {
  case CHUDP_PKT:
    break;		/* deliver it */
  default:
    error("Unknown CHUDP function: %0X",
	  ((struct chudp_header *)buffp)->chudp_function);
    return;
  }

  /* Check who it's from, update Chaos/IP mapping */
  {
    /* check that the packet is complete:
       4 bytes CHUDP header, Chaos header, add Chaos pkt length, plus 6 bytes trailer */
    int chalen = ((buffp[DPCHAOS_CHUDP_DATAOFFSET+2] & 0xf)<<4) | buffp[DPCHAOS_CHUDP_DATAOFFSET+3];
    int datalen = (DPCHAOS_CHUDP_DATAOFFSET + CHAOS_HEADERSIZE + chalen);
    int chafrom = (buffp[cnt-4]<<8) | buffp[cnt-3];
    char *ip = inet_ntoa(ip_sender.sin_addr);
    in_port_t port = ntohs(ip_sender.sin_port);
    time_t now = time(NULL);
    int i, cks;
    if ((cks = ch_checksum(buffp+DPCHAOS_CHUDP_DATAOFFSET,cnt-DPCHAOS_CHUDP_DATAOFFSET)) != 0) {
      if (1 || DBGFLG)
	dbprintln("Bad checksum 0x%x",cks);
      /* #### must free buffer first(?) */
      /* 	    return; */
    }
    if (cnt < datalen + CHAOS_HW_TRAILERSIZE) {	/* may have a byte of padding */
      if (1 || DBGFLG) {
	dbprintln("Rcvd bad length: %d. < %d. (expected), chaos data %d bytes, errno %d",
		  cnt, datalen + CHAOS_HW_TRAILERSIZE, chalen, errno);
	dumppkt(buffp, cnt);
      }
      /* #### must free buffer first(?) */
      /* 	    return; */
    }
    if (dpchaos->dpchaos_ifmeth_chudp) {
#if 0
      if (DBGFLG) {
	dbprintln("Rcv from chaos %o = ip %s port %d., %d. bytes (datalen %d)",
		  chafrom,
		  ip, port,
		  cnt, datalen);
	dumppkt(buffp,cnt);
      }
#endif
      if ((cks == 0) && (chafrom != myaddr) && (dpchaos->dpchaos_chip_tlen < DPCHAOS_CHIP_MAX)) {
	/* Space available, see if we need to add this */
	/* Look for old entries: if one is found which is either
	   static (from config file) or sufficiently fresh,
	   keep it and update the freshness (if not static) */
	for (i = 0; i < dpchaos->dpchaos_chip_tlen; i++)
	  if (chafrom == dpchaos->dpchaos_chip_tbl[i].dpchaos_chip_chaddr) {
	    if (dpchaos->dpchaos_chip_tbl[i].dpchaos_chip_lastrcvd != 0) {
	      if (now - dpchaos->dpchaos_chip_tbl[i].dpchaos_chip_lastrcvd >
		  DPCHAOS_CHIP_DYNAMIC_AGE_LIMIT) {
		/* Old, update it (in case he moved) */
		if (1 || DBGFLG)
		  dbprintln("Updating CHIP entry %d for %o/%s:%d",
			    i, chafrom, ip, port);
		dpchaos->dpchaos_chip_tbl[i].dpchaos_chip_ipport = port;
		memcpy(&dpchaos->dpchaos_chip_tbl[i].dpchaos_chip_ipaddr, &ip_sender.sin_addr, IP_ADRSIZ);
	      }
	      /* update timestamp */
	      dpchaos->dpchaos_chip_tbl[i].dpchaos_chip_lastrcvd = now;
	    }
	    chafrom = -chafrom;	/* Note it shouldn't be added */
	    break;
	  }
	if (chafrom > 0) {
	  /* It's OK to write here, the other fork will see it when tlen is updated */
	  i = dpchaos->dpchaos_chip_tlen;
	  if (1 || DBGFLG)
	    dbprintln("Adding CHIP entry %d for %o/%s:%d",
		      i, chafrom, ip, port);
	  dpchaos->dpchaos_chip_tbl[i].dpchaos_chip_chaddr = chafrom;
	  dpchaos->dpchaos_chip_tbl[i].dpchaos_chip_ipport = port;
	  dpchaos->dpchaos_chip_tbl[i].dpchaos_chip_lastrcvd = now;
	  memcpy(&dpchaos->dpchaos_chip_tbl[i].dpchaos_chip_ipaddr, &ip_sender.sin_addr, IP_ADRSIZ);
	  dpchaos->dpchaos_chip_tlen++;
	}
#if 0
	else if (0 && DBGFLG) {
	  dbprintln("Already know chaos %o",chafrom);
	}
#endif
      }
      else if ((dpchaos->dpchaos_chip_tlen >= DPCHAOS_CHIP_MAX) && DBGFLG) {
	dbprintln("CHIP table full - cannot add %o", chafrom);
      }
    }
  }

  ihl_hhsend(dpchaos, cnt, buffp);
}


void
chaostohost(register struct dpchaos_s *dpchaos)
{
    register struct dpx_s *dpx = dp_dpxfr(&dp);
    unsigned char *inibuf;
    unsigned char *buffp;
    size_t max;

    inibuf = dp_xsbuff(dpx, &max);	/* Get initial buffer ptr */

    /* Tell KLH10 we're initialized and ready by sending initial packet */
    dp_xswait(dpx);			/* Wait until buff free, in case */
    dp_xsend(dpx, DPCHAOS_INIT, 0);	/* Send INIT */

    if (DBGFLG)
	fprintf(stderr, "[dpchaos-R: sent INIT]\r\n");

    for (;;) {
	/* Make sure that buffer is free before clobbering it */
	dp_xswait(dpx);			/* Wait until buff free */

	if (DBGFLG)
	    fprintf(stderr, "[dpchaos-R: InWait]\r\n");

	/* Set up buffer and initialize offsets */
	buffp = inibuf;

	if (!dpchaos->dpchaos_ifmeth_chudp) {
	  // non-CHUDP case (Ethernet)
	  chaostohost_pf(dpchaos, buffp);
	} else { // CHUDP case
	  chaostohost_chudp(dpchaos, buffp);
	}
    }
}


/* Send regular message from CHAOS to HOST.
*/

void
ihl_hhsend(register struct dpchaos_s *dpchaos,
	   int cnt,
	   register unsigned char *pp)
	/* "pp" is packet data ptr, has room for header preceding */
{

    /* Send up to host!  Assume we're already in shared buffer. */

    register struct dpx_s *dpx = dp_dpxfr(&dp);

    dp_xsend(dpx, DPCHAOS_RPKT, cnt-dpchaos->dpchaos_inoff);

    if (DBGFLG)
      fprintf(stderr, "[dpchaos-R: sent RPKT %d+%d]", dpchaos->dpchaos_inoff, cnt-dpchaos->dpchaos_inoff);

}

/* HOSTTOCHAOS - Parent main loop for pumping packets from HOST to CHAOS.
**	Reads CHAOS message from DP superior
**	and interprets it.  If a regular message, bundles it up and
**	outputs to NET.
*/
void
hosttochaos(register struct dpchaos_s *dpchaos)
{
    register struct dpx_s *dpx = dp_dpxto(&dp);	/* Get ptr to "To-DP" dpx */
    register unsigned char *buff;
    size_t max;
    register int rcnt;
    unsigned char *inibuf;
    struct in_addr ipdest;
    in_port_t ipport;
    struct chudp_header chuhdr = { CHUDP_VERSION, CHUDP_PKT, 0, 0 };

    inibuf = dp_xrbuff(dpx, &max);	/* Get initial buffer ptr */

    if (DBGFLG)
	fprintf(stderr, "[dpchaos-W: Starting loop]\r\n");

    for (;;) {
	if (DBGFLG)
	    fprintf(stderr, "[dpchaos-W: CmdWait]\r\n");

	/* Wait until 10 has a command for us */
	dp_xrwait(dpx);		/* Wait until something there */

	/* Process command from 10! */
	switch (dp_xrcmd(dpx)) {

	default:
	    fprintf(stderr, "[dpchaos: Unknown cmd %d]\r\n", dp_xrcmd(dpx));
	    dp_xrdone(dpx);
	    continue;

	case DPCHAOS_SPKT:			/* Send regular packet */
	    rcnt = dp_xrcnt(dpx);
	    /* does rcnt include chudp header size? yes (the output offset) */
	    buff = inibuf;
	    if (DBGFLG) {
		if (DBGFLG & 0x2) {
		    fprintf(stderr, "\r\n[dpchaos-W: Sending %d\r\n", rcnt);
		    dumppkt(buff, rcnt);
		    fprintf(stderr, "]");
		}
		else
		    fprintf(stderr, "[dpchaos-W: SPKT %d]", rcnt);
	    }
	    break;
	}

	/* Come here to handle output packet */
	int chlen = ((buff[dpchaos->dpchaos_outoff+2] & 0xf) << 4) | buff[dpchaos->dpchaos_outoff+3]; // DPCHAOS_CHUDP_DATAOFFSET

	if ((chlen+(chlen%2) + CHAOS_HEADERSIZE + dpchaos->dpchaos_outoff + CHAOS_HW_TRAILERSIZE) > rcnt) {
	  if (1 || DBGFLG)
	    dbprintln("NOT sending less than packet: pkt len %d, expected %d (Chaos data len %d)",
		      rcnt, (chlen + CHAOS_HEADERSIZE + dpchaos->dpchaos_outoff + CHAOS_HW_TRAILERSIZE),
		      chlen);
	  dp_xrdone(dpx);	/* ack it to dvch11, but */
	  continue;		/* don't send it! */
	}

	if (!dpchaos->dpchaos_ifmeth_chudp) {
	  // Ethernet case
	  u_char *ch = &buff[dpchaos->dpchaos_outoff]; // DPCHAOS_CHUDP_DATAOFFSET
	  // length excluding trailer
	  u_short cpklen = dpchaos->dpchaos_outoff+CHAOS_HEADERSIZE+chlen+(chlen%2);

	  // Use hardware dest, which is based on ITS routing table.
	  // But remove trailer before sending on Ethernet.
	  u_short *hdchad = (u_short *)&buff[cpklen]; 
	  u_short dchad = htons(*hdchad);
	  if (DBGFLG & 2)
	    dbprintln("Found dest addr %#o (%#x) at offset %d+%d+%d+%d = %d",
		      dchad, dchad, dpchaos->dpchaos_outoff, CHAOS_HEADERSIZE, chlen, chlen%2,
		      cpklen);
	  if (dchad == 0) {		/* broadcast */
	    if (DBGFLG & 02) dbprintln("Broadcasting on ether");
	    send_chaos_packet((u_char *)&eth_brd, &buff[dpchaos->dpchaos_outoff], cpklen-dpchaos->dpchaos_outoff);
	  } else if (dchad == myaddr) {
	    if (DBGFLG & 02) dbprintln("Send to self ignored (ITS config bug?)");
	  } else {
	    u_char *eaddr = find_arp_entry(dchad);
	    if (eaddr != NULL) {
	      if (DBGFLG & 02) dbprintln("Sending on ether to %#o", dchad);
	      send_chaos_packet(eaddr, &buff[dpchaos->dpchaos_outoff], cpklen-dpchaos->dpchaos_outoff);
	    } else {
	      if (DBGFLG) dbprintln("Don't know %#o, sending ARP request", dchad);
	      if (DBGFLG & 02) {
		dumppkt(buff,rcnt);
	      }
	      send_chaos_arp_request(dchad);
	      // Chaos sender will retransmit, surely.
	    }
	  }
	} else {
	  // CHUDP case
	  memcpy(buff, (void *)&chuhdr, sizeof(chuhdr)); /* put in CHUDP hdr */
	  /* find IP destination given chaos packet  */
	  if (hi_iproute(&ipdest, &ipport, &buff[dpchaos->dpchaos_outoff], rcnt - dpchaos->dpchaos_outoff, dpchaos)) {
	    ip_write(&ipdest, ipport, buff, rcnt, dpchaos);
	  }
	}

	/* Command done, tell 10 we're done with it */
	if (DBGFLG)
	  fprintf(stderr, "[dpchaos-W: CmdDone]");

	dp_xrdone(dpx);
    }
}

int
hi_lookup(struct dpchaos_s *dpchaos,
	  struct in_addr *ipa,	/* Dest IP addr to be put here */
	  in_port_t *ipport,	/* dest IP port to be put here */
	  unsigned int chdest, /* look up this chaos destination */
	  int sbnmatch)		/* just match subnet part */
{
  int i;
  for (i = 0; i < dpchaos->dpchaos_chip_tlen; i++) 
    if (
	((chdest == dpchaos->dpchaos_chip_tbl[i].dpchaos_chip_chaddr) ||
	 /* handle subnet matching */
	 (sbnmatch && ((chdest >> 8) == (dpchaos->dpchaos_chip_tbl[i].dpchaos_chip_chaddr >> 8))))
	&& dpchaos->dpchaos_chip_tbl[i].dpchaos_chip_ipaddr.s_addr != 0) {
      memcpy(ipport, &dpchaos->dpchaos_chip_tbl[i].dpchaos_chip_ipport, sizeof(in_port_t));
      memcpy((void *)ipa, &dpchaos->dpchaos_chip_tbl[i].dpchaos_chip_ipaddr,
	     sizeof(struct in_addr));
      if (DBGFLG) {
	char ipbuf[OSN_IPSTRSIZ];
	dbprintln("found Chaos address %o at %s:%d",
		  chdest, ip_adrsprint(ipbuf, (unsigned char *)ipa), *ipport);
      }
      return TRUE;
    }
  if (DBGFLG)
    dbprintln("failed looking up Chaos address %o",chdest);

  return FALSE;
}

/* HI_IPROUTE - Determine where to actually send Chaos packet.
** Fill in ipa with IP address, based on Chaos trailer.
*/

int
hi_iproute(struct in_addr *ipa,	/* Dest IP addr to be put here */
	   in_port_t *ipport,	/* dest IP port to be put here */
	   unsigned char *lp,	/* Ptr to start of Chaos packet */
	   int cnt,		/* Cnt of data including header */
	   struct dpchaos_s *dpchaos)
{
    unsigned int chdest;
    unsigned int hwdest;

    if (cnt < CHAOS_HEADERSIZE) {
	error("Chaos packet too short: %d", cnt);
	return FALSE;
    }

    /* Derive destination IP address from Chaos header */
    chdest = lp[DPCHAOS_CH_DESTOFF]<<8 | lp[DPCHAOS_CH_DESTOFF+1];
    /* Use hw trailer - ITS knows routing and uses the trailer to tell us where to send it */
    hwdest = lp[cnt-6] << 8 | lp[cnt-5];
    if (DBGFLG)
      dbprintln("looking for route to chaos %o (hw %o) (FC %d.)", chdest, hwdest,
		lp[DPCHAOS_CH_FC] >> 4);
    if (hi_lookup(dpchaos, ipa, ipport, chdest, 0)
	/* try subnet match */
	|| ((chdest != hwdest) && hi_lookup(dpchaos, ipa, ipport, hwdest, 0)) // ITS knows the route
	|| hi_lookup(dpchaos, ipa, ipport, chdest, 1)		// maybe ITS thinks it's directly connected
	|| ((chdest != hwdest) && hi_lookup(dpchaos, ipa, ipport, hwdest, 1)) // last resort
	) {
      return TRUE;
    }
    return FALSE;
}


/* IP_WRITE - Send IP packet out over UDP
*/

void
ip_write(struct in_addr *ipa, in_port_t ipport, unsigned char *buf, int len, struct dpchaos_s *dpchaos)
{
  struct sockaddr_in sin;

  if (DBGFLG) {
    dbprintln("sending to port %d",ipport);
  }
  sin.sin_family = AF_INET;
  sin.sin_port = htons(ipport);
  memcpy((void *)&sin.sin_addr, ipa, sizeof(struct in_addr));

  if (sendto(chudpsock, buf, len, 0, (struct sockaddr *) &sin, (socklen_t) (sizeof(sin))) != len) {
    error("sendto failed - %s", dp_strerror(errno));
  }
}

/* **** Chaos-over-Ethernet functions **** [based on cbridge + dpni20] */

void
send_chaos_packet(unsigned char *ea, unsigned char *buf, int cnt)
{
  unsigned char pktbuf[ARP_PKTSIZ];
  struct dpchaos_s *dpc = (struct dpchaos_s *)dp.dp_adr;
  u_char *my_ea = dpc->dpchaos_eth;
  u_short chatyp = htons(ETHERTYPE_CHAOS);

  // construct ether header
  ea_set(&pktbuf[0], ea);	/* dest ea */
  ea_set(&pktbuf[ETHER_ADDR_LEN], my_ea);  /* src ea */
  memcpy(&pktbuf[ETHER_ADDR_LEN*2], &chatyp, 2); /* pkt type */

  memcpy(&pktbuf[ETHER_ADDR_LEN*2+2], buf, cnt);

  // byte swap for network
  htons_buf((u_short *)&pktbuf[ETHER_ADDR_LEN*2+2], (u_short *)&pktbuf[ETHER_ADDR_LEN*2+2], cnt);
  (void) osn_pfwrite(&pfdata, pktbuf, cnt+ETHER_ADDR_LEN*2+2);
}

#if KLH10_NET_BPF

#define BPF_MTU CH_PK_MAXLEN // (BPF_WORDALIGN(1514) + BPF_WORDALIGN(sizeof(struct bpf_hdr)))

// See dpimp.c
/* Packet byte offsets for interesting fields (in network order) */
#define PKBOFF_EDEST 0		/* 1st shortword of Ethernet destination */
#define PKBOFF_ETYPE 12		/* Shortwd offset to Ethernet packet type */
#define PKBOFF_ARP_PTYPE (sizeof(struct ether_header)+sizeof(u_short))  /* ARP protocol type */

/* BPF simple Loads */
#define BPFI_LD(a)  bpf_stmt(BPF_LD+BPF_W+BPF_ABS,(a))	/* Load word  P[a:4] */
#define BPFI_LDH(a) bpf_stmt(BPF_LD+BPF_H+BPF_ABS,(a))	/* Load short P[a:2] */
#define BPFI_LDB(a) bpf_stmt(BPF_LD+BPF_B+BPF_ABS,(a))	/* Load byte  P[a:1] */

/* BPF Jumps and skips */
#define BPFI_J(op,k,t,f) bpf_jump(BPF_JMP+(op)+BPF_K,(k),(t),(f))
#define BPFI_JEQ(k,n) BPFI_J(BPF_JEQ,(k),(n),0)		/* Jump if A == K */
#define BPFI_JNE(k,n) BPFI_J(BPF_JEQ,(k),0,(n))		/* Jump if A != K */
#define BPFI_JGT(k,n) BPFI_J(BPF_JGT,(k),(n),0)		/* Jump if A >  K */
#define BPFI_JLE(k,n) BPFI_J(BPF_JGT,(k),0,(n))		/* Jump if A <= K */
#define BPFI_JGE(k,n) BPFI_J(BPF_JGE,(k),(n),0)		/* Jump if A >= K */
#define BPFI_JLT(k,n) BPFI_J(BPF_JGE,(k),0,(n))		/* Jump if A <  K */
#define BPFI_JDO(k,n) BPFI_J(BPF_JSET,(k),(n),0)	/* Jump if   A & K */
#define BPFI_JDZ(k,n) BPFI_J(BPF_JSET,(k),0,(n))	/* Jump if !(A & K) */

#define BPFI_CAME(k) BPFI_JEQ((k),1)		/* Skip if A == K */
#define BPFI_CAMN(k) BPFI_JNE((k),1)		/* Skip if A != K */
#define BPFI_CAMG(k) BPFI_JGT((k),1)		/* Skip if A >  K */
#define BPFI_CAMLE(k) BPFI_JLE((k),1)		/* Skip if A <= K */
#define BPFI_CAMGE(k) BPFI_JGE((k),1)		/* Skip if A >= K */
#define BPFI_CAML(k) BPFI_JLT((k),1)		/* Skip if A <  K */
#define BPFI_TDNN(k) BPFI_JDO((k),1)		/* Skip if   A & K */
#define BPFI_TDNE(k) BPFI_JDZ((k),1)		/* Skip if !(A & K) */

/* BPF Returns */
#define BPFI_RET(n) bpf_stmt(BPF_RET+BPF_K, (n))	/* Return N bytes */
#define BPFI_RETFAIL() BPFI_RET(0)			/* Failure return */
#define BPFI_RETWIN()  BPFI_RET((u_int)-1)		/* Success return */

// My addition
#define BPFI_SKIP(n) BPFI_J(BPF_JA,0,(n),(n))  /* skip n instructions */

struct bpf_insn bpf_stmt(unsigned short code, bpf_u_int32 k)
{
    struct bpf_insn ret;
    ret.code = code;
    ret.jt = 0;
    ret.jf = 0;
    ret.k = k;
    return ret;
}
struct bpf_insn bpf_jump(unsigned short code, bpf_u_int32 k,
			 unsigned char jt, unsigned char jf)
{
    struct bpf_insn ret;
    ret.code = code;
    ret.jt = jt;
    ret.jf = jf;
    ret.k = k;
    return ret;
}
#endif // KLH10_NET_BPF

#if (KLH10_NET_PCAP || KLH10_NET_BPF)

// Here is the BPF program, simple
#define BPF_PFMAX 50
struct bpf_insn bpf_pftab[BPF_PFMAX];
struct bpf_program bpf_pfilter = {0, bpf_pftab};

static void pfshow(struct bpf_program *pf);

// this gets called from osn_pfinit
// make a filter for both Chaos and ARP (for Chaos), have the read method handle ARP
struct bpf_program *
pfbuild(void *arg, struct in_addr *ipa)
{
  struct dpchaos_s *dpc = (struct dpchaos_s *)arg;
  struct bpf_program *pfp = &bpf_pfilter;


  // Check the ethernet type field
#if KLH10_NET_PCAP
#define MAX_PFLEN 136 // really need only 130
  u_char bpfilter[MAX_PFLEN];
  char eastr[OSN_EASTRSIZ];
  // must also check for address since interface may be in promisc mode although we didn't ask for it
  sprintf(bpfilter,
	  // arp pkt for Chaosnet
	  "(ether proto 0x0806 && arp[2:2] = 0x0804) || "
	  // or Chaosnet pkt for me or broadcast
	  "(ether proto 0x0804 && (ether dst %s || ether dst ff:ff:ff:ff:ff:ff))",
	  eth_adrsprint(eastr, dpc->dpchaos_eth));
  // Never mind about netmask, this is not IP anyway.
  if (DP_DBGFLG > 1)
    dbprintln("compiling pcap program \"%s\"", bpfilter);
  if (pcap_compile(pfdata.pf_handle, pfp, bpfilter, 1,  PCAP_NETMASK_UNKNOWN) != 0) {
    efatal(1, "pcap_compile failed: %s", pcap_geterr(pfdata.pf_handle));
  }
#else
  // simplistic, only checks types, not addresses
  struct bpf_insn *p = pfp->bf_insns;
  // if etype == arp && arp_ptype == chaos then win
  // if etype == chaos then win
  *p++ = BPFI_LDH(PKBOFF_ETYPE); /* Load ethernet type field */
  *p++ = BPFI_CAMN(ETHERTYPE_CHAOS); /* Win if CHAOS */
  *p++ = BPFI_RETWIN();
  *p++ = BPFI_CAME(ETHERTYPE_ARP); /* Skip if ARP */
  *p++ = BPFI_RETFAIL();	/* Not ARP, ignore */
  // For ARP, check the protocol type
  *p++ = BPFI_LDH(PKBOFF_ARP_PTYPE); /* Check the ARP type */
  *p++ = BPFI_CAME(ETHERTYPE_CHAOS);
  *p++ = BPFI_RETFAIL();	/* Not Chaos, ignore */
  // Never mind about destination here, if we get other ARP info that's nice?
  *p++ = BPFI_RETWIN();
  pfp->bf_len = p - pfp->bf_insns;	/* Set # of items on list */
#endif // pcap/not
    if (DP_DBGFLG > 1)			/* If debugging, print out resulting filter */
	pfshow(pfp);
    return pfp;
}


// from dpni20
/* Debug auxiliary to print out packetfilter we composed.
*/
static void
pfshow(struct bpf_program *pf)
{
    int i;

    fprintf(stderr, "[%s: kernel packetfilter len %d:\r\n",
	    progname, pf->bf_len);
    for (i = 0; i < pf->bf_len; ++i)
	fprintf(stderr, "%04X %2d %2d %0X\r\n",
		pf->bf_insns[i].code,
		pf->bf_insns[i].jt,
		pf->bf_insns[i].jf,
		pf->bf_insns[i].k);
    fprintf(stderr, "]\r\n");
}

#endif /* KLH10_NET_PCAP || KLH10_NET_BPF */

/* **** Chaosnet ARP functions **** */

void init_arp_table()
{
  struct dpchaos_s *dpc = (struct dpchaos_s *)dp.dp_adr;
  memset((char *)dpc->charp_list, 0, sizeof(struct charp_ent)*CHARP_MAX);
  dpc->charp_len = 0;
}

void print_arp_table()
{
  int i;
  time_t age;
  struct dpchaos_s *dpc = (struct dpchaos_s *)dp.dp_adr;
  if (dpc->charp_len > 0) {
    printf("Chaos ARP table:\r\n"
	   "Chaos\tEther\t\t\tAge (s)\r\n");
    for (i = 0; i < dpc->charp_len; i++) {
      age = (time(NULL) - dpc->charp_list[i].charp_age);
      printf("%#o\t\%02X:%02X:%02X:%02X:%02X:%02X\t%lu\t%s\r\n",
	     dpc->charp_list[i].charp_chaddr,
	     dpc->charp_list[i].charp_eaddr[0],
	     dpc->charp_list[i].charp_eaddr[1],
	     dpc->charp_list[i].charp_eaddr[2],
	     dpc->charp_list[i].charp_eaddr[3],
	     dpc->charp_list[i].charp_eaddr[4],
	     dpc->charp_list[i].charp_eaddr[5],
	     age, age > CHARP_MAX_AGE ? "(old)" : "");
    }
  }
}

u_char *find_arp_entry(u_short daddr)
{
  int i;
  struct dpchaos_s *dpc = (struct dpchaos_s *)dp.dp_adr;
  u_short my_chaddr = dpc->dpchaos_myaddr;

  if (DP_DBGFLG) dbprintln("Looking for ARP entry for %#o, ARP table len %d", daddr, dpc->charp_len);
  if (daddr == my_chaddr) {
    if (DP_DBGFLG > 1) dbprintln("#### Looking up ARP for my own address, BUG!");
    return NULL;
  }
  for (i = 0; i < dpc->charp_len && i < CHARP_MAX; i++)
    if (dpc->charp_list[i].charp_chaddr == daddr) {
      if ((dpc->charp_list[i].charp_age != 0)
	  && ((time(NULL) - dpc->charp_list[i].charp_age) > CHARP_MAX_AGE)) {
	if (DP_DBGFLG) dbprintln("Found ARP entry for %#o but it is too old (%lu s)",
				 daddr, (time(NULL) - dpc->charp_list[i].charp_age));
	return NULL;
      }
      if (DP_DBGFLG & 040) dbprintln("Found ARP entry for %#o", daddr);
      return dpc->charp_list[i].charp_eaddr;
    }
  if (DP_DBGFLG)
    print_arp_table();
  return NULL;
}


// from cbridge, rewritten reading arp_myreply from dpni20
void
send_chaos_arp_pkt(u_short atyp, u_short dest_chaddr, u_char *dest_eth)
{
  unsigned char pktbuf[ARP_PKTSIZ];
  struct dpchaos_s *dpc = (struct dpchaos_s *)dp.dp_adr;
  u_short my_chaddr = dpc->dpchaos_myaddr;
  u_char req[sizeof(struct arphdr)+(ETHER_ADDR_LEN+2)*2];
  struct arphdr *arp = (struct arphdr *)&req;
  u_short arptyp = htons(ETHERTYPE_ARP);

  // first ethernet header
  memcpy(&pktbuf[0], dest_eth, ETHER_ADDR_LEN);  /* dest ether */
  ea_set(&pktbuf[ETHER_ADDR_LEN], dpc->dpchaos_eth);  /* source ether = me */
  memcpy(&pktbuf[ETHER_ADDR_LEN*2], &arptyp, 2);  /* ether pkt type */

  // now arp pkt
  memset(&req, 0, sizeof(req));
  arp->ar_hrd = htons(ARPHRD_ETHER); /* Want ethernet address */
  arp->ar_pro = htons(ETHERTYPE_CHAOS);	/* of a Chaosnet address */
  arp->ar_hln = ETHER_ADDR_LEN;
  arp->ar_pln = sizeof(dest_chaddr);
  arp->ar_op = htons(atyp);
  memcpy(&req[sizeof(struct arphdr)], dpc->dpchaos_eth, ETHER_ADDR_LEN);	/* my ether */
  memcpy(&req[sizeof(struct arphdr)+ETHER_ADDR_LEN], &my_chaddr, sizeof(my_chaddr)); /* my chaos */
  /* his ether */
  if (atyp == ARPOP_REPLY)
    memcpy(&req[sizeof(struct arphdr)+ETHER_ADDR_LEN+2], dest_eth, ETHER_ADDR_LEN);
  /* his chaos */
  memcpy(&req[sizeof(struct arphdr)+ETHER_ADDR_LEN+2+ETHER_ADDR_LEN], &dest_chaddr, sizeof(my_chaddr));

  // and merge them
  memcpy(&pktbuf[ETHER_ADDR_LEN*2+2], req, sizeof(req));

  if (DP_DBGFLG & 4) {
    dbprintln("Sending ARP pkt");
    describe_eth_pkt_hdr(pktbuf);
    describe_arp_pkt(arp, req);
    //describe_arp_pkt((struct arphdr *)&pktbuf[0], &pktbuf[ETHER_ADDR_LEN*2+2]);
  }

  // and send it
  osn_pfwrite(&pfdata, pktbuf, sizeof(pktbuf));
}

void
send_chaos_arp_request(u_short chaddr)
{
  send_chaos_arp_pkt(ARPOP_REQUEST, chaddr, (u_char *)&eth_brd);
}

void
send_chaos_arp_reply(u_short dest_chaddr, u_char *dest_eth)
{
  send_chaos_arp_pkt(ARPOP_REPLY, dest_chaddr, dest_eth);
}


void
dumppkt_raw(unsigned char *ucp, int cnt)
{
    int i;

    while (cnt > 0) {
	for (i = 8; --i >= 0 && cnt > 0;) {
	    if (--cnt >= 0)
		fprintf(stderr, "  %02x", *ucp++);
	    if (--cnt >= 0)
		fprintf(stderr, "%02x", *ucp++);
	}
	fprintf(stderr, "\r\n");
    }
}

static char
  *ch_opc[] = { "NIL",
	     "RFC", "OPN", "CLS", "FWD", "ANS", "SNS", "STS", "RUT",
	     "LOS", "LSN", "MNT", "EOF", "UNC", "BRD" };
static char *
ch_opcode(int op)
  {
    char buf[7];
    if (op < 017 && op > 0)
      return ch_opc[op];
    else if (op == 0200)
      return "DAT";
    else if (op == 0300)
      return "DWD";
    else
      return "bogus";
  }

char *
ch_char(unsigned char x, char *buf) {
  if (x < 32)
    sprintf(buf,"^%c", x+64);
  else if (x == 127)
    sprintf(buf,"^?");
  else if (x < 127)
    sprintf(buf,"%2c",x);
  else
    sprintf(buf,"%2x",x);
  return buf;
}

void
dumppkt(unsigned char *ucp, int cnt)
{
  int i, row;
  struct dpchaos_s *dpc = (struct dpchaos_s *)dp.dp_adr;

  int off = dpc->dpchaos_outoff;
  char b1[3],b2[3];

  if (off == DPCHAOS_CHUDP_DATAOFFSET)
    fprintf(stderr,"CHUDP version %d, function %d, ", ucp[0], ucp[1]);
  fprintf(stderr,"Chaos pkt len %d\r\n", cnt);
  ucp += off;
  cnt -= off;
    fprintf(stderr,"Opcode: %#o (%s), unused: %o\r\nFC: %d, Nbytes %d.\r\n",
	    ucp[0], ch_opcode(ucp[0]),
	    ucp[1], ucp[2]>>4, ((ucp[2]&0xf)<<4) | ucp[3]);
    fprintf(stderr,"Dest host: %#o, index %#o\r\nSource host: %#o, index %#o\r\n",
	    (ucp[4]<<8)|ucp[5], (ucp[6]<<8)|ucp[7], 
	    (ucp[8]<<8)|ucp[9], (ucp[10]<<8)|ucp[11]);
    fprintf(stderr,"Packet #%d.\r\nAck #%d.\r\n",
	    (ucp[12]<<8)|ucp[13], (ucp[14]<<8)|ucp[15]);
    fprintf(stderr,"Data:\r\n");

    /* Skip headers */
    ucp += CHAOS_HEADERSIZE;
    /* Show only data portion */
    cnt -= CHAOS_HEADERSIZE+CHAOS_HW_TRAILERSIZE;

    for (row = 0; row*8 < cnt; row++) {
      for (i = 0; (i < 8) && (i+row*8 < cnt); i++) {
	fprintf(stderr, "  %02x", ucp[i+row*8]);
	fprintf(stderr, "%02x", ucp[(++i)+row*8]);
      }
      fprintf(stderr, " (hex)\r\n");
#if 1
      for (i = 0; (i < 8) && (i+row*8 < cnt); i++) {
	fprintf(stderr, "  %2s", ch_char(ucp[i+row*8], (char *)&b1));
	fprintf(stderr, "%2s", ch_char(ucp[(++i)+row*8], (char *)&b2));
      }
      fprintf(stderr, " (chars)\r\n");
      for (i = 0; (i < 8) && (i+row*8 < cnt); i++) {
	fprintf(stderr, "  %2s", ch_char(ucp[i+1+row*8], (char *)&b1));
	fprintf(stderr, "%2s", ch_char(ucp[(i++)+row*8], (char *)&b2));
      }
      fprintf(stderr, " (11-chars)\r\n");
#endif
    }
    /* Now show trailer */
    fprintf(stderr,"HW trailer:\r\n  Dest: %#o\r\n  Source: %#o\r\n  Checksum: %#x\r\n",
	    (ucp[cnt]<<8)|ucp[cnt+1],(ucp[cnt+2]<<8)|ucp[cnt+3],(ucp[cnt+4]<<8)|ucp[cnt+5]);
}


char *
ch_adrsprint(char *cp, unsigned char *ca)
{
  sprintf(cp, "%#o", (ca[1]<<8) | ca[0]);  /* unswapped */
  return cp;
}

/* Add OSDNET shared code here */

#include "osdnet.c"
