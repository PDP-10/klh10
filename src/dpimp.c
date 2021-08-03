/* DPIMP.C - ARPANET IMP device emulator
*/
/* $Id: dpimp.c,v 2.5 2003/02/23 18:07:35 klh Exp $
*/
/*  Copyright � 1992-1999, 2001 Kenneth L. Harrenstien
**  All Rights Reserved
**
**  This file is part of the KLH10 Distribution.  Use, modification, and
**  re-distribution is permitted subject to the terms in the file
**  named "LICENSE", which contains the full text of the legal notices
**  and should always accompany this Distribution.
**
**  This software is provided "AS IS" with NO WARRANTY OF ANY KIND.
**
**  This notice (including the copyright and warranty disclaimer)
**  must be included in all copies or derivations of this software.
*/
/*
 * $Log: dpimp.c,v $
 * Revision 2.5  2003/02/23 18:07:35  klh
 * Add NetBSD read loop fix for bpf.
 *
 * Revision 2.4  2001/11/19 10:31:57  klh
 * Major revision of ARP code to do full ARP request/reply handling,
 * needed for Linux; may come in handy for other ports.
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*
	This is a program intended to be run as a child of the KLH10
PDP-10 emulator, in order to provide an IP-only ethernet interface
that looks like a simplified IMP.  It uses the host system's
packet-filtering mechanism to access the ethernet, and assumes that
only IP packets will be transferred in and out.  This mechanism might
be one of:
	PCAP - The portable packet capture library
	TAP  - Ethernet tunnel device
	TUN  - IP tunnel device (the simplest).

In what follows, "NET" is understood to be one of the above.

	"Messages" to and from the IMP are sent over the standard DP
shared memory mechanism; these are composed of 8-bit bytes in exactly
the same order as if an IMP was sending or receiving them.  Packets to
and from the NET are ethernet packets.

	There are actually two processes active, one which pumps data
from the NET to the IMP-output buffer, and another that pumps in the
opposite direction from an input buffer to the NET.  Generally they
are completely independent.

		-----------------------------

	The IP address used for the emulated host is whatever is
provided in the KLH10 configuration command for the LHDH device, which
interfaces to the IMP.  This address must obviously be identical to
what the emulated host OS thinks it is!

	Ensuring that inbound IP traffic is directed to the right
place is relatively easy.  First, the IP address for the emulated host
must conform to the architecture of the local ethernet that the native
host is on, so that routers/gateways will direct packets to the
correct LAN.  At this point, ARP takes over.

	ARPs are handled by the native host OS.  DPIMP arranges for
this by inserting an ARP entry into the kernel with the ATF_PUBL
(publish) flag set.  This entry has the emulated host IP address paired with
the native host device ethernet address; the ATF_PUBL flag persuades
the native OS to act as a server in response to ARPs for that IP address.
As long as the emulated host IP address fits within the local network
structure, so that routers/gateways will direct packets to the right
LAN, this ARP entry ensures that the packets will arrive at the native
host's ethernet interface.  Note: the code attempts to pick a reasonable
default for the local ethernet interface, unless a particular (and
possibly dedicated) interface is specified.

	The NET packet-filtering mechanism is used to filter out
IP-type ethernet packets where the IP datagram's destination matches
that of the emulated host.  While these IP datagrams are also
processed further within the OS when using a shared interface, it
appears that unless forwarding is explicitly set up or the kernel
compiled as a gateway, these datagrams will just be dropped on the
floor, which is precisely what we want.

	Representing the source address in the IMP leader provided to
the emulated host (as a 24-bit host/imp field) is problematical, as is
the reverse mapping.  Note that on receipt, only the 48-bit ethernet
address of the sender is known; the IP source address generally won't
be that of the last-hop sender.  The ITS code doesn't really need to
pay attention to this field on input, but on output something has to be
picked.

	Outbound routing in general is more difficult.  IP addresses
can be turned into local network addresses by using AF_INET type
addresses in the putmsg() call for NET output; this causes the output
interface to look the IP address up in the ARP tables, and do an actual
ARP if necessary.  However, this only works for the local network, and
is useless for finding a router or gateway; in order to send packets
elsewhere, the correct IP address must be known for a gateway/router on
the local net.
	(NIT WARNING!  This actually DOES NOT WORK for NIT because the
nit_if stream output code checks for and only accepts messages with
AF_UNSPEC addresses, so any NIT output must be accompanied by an
ethernet destination address.  There's a slim possibility that some
sort of "raw" socket output can be done using the NIT (via protosw
dispatch: pr_output entry in nitsw[] table for SOCK_RAW, pointed to by
nitdomain (AF_NIT).  Invoked by raw_usrreq in net/raw_usrreq.c.  Must
investigate... raw socket output uses sendto() to specify address
along with data.)

	One idea is to retain the use of ARPANET addresses for purposes
of talking between the emulated host and the DPIMP, such that only the
arpanet address of the host and a mythical prime gateway or two are known.
The main problem with this is that the ITS code would proceed to use its
ARPANET address in IP headers, which cannot be changed without revising
the IP checksum as well.

	The other method is to pretend that the local network is an IMP
class A network, and map the low 3 octets of IP addresses directly into
the 24-bit host/imp field.  However, it will be necessary to assemble
an ITS that knows its true IP address on the local net, and change the
prime-gateway routing table (at IPGWTG in INET >) to know about the
local net router/gateway.  Any IMP code that assumes it lives on the
ARPANET must likewise be changed, although this is not too hard.
	A bigger pain is that this code also must be modified to know
about the actual subnet mask in use, so it doesn't attempt to directly
send stuff that actually should go to the gateway.  Either ITS must know
the address architecture plus a default gateway, or DPIMP must.
	If the local net is a well-behaved class A, B, or C net then
things may work well at the ITS level.  If not (ie subnetting is in effect)
then perhaps DPIMP should deal with it.

	Compromise method: have ITS know its "real" IP address, so that
it will be inserted into all datagrams properly.  However, disregard the
host/imp address in the IMP leader and always have DPIMP decide for itself
where to send the datagram based on the IP destination address.  This
means DPIMP has to know the local subnetting arrangement and gateway routes
but this information can in principle be extracted from the kernel.
Under this scheme, it doesn't matter too much how the IMP leader
address is put together.  However, just to have SOME consistent scheme,
we'll use:
	IP octet 1 - Would go into NET field, but for now keep 0
	IP octet 2 - Host #
	IP octet 3 - IMP # high byte
	IP octet 4 - IMP # low byte

AGH!  Unfortunately it turns out that while the subnet mask can easily
be procured via an ioctl(), there is no way to get routing table
information other than by reading the kernel memory directly (which is
how netstat gets at the info).  So it's very painful for DPIMP to do
the routing.  Moreover, it will be inefficient for DPIMP to just use a
single default gateway if more than one is on the local network,
because redirects will not be recognized by DPIMP (unless even more
hair is added to intercept and understand them) and any attempt by ITS
to comply with the redirects it receives is useless as long as DPIMP
ignores the IMP leader addressing info.

	For now, let's build the IP address by slapping the 1st IP byte
on from the native host's IP address, and taking the rest from the host/imp
fields per scheme above.  DPIMP then checks to see if it's a local net
address, and if so uses that IP address.  If not (ITS thinks it's local,
but it isn't on right subnet) then DPIMP substitutes a single default
gateway address.  This resolves some but not all of the problem.

-------------------------------------------

Another ARP screw:
	On at some systems (OSF/1 for example) the native host's IP
address may not be listed in the kernel's ARP tables!  This means that
when the emulated host attempts to send an IP packet to the native
host, its attempt to determine the ethernet destination address will
fail because the arp_look() call can't find it.
	This can be fixed in a couple of ways:

(1) Invoke the "arp" command as SU to insert the (perm, pub) entry
    into the tables.
    Pro: ensures set to right thing, DPIMP works without change.
    Con: painful and subject to human error.

(2) DPIMP to look up the e/n address for the native host's default IP
    interface, and store that mapping in its own table.
    Pro: convenient, works invisibly.
    Con: if it picks wrong interface you're hosed invisibly.

Algorithm to use:
	- See if IMP ifc is same as default IP ifc.
	- If yes - shared, not dedicated.
		Get ether addr from pfdata.pf_FD as usual and copy into arptab cache.
	- If no - dedicated, not shared.
		See if native IP address can be found in OS ARP table.
		If yes - save in cache, done.
		If not - open another PF connection just to get EA for that
			ifc.

-------------------------------------------

TUN to the rescue:

	That being said, the new BSD "tun" (IP tunnel) device solves
most of these problems by moving all of the IP routing mechanisms into
the native OS itself.  Once initialized, constant ARP hacking is not
required, nor any ethernet header hacking -- in fact it should work
for any physical network!  KLH10_NET_TUN should eventually become the
default for every OS that implements /dev/tun.

*/

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>

