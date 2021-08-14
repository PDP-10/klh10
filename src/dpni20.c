/* DPNI20.C - Device sub-Process for KLH10 NIA20 Ethernet Interface
*/
/* $Id: dpni20.c,v 2.7 2003/02/23 18:07:50 klh Exp $
*/
/*  Copyright © 1994, 2001 Kenneth L. Harrenstien
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
 * $Log: dpni20.c,v $
 * Revision 2.7  2003/02/23 18:07:50  klh
 * Add NetBSD read loop fix for bpf.
 *
 * Revision 2.6  2002/04/24 07:45:10  klh
 * Minor errmsg tweak
 *
 * Revision 2.5  2001/11/19 10:36:00  klh
 * Solaris port: trivial cast fixes.
 *
 * Revision 2.4  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*
	This is a program intended to be run as a SU child of the KLH10
PDP-10 emulator, in order to provide ethernet interface I/O without
requiring that the KLH10 process itself run as superuser.

	There are actually two threads active in this process, one
which pumps packets from the network to the KLH10 and another that
pumps in the opposite direction from the KLH10 to the network.
Generally they are completely independent.

		-----------------------------

Useful pieces of documentation are in the following man pages:
    FreeBSD: netintro(4) - General net interface ioctls
    FreeBSD: inet(9)     - Kernel network internals
    FreeBSD: /usr/src/   - Actual source (both kernel & user)
	   
    OSF/1: netintro(7) - General net interface ioctls
		Interface addr/config		SIOC{*}IF{*}
    OSF/1: ln(7) - Describes ioctls for Lance ethernet interface, to:
		Read & change phys addr		SIOC{R,S}PHYSADDR
		Add & delete multicast addrs	SIOC{ADD,DEL}MULTI
		Read & read/clear counters 	SIOCRD{Z}CTRS
		Enable/disable loopback mode	SIOC{EN,DIS}ABLBACK
    OSF/1: arp(7) - Ioctls for managing ARP tables:
		Set, Get, Delete ARP entry	SIOC{S,G,D}ARP
    OSF/1: bpf(7) - Berkeley Packet Filter
    OSF/1: packetfilter(7) - BSD/Stanford packet filter

    SUN: intro(4), arp(4P), inet(4f), routing(4n)	- General
	lo(4n), ie(4s), if(4n),				- Interface
	nit(4p), nit_buf(4m), nit_if(4m), nit_pf(4m)	- NIT packetfilter

Useful auxiliary programs:
    OSF/1: pfconfig(8) - Sets system packetfilter config values
    OSF/1: pfstat(1) - Shows system packetfilter status & counts
    gen:   netstat(1) - General net interface status
    gen:   arp(8) - Shows/sets ARP tables
    gen:   ifconfig(8) - Shows/sets interface config stuff
*/

/*
	Some notes on configuration flags

The following configurations are allowed for:


[A] Shared net interface (DEDIC=FALSE, default)
	IFC=    (dpni_ifnam) specifies which interface to use.  Optional.
	ENADDR= (dpni_eth) cannot be set.
	IPADDR= may be set.  If so, filters packets for that IP address.
	DECNET= may be set.  If so, all DECNET packet types are added to
		the filter; otherwise they are ignored.  The host platform
		must not itself use DECNET packets!!!
	DOARP=FALSE

[B] Dedicated net interface (DEDIC=TRUE)
	Virtual 20 receives all packets addressed to its hardware interface,
	including broadcast and multicast packets, of all types.
	Promiscuous mode may be toggled.
	(Although on Solaris, promiscuous mode is always necessary in order to
	read packets of all ethernet types, rather than just one!)

	IFC=    (dpni_ifnam) specifies which interface to use.  Optional,
		but *highly* recommended!
	ENADDR= (dpni_eth) is used to set the ethernet address, if possible.
		Optional.
	DECNET= is ignored.

    [B.1] No funny stuff (DOARP=FALSE, default)
	IPADDR= isn't needed and is ignored.

    [B.2] Hack ARP anyway (DOARP=TRUE)
		Needed because host platform (Solaris) has problems
		understanding TOPS-20 replies -- and TOPS-20 itself sometimes
		corrupts its own replies!
	IPADDR= must be specified!!

For all configurations:
	BACKLOG= (OSF/1 only)
	RDTMO=   (OSF/1 and BPF only)

ARP behavior is selected by a combination of DOARP= plus other things.
The following general situations are possible:

	- The 20 doesn't need to see or send ARPs (TOPS monitor not using IP).
		If ifc dedicated, or no ipaddr, then no filtering is done
		nor any special help.

	- Ensure that 20 can see and send ARP packets (TOPS monitor using IP).
		If shared ifc: do ARP filtering, special help optional.
		If dedicated ifc: no filtering, special help optional.

	- Special help for the 20 if using IP:

    DOARP=2	(1) Register ARP info as "publish" with local platform, so
			platform proxy-answers requests for the 20's address.
			Necessary because it looks like TOPS-20 screws up
			ARP replies sometimes!
			(Also needed on Solaris and OSF/1 because this way
			the packetfilter only needs to pass ARP replies, not
			ARP requests).
    DOARP=4	(2) Register ARP info as "permanent" with local platform, so
			platform knows address itself.
			Necessary because OSF/1 loopback omits ARP packets.
			(May not need on Solaris?)
    DOARP=8	(3) Watch for outgoing ARP reqs for local platform's address,
			and simulate reply.
			Same reason as (1) -- OSF/1 loopback omits ARP
			packets, so platform won't see request!
			(Unsure yet if Solaris needs this)
*/

