/* DPCHUDP.C - Chaos over UDP process
*/
/*  Copyright © 2005 Björn Victor and Kenneth L. Harrenstien
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
   Some things are irrelevant inheritage from dpimp, and  could be cleaned up... */
/*
	This is a program intended to be run as a child of the KLH10
PDP-10 emulator, in order to provide a Chaos-over-UDP tunnel.
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

	"Messages" to and from the IMP are sent over the standard DP
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
#define OSN_USE_IPONLY 1	/* Only need IP stuff */ 
#include "osdnet.h"		/* OSD net defs, shared with DPNI20 and DPIMP */

#include <sys/resource.h>	/* For setpriority() */
#include <sys/mman.h>		/* For mlockall() */

#include "dpchudp.h"	/* DPCHUDP specific defs, grabs DPSUP if needed */


/* Globals */

struct dp_s dp;			/* Device-Process struct for DP ops */

int cpupid;			/* PID of superior CPU process */
int chpid;			/* PID of child (R proc) */
int swstatus = TRUE;
int sock;			/* UDP socket */


int myaddr;			/* My chaos address */
struct in_addr ihost_ip;	/* My host IP addr, net order */

/* FROM HERE: replace 
   "dpchudp" for "dpimp",
   "chudp" for "imp", 
*/

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
void ihl_hhsend(register struct dpchudp_s *dpchudp, int cnt, register unsigned char *pp);
void ip_write(struct in_addr *ipa, in_port_t ipport, unsigned char *buf, int len, struct dpchudp_s *dpchudp);


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
      if (!osn_ifipget(-1, dpchudp->dpchudp_ifnam, (unsigned char *)&ihost_ip)) {
	efatal(1,"osn_ifipget failed for \"%s\"", dpchudp->dpchudp_ifnam);
    }