#include "klh10.h"	/* Get config params */

/* This must precede any other OSD includes to ensure that DECOSF gets
   the right flavor sockaddr (sigh)
*/
#define OSN_USE_IPONLY 1	/* Only need IP stuff */ 
#include "osdnet.h"		/* OSD net defs, shared with DPNI20 */

#include <sys/resource.h>	/* For setpriority() */
#include <sys/mman.h>		/* For mlockall() */

#if CENV_SYS_NETBSD
#include <poll.h>
#endif

#include "dpimp.h"	/* DPIMP specific defs, grabs DPSUP if needed */

#ifdef RCSID
 RCSID(dpimp_c,"$Id: dpimp.c,v 2.5 2003/02/23 18:07:35 klh Exp $")
#endif


/* Structure of data shared between the two DPIMP forks; this is overlaid
   on the "blob" within dpimp_s.
   Currently only the ARP hackery uses this.

   For concurrency fans, the access mechanism implemented here is a variant
   of Peterson's algorithm, which in turn is akin to Dekker's algorithm.
 */
struct dpimpsh_s {
    /* Locking mechanism to control access. */
    int dpimpsh_lock[2];	/* Flag - trying to enter critical section */
    int dpimpsh_lockid;		/* Locker's ID */

    /* ARP cache, may or may not be needed */
    int dpimpsh_arpsiz;		/* # of entries in ARP table */
    int dpimpsh_arprefs;	/* crude timeout counter */
    struct arpent {
	struct in_addr at_iaddr;
	struct ether_addr at_eaddr;
	int at_flags;		/* ARPF_xxx flags, similar to ATF_xxx */
#define ARPF_INUSE 0x1
#define ARPF_COM   0x2
#define ARPF_PERM  0x4
	int at_lastref;		/* Value of arprefs at last ref */
    } dpimpsh_arptab[1];	/* Actually N entries! */
};

#define DPIMPSH(dpimp) ((struct dpimpsh_s *)dpimp->dpimp_blob)


/* Globals */

struct dp_s dp;			/* Device-Process struct for DP ops */

int cpupid;			/* PID of superior CPU process */
int chpid;			/* PID of child (R proc) */
int mylockid;			/* Locker IDs: 1 for W, 0 for R */
int othlockid;
int swstatus = TRUE;
struct pfdata pfdata;		/* Packet-Filter state */
struct osnpf npf;		/* Configuration data */

struct in_addr ehost_ip;	/* Emulated guest IP addr, net order */
struct in_addr ihost_ip;	/* IMP/Native host IP addr, net order */
struct in_addr ihost_nm;	/* IMP/Native host subnet netmask, net order */
struct in_addr ihost_net;	/* IMP/Native host net #, net order */
struct in_addr tun_ip;		/* IP addr of host side of tunnel */
struct in_addr gwdef_ip;	/* IP addr of default prime gateway */

struct ether_addr ehost_ea;	/* Emulated host ethernet addr */
struct ether_addr ihost_ea;	/* IMP/Native host ethernet addr */
int eaflags = 0;
#define EAF_IHOST  01		/* ihost_ea is set - IMP/Native host EA */
#define EAF_EHOST  02		/* ehost_ea is set - Emulated host EA */


/* Debug flag reference.  Use DBGFLG within functions that have "dpimp";
 * all others must use DP_DBGFLG.  Both refer to the same location.
 * Caching a local copy of the debug flag is a no-no because it lives
 * in a shared memory location that may change at any time.
 */
#define DBGFLG (dpimp->dpimp_dpc.dpc_debug)
#define DP_DBGFLG (((struct dpimp_s *)dp.dp_adr)->dpimp_dpc.dpc_debug)

/* Local predeclarations */

void imptohost(struct dpimp_s *);
void hosttoimp(struct dpimp_s *);

void net_init(struct dpimp_s *);

void arp_init(struct dpimp_s *);
struct arpent *arptab_look(struct in_addr);
struct arpent *arp_look(struct in_addr, struct ether_addr *);
struct arpent *arp_tnew(struct in_addr addr, struct ether_addr *, int);
int  arp_refreset(void);
void arp_set(struct arpent *, struct in_addr, struct ether_addr *, int);
void arp_req(struct in_addr *ipa);
void arp_gotrep(unsigned char *buf, int cnt);
void arp_reply(unsigned char *eap, unsigned char *iap);

int  hi_iproute(struct in_addr *ipa, unsigned char *lp, int cnt);
void ip_write(struct in_addr *, unsigned char *, int);
void ether_write(struct eth_header *, unsigned char *, int);

void ihl_frag(int, unsigned char *);
void ihl_hhsend(struct dpimp_s *, int, unsigned char *);
static void ihl_hhsend_nop(struct dpimp_s *dpimp);
void dumppkt(unsigned char *, int);

/* Error and diagnostic output */

static const char progname_i[] = "dpimp";
static const char progname_r[] = "dpimp-R";
static const char progname_w[] = "dpimp-W";
static const char *progname = progname_i;