#include "klh10.h"	/* For config params */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <poll.h>	/* For NetBSD mainly */


/* This must precede any other OSD includes to ensure that DECOSF gets
   the right flavor sockaddr (sigh)
*/
#include "osdnet.h"		/* OSD net defs, shared with DPIMP */

#include <sys/resource.h>	/* For setpriority() */
#include <sys/mman.h>		/* For mlockall() */

#include "dpsup.h"		/* General DP defs */
#include "dpni20.h"		/* NI20 specific defs */


/* Globals */

int chpid;		/* PID of child, handles input (net-to-10). */
int swstatus = TRUE;
struct osnpf npf;
struct pfdata pfdata;		/* Packet-Filter state */
struct dp_s dp;			/* Device-Process struct for DP ops */

struct in_addr ehost_ip;	/* Emulated host IP addr, net order */
#if 0
struct in_addr ihost_ip;	/* Native host's IP addr, net order */
#endif
struct in_addr tun_ip;		/* IP addr of Host side of tunnel, net order */
struct ether_addr ihost_ea;	/* Native host ether addr for selected ifc */

/* Debug flag reference.  Use DBGFLG within functions that have "dpni";
 * all others must use DP_DBGFLG.  Both refer to the same location.
 * Caching a local copy of the debug flag is a no-no because it lives
 * in a shared memory location that may change at any time.
 */
#define DBGFLG (dpni->dpni_dpc.dpc_debug)
#define DP_DBGFLG (((struct dpni20_s *)dp.dp_adr)->dpni_dpc.dpc_debug)

int nmcats = 0;
unsigned char ethmcat[DPNI_MCAT_SIZ][6]; /* Table of known MCAT addresses */


/* Local predeclarations */

void ethtoten(struct dpni20_s *);
void tentoeth(struct dpni20_s *);

void net_init(struct dpni20_s *dpni);
void eth_mcatset(struct dpni20_s *dpni);
void eth_adrset(struct dpni20_s *dpni);
void dumppkt(unsigned char *ucp, int cnt);
int arp_myreply(unsigned char *buf, int cnt, struct dpx_s *dpx);

/* Error and diagnostic output */

static const char progname_i[] = "dpni20";
static const char progname_r[] = "dpni20-R";
static const char progname_w[] = "dpni20-W";
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
    fprintf(stderr, "\n[%s: Fatal error: ", progname);
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
    struct dpni20_s *dpni;

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
    ** we can.  It's important that the NI respond as quickly as possible
    ** or the 10 monitor is apt to declare it dead.
    ** This applies primarily to the 10->NI process, not the NI->10
    ** which is a child of this one.
    */
#if 0 /* was CENV_SYS_SOLARIS */
    if (nice(-20) == -1)
	syserr(errno, "Warning - cannot set high priority");
#elif HAVE_SETPRIORITY
    if (setpriority(PRIO_PROCESS, 0, -20) < 0)
	syserr(errno, "Warning - cannot set high priority");
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
    dpni = (struct dpni20_s *)dp.dp_adr;	/* Make for easier refs */

    /* Now can access DP args!
       From here on we can use DBGFLG, which is actually a shared
       memory reference that dpni points to.  Check here to accomodate the
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
    if (!(dpni->dpni_dpc.dpc_flags & DPCF_MEMLOCK)) {
	if (mlockall(MCL_CURRENT|MCL_FUTURE) != 0) {
	    dbprintln("Warning - cannot lock memory");
	}
    }
#endif

    /* Now set up variables based on parameters passed through
       shared DP area.
    */

    /* If no IP address specified, ignore ARP hackery flag */
    if (memcmp(dpni->dpni_ip, "\0\0\0\0", 4) == 0)
	dpni->dpni_doarp = FALSE;

    /* Canonicalize ARP hackery flags */
    if (dpni->dpni_doarp == TRUE)		/* If simply set to "true", */
	dpni->dpni_doarp = DPNI_ARPF_PUBL |	/* then do all */
		DPNI_ARPF_PERM | DPNI_ARPF_OCHK;

    /* Initialize network stuff including packet filter */
    net_init(dpni);

    /* Make this a status (rather than debug) printout? */
    if (swstatus) {
	char sbuf[OSN_IPSTRSIZ+OSN_EASTRSIZ];	/* Lazily ensure big enough */

	dbprintln("ifc \"%s\" => ether %s",
		  dpni->dpni_ifnam,
		  eth_adrsprint(sbuf, (unsigned char *)&ihost_ea));
#if 0
	dbprintln("  addr  %s",
		  ip_adrsprint(sbuf, (unsigned char *)&ihost_ip));
#endif
	if (pfdata.pf_ip4_only) {
	    dbprintln("  tun  %s",
		      ip_adrsprint(sbuf, (unsigned char *)&tun_ip));
	}
	dbprintln("  VHOST %s",
		  ip_adrsprint(sbuf, (unsigned char *)&ehost_ip));
    }

    /* If ARP hackery desired/needed, set up ARP entry so emulator host
    ** kernel knows about our IP address (and can respond to ARP requests
    ** for it, although this probably isn't necessary if the virtual 20's
    ** monitor can do it).
    */
    if (dpni->dpni_doarp & (DPNI_ARPF_PUBL|DPNI_ARPF_PERM)) {
	if (!osn_arp_stuff(dpni->dpni_ifnam,	/* interface name */
		           &dpni->dpni_ip[0],	/* Set up fake IP addr */
			   &dpni->dpni_eth[0],	/* mapped to this ether addr */
			   (dpni->dpni_doarp & DPNI_ARPF_PUBL))) /* Publicized if nec */
	    esfatal(1, "ARP_STUFF failed");
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
#if HAVE_MLOCKALL
	(void) mlockall(MCL_CURRENT|MCL_FUTURE);
#endif
	progname = progname_r;	/* Reset progname to indicate identity */
	ethtoten(dpni);		/* Child process handles input from net */

	return 1;		/* Do not fall through here, ever */
    }
    progname = progname_w;	/* Reset progname to indicate identity */
    tentoeth(dpni);		/* Parent process handles output to net */

    osn_pfdeinit(&pfdata, &npf);/* Clean up created tunnels etc */
    dp_xrdone(dp_dpxto(&dp));

    return 1;
}

