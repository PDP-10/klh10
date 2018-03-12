/* DPCHUDP.C - Chaos link implementation (over Ethernet or using UDP)
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
[three when DTLS is implemented - but that needs a bit of work, with sessions etc]

Firstly, by implementing Chaosnet over Ethernet (protocol nr 0x0804) and handling
ARP for that protocol. This uses one of the packet filtering
implementations (or only pcap?) provided by osdnet. (Only EthernetII
headers are supported, not 802.3.)
No routing is handled, that's done by ITS.

Secondly, it can do it by using a Chaos-over-UDP tunnel.
Given a Chaos packet and a mapping between Chaosnet and IP addresses,
it simply sends the packet encapsulated in a UDP datagram.

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
and from the NET are CHUDP packets.

	There are actually two processes active, one which pumps data
from the NET to the CH11-output buffer, and another that pumps in the
opposite direction from an input buffer to the NET.  Generally they
are completely independent.
*/

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

#include "dpchudp.h"	/* DPCHUDP specific defs, grabs DPSUP if needed */


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

/* Debug flag reference.  Use DBGFLG within functions that have "dpchudp";
 * all others must use DP_DBGFLG.  Both refer to the same location.
 * Caching a local copy of the debug flag is a no-no because it lives
 * in a shared memory location that may change at any time.
 */
#define DBGFLG (dpchudp->dpchudp_dpc.dpc_debug)
#define DP_DBGFLG (((struct dpchudp_s *)dp.dp_adr)->dpchudp_dpc.dpc_debug)

/* Local predeclarations */

void chudptohost(struct dpchudp_s *);
void hosttochudp(struct dpchudp_s *);

void net_init(struct dpchudp_s *);
void dumppkt(unsigned char *, int);
void dumppkt_raw(unsigned char *ucp, int cnt);

void ihl_hhsend(register struct dpchudp_s *dpchudp, int cnt, register unsigned char *pp, size_t off);
void ip_write(struct in_addr *ipa, in_port_t ipport, unsigned char *buf, int len, struct dpchudp_s *dpchudp);
int hi_iproute(struct in_addr *ipa, in_port_t *ipport, unsigned char *lp, int cnt, struct dpchudp_s *dpchudp);

char *ch_adrsprint(char *cp, unsigned char *ca);
void send_chaos_arp_reply(u_short dest_chaddr, u_char *dest_eth);
void send_chaos_arp_request(u_short chaddr);
void send_chaos_packet(unsigned char *ea, unsigned char *buf, int cnt);
u_char *find_arp_entry(u_short daddr);
void print_arp_table(void);


/* Error and diagnostic output */

static const char progname_i[] = "dpchudp";
static const char progname_r[] = "dpchudp-R";
static const char progname_w[] = "dpchudp-W";
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
    register struct dpchudp_s *dpchudp;	/* Ptr to shared memory area */

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
    dpchudp = (struct dpchudp_s *)dp.dp_adr;	/* Make for easier refs */

    /* Verify that the structure version is compatible */
    if (dpchudp->dpchudp_ver != DPCHUDP_VERSION) {
	efatal(1, "Wrong version of DPCHUDP: ch11=%0lx dpchudp=%0lx",
	       (long)dpchudp->dpchudp_ver, (long)DPCHUDP_VERSION);
    }

    /* Now can access DP args!
       From here on we can use DBGFLG, which is actually a shared
       memory reference that dpchudp points to.  Check here to accomodate the
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
    if (!(dpchudp->dpchudp_dpc.dpc_flags & DPCF_MEMLOCK)) {
	if (mlockall(MCL_CURRENT|MCL_FUTURE) != 0) {
	    dbprintln("Warning - cannot lock memory");
	}
    }
#endif

    /* Now set up legacy variables based on parameters passed through
       shared DP area.
    */
    if ((myaddr = dpchudp->dpchudp_myaddr) == 0) /* My chaos address */
	efatal(1, "no CHAOS address specified");

    /* Initialize various network info */
    net_init(dpchudp);

    /* Make this a status (rather than debug) printout? */
    if (swstatus) {
      int i;
      char ipbuf[OSN_IPSTRSIZ];

      dbprintln("ifc \"%s\" => chaos %lo",
		dpchudp->dpchudp_ifnam, (long)myaddr);
      for (i = 0; i < dpchudp->dpchudp_chip_tlen; i++)
	dbprintln(" chaos %6o => ip %s:%d",
		  dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_chaddr,
		  ip_adrsprint(ipbuf,
			       (unsigned char *)&dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_ipaddr),
		  dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_ipport);
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
	chudptohost(dpchudp);	/* Child process handles input */
    }
    progname = progname_w;	/* Reset progname to indicate identity */

    hosttochudp(dpchudp);		/* Parent process handles output */

    return 1;			/* Never returns, but placate compiler */
}

/* NET_INIT - Initialize net-related variables,
**	given network interface we'll use.
*/
struct ifent *osn_iflookup(char *ifnam);