static void efatal(int num, char *fmt, ...)
{
    fprintf(stderr, "\n[%s: Fatal error: ", progname);
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
    fprintf(stderr, "\n[%s: ", progname);
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
    fprintf(stderr, "\n[%s: ", progname);
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
    struct dpimp_s *dpimp;	/* Ptr to shared memory area */

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
    if (initdebug) {
	dbprintln("Starting");
	dbprintln("Supported ifmeth=%s", osn_networking);
    }

    /* Right off the bat attempt to get the highest scheduling priority
    ** we can, since a slow response will cause the 10 monitor to declare
    ** the interface dead.
    */
#if HAVE_SETPRIORITY
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
    if (geteuid() != 0) {
	error("*** Must usually run as superuser; networking may fail! ***");
    }

    if (!dp_main(&dp, argc, argv)) {
	efatal(1, "DP init failed!");
    }
    dpimp = (struct dpimp_s *)dp.dp_adr;	/* Make for easier refs */

    /* Verify that the structure version is compatible */
    if (dpimp->dpimp_ver != DPIMP_VERSION) {
	efatal(1, "Wrong version of DPIMP: lhdh=%0lx dpimp=%0lx",
	       (long)dpimp->dpimp_ver, (long)DPIMP_VERSION);
    }

    /* Now can access DP args!
       From here on we can use DBGFLG, which is actually a shared
       memory reference that dpimp points to.  Check here to accomodate the
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
#if HAVE_MLOCKALL
    if (!(dpimp->dpimp_dpc.dpc_flags & DPCF_MEMLOCK)) {
	if (mlockall(MCL_CURRENT|MCL_FUTURE) != 0) {
	    dbprintln("Warning - cannot lock memory");
	}
    }
#endif

    /* Now set up legacy variables based on parameters passed through
       shared DP area.
    */
    memcpy((void *)&ehost_ip, dpimp->dpimp_ip, 4);	/* Host IP addr */
    memcpy((void *)&tun_ip, dpimp->dpimp_tun, 4);	/* Tunnel addr */
    memcpy((void *)&gwdef_ip, dpimp->dpimp_gw, 4);	/* Default GW addr */
    memcpy((void *)&ehost_ea, dpimp->dpimp_eth, 6);	/* Host Ether addr */

    /* IMP must always have IP address specified! */
    if (memcmp(dpimp->dpimp_ip, "\0\0\0\0", 4) == 0)
	efatal(1, "no IP address specified");

    /* Canonicalize ARP hackery flags */
    if (dpimp->dpimp_doarp == TRUE)		/* If simply set to "true", */
	dpimp->dpimp_doarp = DPIMP_ARPF_PUBL |	/* then do all */
		DPIMP_ARPF_PERM | DPIMP_ARPF_OCHK;

    /* Set up shared area for the DPIMP forks */
    DPIMPSH(dpimp)->dpimpsh_lock[0] = 0;
    DPIMPSH(dpimp)->dpimpsh_lock[1] = 0;

    /* See if EA provided */
    if (memcmp((void *)&ehost_ea, "\0\0\0\0\0\0", 6) != 0) {
	eaflags |= EAF_EHOST;
    }

    /* Initialize various network info */
    net_init(dpimp);

    if (!pfdata.pf_ip4_only) {
	/* TUN may not have an ethernet address associated with it;
	    not sure what to do if DPIMP turns out to need one.
	 */

	/* See if ether address needs to be set */
	if (memcmp((void *)&ihost_ea, "\0\0\0\0\0\0", 6) != 0)
	    eaflags |= EAF_IHOST;

	switch (eaflags & (EAF_IHOST|EAF_EHOST)) {
	case 0:
	    efatal(1, "no ethernet address");
	case EAF_IHOST:
	    break;			/* OK, don't need anything special */
	case EAF_EHOST:
	    error("couldn't get native ether addr, using specified");
	    ea_set(&ihost_ea, &ehost_ea);
	    break;
	case EAF_IHOST|EAF_EHOST:
	    if (memcmp((void *)&ihost_ea, (void *)&ehost_ea, 6) == 0)
		break;		/* OK, addresses are same */
	    /* Ugh, specified an EA address different from one actually
	    ** in use by interface!  For now, don't allow clobberage.
	    */
	    efatal(1, "changing ethernet addr is disallowed");
	    break;
	}
    }

    /* Make this a status (rather than debug) printout? */
    if (swstatus) {
	char ipbuf[OSN_IPSTRSIZ];
	char eabuf[OSN_EASTRSIZ];

	if (pfdata.pf_ip4_only)  {
	    dbprintln("ifc \"%s\"",
		      dpimp->dpimp_ifnam);
	    dbprintln("  tun %s",
		      ip_adrsprint(ipbuf, (unsigned char *)&tun_ip));
	} else {
	    dbprintln("ifc \"%s\" => ether %s",
		      dpimp->dpimp_ifnam,
		      eth_adrsprint(eabuf, (unsigned char *)&ihost_ea));
	    dbprintln("  inet %s",
		      ip_adrsprint(ipbuf, (unsigned char *)&ihost_ip));
	    dbprintln("  netmask %s",
		      ip_adrsprint(ipbuf, (unsigned char *)&ihost_nm));
	    dbprintln("  net %s",
		      ip_adrsprint(ipbuf, (unsigned char *)&ihost_net));
	    dbprintln("  gwdef %s",
		      ip_adrsprint(ipbuf, (unsigned char *)&gwdef_ip));
	}
	dbprintln("  GUEST %s",
		  ip_adrsprint(ipbuf, (unsigned char *)&ehost_ip));
    }

    /* Init ARP stuff - ensure can talk to native host.
    ** Set up ARP entry so hardware host knows about our IP address and
    ** can respond to ARP requests for it.
    */
    if (!pfdata.pf_ip4_only) {
	arp_init(dpimp);
	if (!osn_arp_stuff(dpimp->dpimp_ifnam,
			   (unsigned char *)&ehost_ip,
			   (unsigned char *)&ihost_ea, TRUE))	/* Set us up */
	    esfatal(1, "OSN_ARP_STUFF failed");
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
	mylockid = 0;
	othlockid = 1;

	/* Fix up xfer mechanism so ACK of DP input goes to correct proc */
	dp.dp_adr->dpc_frdp.dpx_donpid = getpid();

	/* And ensure its memory is locked too, since the lockage isn't
	** inherited over a fork().  Don't bother warning if it fails.
	*/
#if HAVE_MLOCKALL
	(void) mlockall(MCL_CURRENT|MCL_FUTURE);
#endif
	progname = progname_r;	/* Reset progname to indicate identity */
	imptohost(dpimp);	/* Child process handles input */
    }
    mylockid = 1;
    othlockid = 0;
    progname = progname_w;	/* Reset progname to indicate identity */

    hosttoimp(dpimp);		/* Parent process handles output */

    osn_pfdeinit(&pfdata, &npf);/* Clean up created tunnels etc */
    dp_xrdone(dp_dpxto(&dp));

    return 1;
}

/* NET_INIT - Initialize net-related variables,
**	given network interface we'll use.
*/
void
net_init(struct dpimp_s *dpimp)
{
    struct ifreq ifr;
    char *ifnam_for_ipaddr = NULL;/* which interface to ask IP addr/netmask */

    if (osn_iftab_init() <= 0)
	esfatal(0, "Couldn't find interface information");

    if (strcmp(dpimp->dpimp_ifmeth, "pcap") != 0) {
	/*
	 * TUN or TAP or TAP+BRIDGE method.
	 * It is horrible to look at ifmeth; we would really like to
	 * look at something that tells us if the given interface name
	 * is one that the host uses for external communications.
	 */
	struct ifent *ife = osn_ipdefault();
	if (!ife)
	    esfatal(0, "Couldn't find default interface");
	ifnam_for_ipaddr = ife->ife_name;
    } else {
	/* PCAP method */
#if 1	/* This code is identical to dpni20 - merge in osdnet? */

	/* Ensure network device name, if specified, isn't too long */
	if (dpimp->dpimp_ifnam[0] && (strlen(dpimp->dpimp_ifnam)
		    >= sizeof(ifr.ifr_name))) {
	    esfatal(0, "interface name \"%s\" too long - max %d",
		    dpimp->dpimp_ifnam, (int)sizeof(ifr.ifr_name));
	}

	/* Determine network device to use, if none was specified (this only
	** works for shared devices, as dedicated ones will be "down" and
	** cannot be found by iftab_init).
	** Also grab native IP and ethernet addresses, if ARP might need them.
	*/
	if ((!dpimp->dpimp_ifnam[0] && !dpimp->dpimp_dedic)
	  || (dpimp->dpimp_doarp & DPIMP_ARPF_OCHK)) {
	    /* Found at least one!  Pick first one, if a default is needed. */
	    if (!dpimp->dpimp_ifnam[0]) {
		struct ifent *ife = osn_ipdefault();
		if (!ife)
		    esfatal(0, "Couldn't find default interface");
		if (strlen(ife->ife_name) >= sizeof(dpimp->dpimp_ifnam))
		    esfatal(0, "Default interface name \"%s\" too long, max %d",
			ife->ife_name, (int)sizeof(dpimp->dpimp_ifnam));

		strcpy(dpimp->dpimp_ifnam, ife->ife_name);
		if (swstatus)
		    dbprintln("Using default interface \"%s\"", dpimp->dpimp_ifnam);
	    }

	    ifnam_for_ipaddr = dpimp->dpimp_ifnam;
	}
#endif /* 1 */
    }

    /* Set up appropriate net fd and packet filter.
    ** Should also determine interface's ethernet addr, if possible,
    ** and set ihost_ea.
    */
    npf.osnpf_ifnam = dpimp->dpimp_ifnam;
    npf.osnpf_ifmeth = dpimp->dpimp_ifmeth;
    npf.osnpf_dedic = FALSE;		/* Force filtering always! */
    npf.osnpf_rdtmo = dpimp->dpimp_rdtmo;
    npf.osnpf_backlog = dpimp->dpimp_backlog;
    npf.osnpf_ip.ia_addr = ehost_ip;
    npf.osnpf_tun.ia_addr = tun_ip;
    /* Ether addr is both a potential arg and a returned value;
       the packetfilter open may use and/or change it.
       */
    ea_set(&npf.osnpf_ea, dpimp->dpimp_eth);/* Set requested ea if any */
    osn_pfinit(&pfdata, &npf, (void *)dpimp);/* Will abort if fails */
    ea_set(&ihost_ea, &npf.osnpf_ea);	/* Copy actual ea if one */
    tun_ip = npf.osnpf_tun.ia_addr;		/* Copy actual tun if any */

    if (!pfdata.pf_ip4_only) {
	/* Now set remaining stuff */

	/* Find IMP host's IP address for this interface */
	if (!osn_ifiplookup(ifnam_for_ipaddr, (unsigned char *)&ihost_ip)) {
	    efatal(1,"osn_ifipget failed for \"%s\"", dpimp->dpimp_ifnam);
	}

	/* Ditto for its network mask */
	if (!osn_ifnmlookup(ifnam_for_ipaddr, (unsigned char *)&ihost_nm)) {
	    efatal(1,"osn_ifnmlookup failed for \"%s\"", dpimp->dpimp_ifnam);
	}

	/* Now set remaining stuff */
	ihost_net.s_addr = ihost_nm.s_addr & ihost_ip.s_addr;	/* Local net */

	/* Either move this check up much earlier, or find a way to
	** query OS for a default gateway.
	*/
	if (gwdef_ip.s_addr == -1 || gwdef_ip.s_addr == 0)
	    efatal(1, "No default prime gateway specified");
    }
}

/* The DPIMP packet filter must implement the following test:
**
**	if ((dest_IP_addr == ITS_IP_addr) && (ethertype == IP)) { success; }
**
** For efficiency, the strictest test is done first -- the low shortword
** of the IP address, which is the most likely to differ from that of the
** native host.  We do this even before checking whether the packet contains
** IP data; if it's too short, it's rejected anyway.
*/