/* NET_INIT - Initialize net-related variables,
**	given network interface we'll use.
*/
void net_init(struct dpni20_s *dpni)
{
    struct ifreq ifr;

    /* Get the IP address we need to filter on, if shared */
    memcpy((char *)&ehost_ip, (char *)&dpni->dpni_ip, 4);

    /* Get the IP address for the tunnel, if specified */
    memcpy((char *)&tun_ip, (char *)&dpni->dpni_tun, 4);

    /* We should ensure that network device name, if specified, isn't too long.
    ** For some usages however, this isn't really a device name,
    ** so we can't really check it here.
    ** In the paths where a shorter limit holds, there should be a specific
    ** extra check for sizeof(ifr.ifr_name), IFNAMSIZ, or equivalent.
    **/

    /* Determine network device to use, if none was specified (this only
    ** works for shared devices, as dedicated ones will be "down" and
    ** cannot be found by iftab_init).
    ** Also grab native IP and ethernet addresses, if ARP might need them.
    */
    if (osn_iftab_init() <= 0)
	esfatal(0, "Couldn't find interface information");

    if ((!dpni->dpni_ifnam[0] && !dpni->dpni_dedic)
      || (dpni->dpni_doarp & DPNI_ARPF_OCHK)) {

	/* Found at least one!  Pick first one, if a default is needed. */
	if (!dpni->dpni_ifnam[0]) {
	    struct ifent *ife = osn_ipdefault();
	    if (!ife)
		esfatal(0, "Couldn't find default interface");
	    if (strlen(ife->ife_name) >= sizeof(dpni->dpni_ifnam))
		esfatal(0, "Default interface name \"%s\" too long, max %d",
		    ife->ife_name, (int)sizeof(dpni->dpni_ifnam));

	    strcpy(dpni->dpni_ifnam, ife->ife_name);
	    if (swstatus)
		dbprintln("Using default interface \"%s\"", dpni->dpni_ifnam);
	}
    }

    /* Now set remaining stuff */

    /* Set up packet filter.  This also returns in "ihost_ea"
       the ethernet address for the selected interface.
    */

    npf.osnpf_ifnam = dpni->dpni_ifnam;
    npf.osnpf_ifmeth = dpni->dpni_ifmeth;
    npf.osnpf_dedic = dpni->dpni_dedic;
    npf.osnpf_rdtmo = dpni->dpni_rdtmo;
    npf.osnpf_backlog = dpni->dpni_backlog;
    npf.osnpf_ip.ia_addr = ehost_ip;
    npf.osnpf_tun.ia_addr = tun_ip;
    /* Ether addr is both a potential arg and a returned value;
       the packetfilter open may use and/or change it.
    */
    ea_set(&npf.osnpf_ea, dpni->dpni_eth);	/* Set requested ea if any */
    osn_pfinit(&pfdata, &npf, (void *)dpni);	/* Will abort if fails */
    ea_set(&ihost_ea, &npf.osnpf_ea);		/* Copy actual ea */
    tun_ip = npf.osnpf_tun.ia_addr;		/* Get actual tunnel addr */


    /* Now set any return info values in shared struct.
    */
    ea_set(dpni->dpni_eth, (char *)&ihost_ea);	/* Copy ether addr */

    if (DBGFLG)
	dbprint("PF inited");
}

/* Packet Filter setup.
**	No filter at all is used if the interface is dedicated; we see
**	everything received on it.
**
** However, the packet filter for a SHARED interface must implement at least
** the following test:
**
**	if ((dest IP addr == our IP addr && ethertype == IP)
**	    || (broadcast-mcast bit set))
**		 { success; }
**
** For efficiency, the strictest test is done first -- the low shortword
** of the IP address, which is the most likely to differ from that of the
** true host.  We do this even before checking whether the packet contains
** IP data; if it's too short, it's rejected anyway.
*/

/* BPF packetfilter initialization */

#if KLH10_NET_PCAP