void net_init_pf(struct dpchudp_s *dpchudp)
{
  // based on dpni20.c
  /* Set up packet filter.  This also returns in "ihost_ea"
     the ethernet address for the selected interface.
  */

  npf.osnpf_ifnam = dpchudp->dpchudp_ifnam;
  npf.osnpf_ifmeth = dpchudp->dpchudp_ifmeth;
  npf.osnpf_dedic = dpchudp->dpchudp_dedic;
  //npf.osnpf_rdtmo = dpchudp->dpchudp_rdtmo;
  npf.osnpf_backlog = dpchudp->dpchudp_backlog;
  //npf.osnpf_ip.ia_addr = ehost_ip;
  //npf.osnpf_tun.ia_addr = tun_ip;


  (void)osn_iftab_init();

  // finding the interface (and its ethernet address) is necessary for the more clever packet filter,
  // which isn't enabled for now, so...
  if (npf.osnpf_ifnam == NULL || npf.osnpf_ifnam[0] == '\0') {
    // requires ip4 address, but better than nothing?
    struct ifent *ife = osn_ipdefault();
    if (ife && (strlen(ife->ife_name) < sizeof(npf.osnpf_ifnam))) {
      dbprintln("Using default interface '%s'", ife->ife_name);
      strcpy(npf.osnpf_ifnam, ife->ife_name);
    } else
      if (DBGFLG) dbprintln("Can't find default interface.");
  } else
    if (DBGFLG) dbprintln("Interface '%s' specified.", npf.osnpf_ifnam);

  /* Ether addr is both a potential arg and a returned value;
     the packetfilter open may use and/or change it.
  */
  if ((memcmp(dpchudp->dpchudp_eth,"\0\0\0\0\0\0", ETHER_ADDR_LEN) == 0) &&
      npf.osnpf_ifnam != NULL && npf.osnpf_ifnam[0] != '\0') {
    // we need the ea for the pf, so find it already here
    /* Now get our interface's ethernet address. */
    if (osn_ifealookup(npf.osnpf_ifnam, (unsigned char *) &npf.osnpf_ea) == 0) {
      struct ifent *ife = (struct ifent *)osn_iflookup(npf.osnpf_ifnam);
      dbprintln("Can't find EA for \"%s\"", npf.osnpf_ifnam);
      if (ife) {
	dbprintln("Found interface '%s', ea %s", ife->ife_name, (ife->ife_ea != NULL && memcmp(ife->ife_ea,"\0\0\0\0\0\0", ETHER_ADDR_LEN) == 0) ? "found" : "not found");
      } else
	dbprintln("Can't find interface '%s'!", npf.osnpf_ifnam);
    } else {
      if (DP_DBGFLG) dbprintln("Found EA for \"%s\"", npf.osnpf_ifnam);
    }
  } else
    ea_set(&npf.osnpf_ea, dpchudp->dpchudp_eth);	/* Set requested ea if any */
  if (DP_DBGFLG) {
    char eastr[OSN_EASTRSIZ];

    dbprintln("EN addr for \"%s\" = %s",
	      npf.osnpf_ifnam, eth_adrsprint(eastr, (unsigned char *)&npf.osnpf_ea));
  }
  ea_set(dpchudp->dpchudp_eth, (char *)&npf.osnpf_ea);	/* Copy ether addr (so pfbuild can use it)*/
  // pfdata, osnpf, pfarg; pfbuild(pfarg, ehost_ip)
  osn_pfinit(&pfdata, &npf, (void *)dpchudp);	/* Will abort if fails */
  ea_set(dpchudp->dpchudp_eth, &npf.osnpf_ea);		/* Copy actual ea */

  /* Now set any return info values in shared struct.
   */
#if 0 // already done above
  ea_set(dpchudp->dpchudp_eth, (char *)&ihost_ea);	/* Copy ether addr */
#endif
}