#if KLH10_NET_PCAP

/*
** BPF filter program stuff.
** Note that you can also obtain a specific filter program for a given
** expression by using tcpdump(1) with the -d option, for example:
**   tcpdump -d -s 1514 ip dst host 1.2.3.4
** produces:
*/
#if 0
      { 0x20, 0, 0, 0x0000001e },	/* (000) ld [30] */
      { 0x15, 0, 3, 0x01020304 },	/* (001) jeq #0x1020304 jt 2 jf 5 */
      { 0x28, 0, 0, 0x0000000c },	/* (002) ldh [12] */
      { 0x15, 0, 1, 0x00000800 },	/* (003) jeq #0x800 jt 4 jf 5 */
      { 0x06, 0, 0, 0x000005ea },	/* (004) ret #1514 */
      { 0x06, 0, 0, 0x00000000 },	/* (005) ret #0 */
#endif

#define BPF_PFMAX 50		/* Max instructions in BPF filter */
struct bpf_insn    bpf_pftab[BPF_PFMAX];
struct bpf_program bpf_pfilter = { 0,
				   bpf_pftab };

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


static void pfshow(struct bpf_program *);

struct bpf_program *
pfbuild(void *arg, struct in_addr *ipa)
{
    struct dpimp_s *dpimp = (struct dpimp_s *)arg;
    struct bpf_program *pfp = &bpf_pfilter;
    struct bpf_insn *p;

    p = pfp->bf_insns;		/* Point to 1st instruction in BPF program  */

    /* We're interested in IP (and thus ARP as well) packets.
	This is assumed to be the LAST part of the filter, thus
	it must either leave the correct result on the stack, or
	ensure it is empty (if accepting the packet).
     */
    if (ipa->s_addr != 0) {

	/* Want to pass ARP replies as well, so we can see responses to any
	** ARPs we send out?
	** ARP *requests* are also passed!  The hope is that
	** osn_arp_stuff() will have ensured that the host platform
	** proxy-answers requests for our IP address, but that doesn't
	** always work.
	*/
	*p++ = BPFI_LDH(PKBOFF_ETYPE);		/* Load ethernet type field */
	*p++ = BPFI_CAMN(ETHERTYPE_ARP);	/* Skip unless ARP packet */
	*p++ = BPFI_RETWIN();			/* If ARP, win now! */

	/* If didn't pass, check for our IP address */
	*p++ = BPFI_LD(PKBOFF_IPDEST);		/* Get IP dest address */
	*p++ = BPFI_CAME(ntohl(ehost_ip.s_addr));	/* Skip if matches */
	*p++ = BPFI_RETFAIL();			/* Nope, fail */

	/* Passed IP check, one last thing... */
	*p++ = BPFI_LDH(PKBOFF_ETYPE);		/* Load ethernet type field */
	*p++ = BPFI_CAMN(ETHERTYPE_IP);		/* Skip unless IP packet */
	*p++ = BPFI_RETWIN();			/* If IP, win now! */
	*p++ = BPFI_RETFAIL();			/* Nope, fail */

    } else {
	/* If not doing IP, fail at this point because the packet
	    doesn't match any of the desired types.
	*/
	*p++ = BPFI_RETFAIL();			/* Fail */
    }

    pfp->bf_len = p - pfp->bf_insns;	/* Set # of items on list */

    if (DBGFLG)			/* If debugging, print out resulting filter */
	pfshow(pfp);
    return pfp;
}