/*
** BPF filter program stuff.
** Note that you can also obtain a specific filter program for a given
** expression by using tcpdump(1) with the -d option, for example:
**   tcpdump -d -s 1514 ip dst host 1.2.3.4
** produces:
      { 0x20, 0, 0, 0x0000001e },	// (000) ld [30]
      { 0x15, 0, 3, 0x01020304 },	// (001) jeq #0x1020304 jt 2 jf 5
      { 0x28, 0, 0, 0x0000000c },	// (002) ldh [12]
      { 0x15, 0, 1, 0x00000800 },	// (003) jeq #0x800 jt 4 jf 5
      { 0x06, 0, 0, 0x000005ea },	// (004) ret #1514
      { 0x06, 0, 0, 0x00000000 },	// (005) ret #0
*/


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
    struct dpni20_s *dpni = (struct dpni20_s *)arg;
    struct bpf_program *pfp = &bpf_pfilter;
    struct bpf_insn *p;

    p = pfp->bf_insns;		/* Point to 1st instruction in BPF program  */

    /* First check for broadcast/multicast bit in dest address */
    *p++ = BPFI_LDB(PKBOFF_EDEST);	/* Get 1st byte of dest ether addr */
    *p++ = BPFI_TDNE(01);		/* Skip if bit is zero */
    *p++ = BPFI_RETWIN();		/* Bit set, succeed immediately! */

    /* Possibly insert check for DECNET protocol types.
    ** Doing this check is inefficient if most of the traffic is IP.
    ** Hopefully if the user asked for it, it's used a lot.
    ** The following are the known types:
    **	6001 DNA/MOP
    **	6002 RmtCon
    **	6003 DECnet
    **	6004 LAT
    **	6016 ANF-10	(T10 only; not DECNET)
    **	9000 Loopback (?)
    **
    ** For the time being, filtering is done by testing for
    **		(type & ~0x001F) == 0x6000
    ** which accepts all types in the range 6000-601F inclusive.
    ** 9000 is ignored.
    */
    if (dpni->dpni_decnet) {
#if 0
	/* Blunt instrument approach */
	*p++ = BPFI_LDH(PKBOFF_ETYPE);		/* Get ethernet type */

	*p++ = BPFI_CAMN(ETHERTYPE_DECnet);	/* Win if 0x6003 */
	*p++ = BPFI_RETWIN();			/* Win now! */
	*p++ = BPFI_CAMN(ETHERTYPE_LAT);	/* Win if 0x6004 */
	*p++ = BPFI_RETWIN();			/* Win now! */
	*p++ = BPFI_CAMN(0x6016);		/* Check for 0x6016 */
	*p++ = BPFI_RETWIN();			/* Win now! */
	*p++ = BPFI_CAMN(0x6001);		/* Check for 0x6001 */
	*p++ = BPFI_RETWIN();			/* Win now! */
	*p++ = BPFI_CAMN(0x6002);		/* Check for 0x6002 */
	*p++ = BPFI_RETWIN();			/* Win now! */
#else
	/* Slightly faster, although sloppier */
	*p++ = BPFI_LDH(PKBOFF_ETYPE);		/* Get ethernet type */
	*p++ = bpf_stmt(BPF_ALU+BPF_AND+BPF_K, 0xFFE0);	/* Mask out ~0x001F */
	*p++ = BPFI_CAMN(0x6000);		/* Succeed if result 6000 */
	*p++ = BPFI_RETWIN();			/* Win now! */
#endif
    }


    /* Test for an IEEE 802.3 packet with a specific dst/src LSAP.
	Packet is 802.3 if type field is actually packet length -- in which
	case it will be 1 <= len <= 1500 (note 1514 is max, of which header
	uses 6+6+2=14).
	There's seemingly no way to tell what order the ENF_LT, etc operands
	are used in, so until that's established, use a simple masking
	method.  1500 = 0x5dc, so use mask of 0xF800.

	Dst/src LSAPs are in next two bytes (1st shortwd after len).
    */
    if (dpni->dpni_attrs & DPNI20F_LSAP) {
	unsigned short lsaps = dpni->dpni_lsap;

	if (lsaps <= 0xFF) {		/* If only one byte set, */
	    lsaps |= (lsaps << 8);	/* double it up for both dest & src */
	}

	*p++ = BPFI_LDH(PKBOFF_ETYPE);	/* Get ethernet type */
	*p++ = BPFI_JGT(1500, 3);	/* If > 1500, skip next 3 insns */
	*p++ = BPFI_LDH(PKBOFF_SAPS);	/* Get DSAP/SSAP shortwd */
	*p++ = BPFI_CAMN(lsaps);	/* Matches? */
	*p++ = BPFI_RETWIN();		/* Yes, win now! */

    }

    /* See if we're interested in IP (and thus ARP) packets.
	This is assumed to be the LAST part of the filter, thus
	it must either leave the correct result on the stack, or
	ensure it is empty (if accepting the packet).
     */
    if (memcmp(dpni->dpni_ip, "\0\0\0\0", 4) != 0) {

	/* Want to pass ARP replies as well, so 10 can see responses to any
	** ARPs it sends out.
	** NOTE!!!  ARP *requests* are not passed!  The assumption is that
	** osn_arp_stuff() will have ensured that the host platform
	** proxy-answers requests for our IP address.
	*/
	*p++ = BPFI_LDH(PKBOFF_ETYPE);		/* Load ethernet type field */
	*p++ = BPFI_CAMN(ETHERTYPE_ARP);	/* Skip unless ARP packet */
	*p++ = BPFI_RETWIN();			/* If ARP, win now! */

	/* If didn't pass, check for our IP address */
	*p++ = BPFI_LD(PKBOFF_IPDEST);		/* Get IP dest address */
	*p++ = BPFI_CAME(ntohl(ipa->s_addr));	/* Skip if matches */
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

/* LNX packetfilter initialization */

/*
  The Linux PF_PACKET interface is described to some extent
  by the packet(7) man page.

  Linux provides no kernel packet filtering mechanism other than
  possibly a check on the ethernet protocol type, but this is useless
  for us since we'll always want to check for more than just one type;
  e.g. IP and ARP, plus possibly 802.3 or DECNET packets.

  From the man page for packet(7):
       By  default all packets of the specified protocol type are
       passed to a packet socket. To only get packets from a spe-
       cific  interface  use  bind(2)  specifying an address in a
       struct sockaddr_ll to bind the packet socket to an  inter-
       face.  Only  the  sll_protocol and the sll_ifindex address
       fields are used for purposes of binding.
 */

/* Because LNX has no kernel packet filtering, must do it
   manually.  Ugh!

   Call this when using a non-dedicated interface.
   Returns TRUE if packet OK, FALSE if it should be dropped.
   Note that the code parallels that for pfbuild().
*/
int lnx_filter(struct dpni20_s *dpni,
	       unsigned char *bp,
	       int cnt)
{
    /* Code assumes buffer is at least shortword-aligned. */
    unsigned short *sp = (unsigned short *)bp;
    unsigned short etyp;

    /* First check for broadcast/multicast bit in dest address */
    if (bp[PKBOFF_EDEST] & 01)
        return TRUE;		/* Bit set, succeed immediately! */

    /* Now get ethernet protocol type for further checking.
       Could also test packet length, but for now assume higher level
       will take care of those checks.
     */
    etyp = ntohs(sp[PKSWOFF_ETYPE]);
    switch (etyp) {

    case ETHERTYPE_ARP:
	/* Always interested in ARP, unless no IP address */
	return (memcmp(dpni->dpni_ip, "\0\0\0\0", 4) != 0);

    case ETHERTYPE_IP:
	/* For IP packet, return TRUE if IP destination matches ours */
	return (memcmp(dpni->dpni_ip, bp + PKBOFF_IPDEST, 4) == 0);

    /* Check for DECNET protocol types if requested.
    ** The following are the known types:
    **	6001 DNA/MOP
    **	6002 RmtCon
    **	6003 DECnet
    **	6004 LAT
    **	6016 ANF-10	(T10 only; not DECNET)
    **	9000 Loopback (?)
    */
    case 0x6001:	/* DNA/MOP */
    case 0x6002:	/* RmtCon */
    case 0x6003:	/* DECnet */
    case 0x6004:	/* LAT */
    case 0x6016:	/* ANF-10 (T10 only; not DECNET) */
    case 0x9000:	/* Loopback (?) */
 	return (dpni->dpni_decnet);	/* TRUE if wanted Decnet stuff */

    default:
      /* Test for an IEEE 802.3 packet with a specific dst/src LSAP.
	 Packet is 802.3 if type field is actually packet length -- in which
	 case it will be 1 <= len <= 1500 (note 1514 is max, of which header
	 uses 6+6+2=14).

	 Dst/src LSAPs are in the 1st shortwd after packet length.
      */
      if (etyp <= 1500
	&& (dpni->dpni_attrs & DPNI20F_LSAP)
	&& (dpni->dpni_lsap == sp[PKSWOFF_SAPS]))
	  return TRUE;
      break;
    }
    return FALSE;
}



/* ETH_SETADR - Attempt to set physical ethernet address to dpni_rqeth.
**	If successful, reflect this by changing dpni_eth.
*/
void eth_adrset(struct dpni20_s *dpni)
{
#if OSN_USE_IPONLY
    dbprintln("\"%s\" ethernet address change ignored - IP-only interface",
		  dpni->dpni_ifnam);
#else
    unsigned char rdea[ETHER_ADRSIZ];
    char old[OSN_EASTRSIZ];
    char new[OSN_EASTRSIZ];

    /* Before hitting the barf below, do one last check to make sure
    ** we're not setting it to the current address.
    */
    if (ea_cmp(dpni->dpni_eth, dpni->dpni_rqeth) == 0)
	return;			/* Succeed silently */

    /* Set up for simpler output */
    eth_adrsprint(old, dpni->dpni_eth);
    eth_adrsprint(new, dpni->dpni_rqeth);

    /* Check to make sure it's OK to set our address.
    ** Only allow it if interface is dedicated; otherwise, barf so user
    ** knows it has to be set manually.
    */
    if (!dpni->dpni_dedic) {
	/* Actually, allow it if DECNET, unless interface is *already*
	** a DECNET address.  DECNET addrs are always AA:00:04:...
	*/
	if (dpni->dpni_decnet) {
	    static unsigned char dnpref[3] = { 0xAA, 0x00, 0x04 };
	    if (memcmp(dpni->dpni_eth, dnpref, 3) == 0) {
		dbprintln("\"%s\" E/N addr change ignored, Old=%s New=%s - already a DECNET addr!",
			  dpni->dpni_ifnam, old, new);
		return;
	    }
	} else {
	    dbprintln("\"%s\" E/N addr change ignored, Old=%s New=%s - interface not dedicated",
		      dpni->dpni_ifnam, old, new);
	    return;
	}
    }

    if (!osn_ifeaset(&pfdata, -1, dpni->dpni_ifnam, dpni->dpni_rqeth)) {
	error("\"%s\" E/N addr change failed, Old=%s New=%s",
	       dpni->dpni_ifnam, old, new);
	return;
    }

    /* Always print out, to inform user */
    if (1) {
	dbprintln("\"%s\" E/N addr changed: Old=%s New=%s",
		  dpni->dpni_ifnam, old, new);
    }

    /* Apparently won!  Try reading it back just to be paranoid,
     * using packetfilter FD.
     */
    if (!osn_pfeaget(&pfdata, dpni->dpni_ifnam, rdea)) {
	error("Can't read \"%s\" e/n addr!", dpni->dpni_ifnam);
	/* Proceed as if set won, sigh */
    } else {

	/* See if same as requested! */
	if (ea_cmp(rdea, dpni->dpni_rqeth) != 0) {
	    eth_adrsprint(old, rdea);
	    dbprintln("New \"%s\" e/n addr mismatch! Set=%s Read=%s",
		      dpni->dpni_ifnam, new, old);
	}
    }

#endif
    /* Assume succeeded since call succeeded, and clobber our address! */
    ea_set(dpni->dpni_eth, dpni->dpni_rqeth);
}


/* ETH_MCATSET - Set multicast addresses.
**	Problem here is that there is no apparent way of READING the hardware's
**	current multicast addresses!
**	So, unless the OS or hardware is clever about recognizing duplicates,
**	successive runs of this routine could fill the table up.  Sigh.
**
** Another hassle is that we need to keep track of the MCAT so that it's
** possible to tell when addresses are removed from the table by new
** MCAT loads.
*/

void eth_mcatset(struct dpni20_s *dpni)
{
#if OSN_USE_IPONLY
    dbprintln("\"%s\" multicast table ignored - IP-only interface",
		  dpni->dpni_ifnam);
#else
    ossock_t s;
    int i, n, j;
    char ethstr[OSN_EASTRSIZ];

    /* Check to make sure it's OK to set the multicast table.
    ** Only allow it if interface is dedicated; otherwise, barf so user
    ** knows it has to be set manually.
    */
    if (!dpni->dpni_dedic && !dpni->dpni_decnet) {
	dbprintln("\"%s\" multicast table ignored - interface not dedicated",
		  dpni->dpni_ifnam);
	return;
    }

    /* Dunno if packetfilter FD would pass these through, so get another
    ** socket FD for this purpose.
    */
    if (!osn_ifsock(dpni->dpni_ifnam, &s)) {
	syserr(errno, "multicast table set failed - osn_ifsock");
	return;
    }

    /* First flush any old entries that aren't in new table. */
    if ((n = dpni->dpni_nmcats) > DPNI_MCAT_SIZ)
	n = DPNI_MCAT_SIZ;
    for (i = 0; i < nmcats; ++i) {
	for (j = 0; j < n; ++j) {
	    if (ea_cmp(ethmcat[i], dpni->dpni_mcat[j]) == 0)
		break;
	}
	if (j < n)
	    continue;		/* Match found, continue outer loop */

	/* No match found for this old entry, so flush it from OS */
	if (1) {	    /* For now, always print out to warn user */
	    dbprintln("Deleting \"%s\" multicast entry: %s",
		      dpni->dpni_ifnam,
		      eth_adrsprint(ethstr, ethmcat[i]));
	}
        if (!osn_ifmcset(&pfdata, s, dpni->dpni_ifnam, TRUE /*DEL*/, ethmcat[i])) {
	    error("\"%s\" Multicast delete failed", dpni->dpni_ifnam);
	    /* Keep going */
	}
    }

    /* Now grovel in other direction, to find all addrs not already
    ** in old table.
    */
    for (j = 0; j < n; ++j) {
	for (i = 0; i < nmcats; ++i) {
	    if (ea_cmp(ethmcat[i], dpni->dpni_mcat[j]) == 0)
		break;
	}
	if (j < n)
	    continue;		/* Match found, continue outer loop */

	/* No match found for this new entry, so add it to OS */
	if (1) {	    /* For now, always print out to warn user */
	    dbprintln("Adding \"%s\" multicast entry: %s",
		      dpni->dpni_ifnam,
		      eth_adrsprint(ethstr, dpni->dpni_mcat[i]));
	}
        if (!osn_ifmcset(&pfdata, s, dpni->dpni_ifnam, FALSE /*ADD*/,
			 dpni->dpni_mcat[i])) {
	    error("\"%s\" Multicast add failed", dpni->dpni_ifnam);
	    /* Keep going */
	}
    }

    /* Done, close socket and copy new table */
    osn_ifclose(s);

    nmcats = n;
    memcpy(ethmcat[0], dpni->dpni_mcat[0], (n * 6));
#endif
}

/* ARP Hackery */

/* ARP_REQCHECK
**	Check to see if outbound ARP packet is a query to our own host
**	platform.  If so, drops it and generates a reply ourselves.
**
**	This is specially rigged so it only sends one particular kind of
**	reply -- the KLH10 is doing a proxy reply for the host platform,
**	for the case where the KLH10 just sent out an ARP request for
**	its platform!
**
**	This is needed because OSF/1 doesn't process ARP packets sent by
**	packetfilters, so it neither responds to requests nor sees replies.
**	The intent is that this packet will be looped back into the read
**	side of the DPNI20 and thus answer the KLH10's ARP request.
**
**	NOTE!  Although ordinarily the ARP reply should be addressed to
**	the correct ethernet target, here we use the broadcast address
**	because that's the only way to get it past the receive side's
**	packetfilter (short of slowing things down with another header test
**	that checks for and passes ethertype ARP).  There should only be
**	one such packet for every time a monitor is started on the KLH10
**	so this isn't too bad in the way of net citizenship.
*/
#define arp_reqcheck(p, cnt) ( \
    (((struct ether_header *)p)->ether_type == htons(ETHERTYPE_ARP))	\
    && (cnt >= ARP_PKTSIZ)	\
    && (((struct ether_arp *)(p+ETHER_HDRSIZ))->arp_op == htons(ARPOP_REQUEST)))