#endif
    /* Set up appropriate net fd.
    */
  {
    struct sockaddr_in mysin;

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
      efatal(1,"socket failed: %s", dp_strerror(errno));

    mysin.sin_family = AF_INET;
    mysin.sin_port = htons(dpchudp->dpchudp_port);
    mysin.sin_addr.s_addr = INADDR_ANY;
#if 0
    if (dpchudp->dpchudp_ifnam[0] && ihost_ip.s_addr != 0)
      mysin.sin_addr.s_addr = ihost_ip.s_addr;
#endif

    if (bind(sock, (struct sockaddr *)&mysin, sizeof(mysin)) < 0)
      efatal(1,"bind failed: %s", dp_strerror(errno));
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

void
chudptohost(register struct dpchudp_s *dpchudp)
{
    register struct dpx_s *dpx = dp_dpxfr(&dp);
    register int cnt;
    unsigned char *inibuf;
    unsigned char *buffp;
    size_t max;
    int stoploop = 50;
    struct sockaddr_in ip_sender;
    socklen_t iplen;

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

	/* OK, now do a blocking read on UDP socket! */
	errno = 0;		/* Clear to make sure it's the actual error */
	memset(&ip_sender, 0, sizeof(ip_sender));
	iplen = sizeof(ip_sender); /* Supply size of ip_sender, and get actual stored length */
	cnt = recvfrom(sock, buffp, DPCHUDP_MAXLEN, 0, (struct sockaddr *)&ip_sender, &iplen);
	if (cnt <= DPCHUDP_DATAOFFSET) {
	    if (DBGFLG)
		fprintf(stderr, "[dpchudp-R: ERead=%d, Err=%d]\r\n",
					cnt, errno);

	    if (cnt < 0 && (errno == EINTR))	/* Ignore spurious signals */
		continue;

	    /* Error of some kind */
	    fprintf(stderr, "[dpchudp-R: Eread = %d, ", cnt);
	    if (cnt < 0) {
		if (--stoploop <= 0)
		    efatal(1, "Too many retries, aborting]");
		fprintf(stderr, "errno %d = %s]\r\n",
				errno, dp_strerror(errno));
	    } else if (cnt > 0)
		fprintf(stderr, "no chudp data]\r\n");
	    else fprintf(stderr, "no packet]\r\n");

	    continue;		/* For now... */
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
	  continue;
	}
	switch (((struct chudp_header *)buffp)->chudp_function) {
	case CHUDP_PKT:
	  break;		/* deliver it */
	default:
	  error("Unknown CHUDP function: %0X",
		((struct chudp_header *)buffp)->chudp_function);
	  continue;
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

	ihl_hhsend(dpchudp, cnt, buffp);
    }
}


/* Send regular message from CHUDP to HOST.
*/

void
ihl_hhsend(register struct dpchudp_s *dpchudp,
	   int cnt,
	   register unsigned char *pp)
	/* "pp" is packet data ptr, has room for header preceding */
{

    /* Send up to host!  Assume we're already in shared buffer. */

    register struct dpx_s *dpx = dp_dpxfr(&dp);
    size_t off = DPCHUDP_DATAOFFSET;

    dpchudp->dpchudp_inoff = off; /* Tell host what offset is */
    dp_xsend(dpx, DPCHUDP_RPKT, cnt-off);

    if (DBGFLG)
	fprintf(stderr, "[dpchudp-R: sent RPKT %d+%d]", (int)off, cnt-off);

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
#if 1
	{
	  int chlen = ((buff[DPCHUDP_DATAOFFSET+2] & 0xf) << 4) | buff[DPCHUDP_DATAOFFSET+3];
	  if ((chlen + CHAOS_HEADERSIZE + DPCHUDP_DATAOFFSET + CHAOS_HW_TRAILERSIZE) > rcnt) {
	    if (1 || DBGFLG)
	      dbprintln("NOT sending less than packet: pkt len %d, expected %d (Chaos data len %d)",
			rcnt, (chlen + CHAOS_HEADERSIZE + DPCHUDP_DATAOFFSET + CHAOS_HW_TRAILERSIZE),
			chlen);
	    dp_xrdone(dpx);	/* ack it to dvch11, but */
	    return;		/* #### don't send it! */
	  }
	}
#endif
	
	/* find IP destination given chaos packet  */
	if (hi_iproute(&ipdest, &ipport, &buff[DPCHUDP_DATAOFFSET], rcnt - DPCHUDP_DATAOFFSET,dpchudp)) {
	  ip_write(&ipdest, ipport, buff, rcnt, dpchudp);
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

  if (sendto(sock, buf, len, 0, (struct sockaddr *) &sin, (socklen_t) (sizeof(sin))) != len) {
    error("sendto failed - %s", dp_strerror(errno));
  }
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

void
dumppkt(unsigned char *ucp, int cnt)
{
  int i, row;

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
#if 0
      for (i = 0; (i < 8) && (i+row*8 < cnt); i++) {
	fprintf(stderr, "  %2c", ucp[i+row*8]);
	fprintf(stderr, "%2c", ucp[(++i)+row*8]);
      }
      fprintf(stderr, " (chars)\r\n");
      for (i = 0; (i < 8) && (i+row*8 < cnt); i++) {
	fprintf(stderr, "  %2c", ucp[i+1+row*8]);
	fprintf(stderr, "%2c", ucp[(i++)+row*8]);
      }
      fprintf(stderr, " (11-chars)\r\n");
#endif
    }
    /* Now show trailer */
    fprintf(stderr,"HW trailer:\r\n  Dest: %o\r\n  Source: %o\r\n  Checksum: 0x%x\r\n",
	    (ucp[cnt]<<8)|ucp[cnt+1],(ucp[cnt+2]<<8)|ucp[cnt+3],(ucp[cnt+4]<<8)|ucp[cnt+5]);
}


/* Add OSDNET shared code here */

/* OSDNET is overkill and assumes a lot of packet filter defs,
   which we're not at all interested in providing */
#if 0
# include "osdnet.c"
#else
char *
ip_adrsprint(char *cp, unsigned char *ia)
{
    sprintf(cp, "%d.%d.%d.%d", ia[0], ia[1], ia[2], ia[3]);
    return cp;
}
#endif