/* Debug auxiliary to print out packetfilter we composed.
*/
static void
pfshow(struct bpf_program *pf)
{
    int i;

    fprintf(stderr, "[dpimp: kernel packetfilter pri <>, len %d:\r\n",
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


/* Because until very recently LNX had no kernel packet filtering, must do it
   manually.  Ugh!

   Call this even when using a dedicated interface, since only IP stuff
   should be passed through the IMP.

   Returns TRUE if packet OK, FALSE if it should be dropped.
   Note that the code parallels that for pfbuild().
*/
int lnx_filter(struct dpimp_s *dpimp,
	       unsigned char *bp,
	       int cnt)
{
    /* Code assumes buffer is at least shortword-aligned. */
    unsigned short *sp = (unsigned short *)bp;
    unsigned short etyp;

    /* Get ethernet protocol type.
       Could also test packet length, but for now assume higher level
       will take care of those checks.
     */
    etyp = ntohs(sp[PKSWOFF_ETYPE]);
    switch (etyp) {
	/* Must pass on ARP processing (Linux doesn't proxy ARP for us!!)
         */
    case ETHERTYPE_ARP:
	return TRUE;

    case ETHERTYPE_IP:
	/* For IP packet, return TRUE if IP destination matches ours */
	return (memcmp(dpimp->dpimp_ip, bp + PKBOFF_IPDEST, 4) == 0);

    }
    /* No other types allowed through IMP */

    return FALSE;
}



/* ARP hacking code.  Originally modelled after old BSD ARP stuff. */

/* Structure of DPIMP's ARP cache:

    The ARP cache is a simple table that is entered by means of a hash
function, but which has no separate bucket chains -- everything is on
the same "chain".  If a desired entry is not found at the first hash,
all succeeding entries are checked until either an empty entry is hit
or the entire table is checked.  This works because entries are never
flushed once entered.

    The rationale for this is that doing an external ARP lookup is
much more expensive than the time to do a full table scan, so we never
want to re-use an entry unless the table is full, and we want to use
all possible entries.

    If an ITS is ever brought up that becomes consistently busy enough
to thrash on the ARP table then a more sophisticated algorithm can be
used.

 */


/* Set up by init for easier reference */
static struct dpimpsh_s *arpp;
static struct arpent *arptab_lim;

#define	ARPTAB_HASH(max,a) \
	((unsigned long)(a) % (max))


/* ARP_INIT
**	Set up our own ARP cache, and ensure that native host is in
**	it.  Must be called after packetfilter already opened.
*/
void
arp_init(struct dpimp_s *dpimp)
{
    struct dpimpsh_s *dsh = DPIMPSH(dpimp);
    struct ether_addr ea;
    struct arpent *at;

    /* Init statics.  Find # entries available in shared-area ARP table,
       which is assumed to have been already cleared.
       Note we add 1 because 1st entry is already in dpimpsh_s.
     */
    arpp = dsh;
    dsh->dpimpsh_arpsiz = 1 + (
	(dpimp->dpimp_blobsiz <= sizeof(struct dpimpsh_s))
	 ? 0
	 : ((dpimp->dpimp_blobsiz - sizeof(struct dpimpsh_s))
	    / sizeof(struct arpent)));
    arptab_lim = &dsh->dpimpsh_arptab[dsh->dpimpsh_arpsiz];

    if ((at = arp_look(ihost_ip, &ea))) {
	/* It's now there, ensure it stays there */
	at->at_flags |= ARPF_PERM;
	return;
    }
    /* Not found in ARP cache or OS table */

    /* For now, assume shared and stuff an entry in our cache! */
    if (swstatus)
	dbprintln("no native ARP entry, assuming shared ifc");

    /* Store entry, say complete & permanent */
    (void) arp_tnew(ihost_ip, &ihost_ea, ARPF_PERM|ARPF_COM);
}

/* ARP_REFRESET - reset all reference stamps to compensate for
 *	overflow of the main ref counter.
 *	This will be a rare event, so rather than trying to do anything
 *	clever, just clear everything so all entries start from the
 *	same place.  This also obviates any need to lock since it doesn't
 *	matter if it's done twice.
 */
int
arp_refreset(void)
{
    struct dpimpsh_s *dsh = arpp;
    struct arpent *at = &dsh->dpimpsh_arptab[0];
    int max = dsh->dpimpsh_arpsiz;
    int i;

    for (i = 0; i < max; i++, at++) {
	at->at_lastref = 0;
    }
    return dsh->dpimpsh_arprefs = 1;
}


struct arpent *
arptab_look(struct in_addr addr)
{
    struct dpimpsh_s *dsh = arpp;
    int i;
    int max = dsh->dpimpsh_arpsiz;
    struct arpent *at =
	&dsh->dpimpsh_arptab[ARPTAB_HASH(max, addr.s_addr)];

    for (i = 0; i < max; i++, at++) {
	if (at >= arptab_lim)
	    at = &dsh->dpimpsh_arptab[0];
	if (at->at_flags == 0)
	    break;
	if (at->at_iaddr.s_addr == addr.s_addr)
	    return at;
    }
    return NULL;	/* Table full or hit empty entry */
}


/*
 * Enter a new address in arptab, pushing out the oldest entry 
 * from the bucket if there is no room.
 * This always succeeds since no bucket can be completely filled
 * with permanent entries.
 * If new entry matches an existing one, always replaces it; addr may
 * have changed!
 */
struct arpent *
arp_tnew(struct in_addr addr,
	 struct ether_addr *eap,
	 int flags)
{
    struct dpimpsh_s *dsh = arpp;
    int i;
    int max = dsh->dpimpsh_arpsiz;
    struct arpent *at =
	&dsh->dpimpsh_arptab[ARPTAB_HASH(max, addr.s_addr)];

    int oldest = dsh->dpimpsh_arprefs;
    struct arpent *ato = NULL;

    for (i = 0; i < max; i++, at++) {
	if (at >= arptab_lim)
	    at = &dsh->dpimpsh_arptab[0];
	if (at->at_flags == 0)
	    break;			/* Found an empty entry */
	if (at->at_iaddr.s_addr == addr.s_addr)
	    break;			/* Matches existing */
	if (at->at_flags & ARPF_PERM)
	    continue;			/* Never replace this */
	if (at->at_lastref < oldest) {
	    oldest = at->at_lastref;
	    ato = at;
	}
    }
    if (i >= max) {			/* No empty entry found? */
	if (ato == NULL) {
	    efatal(1, "ARP table choked?!");
	}
	at = ato;			/* Re-use oldest entry */
    }

    arp_set(at, addr, eap,
	    (at->at_flags & ARPF_PERM)	/* Preserve ATF_PERM if old entry */ 
	    | ARPF_INUSE | flags);
    return at;
}

/* ARP_SET - Set an ARP cache entry.  Done in one place to centralize
 * the update access control, even though it's quite simple.
 */
void
arp_set(struct arpent *at,
	struct in_addr addr,
	struct ether_addr *eap,
	int flags)
{
    struct dpimpsh_s *dsh = arpp;

    /* Get write lock */
    dsh->dpimpsh_lock[mylockid] = TRUE;
    dsh->dpimpsh_lockid = mylockid;
    while (dsh->dpimpsh_lock[othlockid]	/* Spin wait if other has it */
	   && (dsh->dpimpsh_lockid != mylockid));

    /* Start critical section */
    at->at_flags = flags;
    if (at->at_flags & ARPF_COM)	/* Use EA only if now complete */
	at->at_eaddr = *eap;
    else
	ea_clr(&at->at_eaddr);
    at->at_iaddr = addr;
    at->at_lastref = dsh->dpimpsh_arprefs;
    /* End critical section */

    dsh->dpimpsh_lock[mylockid] = FALSE;
}




/* ARP_LOOK - Look up Ethernet address given IP address.
**	If not in our own cache, checks system.
**	If not in system, fails.
*/
struct arpent *
arp_look(struct in_addr ip,
	 struct ether_addr *eap)
{
    struct arpent *at;

    at = arptab_look(ip);		/* Look up IP addr */
    if (at && (at->at_flags & ARPF_COM)) {
	int i;

	/* Exists and complete */
	ea_set(eap, &(at->at_eaddr));	/* Return ether addr */

	/* Note at_lastref is modified here without locking; this is OK */
	if ((i = ++(arpp->dpimpsh_arprefs)) < 0)
	    i = arp_refreset();
	at->at_lastref = i;
	return at;
    }

    /* Not found in our cache or not yet resolved, try OS query. */
    if (osn_iftab_arpget(ip, (unsigned char *)eap)	/* Try table lookup */
	|| osn_arp_look(&ip, (unsigned char *)eap)) {	/* Attempt OS lookup */
	at = arp_tnew(ip, eap, ARPF_COM);	/* Won!  Store in our cache */
	if (swstatus) {
	    char ipbuf[OSN_IPSTRSIZ];
	    char eabuf[OSN_EASTRSIZ];
	    dbprintln("ARP cached %s = %s",
		      ip_adrsprint(ipbuf, (unsigned char *)&ip),
		      eth_adrsprint(eabuf, (unsigned char *)eap));
	}
	return at;
    }
    return NULL;
}

struct offset_ether_arp {
    unsigned char offset[DPIMP_DATAOFFSET];
    struct ether_arp arp;
};

/* ARP_REQ - Generates and sends ARP request.
   Must remember the fact in our cache, so can process reply ourself
   if any is received.
*/
void
arp_req(struct in_addr *ipa)
{
    static int ethbuild = 0, arpbuild = 0;
    static struct eth_header eh;
    static struct offset_ether_arp arp;
    struct ether_addr ea;

    /* Store request in cache */
    memset((char *)&ea, 0, sizeof(ea));
    arp_tnew(*ipa, &ea, 0);		/* Say incomplete with 0 flag */

    /* Build ethernet header if haven't already */
    if (!ethbuild) {
	memset(eh_dptr(&eh), 0xff,		/* Set dest broadcast addr */
				ETHER_ADRSIZ);
	eh_sset(&eh, &ihost_ea);		/* Set ether source addr */
	eh_tset(&eh, ETHERTYPE_ARP);
	ethbuild = TRUE;
    }

    /* Now put together the ARP packet */
    if (!arpbuild) {
	arp.arp.arp_hrd = htons(ARPHRD_ETHER);	/* Set hdw addr format */
	arp.arp.arp_pro = htons(ETHERTYPE_IP);	/* Set ptcl addr fmt */
	arp.arp.arp_hln = sizeof(arp.arp.arp_sha);	/* Hdw address len */
	arp.arp.arp_pln = sizeof(arp.arp.arp_spa);	/* Ptcl address len */
	arp.arp.arp_op = htons(ARPOP_REQUEST);	/* Type REQUEST */
	ea_set(arp.arp.arp_sha, &ihost_ea);		/* Sender hdw addr */
	memcpy((char *)arp.arp.arp_spa,		/* Sender IP addr */
		(char *)&ihost_ip, sizeof(arp.arp.arp_spa));
	arpbuild = TRUE;
    }

    /* Now do only thing that varies -- set IP addr we're looking up. */
    memcpy((char *)arp.arp.arp_tpa,			/* Target IP addr */
		(char *)ipa, sizeof(arp.arp.arp_tpa));

    /* Now send it! */
    if (swstatus) {
	char ipbuf[OSN_IPSTRSIZ];
	dbprintln("ARP req %s", ip_adrsprint(ipbuf, (unsigned char *)ipa));
    }

    ether_write(&eh, (unsigned char *)&arp.arp, sizeof(arp.arp));
}


/* ARP_GOTREP - Process an ARP Request or Reply.
   If it matches a request we already sent out, remember its
   information.  Next time we try sending a packet to that IP address
   we'll find the entry.
   We should respond to ARP requests for our own IPv4 address.
*/

#define ARP_PKTSIZ (sizeof(struct ether_header)	+ sizeof(struct ether_arp))

void
arp_gotrep(unsigned char *buf, int cnt)
{
    struct ether_arp *aa;
    struct arpent *at;
    struct arpent ent;

    if (DP_DBGFLG) {
	char eabuf[OSN_EASTRSIZ];

	dbprintln("Got ARP from %s", eth_adrsprint(eabuf, eh_sptr(buf)));
    }

    /* Verify packet is an ether ARP reply */
    if (cnt < ARP_PKTSIZ) {
	if (DP_DBGFLG)
	    dbprintln("Dropped ARP, size %d < %d", cnt, (int)ARP_PKTSIZ);
	return;
    }
    aa = (struct ether_arp *)(buf + ETHER_HDRSIZ);
    if (aa->arp_hrd != htons(ARPHRD_ETHER)) {	/* Check hdw addr format */
	if (DP_DBGFLG)
	    dbprintln("Dropped ARP, hrd %0x != %0x",
		      aa->arp_hrd, ARPHRD_ETHER);
	return;
    }
    if (aa->arp_pro != htons(ETHERTYPE_IP)) {	/* Check ptcl addr fmt */
	if (DP_DBGFLG)
	    dbprintln("Dropped ARP, pro %0x != %0x",
		      aa->arp_pro, ETHERTYPE_IP);
	return;
    }
    if (aa->arp_hln != sizeof(aa->arp_sha)) {	/* Check Hdw address len */
	if (DP_DBGFLG)
	    dbprintln("Dropped ARP, hln %d != %d",
		      aa->arp_hln, sizeof(aa->arp_sha));
	return;
    }
    if (aa->arp_pln != sizeof(aa->arp_spa)) {	/* Check Ptcl address len */
	if (DP_DBGFLG)
	    dbprintln("Dropped ARP, pln %d != %d",
		      aa->arp_pln, sizeof(aa->arp_spa));
	return;
    }
    /* Passed so far!  Determine nature of ARP packet */
    if (aa->arp_op == htons(ARPOP_REQUEST)) {
	/* See if request is targetted at our IP address */
	memcpy((char *)&ent.at_iaddr, (char *)aa->arp_tpa, IP_ADRSIZ);
	if (ent.at_iaddr.s_addr == ehost_ip.s_addr) {
	    /* Yep!  Send back our reply! */
	    arp_reply((unsigned char *)aa->arp_sha,
		      (unsigned char *)aa->arp_spa);
	    return;
	}
	if (DP_DBGFLG) {
	    char ipbuf[OSN_IPSTRSIZ];
	    char eabuf[OSN_EASTRSIZ];

	    dbprintln("Dropped ARP req for %s from %s",
		      ip_adrsprint(ipbuf, (unsigned char *)&ent.at_iaddr),
		      eth_adrsprint(eabuf, (unsigned char *)&aa->arp_sha));
	}
	return;
    }
    if (aa->arp_op != htons(ARPOP_REPLY)) {	/* Check ARP type REPLY */
	if (DP_DBGFLG)
	    dbprintln("Dropped ARP, type %0x != (req | rep)", aa->arp_op);
	return;
    }

    /* Passed!  Now extract resolved IP and EA from sender fields */
    memcpy((char *)&ent.at_iaddr, (char *)aa->arp_spa, IP_ADRSIZ);
    memcpy((char *)&ent.at_eaddr, (char *)aa->arp_sha, ETHER_ADRSIZ);

    /* Now look up and determine if it's for an outstanding request of ours */
    at = arptab_look(ent.at_iaddr);
    if (!at || (at->at_flags & ARPF_COM)) {
	if (DP_DBGFLG) {
	    char ipbuf[OSN_IPSTRSIZ];
	    dbprintln("Dropped ARP reply, %s IP %s",
		      (at ? "no req for" : "already have"),
		      ip_adrsprint(ipbuf, (unsigned char *)&ent.at_iaddr));
	}
	return;
    }

    /* Success! */
    arp_set(at,
	    at->at_iaddr,
	    &ent.at_eaddr,		/* Remember new ether addr */
	    at->at_flags | ARPF_COM);	/* Say entry now complete */
    if (swstatus) {
	char ipbuf[OSN_IPSTRSIZ];
	char eabuf[OSN_EASTRSIZ];
	dbprintln("ARP cached %s = %s",
		  ip_adrsprint( ipbuf, (unsigned char *)&ent.at_iaddr),
		  eth_adrsprint(eabuf, (unsigned char *)&ent.at_eaddr));
    }
}


/* ARP_REPLY - Send out an ARP Reply for ourselves
 */
void
arp_reply(unsigned char *eap,	/* Requestor ether addr */
	  unsigned char *iap)	/* Requestor IP addr */
{
    struct eth_header eh;
    struct offset_ether_arp arp;

    /* Build ethernet header */
    eh_dset(&eh, eap);			/* Set dest addr */
    eh_sset(&eh, &ihost_ea);		/* Set ether source addr */
    eh_tset(&eh, ETHERTYPE_ARP);

    /* Now put together the ARP packet */
    arp.arp.arp_hrd = htons(ARPHRD_ETHER);	/* Set hdw addr format */
    arp.arp.arp_pro = htons(ETHERTYPE_IP);	/* Set ptcl addr fmt */
    arp.arp.arp_hln = sizeof(arp.arp.arp_sha);	/* Hdw address len */
    arp.arp.arp_pln = sizeof(arp.arp.arp_spa);	/* Ptcl address len */
    arp.arp.arp_op = htons(ARPOP_REPLY);	/* Type REPLY */

    memcpy((char *)arp.arp.arp_spa,		/* Sender IP addr */
	    (char *)&ihost_ip, sizeof(arp.arp.arp_sha));

    /* Sender hdw addr and IP addr (that's us - the resolved info) */
    ea_set(arp.arp.arp_sha, &ihost_ea);	/* Sender hdw addr */
    memcpy((char *)arp.arp.arp_spa, (char *)&ehost_ip, IP_ADRSIZ);

    /* Target hdw addr and IP addr (for politeness?) */
    ea_set(arp.arp.arp_tha, eap);		/* Target hdw addr */
    memcpy((char *)arp.arp.arp_tpa, iap, IP_ADRSIZ);

    /* Now send it! */
    if (swstatus) {
	char ipbuf[OSN_IPSTRSIZ];
	char eabuf[OSN_EASTRSIZ];
	char ipbuf2[OSN_IPSTRSIZ];
	char eabuf2[OSN_EASTRSIZ];

	dbprintln("ARP reply sent to %s %s (%s %s)",
		  ip_adrsprint(ipbuf, iap),
		  eth_adrsprint(eabuf2, (unsigned char *)&arp.arp.arp_tha),
		  ip_adrsprint(ipbuf2, (unsigned char *)&arp.arp.arp_spa),
		  eth_adrsprint(eabuf, (unsigned char *)&arp.arp.arp_sha));
    }

    ether_write(&eh, (unsigned char *)&arp.arp, sizeof(arp.arp));
}

/* IMPTOHOST - Child-process main loop for pumping packets from IMP to HOST.
**	Reads packets from net, fragments if necessary, and feeds
**	IMP packets to DP superior process.
*/
# define MAXETHERLEN 1600	/* Actually 1519 but be generous */

#define NINBUFSIZ (DPIMP_DATAOFFSET+MAXETHERLEN)

void
imptohost(struct dpimp_s *dpimp)
{
    struct dpx_s *dpx = dp_dpxfr(&dp);
    int cnt;
    unsigned char *inibuf;
    unsigned char *buffp;
    size_t max;
    int stoploop = 50;
    int ether_hdr_offset;

    inibuf = dp_xsbuff(dpx, &max);	/* Get initial buffer ptr */

    /* Tell KLH10 we're initialized and ready by sending initial packet */
    dp_xswait(dpx);			/* Wait until buff free, in case */
    dp_xsend(dpx, DPIMP_INIT, 0);	/* Send INIT */

    if (DBGFLG)
	fprintf(stderr, "[dpimp-R: sent INIT]\r\n");

    if (pfdata.pf_ip4_only) {
	ether_hdr_offset = 0;
    } else {
	ether_hdr_offset = ETHER_HDRSIZ;
    }

    // wait here for first NOP received
    dp_xswait(dpx);
    // then send a NOP reply to host
    ihl_hhsend_nop(dpimp);

    for (;;) {
	/* Make sure that buffer is free before clobbering it */
	dp_xswait(dpx);			/* Wait until buff free */

	if (DBGFLG)
	    fprintf(stderr, "[dpimp-R: InWait]\r\n");

	/* Set up buffer and initialize offsets */
	buffp = inibuf + DPIMP_DATAOFFSET - ether_hdr_offset;

	/* OK, now do a blocking read on packetfilter input! */
	cnt = osn_pfread(&pfdata, buffp, MAXETHERLEN);

	if (cnt <= ether_hdr_offset) {
	    /* If call times out due to E/BIOCSRTIMEOUT, will return 0 */
	    if (cnt == 0 && dpimp->dpimp_rdtmo)
		continue;		/* Just try again */
	    if (DBGFLG)
		dbprintln("ERead=%d, Err=%d", cnt, errno);

	    if (cnt >= 0) {
		dbprintln("Eread = %d, %s", cnt,
			  (cnt > 0) ? "no ether data" : "no packet");
		continue;
	    }

	    /* System call error of some kind */
	    if (errno == EINTR)		/* Ignore spurious signals */
		continue;

	    syserr(errno, "Eread = %d, errno %d", cnt, errno);
	    if (--stoploop <= 0)
		efatal(1, "Too many retries, aborting");
	    continue;		/* For now... */
	}
	if (DBGFLG) {
	    if (DBGFLG & 0x4) {
		fprintf(stderr, "\r\n[dpimp-R: Read=%d\r\n", cnt);
		dumppkt(buffp, cnt);
		fprintf(stderr, "]");
	    }
	    else
		dbprint("Read=%d", cnt);
	}

	/* Have packet, now dispatch it to host */

	/* If there hasn't been any packet filtering yet, then must apply
	 * manual check to each and every packet read, even if dedicated.
	 */
	if (!pfdata.pf_can_filter) {
	    if (!lnx_filter(dpimp, buffp, cnt)) {
		if (DBGFLG)
		    dbprint("Dropped");
		continue;		/* Drop packet, continue reading */
	    }
	}

	if (!pfdata.pf_ip4_only) {
	    /* Verify that pf filtering is doing its job */
	    switch (eh_tget((struct eth_header *)buffp)) {
	    case ETHERTYPE_IP:
		break;
	    case ETHERTYPE_ARP:		/* If ARP, */
		arp_gotrep(buffp, cnt);	/* attempt to process replies */
		continue;		/* and always drop packet */
	    default:
		error("Non-IP ether packet: %0X",
			    eh_tget((struct eth_header *)buffp));
		continue;
	    }
	}

	/* OK, it claims to be an IP packet, see if so long that we
	** need to fragment it.  Yech!
	*/
	cnt -= ether_hdr_offset;
	buffp += ether_hdr_offset;

	if (cnt > SI_MAXMSG) {
	    ihl_frag(cnt, buffp);
	} else {
	    /* Small enough to constitute one IMP message, so pass it on! */
	    ihl_hhsend(dpimp, cnt, buffp);
	}

    }
}

void
ihl_frag(int cnt, unsigned char *pp)
{
    /* For now, just drop it. */
    error("Too-large packet (%d), can't fragment yet", cnt);
}


/* Send regular message from IMP to HOST.
**	One problem here is what value to put in as the source host/imp.
** All we have is the ethernet source address (the IP header source addr
** is not meaningful as it is that of the ultimate source, not the
** last gateway/router).
**	One possibility is to query the native host's ARP tables to look
** up the IP address for that ethernet address, and translate that.
**	Another tactic is to do the inverse of hi_iproute() by
** extracting the IP source addr and seeing if it's on our local net.
** If so, put it in the IMP leader, otherwise substitute our default
** gateway.
**	Fastest punt would be to just use the IP address of the native host,
** as if it were the final gateway -- and in a sense it is!
*/

/* Buffer for I->H leader, must initialize with source-host at startup */
unsigned char ihobuf[SIH_HSIZ+SI_LDRSIZ] = {
#if SIH_HSIZ
	       0, 0, 0, 0,		/* SIH_HDR, SIH_TDATA, 0, 0 */
#endif
	     017, 0, 0, 0,		/* IMP->Host normal message */
	       0, 0, 0, 0,		/* IMP->Host source host */
	SILNK_IP, 0, 0, 0		/* IMP->Host IP msg ID */
};


static void
ihl_hhsend_nop(struct dpimp_s *dpimp)
/* Send NOP response to host */
{
  struct dpx_s *dpx = dp_dpxfr(&dp);
  size_t off, max;
  unsigned int m;
  int cnt, nmsiz = 0;
  unsigned char *ea = (unsigned char *)&ehost_ip;
  unsigned char *buff = dp_xsbuff(dpx, &max); /* get initial buffer ptr */
  dp_xswait(dpx);			      /* wait until buff free */
  off = 0;				      // hmm
  memset(buff, 0, SIH_HSIZ+SI_LDRSIZ);
  // fill buff with NOP leader with IP info
  buff[SIH_HSIZ+SIL_FMT] = 017;  	// format
  // could use SIL_HTY which is next to the rest, but this is easy to test IMP in ITS.
  buff[SIH_HSIZ+SIL_NET] = ea[0]; // network, "currently set to zero"
  buff[SIH_HSIZ+SIL_TYP] = SIMT_NOP;		    // message type
  // source host and imp
  buff[SIH_HSIZ+SIL_HST] = ea[1];
  buff[SIH_HSIZ+SIL_IMP1] = ea[2];
  buff[SIH_HSIZ+SIL_IMP0] = ea[3];
  if (ihost_nm.s_addr == 0)
    // the netmask for tun really shouldn't matter.
    nmsiz = 24;
  else {
    // calculate netmask size - is there a cleverer way?
    m = ihost_nm.s_addr;
    if (m != 0) {
      while ((m & 1) == 0) m >>= 1;
    }
    while (((m & 1) == 1) && (nmsiz < 32)) {
      nmsiz++;
      m >>= 1;
    }
  }
  // abuse these bits which are not used by NOP normally (handling type)
  buff[SIH_HSIZ+SIL_HTY] = nmsiz;
  if (DBGFLG)
    fprintf(stderr,"IMP: sending NOP (ehost %#x, imask %#x) with address %d.%d.%d.%d and mask size %d\r\n",
            ntohl(ehost_ip.s_addr), ntohl(ihost_nm.s_addr),
	    buff[SIH_HSIZ+SIL_NET], buff[SIH_HSIZ+SIL_HST], buff[SIH_HSIZ+SIL_IMP1], buff[SIH_HSIZ+SIL_IMP0], 
	    buff[SIH_HSIZ+SIL_HTY]);
  // send it off
  cnt = SIH_HSIZ + SI_LDRSIZ;
  dpimp->dpimp_inoff = off;
  dp_xsend(dpx, DPIMP_RPKT, cnt+off);
  if (DBGFLG)
    fprintf(stderr, "[dpimp-R: sent NOP RPKT %d+%d]", (int)off, cnt);
}

void
ihl_hhsend(struct dpimp_s *dpimp,
	   int cnt,
	   unsigned char *pp)
	/* "pp" is packet data ptr, has room for header preceding */
{
    int bits = cnt * 8;	/* Why not... msg length in bits */
    union ipaddr haddr;

    /* Set up IMP leader */
    ihobuf[SIH_HSIZ+SIL_LEN0] = bits & 0377;		/* Lo 8 bits */
    ihobuf[SIH_HSIZ+SIL_LEN1] = (bits>>8) & 0377;	/* Hi 8 bits */

    /* Hack to set host/imp value as properly as possible. */
    memcpy((char *)&haddr.ia_octet[0], pp + IPBOFF_SRC, 4);
    if ((haddr.ia_addr.s_addr & ihost_nm.s_addr) != ihost_net.s_addr) {
	if (pfdata.pf_ip4_only) {
	    haddr.ia_addr = tun_ip;	/* Not local, use tunnel end */
	} else {
	    haddr.ia_addr = gwdef_ip;	/* Not local, use default GW */
	}
    }

    ihobuf[SIH_HSIZ+SIL_HST]  = haddr.ia_octet[1];
    ihobuf[SIH_HSIZ+SIL_IMP1] = haddr.ia_octet[2];
    ihobuf[SIH_HSIZ+SIL_IMP0] = haddr.ia_octet[3];

    cnt += SI_LDRSIZ;		/* Compensate for IMP leader size */
#if SIH_HSIZ
    ihobuf[2] = (cnt >> 8) & 0377;	/* High byte of count */
    ihobuf[3] = (cnt & 0377);		/* Low byte of count */
#endif

    pp -= sizeof(ihobuf);		/* Back up to start of header+leader */
    cnt += SIH_HSIZ;
    memcpy(pp, ihobuf, sizeof(ihobuf));	/* Prepend onto data */

    /* Send up to host!  Assume we're already in shared buffer. */
  {
    struct dpx_s *dpx = dp_dpxfr(&dp);
    unsigned char *buff;
    size_t off, max;

    buff = dp_xsbuff(dpx, &max);	/* Set up buffer ptr & max count */
    if ((off = pp - buff) >= max) {
	efatal(1, "Bogus IH offset: %ld", (long)off);
    }
    dpimp->dpimp_inoff = off;		/* Tell host what offset is */
    dp_xsend(dpx, DPIMP_RPKT, cnt+off);

    if (DBGFLG)
	fprintf(stderr, "[dpimp-R: sent RPKT %d+%d]", (int)off, cnt);
  }
}

/* HOSTTOIMP - Parent main loop for pumping packets from HOST to IMP.
**	Reads IMP message from DP superior
**	and interprets it.  If a regular message, bundles it up and
**	outputs to NET.
*/
void
hosttoimp(struct dpimp_s *dpimp)
{
    struct dpx_s *dpx = dp_dpxto(&dp);	/* Get ptr to "To-DP" dpx */
    unsigned char *buff;
    size_t max;
    int rcnt;
    unsigned char *inibuf;
    struct in_addr ipdest;

    inibuf = dp_xrbuff(dpx, &max);	/* Get initial buffer ptr */

    if (DBGFLG)
	fprintf(stderr, "[dpimp-W: Starting loop]\r\n");

    for (;;) {
	if (DBGFLG)
	    fprintf(stderr, "[dpimp-W: CmdWait]\r\n");

	/* Wait until 10 has a command for us */
	dp_xrwait(dpx);		/* Wait until something there */

	/* Process command from 10! */
	switch (dp_xrcmd(dpx)) {

	default:
	    fprintf(stderr, "[dpimp: Unknown cmd %d]\r\n", dp_xrcmd(dpx));
	    dp_xrdone(dpx);
	    continue;

	case DPIMP_SPKT:			/* Send regular packet */
	    rcnt = dp_xrcnt(dpx);

	    /* Adjust for offset */
	    rcnt -= dpimp->dpimp_outoff;
	    buff = inibuf + dpimp->dpimp_outoff;
	    if (DBGFLG) {
		if (DBGFLG & 0x2) {
		    fprintf(stderr, "\r\n[dpimp-W: Sending %d\r\n", rcnt);
		    dumppkt(buff, rcnt);
		    fprintf(stderr, "]");
		}
		else
		    fprintf(stderr, "[dpimp-W: SPKT %d]", rcnt);
	    }
	    break;

	case DPIMP_QUIT:
	    if (DBGFLG)
		fprintf(stderr, "[dpimp-W: QUIT]\r\n");
	    return;

	}

	/* Come here to handle output packet */

	if (rcnt < (SIH_HSIZ+SI_LDRSIZ)) {
	    error("Host-Imp message too small: %d", rcnt);
	    continue;
	}
#if 1
	if (buff[SIH_HSIZ+SIL_FMT] != 017) {
	    error("Old-format leader received: %o", buff[SIH_HSIZ+SIL_FMT]);
	    continue;
	}
#endif
	/* Dispatch by message type.  ITS actually only sends
	** NOP and HH messages, nothing else.
	*/
	switch (buff[SIH_HSIZ+SIL_TYP]) {

	case SIMT_HH:	/* Regular Host-Host Message */
	    if (buff[SIH_HSIZ+SIL_LNK] != SILNK_IP) {
		error("Non-IP Host-Imp msg received: %#o",
				buff[SIH_HSIZ+SIL_LNK]);
		continue;
	    }
	    int res = -1;	/* Assume error */
	    if (pfdata.pf_ip4_only) {
		if (DBGFLG)
		    dbprintln("net out = %d", rcnt - (SIH_HSIZ+SI_LDRSIZ));
		if (osn_pfwrite(&pfdata, &buff[SIH_HSIZ+SI_LDRSIZ],
			  rcnt - (SIH_HSIZ+SI_LDRSIZ)) < 0) {
		    syserr(errno, "tun write() failed");
		} else {
		    res = 0;
		}
	    } else {
		if (hi_iproute(&ipdest, &buff[SIH_HSIZ], rcnt - SIH_HSIZ)) {
		    ip_write(&ipdest, &buff[SIH_HSIZ+SI_LDRSIZ],
					    rcnt - (SIH_HSIZ+SI_LDRSIZ));
		    res = 0;
		}
	    }
#if !SICONF_SIMP
# error	"Too hard to implement non-Simple IMP model!"
# if 0
	    if (res == 0) {
		/* IP packet sent out to net, now send RFNM to host */
		buff[SIH_HSIZ+SIL_TYP] = SIMT_RFNM;
		buff[2] = 0;		/* High byte of count */
		buff[3] = SI_LDRSIZ;	/* Low byte of count */
		res = write(ih_fd, buff, rcnt = SIH_HSIZ+SI_LDRSIZ);
		if (res != rcnt) {
		    esfatal(2, "IH pipe write failed: %d != %d", res, rcnt);
		} else if (swurgsig)
		    kill(cpupid, swurgsig);	/* Wake host (cpu) up */
	    }
# endif
#else
	    (void)res;
#endif
	    break;

	case SIMT_LERR:	/* Error in leader: err in previous IMP-to-Host ldr */
	    error("Received Host-to-IMP leader error msg");
	    break;

	case SIMT_GDWN:	/* Host Going Down */
	    error("Received Host-going-down msg");
	    break;

	case SIMT_NOP:	/* NOP */
	    /* A real IMP would examine this to see how much padding to
	    ** add onto its leaders.  However, ITS never wants any.
	    */
	    if (DBGFLG) fprintf(stderr, "[dpimp-W: NOP]\r\n");
	    // respond with a NOP including IP address and mask
	    // #### NOTE: using dpxfr to let imptohost proceed, instead of dvlhdh doing it
	    dp_xrdone(dp_dpxfr(&dp));
	    break;

	case SIMT_DERR:	/* Error in Data (has msg-id) */
	    error("Received Host-to-IMP data error msg");
	    break;

	default:
	    error("Unknown host-imp msg type: %#o", buff[SIH_HSIZ+SIL_TYP]);
	}

	/* Command done, tell 10 we're done with it */
	if (DBGFLG)
	    fprintf(stderr, "[dpimp-W: CmdDone]");

	dp_xrdone(dpx);
    }
}

/* HI_IPROUTE - Determine where to actually send Host-Host IP datagram.
**	See discussion of routing in comments at start of file.
	For now, let's build the IP address by slapping the 1st IP byte
on from the native host's IP address, and taking the rest from the host/imp
fields per scheme above.  SIMP then checks to see if it's a local net
address, and if so uses that IP address.  If not (ITS thinks it's local,
but it isn't on right subnet) then SIMP substitutes a single default
gateway address.  This resolves some but not all of the problem.
*/

int
hi_iproute(struct in_addr *ipa,	/* Dest IP addr to be put here */
	   unsigned char *lp,	/* Ptr to start of IMP-Host leader */
	   int cnt)		/* Cnt of data including leader */
{
    union ipaddr haddr;

    if (cnt < (SI_LDRSIZ+IPBOFF_DEST+4)) {
	error("Host-Imp IP datagram too short: %d", cnt);
	return FALSE;
    }

    /* Derive destination IP address from IMP leader */
    haddr.ia_addr = ihost_ip;		/* Init with native IP addr */
    haddr.ia_octet[1] = lp[SIL_HST];	/* Set up host byte */
    haddr.ia_octet[2] = lp[SIL_IMP1];	/* High imp byte (old "logical host")*/
    haddr.ia_octet[3] = lp[SIL_IMP0];	/* Low byte of imp */

    /* Now see if address is local, or if gateway routing needed. */
    if ((haddr.ia_addr.s_addr & ihost_nm.s_addr) == ihost_net.s_addr) {
	*ipa = haddr.ia_addr;		/* Local, win! */
	return TRUE;
    }

    /* Yucko, need to substitute gateway address.  Lotsa luck. */
    *ipa = gwdef_ip;
    return TRUE;
}


/* IP_WRITE - Send IP packet out onto ethernet via packetfilter.
*/

void
ip_write(struct in_addr *ipa, unsigned char *buf, int len)
{
    struct eth_header eh;

    /* Set up ethernet header */
    if (!arp_look(*ipa, (struct ether_addr *)eh_dptr(&eh))) {
	/* Failed, so generate an ARP request and send that instead.
	** Perhaps later add code to hang on to the packet; problem is figuring
	** out when it's time to try sending it again.
	** Could spin off thread to do timeout & re-send?
	*/
	arp_req(ipa);		/* Send ARP request for this packet */
	sleep(1);		/* Gross crock */
	if (!arp_look(*ipa, (struct ether_addr *)eh_dptr(&eh))) {
	    /* Try again */
	    if (swstatus) {
		char ipbuf[OSN_IPSTRSIZ];
		dbprintln("No ARP, dropped pkt to %s",
			  ip_adrsprint(ipbuf, (unsigned char *)ipa));
	    }
	    return;			/* Failed, just ignore for now */
	}
    }
    eh_sset(&eh, &ihost_ea);	/* Set source addr */
    eh_tset(&eh, ETHERTYPE_IP);

    ether_write(&eh, buf, len);
}

/*
 * Write an ethernet frame, consisting of the header and the
 * data. They are supplied separately but sent together.
 *
 * It is assumed there is space before the payload data to put
 * a copy of the header!
 */
void
ether_write(struct eth_header *hp,
	    unsigned char *pp,
	    int cnt)
{
    char *buf = (char *)(pp - ETHER_HDRSIZ);

    if (DP_DBGFLG)
	dbprintln("net out = %d", cnt);

    memcpy(buf, (char *)hp, ETHER_HDRSIZ);

    if (osn_pfwrite(&pfdata, buf, (size_t)(cnt + ETHER_HDRSIZ)) < 0) {
	/* What to do here?  For debugging, complain but return. */
	error("write failed - %s", dp_strerror(errno));
    }
}


void
dumppkt(unsigned char *ucp, int cnt)
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

/* Add OSDNET shared code here */

#include "osdnet.c"