#define ARP_PKTSIZ (sizeof(struct ether_header)	+ sizeof(struct ether_arp))

int arp_myreply(unsigned char *buf, int cnt, struct dpx_s *dpx)
{
    struct ifent *ife;
    unsigned char *ucp;
    struct in_addr ia;
    struct ether_arp arp;
    unsigned char pktbuf[ARP_PKTSIZ];

    /* Have an ARP request.  Carry out final check to be sure
    ** the request is for an IP address belonging to our native
    ** host, which we must thus act as a proxy for.
    */
    memcpy((void *)&ia,		/* Copy IP addr to ensure aligned */
	   (void *)((struct ether_arp *)(buf+ETHER_HDRSIZ))->arp_tpa,
	   IP_ADRSIZ);
    if ((ife = osn_iftab_arp(ia)) == NULL)
	return FALSE;		/* Request for host we dunno about */

    /* Found it!  ife now points to matching entry */
    if (!ife->ife_gotea) {
	if (!osn_ifeaget2(ife->ife_name, ife->ife_ea)) {
	    error("ARP MyReply failed, no E/N addr for %s", ife->ife_name);
	    return FALSE;
	}
	ife->ife_gotea = TRUE;	/* Remember found the EN addr */
    }

    /* Now build ARP reply */

    /* First do ethernet header. Can't use ether_header cuz
    ** different systems have defined it in incompatible ways so it's
    ** impossible to win with C code.  This is progress? 
    ** Same problem afflicts arp_sha and arp_tha, which are sometimes
    ** "uchar[]" and sometimes "struct ether_addr".
    */
    ucp = pktbuf;
#if 0	/* No!  See note above... must use bcast addr */
    ea_set(ucp,    (char *)&ihost_ea);	/* First do dest addr */
#else
    memset(ucp, 0xFF, ETHER_ADRSIZ);	/* Set dest to bcast */
#endif
    ucp += ETHER_ADRSIZ;
    ea_set(ucp, (char *)&ihost_ea);	/* Set source addr to ours! */
    ucp[5]++;		/* but modified to pass the echo check */
    ucp += ETHER_ADRSIZ;
    *ucp++ = (ETHERTYPE_ARP>>8)&0377;	/* Set high byte of type */
    *ucp++ = (ETHERTYPE_ARP   )&0377;	/* Set low byte of type */

    /* Now put together the ARP packet */
    arp.arp_hrd = htons(ARPHRD_ETHER);	/* Set hdw addr format */
    arp.arp_pro = htons(ETHERTYPE_IP);	/* Set ptcl addr fmt */
    arp.arp_hln = sizeof(arp.arp_sha);	/* Hdw address len */
    arp.arp_pln = sizeof(arp.arp_spa);	/* Ptcl address len */
    arp.arp_op = htons(ARPOP_REPLY);	/* Type REPLY */

    /* Sender hdw addr and IP addr for host platform */
#if 0 /* CENV_SYS_SOLARIS */
    memcpy((char *)&arp.arp_sha, ife->ife_ea, ETHER_ADRSIZ);
#else
    memcpy((char *)arp.arp_sha, ife->ife_ea, ETHER_ADRSIZ);
#endif
    memcpy((char *)arp.arp_spa, ife->ife_ipchr, IP_ADRSIZ);

    /* Target hdw addr and IP addr for emulated 20 */
#if 0 /* CENV_SYS_SOLARIS */
    memcpy((char *)&arp.arp_tha, (char *)&ihost_ea, ETHER_ADRSIZ);
#else
    memcpy((char *)arp.arp_tha, (char *)&ihost_ea, ETHER_ADRSIZ);
#endif
    memcpy((char *)arp.arp_tpa, (char *)&ehost_ip, IP_ADRSIZ);

    /* Now build raw packet.  Do it this way to avoid potential
    ** problems with padding introduced by structure alignment.
    */
    memcpy(ucp, (char *)&arp, sizeof(struct ether_arp));

    /* Now send it!  Ignore any errors. */
    if (swstatus) {
	char ipstr[OSN_IPSTRSIZ];
	char ethstr[OSN_EASTRSIZ];
	dbprintln("ARP MyReply %s %s", ip_adrsprint(ipstr, ife->ife_ipchr),
			eth_adrsprint(ethstr, ife->ife_ea));
    }

#if 1
    /* XXX
     * Why is this sent to the packet filter (= host) and not to the -10?????
     */
    (void)osn_pfwrite(&pfdata, pktbuf, sizeof(pktbuf));

    return FALSE;
#else
    /* ARP reply packet, pass to 10 via DPC.
     * Can we do that? We're not the process which normally does that...
     */
    unsigned char *buff;
    size_t max;

    dp_xrdone(dpx);			/* First reply to the send command */

    buff = dp_xsbuff(dpx, &max);	/* Set up buffer ptr & max count */

    if (sizeof(pktbuf) <= max &&
	    dp_xswait(dpx)) {		/* Wait until buff free, in case...
				         * but we can't do that, since we are
					 * not the process receiving that
					 * signal...
					 */
	memcpy(buff, pktbuf, ARP_PKTSIZ);
	dp_xsend(dpx, DPNI_RPKT, ARP_PKTSIZ);
	if (DP_DBGFLG)
	    dbprint("sent ARP reply to -10");
    }

    return TRUE;
#endif
}