void net_init_chudp(struct dpchudp_s *dpchudp)
{
  struct sockaddr_in mysin;

  if ((chudpsock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    efatal(1,"socket failed: %s", dp_strerror(errno));

  mysin.sin_family = AF_INET;
  mysin.sin_port = htons(dpchudp->dpchudp_port);
  mysin.sin_addr.s_addr = INADDR_ANY;
#if 0
  if (dpchudp->dpchudp_ifnam[0] && ihost_ip.s_addr != 0)
    mysin.sin_addr.s_addr = ihost_ip.s_addr;
#endif

  if (bind(chudpsock, (struct sockaddr *)&mysin, sizeof(mysin)) < 0)
    efatal(1,"bind failed: %s", dp_strerror(errno));
}

void
net_init(register struct dpchudp_s *dpchudp)
{
    struct ifreq ifr;

    /* Ensure network device name, if specified, isn't too long */
    if (dpchudp->dpchudp_ifnam[0] && (strlen(dpchudp->dpchudp_ifnam)
		>= sizeof(ifr.ifr_name))) {
	esfatal(0, "interface name \"%s\" too long - max %d",
		dpchudp->dpchudp_ifnam, (int)sizeof(ifr.ifr_name));
    }

#if 0
    /* Don't determine network device to use, if none was specified
    ** (since we simply use all interfaces in that case).
    */
    if (dpchudp->dpchudp_ifnam[0]) {
      /* Find host's IP address for this interface */
      if (!osn_ifipget(-1, dpchudp->dpchudp_ifnam, (unsigned char *)&ihost_ip))
	efatal(1,"osn_ifipget failed for \"%s\"", dpchudp->dpchudp_ifnam);
    }
#endif
    /* Set up appropriate net fd.
    */
    if (dpchudp->dpchudp_ifmeth_chudp) {
      // CHUDP case
      net_init_chudp(dpchudp);
    } else {
      // Ethernet case
      net_init_pf(dpchudp);
    }
}

/* CHUDPTOHOST - Child-process main loop for pumping packets from CHUDP to HOST.
**	Reads packets from net and feeds CHUDP packets to DP superior process.
*/

#define NINBUFSIZ (DPCHUDP_DATAOFFSET+MAXETHERLEN)

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
  char ethstr[OSN_EASTRSIZ];
  dbprintln("Ether header:");
  dbprintln(" dest %s", eth_adrsprint(ethstr, buffp));
  dbprintln(" src  %s", eth_adrsprint(ethstr, &buffp[ETHER_ADDR_LEN]));
  dbprintln(" prot %#x", (buffp[ETHER_ADDR_LEN*2] << 8) || buffp[ETHER_ADDR_LEN*2+1]);
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

void chudptohost_pf_arp(struct dpchudp_s *dpchudp, unsigned char *buffp, int cnt)
{
  struct arphdr *arp = (struct arphdr *)buffp;
  if (DBGFLG) describe_arp_pkt(arp, buffp);

  if (arp->ar_pro != htons(ETHERTYPE_CHAOS)) {
    if (DBGFLG)
      dbprintln("unexpected ARP protocol %#x received", ntohs(arp->ar_pro));
    return;
  }

  u_short schad = ntohs((buffp[sizeof(struct arphdr)+arp->ar_hln]<<8) |
			buffp[sizeof(struct arphdr)+arp->ar_hln+1]);
  u_char *sead = &buffp[sizeof(struct arphdr)];
  u_short dchad =  ntohs((buffp[sizeof(struct arphdr)+arp->ar_hln+arp->ar_hln+arp->ar_pln]<<8) |
			 buffp[sizeof(struct arphdr)+arp->ar_hln+arp->ar_hln+arp->ar_pln+1]);
  if (arp->ar_op == htons(ARPOP_REQUEST)) {
    if (dchad == dpchudp->dpchudp_myaddr) {
      if (DBGFLG) dbprintln("ARP: Sending reply for %#o (me) to %#o", dchad, schad);
      send_chaos_arp_reply(schad, sead); /* Yep. */
    }
    return;
  }
  /* Now see if we should add this to our Chaos ARP list */
  int i, found = 0;
  for (i = 0; i < dpchudp->charp_len && i < CHARP_MAX; i++)
    if (dpchudp->charp_list[i].charp_chaddr == schad) {
      found = 1;
      dpchudp->charp_list[i].charp_age = time(NULL);  // update age
      if (memcmp(&dpchudp->charp_list[i].charp_eaddr, sead, ETHER_ADDR_LEN) != 0) {
	memcpy(&dpchudp->charp_list[i].charp_eaddr, sead, ETHER_ADDR_LEN);
	if (DBGFLG) {
	  dbprintln("ARP: Changed MAC addr for %#o", schad);
	  // print_arp_table();
	}
      } else
	if (DBGFLG) {
	  dbprintln("ARP: Updated age for %#o", schad);
	  // print_arp_table();
	}
      break;
    }
  /* It's not in the list already, is there room? */
  // @@@@ should reuse outdated entries
  if (!found) {
    if (dpchudp->charp_len < CHARP_MAX) {
      if (DBGFLG) dbprintln("ARP: Adding new entry for Chaos %#o", schad);
      dpchudp->charp_list[dpchudp->charp_len].charp_chaddr = schad;
      dpchudp->charp_list[dpchudp->charp_len].charp_age = time(NULL);
      memcpy(&dpchudp->charp_list[dpchudp->charp_len].charp_eaddr, sead, ETHER_ADDR_LEN);
      dpchudp->charp_len++;
      if (DBGFLG) print_arp_table();
    } else {
      dbprintln("ARP: table full! Please increase size from %d and/or implement GC", CHARP_MAX);
    }
  }
}

void chudptohost_pf(struct dpchudp_s *dpchudp, unsigned char *buffp)
{
  register int cnt;

  /* OK, now do a blocking read on packetfilter input! */
  cnt = osn_pfread(&pfdata, buffp, DPCHUDP_MAXLEN);

  if (DBGFLG)
    dbprintln("Read=%d", cnt);

  if (cnt == 0)
    return;

  if (((struct ether_header *)buffp)->ether_type == htons(ETHERTYPE_ARP)) {
    if (DBGFLG)
      dbprintln("Got ARP");
    chudptohost_pf_arp(dpchudp, buffp+(sizeof(struct ether_header)), cnt-sizeof(struct ether_header));

    return;
  } else if (((struct ether_header *)buffp)->ether_type == htons(ETHERTYPE_CHAOS)) {
    if (cnt <= sizeof(sizeof(struct ether_header))) { /* Must get enough for ether header */

#if 0
      /* If call timed out, should return 0 */
      if (cnt == 0 && dpchudp->dpchudp_rdtmo)
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

#define PKBOFF_PKPAYLOAD (PKBOFF_ETYPE+2)
    u_char *pp = buffp+sizeof(struct ether_header);
    int pcnt = cnt - sizeof(struct ether_header);

    if (DBGFLG) {
      dbprintln("Read Chaos pkt len %d", pcnt);
    }

    // byte swap
    ntohs_buf((u_short *)pp, (u_short *)pp, pcnt);
    if (DBGFLG & 0x2) {
      dumppkt(pp-4,pcnt+4);	/* mod CHUDP hdr */
    }
    // "assume we're already in shared buffer" (so buffp arg is not used)
    ihl_hhsend(dpchudp, cnt, buffp, sizeof(struct ether_header));

    return;
  } else {
    if (DBGFLG) {
      dbprintln("unexpected ether type %#x", ntohs(((struct ether_header *)buffp)->ether_type));
      (void) dumppkt_raw(buffp, (cnt > 8*8) ? 8*8 : cnt);
    }
    return;		/* get another pkt */
  }
}

void chudptohost_chudp(struct dpchudp_s *dpchudp, unsigned char *buffp)
{
  struct sockaddr_in ip_sender;
  socklen_t iplen;
  register int cnt;

  /* OK, now do a blocking read on UDP socket! */
  errno = 0;		/* Clear to make sure it's the actual error */
  memset(&ip_sender, 0, sizeof(ip_sender));
  iplen = sizeof(ip_sender); /* Supply size of ip_sender, and get actual stored length */
  cnt = recvfrom(chudpsock, buffp, DPCHUDP_MAXLEN, 0, (struct sockaddr *)&ip_sender, &iplen);
  // buff now has a CHUDP pkt
  if (cnt <= DPCHUDP_DATAOFFSET) {
    if (DBGFLG)
      fprintf(stderr, "[dpchudp-R: ERead=%d, Err=%d]\r\n",
	      cnt, errno);

    if (cnt < 0 && (errno == EINTR))	/* Ignore spurious signals */
      return;

    /* Error of some kind */
    fprintf(stderr, "[dpchudp-R: Eread = %d, ", cnt);
    if (cnt < 0) {
      fprintf(stderr, "errno %d = %s]\r\n",
	      errno, dp_strerror(errno));
    } else if (cnt > 0)
      fprintf(stderr, "no chudp data]\r\n");
    else fprintf(stderr, "no packet]\r\n");

    return;		/* For now... */
  }
  if (DBGFLG) {
    if (DBGFLG & 0x4) {
      fprintf(stderr, "\r\n[dpchudp-R: Read=%d\r\n", cnt);
      dumppkt(buffp, cnt);
      fprintf(stderr, "]");
    }
    else
      fprintf(stderr, "[dpchudp-R: Read=%d]", cnt);
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
    int chalen = ((buffp[DPCHUDP_DATAOFFSET+2] & 0xf)<<4) | buffp[DPCHUDP_DATAOFFSET+3];
    int datalen = (DPCHUDP_DATAOFFSET + CHAOS_HEADERSIZE + chalen);
    int chafrom = (buffp[cnt-4]<<8) | buffp[cnt-3];
    char *ip = inet_ntoa(ip_sender.sin_addr);
    in_port_t port = ntohs(ip_sender.sin_port);
    time_t now = time(NULL);
    int i, cks;
    if ((cks = ch_checksum(buffp+DPCHUDP_DATAOFFSET,cnt-DPCHUDP_DATAOFFSET)) != 0) {
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
    if (dpchudp->dpchudp_ifmeth_chudp) {
#if 0
      if (DBGFLG) {
	dbprintln("Rcv from chaos %o = ip %s port %d., %d. bytes (datalen %d)",
		  chafrom,
		  ip, port,
		  cnt, datalen);
	dumppkt(buffp,cnt);
      }
#endif
      /* #### remove cks when buffer freed instead */
      if ((cks == 0) && (chafrom != myaddr) && (dpchudp->dpchudp_chip_tlen < DPCHUDP_CHIP_MAX)) {
	/* Space available, see if we need to add this */
	/* Look for old entries: if one is found which is either
	   static (from config file) or sufficiently fresh,
	   keep it and update the freshness (if not static) */
	for (i = 0; i < dpchudp->dpchudp_chip_tlen; i++)
	  if (chafrom == dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_chaddr) {
	    if (dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_lastrcvd != 0) {
	      if (now - dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_lastrcvd >
		  DPCHUDP_CHIP_DYNAMIC_AGE_LIMIT) {
		/* Old, update it (in case he moved) */
		if (1 || DBGFLG)
		  dbprintln("Updating CHIP entry %d for %o/%s:%d",
			    i, chafrom, ip, port);
		dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_ipport = port;
		memcpy(&dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_ipaddr, &ip_sender.sin_addr, IP_ADRSIZ);
	      }
	      /* update timestamp */
	      dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_lastrcvd = now;
	    }
	    chafrom = -1;	/* Not it shouldn't be added */
	    break;
	  }
	if (chafrom > 0) {
	  /* It's OK to write here, the other fork will see it when tlen is updated */
	  i = dpchudp->dpchudp_chip_tlen;
	  if (1 || DBGFLG)
	    dbprintln("Adding CHIP entry %d for %o/%s:%d",
		      i, chafrom, ip, port);
	  dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_chaddr = chafrom;
	  dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_ipport = port;
	  dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_lastrcvd = now;
	  memcpy(&dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_ipaddr, &ip_sender.sin_addr, IP_ADRSIZ);
	  dpchudp->dpchudp_chip_tlen++;
	}
#if 0
	else if (0 && DBGFLG) {
	  dbprintln("Already know chaos %o",chafrom);
	}
#endif
      }
      else if ((dpchudp->dpchudp_chip_tlen >= DPCHUDP_CHIP_MAX) && DBGFLG) {
	dbprintln("CHIP table full - cannot add %o", chafrom);
      }
    }
  }

  ihl_hhsend(dpchudp, cnt, buffp, DPCHUDP_DATAOFFSET);
}


void
chudptohost(register struct dpchudp_s *dpchudp)
{
    register struct dpx_s *dpx = dp_dpxfr(&dp);
    unsigned char *inibuf;
    unsigned char *buffp;
    size_t max;
    int stoploop = 50;

    inibuf = dp_xsbuff(dpx, &max);	/* Get initial buffer ptr */

    /* Tell KLH10 we're initialized and ready by sending initial packet */
    dp_xswait(dpx);			/* Wait until buff free, in case */
    dp_xsend(dpx, DPCHUDP_INIT, 0);	/* Send INIT */

    if (DBGFLG)
	fprintf(stderr, "[dpchudp-R: sent INIT]\r\n");

    for (;;) {
	/* Make sure that buffer is free before clobbering it */
	dp_xswait(dpx);			/* Wait until buff free */

	if (DBGFLG)
	    fprintf(stderr, "[dpchudp-R: InWait]\r\n");

	/* Set up buffer and initialize offsets */
	buffp = inibuf;

	if (!dpchudp->dpchudp_ifmeth_chudp) {
	  // non-CHUDP case (Ethernet)
	  chudptohost_pf(dpchudp, buffp);
	} else { // CHUDP case
	  chudptohost_chudp(dpchudp, buffp);
	}
#if 0
	if (--stoploop <= 0)
	  efatal(1, "Too many retries, aborting");
#endif
    }
}


/* Send regular message from CHUDP to HOST.
*/

void
ihl_hhsend(register struct dpchudp_s *dpchudp,
	   int cnt,
	   register unsigned char *pp,
	   size_t off)
	/* "pp" is packet data ptr, has room for header preceding */
{

    /* Send up to host!  Assume we're already in shared buffer. */

    register struct dpx_s *dpx = dp_dpxfr(&dp);

    dpchudp->dpchudp_inoff = off; /* Tell host what offset is */
    dp_xsend(dpx, DPCHUDP_RPKT, cnt-off);

    if (DBGFLG)
      fprintf(stderr, "[dpchudp-R: sent RPKT %d+%d]", (int)off, (int)(cnt-off));

}

/* HOSTTOCHUDP - Parent main loop for pumping packets from HOST to CHUDP.
**	Reads CHUDP message from DP superior
**	and interprets it.  If a regular message, bundles it up and
**	outputs to NET.
*/
void
hosttochudp(register struct dpchudp_s *dpchudp)
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
	fprintf(stderr, "[dpchudp-W: Starting loop]\r\n");

    for (;;) {
	if (DBGFLG)
	    fprintf(stderr, "[dpchudp-W: CmdWait]\r\n");

	/* Wait until 10 has a command for us */
	dp_xrwait(dpx);		/* Wait until something there */

	/* Process command from 10! */
	switch (dp_xrcmd(dpx)) {

	default:
	    fprintf(stderr, "[dpchudp: Unknown cmd %d]\r\n", dp_xrcmd(dpx));
	    dp_xrdone(dpx);
	    continue;

	case DPCHUDP_SPKT:			/* Send regular packet */
	    rcnt = dp_xrcnt(dpx);
	    /* does rcnt include chudp header size? yes. */
	    buff = inibuf;
	    memcpy(buff, (void *)&chuhdr, sizeof(chuhdr)); /* put in CHUDP hdr */
	    if (DBGFLG) {
		if (DBGFLG & 0x2) {
		    fprintf(stderr, "\r\n[dpchudp-W: Sending %d\r\n", rcnt);
		    dumppkt(buff, rcnt);
		    fprintf(stderr, "]");
		}
		else
		    fprintf(stderr, "[dpchudp-W: SPKT %d]", rcnt);
	    }
	    break;
	}

	/* Come here to handle output packet */
	int chlen = ((buff[DPCHUDP_DATAOFFSET+2] & 0xf) << 4) | buff[DPCHUDP_DATAOFFSET+3];

	if ((chlen + CHAOS_HEADERSIZE + DPCHUDP_DATAOFFSET + CHAOS_HW_TRAILERSIZE) > rcnt) {
	  if (1 || DBGFLG)
	    dbprintln("NOT sending less than packet: pkt len %d, expected %d (Chaos data len %d)",
		      rcnt, (chlen + CHAOS_HEADERSIZE + DPCHUDP_DATAOFFSET + CHAOS_HW_TRAILERSIZE),
		      chlen);
	  dp_xrdone(dpx);	/* ack it to dvch11, but */
	  continue;		/* don't send it! */
	}

	if (!dpchudp->dpchudp_ifmeth_chudp) {
	  // Ethernet case
	  u_char *ch = &buff[DPCHUDP_DATAOFFSET];
	  u_short dchad = (buff[4+DPCHUDP_DATAOFFSET]<<8)|buff[5+DPCHUDP_DATAOFFSET];

	  if (dchad == 0) {		/* broadcast */
	    if (DBGFLG & 020) dbprintln("Broadcasting on ether");
	    send_chaos_packet((u_char *)&eth_brd, &buff[DPCHUDP_DATAOFFSET], rcnt-DPCHUDP_DATAOFFSET);
	  } else {
	    u_char *eaddr = find_arp_entry(dchad);
	    if (eaddr != NULL) {
	      if (DBGFLG & 020) dbprintln("Sending on ether to %#o", dchad);
	      send_chaos_packet(eaddr, &buff[DPCHUDP_DATAOFFSET], rcnt-DPCHUDP_DATAOFFSET);
	    } else {
	      if (DBGFLG) dbprintln("Don't know %#o, sending ARP request", dchad);
	      send_chaos_arp_request(dchad);
	      // Chaos sender will retransmit, surely.
	    }
	  }
	} else {
	  // CHUDP case
	  /* find IP destination given chaos packet  */
	  if (hi_iproute(&ipdest, &ipport, &buff[DPCHUDP_DATAOFFSET], rcnt - DPCHUDP_DATAOFFSET,dpchudp)) {
	    ip_write(&ipdest, ipport, buff, rcnt, dpchudp);
	  }
	}

	/* Command done, tell 10 we're done with it */
	if (DBGFLG)
	  fprintf(stderr, "[dpchudp-W: CmdDone]");

	dp_xrdone(dpx);
    }
}

int
hi_lookup(struct dpchudp_s *dpchudp,
	  struct in_addr *ipa,	/* Dest IP addr to be put here */
	  in_port_t *ipport,	/* dest IP port to be put here */
	  unsigned int chdest, /* look up this chaos destination */
	  int sbnmatch)		/* just match subnet part */
{
  int i;
  for (i = 0; i < dpchudp->dpchudp_chip_tlen; i++) 
    if (
#if DPCHUDP_DO_ROUTING
	((!sbnmatch && (chdest == dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_chaddr))
	 ||
	 /* handle subnet matching */
	 (sbnmatch && ((chdest >> 8) == (dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_chaddr >> 8))))
#else
	(chdest == dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_chaddr)
#endif
	&& dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_ipaddr.s_addr != 0) {
      memcpy(ipport, &dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_ipport, sizeof(in_port_t));
      memcpy((void *)ipa, &dpchudp->dpchudp_chip_tbl[i].dpchudp_chip_ipaddr,
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
** Fill in ipa with IP address, based on Chaos header in lp,
** or if DPCHUDP_DO_ROUTING, based on Chaos trailer,
**    and also do some basic routing if destination is DEFAULT_CHAOS_ROUTER (3040)
**    or just some other subnet we know about.
*/

int
hi_iproute(struct in_addr *ipa,	/* Dest IP addr to be put here */
	   in_port_t *ipport,	/* dest IP port to be put here */
	   unsigned char *lp,	/* Ptr to start of Chaos packet */
	   int cnt,		/* Cnt of data including header */
	   struct dpchudp_s *dpchudp)
{
    unsigned int chdest;
#if DPCHUDP_DO_ROUTING
    unsigned int hwdest;
#endif

    if (cnt < CHAOS_HEADERSIZE) {
	error("Chaos packet too short: %d", cnt);
	return FALSE;
    }

#if !DPCHUDP_DO_ROUTING
    /* Derive destination IP address from Chaos header */
    chdest = lp[DPCHUDP_CH_DESTOFF]<<8 | lp[DPCHUDP_CH_DESTOFF+1];
#else
    /* Use hw trailer instead */
    hwdest = lp[cnt-6] << 8 | lp[cnt-5];
    if ((hwdest == DEFAULT_CHAOS_ROUTER) || ((hwdest >> 8) != (myaddr >> 8))) {
      /* Look up header dest,
	 if successful, change trailer dest, increase FW count, recompute checksum */
      if ((lp[DPCHUDP_CH_FC] >> 4) == 0xf) /* FC about to wrap */
	return FALSE;		/* drop it */
      /* look in header */
      chdest = lp[DPCHUDP_CH_DESTOFF]<<8 | lp[DPCHUDP_CH_DESTOFF+1];
    } else
      /* my subnet */
      chdest = hwdest;
    if (DBGFLG)
      dbprintln("looking for route to chaos %o (hw %o) (FC %d.)", chdest, hwdest,
		lp[DPCHUDP_CH_FC] >> 4);
#endif
    if (hi_lookup(dpchudp, ipa, ipport, chdest, 0)
#if DPCHUDP_DO_ROUTING
	/* try subnet match */
	|| ((chdest != hwdest) && hi_lookup(dpchudp, ipa, ipport, chdest, 1))
#endif
	) {
#if DPCHUDP_DO_ROUTING
      if (chdest != hwdest) {
	int cks;
	if (DBGFLG)
	  dbprintln("found route");
	/* change hw dest */
	lp[cnt-6] = chdest >> 8;
	lp[cnt-5] = chdest & 0xff;
	/* make sure it's got the right hw source addr */
	lp[cnt-4] = myaddr >> 8;
	lp[cnt-3] = myaddr & 0xff;
	/* increase FW count */
	lp[DPCHUDP_CH_FC] = (lp[DPCHUDP_CH_FC] & 0xf) | ((lp[DPCHUDP_CH_FC]>>4)+1)<<4;
	/* recompute checksum */
	cks = ch_checksum(lp,cnt-2); /* w/o orig checksum */
	lp[cnt-2] = cks >> 8;
	lp[cnt-1] = cks & 0xff;
      }
#endif
      return TRUE;
    }
    return FALSE;
}


/* IP_WRITE - Send IP packet out over UDP
*/

void
ip_write(struct in_addr *ipa, in_port_t ipport, unsigned char *buf, int len, struct dpchudp_s *dpchudp)
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
  struct dpchudp_s *dpc = (struct dpchudp_s *)dp.dp_adr;
  u_char *my_ea = dpc->dpchudp_eth;
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

#if KLH10_NET_PCAP

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
  struct dpchudp_s *dpc = (struct dpchudp_s *)arg;
  struct bpf_program *pfp = &bpf_pfilter;
  struct bpf_insn *p = pfp->bf_insns;

  // if etype == arp && arp_ptype == chaos then win
  // if etype == chaos && (edest == myeth || edest == broadcast) then win

  // Check the ethernet type field
#if 1 // simplistic, only checks types, not addresses
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
#else // the chaosnet checking doesn't work as is.
  *p++ = BPFI_LDH(PKBOFF_ETYPE); /* Load ethernet type field */
  *p++ = BPFI_CAME(ETHERTYPE_ARP); /* Skip if ARP */
  *p++ = BPFI_SKIP(4);	/* nope, go on below*/
  // For ARP, check the protocol type
  *p++ = BPFI_LDH(PKBOFF_ARP_PTYPE); /* Check the ARP type */
  *p++ = BPFI_CAME(ETHERTYPE_CHAOS);
  *p++ = BPFI_RETFAIL();	/* Not Chaos, ignore */
  // Never mind about destination here, if we get other ARP info that's nice?
  *p++ = BPFI_RETWIN();
  // Not ARP, check for CHAOS
  *p++ = BPFI_CAME(ETHERTYPE_CHAOS); /* Skip if CHAOS */
  *p++ = BPFI_RETFAIL();	/* Not Chaos, ignore */
  // For Ethernet pkts, also filter for our own address or broadcast,
  // in case someone else makes the interface promiscuous
  u_char *myea = (u_char *)&dpc->dpchudp_eth;
  u_short ea1 = (myea[0]<<8)|myea[1];
  u_long ea2 = (((myea[2]<<8)|myea[3])<<8|myea[4])<<8 | myea[5];
  *p++ = BPFI_LD(PKBOFF_EDEST+2);	/* last word of Ether dest */
  *p++ = BPFI_CAME(ea2);
  *p++ = BPFI_SKIP(3); /* no match, skip forward and check for broadcast */
  *p++ = BPFI_LDH(PKBOFF_EDEST);  /* get first part of dest addr */
  *p++ = BPFI_CAMN(ea1);
  *p++ = BPFI_RETWIN();		/* match both, win! */
  *p++ = BPFI_LD(PKBOFF_EDEST+2);	/* 1st word of Ether dest again */
  *p++ = BPFI_CAME(0xffffffff);	/* last hword is broadcast? */
  *p++ = BPFI_RETFAIL();
  *p++ = BPFI_LDH(PKBOFF_EDEST);  /* get first part of dest addr */
  *p++ = BPFI_CAME(0xffff);
  *p++ = BPFI_RETFAIL();	/* nope */
#endif
  *p++ = BPFI_RETWIN();		/* win */

    pfp->bf_len = p - pfp->bf_insns;	/* Set # of items on list */

    if (DP_DBGFLG)			/* If debugging, print out resulting filter */
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

    fprintf(stderr, "[%s: kernel packetfilter pri <>, len %d:\r\n",
	    progname,
	    /* pf->PF_PRIO, */ pf->bf_len);
    for (i = 0; i < pf->bf_len; ++i)
	fprintf(stderr, "%04X %2d %2d %0X\r\n",
		pf->bf_insns[i].code,
		pf->bf_insns[i].jt,
		pf->bf_insns[i].jf,
		pf->bf_insns[i].k);
    fprintf(stderr, "]\r\n");
}

#endif /* KLH10_NET_PCAP */

/* **** Chaosnet ARP functions **** */

void init_arp_table()
{
  struct dpchudp_s *dpc = (struct dpchudp_s *)dp.dp_adr;
  memset((char *)dpc->charp_list, 0, sizeof(struct charp_ent)*CHARP_MAX);
  dpc->charp_len = 0;
}

void print_arp_table()
{
  int i;
  struct dpchudp_s *dpc = (struct dpchudp_s *)dp.dp_adr;
  if (dpc->charp_len > 0) {
    printf("Chaos ARP table:\r\n"
	   "Chaos\tEther\t\t\tAge (s)\r\n");
    for (i = 0; i < dpc->charp_len; i++)
      printf("%#o\t\%02X:%02X:%02X:%02X:%02X:%02X\t%lu\r\n",
	     dpc->charp_list[i].charp_chaddr,
	     dpc->charp_list[i].charp_eaddr[0],
	     dpc->charp_list[i].charp_eaddr[1],
	     dpc->charp_list[i].charp_eaddr[2],
	     dpc->charp_list[i].charp_eaddr[3],
	     dpc->charp_list[i].charp_eaddr[4],
	     dpc->charp_list[i].charp_eaddr[5],
	     (time(NULL) - dpc->charp_list[i].charp_age));
  }
}

u_char *find_arp_entry(u_short daddr)
{
  int i;
  struct dpchudp_s *dpc = (struct dpchudp_s *)dp.dp_adr;
  u_short my_chaddr = dpc->dpchudp_myaddr;

  if (DP_DBGFLG) dbprintln("Looking for ARP entry for %#o, ARP table len %d", daddr, dpc->charp_len);
  if (daddr == my_chaddr) {
    dbprintln("#### Looking up ARP for my own address, BUG!");
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
  struct dpchudp_s *dpc = (struct dpchudp_s *)dp.dp_adr;
  u_short my_chaddr = dpc->dpchudp_myaddr;
  u_char req[sizeof(struct arphdr)+(ETHER_ADDR_LEN+2)*2];
  struct arphdr *arp = (struct arphdr *)&req;
  u_short arptyp = htons(ETHERTYPE_ARP);

  // first ethernet header
  memcpy(&pktbuf[0], dest_eth, ETHER_ADDR_LEN);  /* dest ether */
  ea_set(&pktbuf[ETHER_ADDR_LEN], dpc->dpchudp_eth);  /* source ether = me */
  memcpy(&pktbuf[ETHER_ADDR_LEN*2], &arptyp, 2);  /* ether pkt type */

  // now arp pkt
  memset(&req, 0, sizeof(req));
  arp->ar_hrd = htons(ARPHRD_ETHER); /* Want ethernet address */
  arp->ar_pro = htons(ETHERTYPE_CHAOS);	/* of a Chaosnet address */
  arp->ar_hln = ETHER_ADDR_LEN;
  arp->ar_pln = sizeof(dest_chaddr);
  arp->ar_op = htons(atyp);
  memcpy(&req[sizeof(struct arphdr)], dpc->dpchudp_eth, ETHER_ADDR_LEN);	/* my ether */
  memcpy(&req[sizeof(struct arphdr)+ETHER_ADDR_LEN], &my_chaddr, sizeof(my_chaddr)); /* my chaos */
  /* his ether */
  if (atyp == ARPOP_REPLY)
    memcpy(&req[sizeof(struct arphdr)+ETHER_ADDR_LEN+2], dest_eth, ETHER_ADDR_LEN);
  /* his chaos */
  memcpy(&req[sizeof(struct arphdr)+ETHER_ADDR_LEN+2+ETHER_ADDR_LEN], &dest_chaddr, sizeof(my_chaddr));

  // and merge them
  memcpy(&pktbuf[ETHER_ADDR_LEN*2+2], req, sizeof(req));

  if (DP_DBGFLG) {
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
  char b1[3],b2[3];

    fprintf(stderr,"CHUDP version %d, function %d\r\n", ucp[0], ucp[1]);
    fprintf(stderr,"Opcode: %o (%s), unused: %o\r\nFC: %o, Nbytes %o\r\n",
	    ucp[0+DPCHUDP_DATAOFFSET], ch_opcode(ucp[0+DPCHUDP_DATAOFFSET]),
	    ucp[1+DPCHUDP_DATAOFFSET], ucp[2+DPCHUDP_DATAOFFSET]>>4, ((ucp[2+DPCHUDP_DATAOFFSET]&0xf)<<4) | ucp[3+DPCHUDP_DATAOFFSET]);
    fprintf(stderr,"Dest host: %o, index %o\r\nSource host: %o, index %o\r\n",
	    (ucp[4+DPCHUDP_DATAOFFSET]<<8)|ucp[5+DPCHUDP_DATAOFFSET], (ucp[6+DPCHUDP_DATAOFFSET]<<8)|ucp[7+DPCHUDP_DATAOFFSET], 
	    (ucp[8+DPCHUDP_DATAOFFSET]<<8)|ucp[9+DPCHUDP_DATAOFFSET], (ucp[10+DPCHUDP_DATAOFFSET]<<8)|ucp[11+DPCHUDP_DATAOFFSET]);
    fprintf(stderr,"Packet #%o\r\nAck #%o\r\n",
	    (ucp[12+DPCHUDP_DATAOFFSET]<<8)|ucp[13+DPCHUDP_DATAOFFSET], (ucp[14+DPCHUDP_DATAOFFSET]<<8)|ucp[15+DPCHUDP_DATAOFFSET]);
    fprintf(stderr,"Data:\r\n");

    /* Skip headers */
    ucp += DPCHUDP_DATAOFFSET+CHAOS_HEADERSIZE;
    /* Show only data portion */
    cnt -= DPCHUDP_DATAOFFSET+CHAOS_HEADERSIZE+CHAOS_HW_TRAILERSIZE;

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
    fprintf(stderr,"HW trailer:\r\n  Dest: %o\r\n  Source: %o\r\n  Checksum: 0x%x\r\n",
	    (ucp[cnt]<<8)|ucp[cnt+1],(ucp[cnt+2]<<8)|ucp[cnt+3],(ucp[cnt+4]<<8)|ucp[cnt+5]);
}


char *
ch_adrsprint(char *cp, unsigned char *ca)
{
  sprintf(cp, "%#o", (ca[0]<<8) | ca[1]);
  return cp;
}

/* Add OSDNET shared code here */

/* OSDNET is overkill and assumes a lot of packet filter defs,
   which we're not at all interested in providing */
#if 1
# include "osdnet.c"
#else
char *
ip_adrsprint(char *cp, unsigned char *ia)
{
    sprintf(cp, "%d.%d.%d.%d", ia[0], ia[1], ia[2], ia[3]);
    return cp;
}
#endif