/* ETHTOTEN - Main loop for thread pumping packets from Ethernet to 10.
**	Reads packets from packetfilter and relays them to 10 using DPC
**	mechanism.
*/

#define MAXETHERLEN 1600	/* Actually 1519 but be generous */

void ethtoten(struct dpni20_s *dpni)
{
    struct dpx_s *dpx;
    int cnt;
    unsigned char *buff;
    size_t max;
    int cmin = sizeof(struct ether_header);
    int stoploop = 50;

    dpx = dp_dpxfr(&dp);		/* Get ptr to from-DP comm rgn */
    buff = dp_xsbuff(dpx, &max);	/* Set up buffer ptr & max count */

    /* Tell KLH10 we're initialized and ready by sending initial packet */
    dp_xswait(dpx);			/* Wait until buff free, in case */
    dp_xsend(dpx, DPNI_INIT, 0);	/* Send INIT */

    if (DBGFLG)
	dbprintln("sent INIT");

    /* Standard algorithm, one packet per read call */
    for (;;) {
	/* Make sure that buffer is free before clobbering it */
	dp_xswait(dpx);			/* Wait until buff free */

	if (DBGFLG)
	    dbprintln("InWait");

	/* OK, now do a blocking read on packetfilter input! */
	cnt = osn_pfread(&pfdata, buff, max);

	if (cnt <= cmin) {		/* Must get enough for ether header */

	    /* If call timed out, should return 0 */
	    if (cnt == 0 && dpni->dpni_rdtmo)
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
		fprintf(stderr, "\r\n[%s: Read=%d\r\n", progname, cnt);
		dumppkt(buff, cnt);
		fprintf(stderr, "]");
	    }
	    else
		dbprint("Read=%d", cnt);
	}

	/* Linux has no packet filtering, thus must apply manual check to
	   each and every packet read, unless dedicated.  Ugh!
	*/
	if (!pfdata.pf_can_filter && !dpni->dpni_dedic) {
	    /* Sharing interface.  Check for IP, DECNET, 802.3 */
	    if (!lnx_filter(dpni, buff, cnt)) {
		if (DBGFLG)
		    dbprint("Dropped");
		continue;		/* Drop packet, continue reading */
	    }
	}

#if 0
	if (DBGFLG)
	    if (((struct ether_header *)buff)->ether_type == htons(ETHERTYPE_ARP))
		dbprintln("Got ARP");
#endif

	/* Normal packet, pass to 10 via DPC */
	dp_xsend(dpx, DPNI_RPKT, cnt);
	if (DBGFLG)
	    dbprint("sent RPKT");
    }	/* Infinite loop reading packetfilter input */
}

/* TENTOETH - Main loop for thread pumping packets from 10 to Ethernet.
**	Reads DPC message from 10 and interprets it; if a regular
**	data message, sends to ethernet.
*/

void tentoeth(struct dpni20_s *dpni)
{
    struct dpx_s *dpx;
    int cnt;
    unsigned char *buff;
    size_t max;
    int rcnt;
    int doarpchk;
    int stoploop = 50;

    /* Must check for outbound ARP requests if asked to and have
    ** at least one entry in our table of host's IP interfaces.
    */
    /*doarpchk = (dpni->dpni_doarp & DPNI_ARPF_OCHK) && (osn_nifents() > 0);*/
    doarpchk = FALSE; /* arp_myreply() is broken anyway... */

    dpx = dp_dpxto(&dp);		/* Get ptr to "To-DP" xfer stuff */
    buff = dp_xrbuff(dpx, &max);

    if (DBGFLG)
	dbprintln("Starting loop");

    for (;;) {

	if (DBGFLG)
	    dbprintln("CmdWait");

	/* Wait until 10 has a command for us */
	dp_xrwait(dpx);		/* Wait until something there */

	/* Process command from 10! */
	switch (dp_xrcmd(dpx)) {

	case DPNI_SPKT:			/* Send regular packet */
	    rcnt = dp_xrcnt(dpx);
	    if (DBGFLG) {
		if (DBGFLG & 0x2)
		{
		    fprintf(stderr, "\r\n[%s: Sending %d\r\n", progname, rcnt);
		    dumppkt(buff, rcnt);
		    fprintf(stderr, "]");
		}
		else
		    dbprint("SPKT %d", rcnt);
	    }
	    if (doarpchk			/* If must check ARPs */
	      && arp_reqcheck(buff, rcnt)	/* and this is an ARP req */
	      && arp_myreply(buff, rcnt, dpx)) {/* and it fits, & is hacked */
		break;				/* then drop this req pkt */
	    }

	    cnt = osn_pfwrite(&pfdata, buff, rcnt);
	    if (cnt != rcnt) {
		if ((cnt < 0) && (errno == EINTR)) {
		    continue;		/* Start over, may have new cmd */
		}
		syserr(errno, "PF write %d != %d, errno %d", cnt, rcnt, errno);
		if (--stoploop <= 0)
		    efatal(1, "Too many retries, aborting");
		continue;
	    }
	    break;

	case DPNI_SETETH:
	    /* Attempt to change physical ethernet addr */
	    if (DBGFLG)
		dbprint("SETETH");
	    eth_adrset(dpni);
	    break;

	case DPNI_SETMCAT:
	    /* Attempt to change MCAT (multicast table) */
	    if (DBGFLG)
		dbprint("SETMCAT");
	    eth_mcatset(dpni);
	    break;

	case DPNI_RESET:
	    /* Attempt to do complete reset */
	    dbprint("RESET");
#if 0
	    dpni_restart(2);
#endif
	    break;

	case DPNI_QUIT:
	    /* Attempt to quit the device process gracefully */
	    if (DBGFLG)
		dbprint("QUIT");
	    return;

	default:
	    dbprintln("Unknown cmd %d", dp_xrcmd(dpx));
	    break;
	}

	/* Command done, tell 10 we're done with it */
	if (DBGFLG)
	    dbprint("CmdDone");

	dp_xrdone(dpx);
    }
}

void dumppkt(unsigned char *ucp, int cnt)
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
