/* OSDNET.C - OS Dependent Network facilities
*/
/* From: $Id: osdnet.c,v 2.8 2003/02/23 18:22:08 klh Exp $
*/
/*  Copyright © 1999, 2001 Kenneth L. Harrenstien
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

/*	This file, like DPSUP.C, is intended to be included directly
    into source code rather than compiled separately and then linked 
    together.  The reason for this is that the actual configuration
    of code will vary depending on its intended use, so a single .o
    file cannot satisfy all programs.
 */

#include <unistd.h>	/* For basic Unix syscalls */
#include <stdlib.h>

/* The possible configuration macro definitions, with defaults:
 */
#ifndef NETIFC_MAX
# define NETIFC_MAX 20
#endif

#ifndef OSDNET_INCLUDED
# include "osdnet.h"	/* Insurance to make sure our defs are there */
#endif

/* Local predeclarations */

struct ifent *osn_iflookup(char *ifnam);


/* Get a socket descriptor suitable for general net interface
   examination and manipulation; this is not necessarily suitable for
   use as a packetfilter.
   This may only make sense on Unix.
 */
int
osn_ifsock(char *ifnam, ossock_t *as)
{
    return ((*as = socket(AF_INET, SOCK_DGRAM, 0)) >= 0);
}

int
osn_ifclose(ossock_t s)
{
    return (close(s) >= 0);
}


/* Minor utilities */

char *
eth_adrsprint(char *cp, unsigned char *ea)
{
    sprintf(cp, "%x:%x:%x:%x:%x:%x", ea[0], ea[1], ea[2], ea[3], ea[4], ea[5]);
    return cp;
}

char *
ip_adrsprint(char *cp, unsigned char *ia)
{
    sprintf(cp, "%d.%d.%d.%d", ia[0], ia[1], ia[2], ia[3]);
    return cp;
}


/* Interface Table initialization
**	Gets info about all net interfaces known to the native system.
**	Used for several purposes, hence fairly general.
**	This table is used as much as possible, instead of using
**	system specific lookup 
**	A sticky point remains the ethernet addresses. They may or may
**	not be reported, and if they are it is in a system specific way.
**	Therefore we need to keep the system-specific functions for that.
**
*/
/*
  Note that searching for AF_INET or IP addresses only finds interfaces that
  are presently configured with IP addresses by "ifconfig", typically those
  that are also up.  An interface that is dedicated to the emulator will
  normally be both "down" and have no IP address bound to it.

  Another way to look at this is that until an interface is configured
  up, it has no IP address bound to it.  This is the same reason that
  SIOCGIFADDR cannot be used to find the IP address of a dedicated
  interface; there is none.
*/

/* Our own internal table of interface entries.
 */
static int iftab_initf = 0;
static int iftab_nifs = 0;
static struct ifent iftab[NETIFC_MAX];

static struct ifent *
osn_iftab_addaddress(char *name, struct sockaddr *addr);

/* Get table of all interfaces, using our own generic entry format.
 *
 * This uses either getifaddrs(3) (available on at least BSD, linux,
 * OS X) or otherwise pcap_findalldevs(3).
 *
 * getifaddrs(3) will provide link-level (ethernet) addresses on at
 * least NetBSD 1.5+, FreeBSD 4.1+, MacOS X 10.6+, Linux.
 *
 * pcap_findalldevs(3) may omit interfaces that are not IFF_UP.
 * It will provide ethernet addresses on at least NetBSD, FreeBSD, Linux.
 */
int
osn_iftab_init(void)
{
    struct ifent *ife;

    /* Start out with empty table */
    memset(&iftab[0], sizeof(iftab), 0);
    iftab_nifs = 0;

#if HAVE_GETIFADDRS
    struct ifaddrs *ifp;

    getifaddrs(&ifp);

    while (ifp) {
	if (!ifp->ifa_name)
	    continue;
	ife = osn_iftab_addaddress(ifp->ifa_name, ifp->ifa_addr);

	if (ife) {
	    ife->ife_flags = ifp->ifa_flags;
	}

	ifp = ifp->ifa_next;
    }
    freeifaddrs(ifp);

#elif HAVE_LIBPCAP
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_if_t *alldevs;
    struct ifreq ifr;
    int s;

    if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	syserr(errno, "Can't get socket for SIOCGIFFLAGS");
	return FALSE;
    }

    /*
     * This may only find interfaces that are IFF_UP.
     */
    if (pcap_findalldevs(&alldevs, errbuf) == -1) {
	error("pcap_findalldevs: %s", errbuf);
	return;
    }

    while (alldevs) {
	pcap_addr_t *addr;

	if (!alldevs->name)
	    continue;

	/* These ioctls should work for all interfaces */
	strncpy(ifr.ifr_name, alldevs->name, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFFLAGS, (char *)&ifr) < 0) {
	    syserr(errno, "SIOCGIFFLAGS for \"%s\"", alldevs->name);
	    continue;
	}

	addr = alldevs->addresses;

	while (addr) {
	    ife = osn_iftab_addaddress(alldevs->name, addr->addr);
	    addr = addr->next;
	}

	if (ife) {
	    ife->ife_flags = ifr.ifr_flags;
	}

	alldevs = alldevs->next;
    }

    pcap_freealldevs(alldevs);

    close(s);
#endif

    if (DP_DBGFLG)
	osn_iftab_show(stdout, &iftab[0], iftab_nifs);

    return iftab_nifs;
}

int
osn_nifents(void)
{
    return iftab_nifs;
}

/*
 * Remember a specific address for an interface.
 *
 * If multiple addresses are found, they overwrite each other.
 *
 * For AF_LINK addresses, in NetBSD at least it seems that the first one
 * is the default hardware address, and later ones were set with
 * "ifconfig re0 link <addr>" or removed with "ifconfig re0 link <addr>
 * -alias".  The default hardware address can't be removed this way.
 *  The net effect is that the last address set up (which is presumably
 *  the one in use) is remembered.
 *
 * Maybe we should simply remember an array of addresses of each type.
 */

static struct ifent *
osn_iftab_addaddress(char *name, struct sockaddr *addr)
{
    struct ifent *ife;
    int i;
    int idx;

    if (!name) {
	return NULL;
    }

    /* First see if the name is already known */

    idx = -1;
    for (i = iftab_nifs - 1; i >= 0; i--) {
	if (strcmp(iftab[i].ife_name, name) == 0) {
	    idx = i;
	    break;
	}
    }

    if (idx < 0) {
	idx = iftab_nifs++;
	if (idx >= NETIFC_MAX)
	    return NULL;	/* doesn't fit */
	strncpy(iftab[idx].ife_name, name, IFNAMSIZ);
	iftab[idx].ife_name[IFNAMSIZ] = '\0';
    }

    ife = &iftab[idx];

    if (addr) {
	switch (addr->sa_family) {
	case AF_INET: {
	    struct sockaddr_in *sin = (struct sockaddr_in *)addr;
	    ife->ife_ipia = sin->sin_addr;
	    ife->ife_gotip4 = TRUE;
	    break;
		      }
#if defined(AF_LINK)
	case AF_LINK: {
	    struct sockaddr_dl *sdl = (struct sockaddr_dl *)addr;
	    if (sdl->sdl_type == IFT_ETHER && sdl->sdl_alen == ETHER_ADRSIZ) {
		ea_set(ife->ife_ea, LLADDR(sdl));
		ife->ife_gotea = TRUE;
	    }
		      }
#endif /* AF_LINK*/
#if defined(AF_PACKET)
	case AF_PACKET: {
	    struct sockaddr_ll *sll = (struct sockaddr_ll *)addr;
	    if (sll->sll_hatype == ARPHRD_ETHER && sll->sll_halen == ETHER_ADRSIZ) {
		ea_set(ife->ife_ea, &sll->sll_addr);
		ife->ife_gotea = TRUE;
	    }
			}
#endif /* AF_PACKET*/
	}
    }

    return ife;
}

void
osn_iftab_show(FILE *f, struct ifent *ifents, int nents)
{
    struct ifent *ife;
    int i;

    fprintf(f, "Filtered IFE table: %d entries\r\n", nents);

    for (i = 0, ife = ifents; i < nents; ++i, ++ife) {
	fprintf(f, "%2d: \"%s\"", i, ife->ife_name);
	if (ife->ife_gotip4) {
	    unsigned char *ucp = ife->ife_ipchr;
	    fprintf(f, " (IP %d.%d.%d.%d)",
		    ucp[0], ucp[1], ucp[2], ucp[3]);
	}
	if (ife->ife_gotea) {
	    unsigned char *ucp = ife->ife_ea;
	    fprintf(f, " (%sEther %x:%x:%x:%x:%x:%x)",
		    "Extracted ",
		    ucp[0], ucp[1], ucp[2], ucp[3], ucp[4], ucp[5]);
	}
	if (ife->ife_flags & IFF_UP) {
	    fprintf(f, " UP");
	}
	if (ife->ife_flags & IFF_LOOPBACK) {
	    fprintf(f, " LOOPBACK");
	}
	fprintf(f, "\r\n");
    }
}

/* OSN_IFTAB_ARP - Look up an entry ARP-style (given IP address)
 */
struct ifent *
osn_iftab_arp(struct in_addr ia)
{
    int i = 0;
    struct ifent *ife = iftab;

    for (; i < iftab_nifs; ++i, ++ife)
	if (ife->ife_ipia.s_addr == ia.s_addr)
	    return ife;
    return NULL;
}

/* OSN_IFTAB_ARPGET - As above, but returns EA if found.
 */
int
osn_iftab_arpget(struct in_addr ia, unsigned char *eap)
{
    struct ifent *ife;

    if ((ife = osn_iftab_arp(ia)) && ife->ife_gotea) {
	ea_set(eap, ife->ife_ea);
	return TRUE;
    }
    return FALSE;
}


/* OSN_IPDEFAULT - Find a default IP interface entry; take the first one
 *	that's up and isn't a loopback.
 */
struct ifent *
osn_ipdefault(void)
{
    int i = 0;
    struct ifent *ife = iftab;

    for (; i < iftab_nifs; ++i, ++ife)
	if ((ife->ife_flags & IFF_UP) && !(ife->ife_flags & IFF_LOOPBACK))
	    return ife;
    return NULL;
}

/* OSN_IFLOOKUP - Find interface entry in our table; NULL if none.
 */
struct ifent *
osn_iflookup(char *ifnam)
{
    int i = 0;
    struct ifent *ife = iftab;

    for (; i < iftab_nifs; ++i, ++ife)
	if (strcmp(ifnam, ife->ife_name) == 0)
	    return ife;
    return NULL;
}

/* OSN_IFCREATE - Create or find an interface entry in our table.
 */
struct ifent *
osn_ifcreate(char *ifnam)
{
    struct ifent *ife = osn_iflookup(ifnam);

    if (!ife && iftab_nifs < NETIFC_MAX) {
	ife = &iftab[iftab_nifs];
	iftab_nifs++;
	strncpy(ife->ife_name, name, IFNAMSIZ);
	ife->ife_name[IFNAMSIZ] = '\0';
    }

    return ife;
}

/* OSN_IFEALOOKUP - Find ethernet address, barf if neither in our table nor
 *	available via OS.
 */
int
osn_ifealookup(char *ifnam,		/* Interface name */
	       unsigned char *eap)	/* Where to write ether address */
{
    struct ifent *ife;

    if (ife = osn_iflookup(ifnam)) {
	if (!ife->ife_gotea) {
	    ife->ife_gotea = osn_ifeaget2(ifnam, ife->ife_ea);
	}
	if (ife->ife_gotea) {
	    ea_set(eap, ife->ife_ea);
	    return TRUE;
	}
    }
    return FALSE;
}

/* OSN_ARP_STUFF - stuff emulated-host ARP entry into kernel.
**	Note it isn't necessary to specify an interface!
**	Also, the code assumes that if an ARP entry already exists in the
**	kernel for the given IP address, it will be reset to this new
**	setting rather than (eg) failing.
*/
int
osn_arp_stuff(char *ifname, unsigned char *ipa, unsigned char *eap, int pubf)
{
    char ipbuf[OSN_IPSTRSIZ];
    char eabuf[OSN_EASTRSIZ];

    if (DP_DBGFLG) {
	dbprintln("Set up ARP: %s %s %s", 
			ip_adrsprint(ipbuf, ipa),
			eth_adrsprint(eabuf, eap),
			(pubf ? "pub" : ""));
    }

#if CENV_SYS_LINUX && OSN_USE_IPONLY
    /**
     * Linux won't do proxy ARP by default. It needs to be turned on.
     * This is needed when we use an Ethernet device, not an IP tunnel.
     *
     *-----
     * According to
     * http://mailman.ds9a.nl/pipermail/lartc/2003q2/008315.html, this
     * is how Linux handles ARP requests:
     *
     * When an arp request arrives on an interface, if proxy_arp is OFF at
     * that interface, then we reply only if it asks who has an IP address
     * assigned to that interface.  In that case we reply that this IP
     * address is at the MAC address of the receiving interface.
     * 
     * If, however, proxy_arp is ON at that interface, then we check the
     * routing table (here things get a little fuzzy, since in reality the
     * routing can depend on all sorts of things other than the destination
     * address, and the arp request isn't specifying any of those) to find
     * out, if we were sending a packet to that IP address, which interface
     * we would use to send it out.  If there is such an interface (we do
     * have a route to that address) and it's NOT the same one that the
     * request arrived on, then we reply with the MAC address of the
     * interface on which the request arrived.
     *-----
     * So, that helps with a tunnel device, not with a tap device.
     * Why then does linux have ATF_PUBL flags in its ARP table?
     */
    int fd;
    char devproc[64];

    snprintf(devproc, sizeof(devproc)-1, "/proc/sys/net/ipv4/conf/%s/proxy_arp", ifname);
    /*
     * or sysctl -w net.ipv4.conf.%s.proxy_arp=1
     */
    fd = open(devproc, O_WRONLY|O_TRUNC);
    if (fd >= 0) {
	(void)write(fd, "1\n", 2);
	close(fd);
	dbprintln("Enabled net.ipv4.conf.%s.proxy_arp", ifname);
    }
    /*
     * or sysctl -w net.ipv4.conf.all.proxy_arp=1 (seems not needeede)
     */
    /**
     * When we use an IP tunnel, this is needed to cause ARP replies to
     * be sent by Linux if OSN_USE_IPONLY and if you try to connect to
     * ITS from elsewhere on the network.
     * MAYBE also for plain proxy ARP...
     *
     * Or sysctl -w net.ipv4.ip_forward=1
     */
    fd = open("/proc/sys/net/ipv4/ip_forward", O_WRONLY|O_TRUNC);
    if (fd >= 0) {
	(void)write(fd, "1\n", 2);
	close(fd);
	dbprintln("Enabled net.ipv4.ip_forward");
    }
#endif /* CENV_SYS_LINUX  && OSN_USE_IPONLY */

#if NETIF_HAS_ARPIOCTL
    struct arpreq arq;
    int sock;

    memset((char *)&arq, 0, sizeof(arq));	/* Clear & set up ARP req */
    arq.arp_pa.sa_family = AF_INET;		/* Protocol addr is IP type */
    memcpy(					/* Copy IP addr */
	(char *) &((struct sockaddr_in *)&arq.arp_pa)->sin_addr,
	ipa, sizeof(struct in_addr)); 
    arq.arp_ha.sa_family = AF_UNSPEC;		/* Hardware addr is Ether */
    ea_set(arq.arp_ha.sa_data, eap);		/* Copy Ether addr */

    /* Set ARP flags.  Always make permanent so needn't keep checking. */
    arq.arp_flags = ATF_PERM;			/* Make permanent */
    if (pubf)
	arq.arp_flags |= ATF_PUBL;		/* Publish it for us too! */

#if CENV_SYS_LINUX
    /*
     * Fill in the name of the interface from which we expect the
     * requests.
     */
    strncpy(arq.arp_dev, ifname, sizeof (arq.arp_dev));
#endif
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {	/* Get random socket */
	syserr(errno, "Cannot set ARP entry for %s %s - socket()",
			ip_adrsprint(ipbuf, ipa),
			eth_adrsprint(eabuf, eap));
	return FALSE;
    }
    (void) ioctl(sock, SIOCDARP, (char *)&arq);		/* Clear old info */
    if (ioctl(sock, SIOCSARP, (char *)&arq) < 0) {	/* Set new */
	syserr(errno, "Cannot set ARP entry for %s %s - SIOCSARP",
			ip_adrsprint(ipbuf, ipa),
			eth_adrsprint(eabuf, eap));
	close(sock);
	return FALSE;
    }
    close(sock);
    return TRUE;
#elif CENV_SYS_XBSD
    /* The new BSD systems completely did away with the ARP ioctls
       and instead substituted a far more complicated PF_ROUTE socket hack.
       Rather than attempt to duplicate the arp(8) utility code here,
       let's try simply invoking it!
       		arp -S <ipaddr> <ethaddr> pub
       Note that NetBSD doesn't support -S yet, only -s.  -S is like -s
       but deletes any existing entry first, avoiding the need for -d
       which is needed on NetBSD to avoid complaints from arp(8).
    */
    FILE *f;
    int err;
    char arpbuff[200];
    char resbuff[200];

    sprintf(arpbuff,
# if CENV_SYS_NETBSD
	    "/usr/sbin/arp -d %s; /usr/sbin/arp -s %s %s %s",
	    ip_adrsprint(ipbuf, ipa),
# else
	    "/usr/sbin/arp -S %s %s %s",
# endif
	    ip_adrsprint(ipbuf, ipa),
	    eth_adrsprint(eabuf, eap),
	    (pubf ? "pub" : ""));
    if (DP_DBGFLG)
	dbprintln("invoking \"%s\"", arpbuff);
    if ((f = popen(arpbuff, "r")) == NULL) {
	syserr(errno, "cannot popen: %s", arpbuff);
	error("Cannot set ARP entry for %s %s",
	      ip_adrsprint(ipbuf, ipa),
	      eth_adrsprint(eabuf, eap));
	return FALSE;
    }
    /* Read resulting output to avoid possibility it might hang otherwise */
    resbuff[0] = '\0';
    (void) fgets(resbuff, sizeof(resbuff)-1, f);
    err = pclose(f);		/* Hope this doesn't wait4() too long */
    if (err) {
	dbprintln("arp exit error: status %d", err);
	dbprintln("arp command was: %s", arpbuff);
    }
    if (DP_DBGFLG)
	dbprintln("arp result \"%s\"", resbuff);
    return TRUE;
#else
    error("Cannot set ARP entry for %s %s - no implementation",
	      ip_adrsprint(ipbuf, ipa),
	      eth_adrsprint(eabuf, eap));
    return FALSE;
#endif /* NETIF_HAS_ARPIOCTL, CENV_SYS_XBSD, or else */
}

/* OSN_IFIPGET - get IP address for a given interface name.
 *	This is separate from osn_ifiplook() in case iftab_init
 *	screws up for some ports.
*/
int
osn_ifipget(int s,		/* Socket for (AF_INET, SOCK_DGRAM, 0) */
	    char *ifnam,	/* Interface name */
	    unsigned char *ipa)	/* Where to write IP address */
{
    int ownsock = FALSE;
    char ipstr[OSN_IPSTRSIZ];
    struct ifreq ifr;

    if (s == -1) {
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	    syserr(errno, "Can't get IP addr for \"%s\": socket()", ifnam);
	    return FALSE;
	}
	ownsock = TRUE;
    }

    /* Find IP address for this interface */
    strncpy(ifr.ifr_name, ifnam, sizeof(ifr.ifr_name));
    if (ioctl(s, SIOCGIFADDR, (caddr_t *)&ifr) < 0) {
	syserr(errno, "Can't get IP addr for \"%s\": SIOCGIFADDR", ifnam);
	if (ownsock)
	    close(s);
	return FALSE;
    }
    memcpy(ipa,
	   (char *)&(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr),
	   IP_ADRSIZ);

    if (ownsock)
	close(s);
    if (DP_DBGFLG)
	dbprintln("IP address for \"%s\" = %s",
		  ifnam, ip_adrsprint(ipstr, ipa));
    return TRUE;
}

/* OSN_IFNMGET - get IP netmask for a given interface name.
 *	Only needed for IMP, not NI20.
*/
int
osn_ifnmget(int s,		/* Socket for (AF_INET, SOCK_DGRAM, 0) */
	    char *ifnam,	/* Interface name */
	    unsigned char *ipa)	/* Where to write IP netmask */
{
    int ownsock = FALSE;
    char ipstr[OSN_IPSTRSIZ];
    struct ifreq ifr;

    if (s == -1) {
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	    syserr(errno, "Can't get IP netmask for \"%s\": socket()", ifnam);
	    return FALSE;
	}
	ownsock = TRUE;
    }

    /* Find IP address for this interface */
    strncpy(ifr.ifr_name, ifnam, sizeof(ifr.ifr_name));
    if (ioctl(s, SIOCGIFNETMASK, (caddr_t *)&ifr) < 0) {
	syserr(errno, "Can't get IP netmask for \"%s\": SIOCGIFNETMASK",ifnam);
	if (ownsock)
	    close(s);
	return FALSE;
    }
    memcpy(ipa,
	   (char *)&(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr),
	   IP_ADRSIZ);

    if (ownsock)
	close(s);
    if (DP_DBGFLG)
	dbprintln("IP netmask for \"%s\" = %s",
		  ifnam, ip_adrsprint(ipstr, ipa));
    return TRUE;
}


/* OSN_IFEAGET2 - get physical ethernet address for a given interface name.
 *
 * This is fairly tricky as the OSD mechanism for this tends to
 * be *very* poorly documented.
 *
 * Usually this information was already found by the information from
 * either getifaddrs(3) or pcap_findalldevs(3).
 *
 * In the cases where it wasn't, we have fallbacks for DECOSF
 * and via ARP.
 *
 * Apparently only DEC OSF/1 and Linux can find this directly.
 * However, other systems seem to be divided into either of two
 * camps:
 *	4.3BSD: Hack - get the IP address of the interface, then use
 *		use SIOCGARP to look up the hardware address from the
 *		kernel's ARP table.  Clever and gross.
 *	4.4BSD: New regime, no ARP.  But can get it directly as an
 *		AF_LINK family address in the ifconf table.
*/
int
osn_ifeaget2(char *ifnam,	/* Interface name */
	    unsigned char *eap)	/* Current ether address */
{
    char eastr[OSN_EASTRSIZ];

#if CENV_SYS_DECOSF		/* Direct approach */
    {
	int ownsock = FALSE;
	struct ifdevea ifdev;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	    syserr(errno, "Can't get EN addr for \"%s\": socket()", ifnam);
	    return FALSE;
	}

	strncpy(ifdev.ifr_name, ifnam, sizeof(ifdev.ifr_name));
	if (ioctl(s, SIOCRPHYSADDR, &ifdev) < 0) {
	    syserr(errno, "Can't get EN addr for \"%s\": SIOCRPHYSADDR", ifnam);
	    if (ownsock) close(s);
	    return FALSE;
	}
	close(s);
	ea_set(eap, ifdev.current_pa);
    }

#else
    {
	/* Much more general hack */
	unsigned char ipchr[IP_ADRSIZ];
	char ipstr[OSN_IPSTRSIZ];

	/* Get IP address for this interface, as an argument for ARP lookup */
	if (!osn_ifipget(-1, ifnam, ipchr)) {
	    error("Can't get EN addr for \"%s\": osn_ifipget failed", ifnam);
	    return FALSE;
	}

	/* Have IP address, now do ARP lookup hackery */
	if (!osn_arp_look((struct in_addr *)ipchr, eap)) {
	    syserr(errno,"Can't find EN addr for \"%s\" %s using ARP",
		    ifnam, ip_adrsprint(ipstr, ipchr));
	    return FALSE;
	}
    }
#endif
    dbprintln("EN addr for \"%s\" = %s",
	      ifnam, eth_adrsprint(eastr, eap));
    return TRUE;
}

static struct eth_addr emhost_ea = 	/* Emulated host ether addr for tap */
    { 0xf2, 0x0b, 0xa4, 0xff, 0xff, 0xff };

/* OSN_PFEAGET - get physical ethernet address for an open packetfilter FD.
 *
 * Also not well documented, but generally easier to perform.
*/
int
osn_pfeaget(int pfs,		/* Packetfilter socket or FD */
	    char *ifnam,	/* Interface name (sometimes needed) */
	    unsigned char *eap)	/* Where to write ether address */
{

#if KLH10_NET_NIT
    /* SunOS/Solaris: The EA apparently can't be found until after the PF FD
    ** is open; an attempt to simply do a socket(AF_UNSPEC, SOCK_DGRAM, 0)
    ** will just fail with a protocol-not-supported error.  And using an
    ** AF_INET socket just gets us the IP address, sigh.
    ** Perhaps using AF_DECnet would work, as it does for OSF/1?
    */
    struct ifreq ifr;
    struct strioctl si;		/* Must use kludgy indirect cuz stream */

    strncpy(ifr.ifr_name, ifnam, sizeof(ifr.ifr_name));
    si.ic_timout = INFTIM;
    si.ic_cmd = SIOCGIFADDR;
    si.ic_len = sizeof ifr;
    si.ic_dp = (char *)&ifr;
    if (ioctl(pfs, I_STR, (char *)&si) < 0) {
	syserr(errno, "ether SIOCGIFADDR failed for \"%s\"", ifnam);
	ea_clr(eap);
	return FALSE;
    }
    ea_set(eap, &ifr.ifr_addr.sa_data[0]);	/* Return the ether address */

#elif KLH10_NET_PFLT	/* Really DECOSF */
    struct endevp endp;

    if (ioctl(pfs, EIOCDEVP, (caddr_t *)&endp) < 0) {
	syserr(errno, "EIOCDEVP failed");
	ea_clr(eap);
	return FALSE;
    }
    if (endp.end_dev_type != ENDT_10MB
	|| endp.end_addr_len != ETHER_ADRSIZ) {
	syserr(errno, "EIOCDEVP returned non-Ethernet info!");
	ea_clr(eap);
	return FALSE;
    }
    ea_set(eap, endp.end_addr);

#elif KLH10_NET_TAP_BRIDGE
    /* If we do tap(4) + bridge(4), the ether address of the tap is wholly
     * irrelevant, it is on the other side of the "wire".
     * Our own address is something we can make up completely.
     */
    if (emhost_ea.ea_octets[5] == 0xFF) {
	time_t t = time(NULL);
	emhost_ea.ea_octets[5] =  t        & 0xFE;
	emhost_ea.ea_octets[4] = (t >>  8) & 0xFF;
	emhost_ea.ea_octets[3] = (t >> 16) & 0xFF;
    }
    ea_set(eap, &emhost_ea);	/* Return the ether address */
#elif (KLH10_NET_BPF && !CENV_SYS_NETBSD && !CENV_SYS_FREEBSD)
    /* NetBSD no longer seems to support this (on bpf) */
    struct ifreq ifr;

    strncpy(ifr.ifr_name, ifnam, sizeof(ifr.ifr_name));
    if (ioctl(pfs, SIOCGIFADDR, (char *) &ifr) < 0) {
	syserr(errno, "SIOCGIFADDR for EA failed (%d, \"%s\")",
					pfs, ifnam);
	ea_clr(eap);
	return FALSE;
    }
    ea_set(eap, &ifr.ifr_addr.sa_data[0]);	/* Return the ether address */

#elif KLH10_NET_DLPI
  {
    static int dleaget(int fd, unsigned char *eap);

    /* Use handy auxiliary to hide hideous DLPI details */
    if (!dleaget(pfs, eap))
	return FALSE;
  }
#else
    if (!osn_ifealookup(ifnam, eap))
	return FALSE;
#endif

    if (DP_DBGFLG) {
	char eastr[OSN_EASTRSIZ];

	dbprintln("EN addr for \"%s\" = %s",
		ifnam, eth_adrsprint(eastr, eap));
    }
    return TRUE;
}

#if CENV_SYS_XBSD
static int osn_pareth(char *cp, unsigned char *adr);
#endif

/* OSN_ARP_LOOK - Attempt looking up ARP information from OS
 *	This may be needed by DPIMP to find the ether addr for an IP address,
 *	or by DPNI20 when trying to determine its own ether address.
 *	In any case, it's a very OS-dependent technique worth encapsulating.
 *
 * NOTE: Failure to find an address should not cause an error printout,
 *	unless there is some actual system glitch.
 */
int
osn_arp_look(struct in_addr *ipa,	/* Look up this IP address */
	     unsigned char *eap)	/* Return EA here */
{
#if NETIF_HAS_ARPIOCTL
    char ipstr[OSN_IPSTRSIZ];
    struct arpreq arq;
    int sock;

    /* Query kernel directly. */
    memset((char *)&arq, 0, sizeof(arq));	/* Clear & set up ARP req */
    arq.arp_pa.sa_family = AF_INET;		/* Protocol addr is IP type */
    arq.arp_ha.sa_family = AF_UNSPEC;		/* Hardware addr is Ether */
    ((struct sockaddr_in *)&arq.arp_pa)->sin_addr = *ipa;

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {	/* Get random socket */
	syserr(errno, "osn_arp_look socket()");
	return FALSE;
    }
    if (ioctl(sock, SIOCGARP, (char *)&arq) != 0) {	/* Get ARP info */
	close(sock);
	/* If SIOCGARP fails, assume it just means the entry wasn't found,
	   rather than implying some error.
	 */
	if (DP_DBGFLG)
	    dbprintln("No ARP info for %s",
		      ip_adrsprint(ipstr, (unsigned char *)ipa));
	return FALSE;
    }
    close(sock);

    /* Won, check flags */
    if (!(arq.arp_flags & ATF_COM)) {
	if (DP_DBGFLG)
	    dbprintln("ARP entry incomplete for %s",
		      ip_adrsprint(ipstr, (unsigned char *)ipa));
	return FALSE;
    }
    ea_set((char *)eap, arq.arp_ha.sa_data);	/* Copy ether addr */
    return TRUE;

#elif CENV_SYS_XBSD
    /* The new BSD stuff did away with the ARP ioctls and substituted an
    ** extremely complicated routing IPC mechanism.  For the time being
    ** I'll just use a horrible hack.
    */
    unsigned char *cp = (unsigned char *)ipa;
    char *arppath = "/usr/sbin/arp";
    char arpbuff[128];
    FILE *f;
    char fhost[100];
    char fip[32];
    char fat[8];
    char fhex[32];
    struct eth_addr etha;
    int res;

    /* Use -n to avoid symbolic lookup hangs */
    sprintf(arpbuff, "%s -n %u.%u.%u.%u", arppath,
		cp[0], cp[1], cp[2], cp[3]);
    if (DP_DBGFLG)
	dbprintln("invoking \"%s\"", arpbuff);
    if ((f = popen(arpbuff, "r")) == NULL) {
	syserr(errno, "cannot popen: %s", arpbuff);
	return FALSE;
    }
    arpbuff[0] = '\0';
    (void) fgets(arpbuff, sizeof(arpbuff)-1, f);
    res = pclose(f);		/* Hope this doesn't wait4() too long */
    if (res)
	dbprintln("arp exit error: status %d", res);
    if (DP_DBGFLG)
	dbprintln("arp result \"%s\"", arpbuff);

    /* Parse the result.  There are three possible formats:
    **	<hostid> (<d.d.d.d>) at <etherhex> <options?>	4+ words
    **	<hostid> (<d.d.d.d>) at (incomplete)	4 words only?
    **	<hostid> (<d.d.d.d>) -- no entry	5 words only
    */
    res = sscanf(arpbuff, "%99s %31s %7s %31s",  fhost, fip, fat, fhex);
    if (res == 4 && (strcmp(fat, "at")==0)
		 && osn_pareth(fhex, (unsigned char *)&etha)) {
	ea_set(eap, &etha);
	return TRUE;
    }

    /* Failed, see if failure is understood or not */
    if (res == 4
      && ( ((strcmp(fat, "at")==0) && strcmp(fhex, "(incomplete)")==0)
	|| ((strcmp(fat, "--")==0) && strcmp(fhex, "no")==0))) {

	/* Failed in a way we understand, so don't complain */
	return FALSE;
    }
    error("osn_arp_look result unparseable: \"%s\"", arpbuff);
    return FALSE;

#else
    error("osn_arp_look not implemented");
    return FALSE;
#endif
}

#if CENV_SYS_XBSD	/* Auxiliary to support code in osn_arp_look() */
static int
osn_pareth(char *cp, unsigned char *adr)
{
    unsigned int b1, b2, b3, b4, b5, b6;
    int cnt;

    cnt = sscanf(cp, "%x:%x:%x:%x:%x:%x", &b1, &b2, &b3, &b4, &b5, &b6);
    if (cnt != 6) {
	/* Later try as single large address #? */
	return FALSE;
    }
    if (b1 > 255 || b2 > 255 || b3 > 255 || b4 > 255 || b5 > 255 || b6 > 255)
	return FALSE;
    *adr++ = b1;
    *adr++ = b2;
    *adr++ = b3;
    *adr++ = b4;
    *adr++ = b5;
    *adr   = b6;
    return TRUE;
}
#endif /* CENV_SYS_XBSD */

#if !OSN_USE_IPONLY	/* This stuff not needed for DPIMP */

/* OSN_IFEASET - Set physical ethernet address for a given interface name.
 *
 * Like osn_ifeaget, also tricky with obscure non-standard mechanisms.
 *
*/
int
osn_ifeaset(int s,		/* Socket for (AF_INET, SOCK_DGRAM, 0) */
	    char *ifnam,	/* Interface name */
	    unsigned char *newpa)	/* New ether address */
{
#if CENV_SYS_DECOSF || CENV_SYS_LINUX || KLH10_NET_TAP_BRIDGE \
		    || (CENV_SYS_FREEBSD && defined(SIOCSIFLLADDR))

    /* Common preamble code */
    int ownsock = FALSE;
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifnam, sizeof(ifr.ifr_name));
    if (s == -1) {
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	    syserr(errno, "Failed osn_ifeaset socket()");
	    return FALSE;
	}
	ownsock = TRUE;
    }

# if CENV_SYS_DECOSF		/* Direct approach */
    /* NOTE!!! DECOSF Doc bug!
    ** Contrary to appearances in <sys/ioctl.h where SIOCSPHYSADDR is
    ** said to take a "struct devea" arg, it actually uses an ifreq!
    ** SIOCRPHYSADDR does use devea... barf!
    */
    ifr.ifr_addr.sa_family = AF_DECnet;		/* Odd choice, but works */
    ea_set(ifr.ifr_addr.sa_data, newpa);
    if (ioctl(s, SIOCSPHYSADDR, &ifr) < 0) {
	syserr(errno, "\"%s\" SIOCSPHYSADDR failed", ifnam);
	if (ownsock) close(s);
	return FALSE;
    }

# elif CENV_SYS_LINUX

    /* Address family must match what device thinks it is, so find that
       out first... sigh.
    */
    if (ioctl(s, SIOCGIFHWADDR, &ifr) < 0) {
	syserr(errno, "\"%s\" SIOCGIFHWADDR failed", ifnam);
	if (ownsock) close(s);
	return FALSE;
    }
    ea_set(ifr.ifr_addr.sa_data, newpa);	/* Now set new addr */
    if (ioctl(s, SIOCSIFHWADDR, &ifr) < 0) {
	syserr(errno, "\"%s\" SIOCSIFHWADDR failed", ifnam);
	if (ownsock) close(s);
	return FALSE;
    }

# elif CENV_SYS_FREEBSD && defined(SIOCSIFLLADDR)
    /* This works for 4.2 and up; as of 3.3 no known way to set ether addr. */

    ifr.ifr_addr.sa_len = 6;
    ifr.ifr_addr.sa_family = AF_LINK;		/* Must be this */
    ea_set(ifr.ifr_addr.sa_data, newpa);
    if (ioctl(s, SIOCSIFLLADDR, &ifr) < 0) {
	syserr(errno, "\"%s\" SIOCSIFLLADDR failed", ifnam);
	if (ownsock) close(s);
	return FALSE;
    }
# elif KLH10_NET_TAP_BRIDGE
    ea_set(&emhost_ea, newpa);
# else
#  error "Unimplemented OS routine osn_ifeaset()"
# endif
    /* Common postamble code */
    if (ownsock)
	close(s);
    return TRUE;

#else
# ifdef __GNUC__
#  if CENV_SYS_NETBSD	/* As of 1.6 NetBSD STILL has no way to do this */ 
#   warning "NetBSD still sucks - Unimplemented OS routine osn_ifeaset()"
#  else
#   warning "Unimplemented OS routine osn_ifeaset()"
#  endif
# endif
    error("\"%s\" could not set ether addr - osn_ifeaset() unimplemented",
	  ifnam);
    return FALSE;
#endif
}



/* OSN_IFMCSET - Set ethernet multicast address (add or delete),
 *	or change promiscuous mode.
 *	Hack for now: if "pa" argument is NULL, we are dealing with
 *	promiscuous mode; "delf" is TRUE to turn off, else it's turned on.
 */
int
osn_ifmcset(int s,
	    char *ifnam,
	    int delf,
	    unsigned char *pa)
{
#if CENV_SYS_DECOSF || CENV_SYS_LINUX || CENV_SYS_FREEBSD || CENV_SYS_NETBSD

    /* Common preamble code */
    int ownsock = FALSE;
    struct ifreq ifr;

    if (s == -1) {
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	    syserr(errno, "Failed osn_ifmcset socket()");
	    return FALSE;
	}
	ownsock = TRUE;
    }
    strncpy(ifr.ifr_name, ifnam, sizeof(ifr.ifr_name));

# if CENV_SYS_DECOSF
    ifr.ifr_addr.sa_family = AF_DECnet;	/* Known to work; AF_UNSPEC may not */
# elif CENV_SYS_LINUX
    ifr.ifr_addr.sa_family = AF_UNSPEC;	/* MUST be this for Linux! */
# elif CENV_SYS_FREEBSD || CENV_SYS_NETBSD
    ifr.ifr_addr.sa_family = AF_LINK;	/* MUST be this for FreeBSD! */
# else
#  error "Unimplemented OS routine osn_ifmcset()"
# endif

    if (pa) {
	/* Doing multicast stuff */
	ea_set(ifr.ifr_addr.sa_data, pa);

	if (ioctl(s, (delf ? SIOCDELMULTI : SIOCADDMULTI), &ifr) < 0) {
	    syserr(errno, "\"%s\" %s failed", ifnam,
		   (delf ? "SIOCDELMULTI" : "SIOCADDMULTI"));
	bad:
	    if (ownsock) close(s);
	    return FALSE;
	}
    } else {
	/* Doing promiscuous stuff */
	int flags;

	if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0) {
	    syserr(errno, "SIOCGIFFLAGS failed for interface \"%s\"", ifnam);
	    goto bad;

	}
	if (delf)
	    ifr.ifr_flags &= ~IFF_PROMISC;	/* Turn off */
	else
	    ifr.ifr_flags |= IFF_PROMISC;	/* Turn on */

	if (ioctl(s, SIOCSIFFLAGS, &ifr) < 0) {
	    syserr(errno, "SIOCSIFFLAGS failed for interface \"%s\"", ifnam);
	    goto bad;
	}
    }
    if (ownsock)
	close(s);
    return TRUE;

#else
# ifdef __GNUC__
#  warning "Unimplemented OS routine osn_ifmcset()"
# endif
    error("\"%s\" could not %s multicast addr - osn_ifmcset() unimplemented",
	  ifnam, (delf ? "delete" : "add"));
    return FALSE;
#endif
}

#endif /* !OSN_USE_IPONLY */

/* OSN_PFINIT - Get and initialize file descriptor for packetfilter.
 *	Actual degree to which the PF is initialized is still
 *	very OSD.
 *	FD is always opened for both read/write.
 */
#if KLH10_NET_PCAP
void
osn_pfinit(struct pfdata *pfdata, struct osnpf *osnpf, void *pfarg)
{
    char errbuf[PCAP_ERRBUF_SIZE];
    char *what = "";
    pcap_t *pc;
    char *ifnam = osnpf->osnpf_ifnam;

    if (DP_DBGFLG)
	dbprint("Opening PCAP device");

    if (!ifnam || !ifnam[0]) {	/* Allow default ifc */
	struct ifent *ife = osn_ipdefault();
	if (!ife)
	    esfatal(1, "Ethernet interface must be specified");
	ifnam = ife->ife_name;
    }

    pfdata->pf_handle = pc = pcap_create(ifnam, errbuf);
    pfdata->pf_ip4_only = FALSE;

    if (!pc) {
	what = "pcap_create";
	goto error;
    }
    if (pcap_set_snaplen(pc, ETHER_MTU + 100) < 0) {	/* fuzz */
	what = "pcap_set_snaplen";
	goto error;
    }
    /* Set immediate mode so that packets are processed as they arrive,
       rather than waiting until timeout or buffer full.

       WARNING: NetBSD does not implement this correctly!  The code in
       src/sys/net/bpf.c:bpfread() treats the immediate flag in a way that
       causes it to return EWOULDBLOCK [[ note: I see EAGAIN]] if no input is
       available.  But this flag must still be set in order for bpfpoll() to
       detect input as soon as it arrives!
       See read loops in osn_pfread() below for workaround.
     */
    if (pcap_set_immediate_mode(pc, 1) < 0) {
	what = "pcap_set_immediate_mode";
	goto error;
    }

    /* Set read timeout.
       Safety check in order to avoid infinite hangs if something
       wedges up.  The periodic re-check overhead is insignificant.
     */
    if (osnpf->osnpf_rdtmo) {
	if (pcap_set_timeout(pc, osnpf->osnpf_rdtmo * 1000) < 0) {
	    syserr(1, "pcap_set_timeout failed");
	}
    }

    if (pcap_activate(pc) < 0) {
	what = "pcap_activate";
	goto error;
    }

    /* Set up packet filter for it - only needed if sharing interface?
       Not sure whether it's needed for a dedicated interface; will need
       to experiment.
     */
    if (!osnpf->osnpf_dedic) {
	struct OSN_PFSTRUCT *pf;

	/* Set the kernel packet filter */
	pf = pfbuild(pfarg, &(osnpf->osnpf_ip.ia_addr));
	if (pcap_setfilter(pc, pf) < 0) {
	    syserr(1, "pcap_setfilter failed");
	}
	pfdata->pf_can_filter = TRUE;
    } else {
	pfdata->pf_can_filter = FALSE;
    }

    if (pcap_setdirection(pc, PCAP_D_INOUT) < 0) {
	what = "pcap_setdirection";
	goto error;
    }

    if (pcap_set_datalink(pc, DLT_EN10MB) < 0) {
	what = "pcap_set_datalink";
	goto error;
    }

    pfdata->pf_fd = pcap_get_selectable_fd(pc);

    /* Now get our interface's ethernet address.
    **	In general, this has to wait until after the packetfilter is opened,
    **	since until then we don't have a handle on the specific interface
    **	that will be used.
    */
    //(void) osn_pfeaget(pfdata->pf_fd, ifnam, (unsigned char *)&(osnpf->osnpf_ea));
    (void) osn_ifealookup(ifnam, (unsigned char *) &osnpf->osnpf_ea);

    return;

error:
    if (pc) {
	syserr(1, "pcap_geterr: %s", pcap_geterr(pc));
	pcap_close(pc);
    }
    esfatal(1, "pcap error for %s, %s: %s", ifnam, what, errbuf);

    /* not reached */
}


#if CENV_SYS_NETBSD
#include <poll.h>	/* For NetBSD mainly */
#endif

/*
 * Like the standard read(2) call:
 * Receives a single packet and returns its size.
 * Include link-layer headers, but no BPF headers or anything like that.
 */
inline
ssize_t
osn_pfread(struct pfdata *pfdata, void *buf, size_t nbytes)
{
    struct pcap_pkthdr pkt_header;
    const u_char *pkt_data;

tryagain:
    errno = 0;
    pkt_data = pcap_next(pfdata->pf_handle, &pkt_header);

    if (pkt_data) {
	if (pkt_header.caplen < nbytes)
	    nbytes = pkt_header.caplen;
	memcpy(buf, pkt_data, nbytes);

	if (DP_DBGFLG)
	    dbprint("osn_pfread: read %d bytes", nbytes);

	return nbytes;
    }

#if CENV_SYS_NETBSD
	    /* NetBSD bpf is broken.
	       See osdnet.c:osn_pfinit() comments re BIOCIMMEDIATE to
	       understand why this crock is necessary.
	       Always block for at least 10 sec, will wake up sooner if
	       input arrives.
	     */
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
	int ptimeout = 10;
	struct pollfd myfd;
	int err = errno;

	if (DP_DBGFLG)
	    dbprint("osn_pfread: polling after reading nothing (err=%d)", err);
	myfd.fd = pfdata->pf_fd;
	myfd.events = POLLIN;
	(void) poll(&myfd, 1, ptimeout * 1000);
	goto tryagain;
    }
#endif

    /* From pcap_next_ex(3): "Unfortunately, there is no way to determine
     * whether an error occurred or not."
     */
    return 0;
}

/*
 * Like the standard write(2) call:
 * Expect a full ethernet frame including link-layer header.
 * returns the number of bytes written.
 */
inline
int
osn_pfwrite(struct pfdata *pfdata, const void *buf, size_t nbytes)
{
    //if (DP_DBGFLG)
    //	dbprint("osn_pfwrite: writing %d bytes", nbytes);
    return pcap_inject(pfdata->pf_handle, buf, nbytes);
}
#endif

#if KLH10_NET_BPF

/* Adapted from DEC's pfopen.c - doing it ourselves here because pfopen(3)
 * did not always exist, and this way we can report errors better.
 */

static int
pfopen(void)
{
# define PFDEVPREF "/dev/bpf"
    char pfname[sizeof(PFDEVPREF)+10];
    int fd;
    int i = 0;

    /* Find first free packetfilter device */
    do {
	(void) sprintf(pfname, "%s%d", PFDEVPREF, i++);
	fd = open(pfname, O_RDWR, 0);
    } while (fd < 0 && errno == EBUSY);	/* If device busy, keep looking */

    if (fd < 0) {
	/* Note possible error meanings:
	   ENOENT - no such filename
	   ENXIO  - not configured in kernel
	*/
	esfatal(1, "Couldn't find or open packetfilter device, last tried %s",
		pfname);
    }
    return fd;		/* Success! */
}

#endif /* KLH10_NET_BPF */


#if KLH10_NET_TUN
/*
  In order to use the TUN interface we have to do the equivalent of
	(1) "ifconfig tun0 <localaddr> <destaddr> up"
		as well as
	(2) "arp <destaddr> <localetheraddr> perm pub"
		and finally
	(3) "sysctl -w net.inet.ip.forwarding=1"
		if you want to connect in from other machines besides
		the one the emulator is running on.  This last step must
		still be done externally as it affects overall system
		security.

For (1) the code must flush any existing address, add the new address, and
ensure the interface's IFF_UP flag is set.  Assuming we'll always be running
on *BSD because that's the only system supporting TUN at the moment, the
necessary calls are
		SIOCDIFADDR - Delete existing address
		SIOCAIFADDR - Add existing address (and broadcast and mask)
		SIOCSIFFLAGS - To set IFF_UP

  Some comments about how SIOCAIFADDR and SIOCDIFADDR work, based on
  observation of the FreeBSD code:

  The miniscule doc is in netintro(4).  Use an AF_INET, DGRAM socket.
  Both take:

DELETE takes a struct ifreq (can use an ifaliasreq like the doc claims,
but the SIOCDIFADDR ioctl only defines it as taking an ifreq's worth of
data!).  If the first addr matches an existing one, that one is
deleted.  Otherwise, the first AF_INET address is deleted; INADDR_ANY
from in.h (ie all zeros) is probably the right thing for this case.


ADD takes a new struct so as to set everything at once:

    struct ifaliasreq {
             char    ifra_name[IFNAMSIZ];   // if name, e.g. "en0"
             struct  sockaddr        ifra_addr;
             struct  sockaddr        ifra_broadaddr;
             struct  sockaddr        ifra_mask;
     };

For ADD, the ifra_addr field is the new address to add.
However, the ifra_broadaddr field is actually used in two different ways.
    If the interface has IFF_BROADCAST set, then ifra_broadaddr is the
	broadcast address, as advertised.
    But if it has IFF_POINTOPOINT set, it is interpreted as "ifra_dstaddr";
	the destination (or remote) address.  It is an error if this address
	is INADDR_ANY.
The ifra_mask field is ignored (left as-is or zeroed if new) unless
    ifra_mask.sin_len is non-zero.

 */

void
osn_pfinit(struct pfdata *pfdata, struct osnpf *osnpf, void *arg)
{
    int allowextern = TRUE;	/* For now, always try for external access */
    int fd;
#if CENV_SYS_LINUX		/* [BV: tun support for Linux] */
    struct ifreq ifr;
    char ifnam[IFNAMSIZ];
#else /* not CENV_SYS_LINUX */
    char tunname[sizeof "/dev/tun000"];
    char *ifnam = tunname + sizeof("/dev/")-1;
    int i = -1;
#endif /* CENV_SYS_LINUX */
    char ipb1[OSN_IPSTRSIZ];
    char ipb2[OSN_IPSTRSIZ];
    struct ifent *ife = NULL;	/* Native host's default IP interface if one */
    struct in_addr iplocal;	/* TUN ifc address at hardware OS end */
    struct in_addr ipremote;	/* Address at remote (emulated host) end */
    static unsigned char ipremset[4] = { 192, 168, 0, 44};

    /* Remote address is always that of emulated machine */
    ipremote = osnpf->osnpf_ip.ia_addr;
    iplocal = osnpf->osnpf_tun.ia_addr;

    if (DP_DBGFLG)
	dbprint("Opening TUN device");

    /* Local address can be set explicitly if we plan to do full IP
       masquerading. */
    if (memcmp((char *)&iplocal, "\0\0\0\0", IP_ADRSIZ) == 0) {
      /* Local address is that of hardware machine if we want to permit
	 external access.  If not, it doesn't matter (and may not even
	 exist, if there is no hardware interface)
      */
      if (allowextern) {
	  if (osn_iftab_init() && (ife = osn_ipdefault())) {
	      iplocal = ife->ife_ipia;
	  } else {
	      error("Cannot find default IP interface for host");
	      allowextern = FALSE;
	  }
      }
      if (!allowextern) {
	  /* Make up bogus IP address for internal use */
	  memcpy((char *)&iplocal, ipremset, 4);
      }
      osnpf->osnpf_tun.ia_addr = iplocal;
    }

#if CENV_SYS_LINUX		/* [BV: Linux way] */
    if ((fd = open("/dev/net/tun", O_RDWR)) < 0) /* get a fresh device */
      esfatal(0, "Couldn't open tunnel device /dev/net/tun");
    memset(&ifr, 0, sizeof(ifr));
# if OSN_USE_IPONLY
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI; /* TUN (no Ethernet headers), no pkt info */
    /* ip tuntap add mode tun */
# else
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI; /* TAP (yes Ethernet headers), no pkt info */
    /* ip tuntap add mode tap */
# endif /* OSN_USE_IPONLY */
    if (ioctl(fd, TUNSETIFF, (void *) &ifr) < 0) /* turn it on */
      esfatal(0, "Couldn't set tun device");
    strcpy(ifnam, ifr.ifr_name); /* get device name (typically "tun0") */
#else /* not CENV_SYS_LINUX */
    do {
# if OSN_USE_IPONLY
	sprintf(tunname, "/dev/tun%d", ++i);
# else
	sprintf(tunname, "/dev/tap%d", ++i);
# endif /* OSN_USE_IPONLY */
    } while ((fd = open(tunname, O_RDWR)) < 0 && errno == EBUSY);

    if (fd < 0)
	esfatal(1, "Couldn't open tunnel device %s", tunname);
#endif /* CENV_SYS_LINUX */

    if (DP_DBGFLG)
	dbprintln("Opened %s, configuring for local %s, remote %s",
	    ifnam,
	    ip_adrsprint(ipb1, (unsigned char *)&iplocal),
	    ip_adrsprint(ipb2, (unsigned char *)&ipremote));

    strcpy(osnpf->osnpf_ifnam, ifnam);

    /* Activate TUN device.
       First address is "local" -- doesn't matter if all we care about is
       talking to the native/hardware host, and it can be set to any of the IP
       address ranges reserved for LAN-only (non-Internet) use, such as
       10.0.0.44.
       However, if planning to allow other machines to access the virtual
       host, probably best to use an address suitable for the same LAN
       subnet as the hardware host.
       Unclear yet whether it works to use the host's own address; it at
       least allows the configuration to happen.

       Second address is "remote" -- the one the emulated host is using.
       It should probably match the same network as the local address,
       especially if planning to connect from other machines.

       This is only needed for TUNNEL devices, not TAP devices.
    */
#if 0	/* Hacky method */
  {
    char cmdbuff[128];
    int res;
    sprintf(cmdbuff, "ifconfig %s %s %s up",
	    ifnam,
	    ip_adrsprint(ipb1, (unsigned char *)&iplocal),
	    ip_adrsprint(ipb2, (unsigned char *)&ipremote));

    if ((res = system(cmdbuff)) != 0) {
	esfatal(1, "osn_pfinit: ifconfig failed to initialize tunnel device?");
    }
  }
#elif CENV_SYS_LINUX		/* [BV: Linux tun device] */
				/* "Hacky" but simple method */
# if OSN_USE_IPONLY
  {
    char cmdbuff[128];
    int res;

    /* ifconfig DEV IPLOCAL pointopoint IPREMOTE */
    sprintf(cmdbuff, "ifconfig %s %s pointopoint %s up",
	    ifnam,
	    ip_adrsprint(ipb1, (unsigned char *)&iplocal),
	    ip_adrsprint(ipb2, (unsigned char *)&ipremote));
    if (DP_DBGFLG)
      dbprintln("running \"%s\"",cmdbuff);
    if ((res = system(cmdbuff)) != 0) {
	esfatal(1, "osn_pfinit: ifconfig failed to initialize tunnel device?");
    }
  }
# endif /* OSN_USE_IPONLY */
#else /* not CENV_SYS_LINUX */
  {
    /* Internal method */
    int s;
    struct ifaliasreq ifra;
    struct ifreq ifr;

    if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	esfatal(1, "pf_init: tun socket() failed");
    }

    /* Delete first (only) IP address for this device, if any.
       Ignore errors.
     */
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifnam, sizeof(ifr.ifr_name));
    if (ioctl(s, SIOCDIFADDR, &ifr) < 0) {
	if (DP_DBGFLG)
	    syserr(errno, "osn_pfinit tun SIOCDIFADDR failed");
    }

# if OSN_USE_IPONLY
    /*
     * Then set the point-to-point addresses for the tunnel.
     */
    memset(&ifra, 0, sizeof(ifra));
    strncpy(ifra.ifra_name, ifnam, sizeof(ifra.ifra_name));
    ((struct sockaddr_in *)(&ifra.ifra_addr))->sin_len = sizeof(struct sockaddr_in);
    ((struct sockaddr_in *)(&ifra.ifra_addr))->sin_family = AF_INET;
    ((struct sockaddr_in *)(&ifra.ifra_addr))->sin_addr   = iplocal;
    ((struct sockaddr_in *)(&ifra.ifra_broadaddr))->sin_len = sizeof(struct sockaddr_in);
    ((struct sockaddr_in *)(&ifra.ifra_broadaddr))->sin_family = AF_INET;
    ((struct sockaddr_in *)(&ifra.ifra_broadaddr))->sin_addr   = ipremote;
    if (ioctl(s, SIOCAIFADDR, &ifra) < 0) {
	esfatal(1, "osn_pfinit tun SIOCAIFADDR failed");
    }
# endif /* OSN_USE_IPONLY */


    /* Finally, turn on IFF_UP just in case the above didn't do it.
       Note interface name is still there from the SIOCDIFADDR.
     */
    if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0) {
	esfatal(1, "osn_pfinit tun SIOCGIFFLAGS failed");
    }
    if (!(ifr.ifr_flags & IFF_UP)) {
	ifr.ifr_flags |= IFF_UP;
	if (ioctl(s, SIOCSIFFLAGS, &ifr) < 0) {
	    esfatal(1, "osn_pfinit tun SIOCSIFFLAGS failed");
	}
	if (DP_DBGFLG)
	    dbprint("osn_pfinit tun did SIOCSIFFLAGS");
    }
  }
#endif /* CENV_SYS_LINUX */

    /* Now optionally determine ethernet address.
       This amounts to what if anything we should put in the native
       host's ARP tables.
       - If we only intend to use the net between the virtual host and
       its hardware host, then no ARP hackery is needed.
       - However, if the intent is to allow traffic between the virtual
       host and other machines on the LAN or Internet, then an ARP
       entry is required.  It must advertise the virtual host's IP
       address, using one of the hardware host's ethernet addresses
       so any packets on the LAN for the virtual host will at least
       wind up arriving at the hardware host it's running on.
     */

    /* Simple method is to get the ifconf table and scan it to find the
       first interface with both an IP and ethernet address given.
       Non-4.4BSD systems may not provide the latter datum, but if
       we're using TUN then we almost certainly also have the good stuff
       in ifconf.
    */
    if (allowextern && ife) {
	/* Need to determine ether addr of our default interface, then
	   publish an ARP entry mapping the virtual host to the same
	   ether addr.
	 */
	(void) osn_arp_stuff(ifnam, (unsigned char *)&ipremote, ife->ife_ea, TRUE);

	/* Return that as our ether address */
	ea_set((char *)&osnpf->osnpf_ea, ife->ife_ea);
    } else {
	/* ARP hackery will be handled by IP masquerading and packet forwarding. */
#if 1 /*OSN_USE_IPONLY*/ /* TOPS-20 does not like NI20 with made up address? */
	/* Assume no useful ether addr for tun interface. */
	ea_clr((char *)&osnpf->osnpf_ea);
#else
        /* Assign requested address to tap interface or get kernel assigned one. */
        if (memcmp((char *)&osnpf->osnpf_ea, "\0\0\0\0\0\0", ETHER_ADRSIZ) == 0) {
          osn_ifeaget(-1, ifnam, (unsigned char *)&osnpf->osnpf_ea, NULL);
        }
        else {
          osn_ifeaset(-1, ifnam, (unsigned char *)&osnpf->osnpf_ea);
        }
#endif
    }

    pfdata->pf_fd = fd;
    pfdata->pf_handle = 0;
    pfdata->pf_can_filter = FALSE;
    pfdata->pf_ip4_only = OSN_USE_IPONLY;

    if (DP_DBGFLG)
	dbprintln("osn_pfinit tun completed");
}

#endif /* KLH10_NET_TUN */

/*
 * Too bad that this is never called...
 */
void
osn_pfdeinit(void)
{
#if KLH10_NET_TAP_BRIDGE
    void tap_bridge_close();
    tap_bridge_close();
#endif
}

#if KLH10_NET_TAP_BRIDGE

void
osn_pfinit(struct pfdata *pfdata, struct osnpf *osnpf, void *arg)
{
    int fd;
    char *ifnam = osnpf->osnpf_ifnam;

    if (DP_DBGFLG)
	dbprint("Opening TAP+BRIDGE device");

    /* No "default interface" concept here */
    if (!ifnam || !ifnam[0])
	esfatal(1, "Packetfilter interface must be specified");

    fd = tap_bridge_open(ifnam);

    /* Now get our fresh new virtual interface's ethernet address.
    */
    (void) osn_pfeaget(fd, ifnam, (unsigned char *)&(osnpf->osnpf_ea));

    struct ifent *ife = osn_ifcreate(ifnam);
    if (ife) {
	ife->ife_flags = IFF_UP;
	ea_set(ife->ife_ea, (unsigned char *)&osnpf->osnpf_ea);
	ife->ife_gotea = TRUE;
    }

    pfdata->pf_fd = fd;
    pfdata->pf_handle = 0;
    pfdata->pf_ip4_only = FALSE;
    pfdata->pf_can_filter = FALSE;
}

#endif /* KLH10_NET_TAP_BRIDGE */

#if KLH10_NET_TAP_BRIDGE || KLH10_NET_TUN

/*
 * Like the standard read(2) call:
 * Receives a single packet and returns its size.
 * Include link-layer headers, but no BPF headers or anything like that.
 */
inline
ssize_t
osn_pfread(struct pfdata *pfdata, void *buf, size_t nbytes)
{
    return read(pfdata->pf_fd, buf, nbytes);
}

/*
 * Like the standard write(2) call:
 * Expect a full ethernet frame including link-layer header.
 * returns the number of bytes written.
 */
inline
int
osn_pfwrite(struct pfdata *pfdata, const void *buf, size_t nbytes)
{
    return write(pfdata->pf_fd, buf, nbytes);
}

#endif /* KLH10_NET_TAP_BRIDGE || KLH10_NET_TUN */

#if KLH10_NET_TAP_BRIDGE

#include <net/if_tap.h>
#include <net/if_bridgevar.h>
#include <stdint.h>

static struct ifreq br_ifr;
static struct ifreq tap_ifr;
static int my_tap;

/*
 * A TAP is a virtual ethernet interface, much like TUN is a virtual IP
 * interface. We can use it to inject packets into the Unix input stream,
 * provided it is UP and the host side has a matching IP address and
 * netmask (also much like TUN), or that it is bridged to another interface.
 *
 * Here we try to create the user-given interface and then bridge it to
 * the "default" interface. This is probably the most common configuration.
 * If something else is desired, the user can set up the tap herself,
 * and we'll just use it as it is. This is useful for a routed approach,
 * for instance.
 */
int
tap_bridge_open(char *ifnam)
{
    int tapfd;
    int res;
    union ipaddr netmask;
    char cmdbuff[128];
    struct ifent *ife;
    int s;
    int i;
    struct ifbreq br_req;
    struct ifdrv br_ifd;

    if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	esfatal(1, "tap_bridge_open: socket() failed");
    }

    /* try to create tapN as specified by the user */
    memset(&tap_ifr, 0, sizeof(tap_ifr));
    strcpy(tap_ifr.ifr_name, ifnam);
    res = ioctl(s, SIOCIFCREATE, &tap_ifr);
    if (res == 0) {
	my_tap = 1;
	dbprintln("Created host-side tap \"%s\"", ifnam);
    } else {
	if (errno != EEXIST)
	    esfatal(1, "tap_bridge_open: can't create tap \"%s\"?", ifnam);
	my_tap = 0;
	dbprintln("Host-side tap \"%s\" alread exists; use it as-is", ifnam);
    }

    sprintf(cmdbuff, "/dev/%s", ifnam);
    tapfd = open(cmdbuff, O_RDWR, 0);

    if (tapfd < 0) {
	/* Note possible error meanings:
	   ENOENT - no such filename
	   ENXIO  - not configured in kernel
	*/
	esfatal(1, "Couldn't find or open 10-side tap \"%s\"", cmdbuff);
    }

    dbprintln("Opened 10-side tap \"%s\"", cmdbuff);

    /* Finally, turn on IFF_UP just in case the above didn't do it.
       Note interface name is still there from the SIOCIFCREATE.
     */
    if (ioctl(s, SIOCGIFFLAGS, &tap_ifr) < 0) {
	esfatal(1, "tap_bridge_open tap SIOCGIFFLAGS failed");
    }
    if (!(tap_ifr.ifr_flags & IFF_UP)) {
	tap_ifr.ifr_flags |= IFF_UP;
	if (ioctl(s, SIOCSIFFLAGS, &tap_ifr) < 0) {
	    esfatal(1, "tap_bridge_open tap SIOCSIFFLAGS failed");
	}
	if (DP_DBGFLG)
	    dbprint("tap_bridge_open tap did SIOCSIFFLAGS");
    }

    if (my_tap) {
	for (i = 0; i < 1000; i++) {
	    /* try to create bridge%d */
	    memset(&br_ifr, 0, sizeof(br_ifr));
	    sprintf(br_ifr.ifr_name, "bridge%d", i);
	    res = ioctl(s, SIOCIFCREATE, &br_ifr);
	    if (res == 0)
		break;
	    if (errno != EEXIST)
		esfatal(1, "tap_bridge_open: can't create bridge \"%s\"?", br_ifr.ifr_name);
	}
	dbprintln("Created bridge \"%s\"", br_ifr.ifr_name);

	/*
	 * Find default IP interface to bridge with.
	 * It might find the wrong one if there is more than one.
	 */

	ife = osn_ipdefault();
	if (!ife)
	    esfatal(0, "Couldn't find default interface");

	if (swstatus)
	    dbprintln("Bridging with default interface \"%s\"", ife->ife_name);

	if (1) {
	    sprintf(cmdbuff, "/sbin/brconfig %s add %s add %s up",
		    br_ifr.ifr_name, ife->ife_name, ifnam);
	    res = system(cmdbuff);
	    dbprintln("%s => %d", cmdbuff, res);
	} else {
	    /* do whatever brconfig bridge0 add intf0 does... */
	    memset(&br_ifd, 0, sizeof(br_ifd));
	    memset(&br_req, 0, sizeof(br_req));

	    /* set name of the bridge */
	    strcpy(br_ifd.ifd_name, br_ifr.ifr_name);
	    br_ifd.ifd_cmd = BRDGADD;
	    br_ifd.ifd_len = sizeof(br_req);
	    br_ifd.ifd_data = &br_req;

	    /* brconfig bridge0 add tap0 (the virtual interface) */
	    strcpy(br_req.ifbr_ifsname, ifnam);
	    res = ioctl(s, SIOCSDRVSPEC, &br_ifd);
	    if (res == -1)
		esfatal(1, "tap_bridge_open: can't add virtual intf to bridge?");

	    /* brconfig bridge0 add vr0 (the hardware interface) */
	    strcpy(br_req.ifbr_ifsname, ife->ife_name);
	    res = ioctl(s, SIOCSDRVSPEC, &br_ifd);
	    if (res == -1)
		esfatal(1, "tap_bridge_open: can't add real intf to bridge?");

	    /* Finally, turn on IFF_UP just in case the above didn't do it.
	     * Note interface name is still there.
	     */
	    if (ioctl(s, SIOCGIFFLAGS, &br_ifr) < 0) {
		esfatal(1, "tap_bridge_open bridge SIOCGIFFLAGS failed");
	    }
	    if (!(br_ifr.ifr_flags & IFF_UP)) {
		br_ifr.ifr_flags |= IFF_UP;
		if (ioctl(s, SIOCSIFFLAGS, &br_ifr) < 0) {
		    esfatal(1, "tap_bridge_open bridge SIOCSIFFLAGS failed");
		}
		if (DP_DBGFLG)
		    dbprint("tap_bridge_open bridge did SIOCSIFFLAGS");
	    }

	}
    }
    close(s);

    return tapfd;		/* Success! */
}

void
tap_bridge_close()
{
    if (my_tap) {
	int s, res;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	    esfatal(1, "tap_bridge_close: socket() failed");
	}

	/* Destroy bridge */
	res = ioctl(s, SIOCIFDESTROY, &br_ifr);
	res = ioctl(s, SIOCIFDESTROY, &tap_ifr);

	close(s);
    }
}

#endif /* KLH10_NET_TAP_BRIDGE */

#if KLH10_NET_DLPI

/* DLPI packetfilter initialization */

#define	MAXSIZE   (32 * 1024)	/* Whuffo? */
#define	MAXADDR	  1024
#define	MAXCTLBUF (sizeof (dl_unitdata_req_t) + sizeof (long) + MAXADDR)
#define	MAXDLBUF  8192		/* Maybe just MAXCTLBUF */

struct dlpictx {
	int  dc_fd;
	struct strbuf dc_ctl;
	char dc_ebuf[200];		/* Error output */
	long dc_buf[MAXDLBUF];
};

static int dl_sendreq(struct dlpictx *dc, char *ptr, int len, char *what);
static int strgetmsg(struct dlpictx *dc, char *caller, struct strbuf *datap,
		     int *flagsp);
static int dladdrreq(struct dlpictx *dc, int type);
static int dladdrack(struct dlpictx *dc, unsigned char *addr);
static int dlsetaddrreq(struct dlpictx *dc, unsigned char *addr);


/* Handy auxiliary to pick up EA address given the PF FD, in case
   it's needed again after osn_pfinit is done.
 */
static int
dleaget(int fd, unsigned char *eap)
{
    struct dlpictx dc;

    dc.dc_fd = fd;
    if ((dladdrreq(&dc, DL_CURR_PHYS_ADDR) < 0)
     || (dladdrack(&dc, (unsigned char *)eap) < 0)) {
	error(dc.dc_ebuf);
	ea_clr(eap);
	return FALSE;
    }
    return TRUE;
}


/* Derived variously from TCPDUMP and Sun's ugly DLTX sample program
 */

static int
dl_sendreq(struct dlpictx *dc, char *ptr, int len, char *what)
{
	struct	strbuf	ctl;
	int	flags = 0;

	ctl.maxlen = 0;
	ctl.len = len;
	ctl.buf = ptr;

	if (putmsg(dc->dc_fd, &ctl, (struct strbuf *) NULL, flags) < 0) {
		sprintf(dc->dc_ebuf, "%s putmsg failed: %s",
		    what, dp_strerror(errno));
		return (-1);
	}
	return (0);
}

#if OSN_USE_IPONLY
# define DL_MAXWAIT 0
#else
# define DL_MAXWAIT 15	/* secs to wait for ACK msg */
#endif

#if DL_MAXWAIT

#include <signal.h>

static void
sigalrm(s)
{
}
#endif

static int
strgetmsg(struct dlpictx *dc,
	  char *caller,
	  struct strbuf *datap,
	  int *flagsp)
{
    int	rc;
    int flags = 0;

    dc->dc_ctl.maxlen = MAXDLBUF;
    dc->dc_ctl.len = 0;
    dc->dc_ctl.buf = (char *)dc->dc_buf;
    if (flagsp)
	*flagsp = 0;
    else
        flagsp = &flags;

#if DL_MAXWAIT
    signal(SIGALRM, sigalrm);
    if (alarm(DL_MAXWAIT) < 0) {
	esfatal(1, "%s: alarm set", caller);
    }
#endif

    /* Get expected message */
    if ((rc = getmsg(dc->dc_fd, &dc->dc_ctl, datap, flagsp)) < 0) {
	sprintf(dc->dc_ebuf, "%s:  getmsg - %s",
		caller, dp_strerror(errno));
	return -1;
    }

#if DL_MAXWAIT
    if (alarm(0) < 0) {
	esfatal(1, "%s: alarm clear", caller);
    }
#endif

    /* Got it - now do paranoia check */
    if (rc & (MORECTL | MOREDATA)) {
	sprintf(dc->dc_ebuf, "%s: getmsg returned%s%s",
		caller,
		((rc & MORECTL)  ? " MORECTL"  : ""),
		((rc & MOREDATA) ? " MOREDATA" : ""));
	return -1;
    }

    /* Yet more paranoia - control info should exist */
    /* Take this out if all callers check length */
    if (dc->dc_ctl.len < sizeof (long)) {
	sprintf(dc->dc_ebuf, "%s: getmsg ctl.len too short: %d",
		dc->dc_ctl.len);
	return -1;
    }
    return 0;
}
#undef DL_MAXWAIT

/*
 * type arg: DL_FACT_PHYS_ADDR for "factory" address,
 *	     DL_CURR_PHYS_ADDR for actual current address.
 */
static int
dladdrreq(struct dlpictx *dc, int type)
{
    dl_phys_addr_req_t req;

    req.dl_primitive = DL_PHYS_ADDR_REQ;
    req.dl_addr_type = type;		/* DL_{FACT,CURR}_PHYS_ADDR */
    return dl_sendreq(dc, (char *)&req, sizeof(req), "dladdr");
}

static int
dladdrack(struct dlpictx *dc, unsigned char *addr)
{
    union DL_primitives	*dlp;
    unsigned char *ucp;
    int len;

    if (strgetmsg(dc, "dladdrack", NULL, NULL) < 0)
	return -1;

    dlp = (union DL_primitives *) dc->dc_ctl.buf;

    if (dlp->dl_primitive != DL_PHYS_ADDR_ACK) {
	sprintf(dc->dc_ebuf, "dladdrack unexpected response %0lX",
			    (long)dlp->dl_primitive);
	return -1;
    }
    if (dc->dc_ctl.len < sizeof (dl_phys_addr_ack_t)) {
	sprintf(dc->dc_ebuf, "dladdrack: short response: %d",
		dc->dc_ctl.len);
	return -1;
    }

    /* Hardwired paranoia check, assume ethernet for now */
    len = dlp->physaddr_ack.dl_addr_length;
    if (len != ETHER_ADRSIZ) {
	sprintf(dc->dc_ebuf, "dladdrack: non-Ether addr len %d", len);
	return (-1);
    }
    ucp = ((unsigned char *)dlp) + dlp->physaddr_ack.dl_addr_offset;
    memcpy(addr, ucp, (size_t)len);

    return 0;
}

static int
dlsetaddrreq(struct dlpictx *dc, unsigned char *addr)
{
    /* Request is bigger than struct def; overlay it in buffer */
    dl_set_phys_addr_req_t *req = (dl_set_phys_addr_req_t *)dc->dc_buf;

    req->dl_primitive = DL_SET_PHYS_ADDR_REQ;
    req->dl_addr_length = ETHER_ADRSIZ;
    req->dl_addr_offset = sizeof(dl_set_phys_addr_req_t);
    memcpy(&dc->dc_buf[req->dl_addr_offset], addr, ETHER_ADRSIZ);

    return dl_sendreq(dc, (char *)req, sizeof(*req) + ETHER_ADRSIZ,
			"dlsetaddr");
}

#endif /* KLH10_NET_DLPI */

/* Auxiliary OSDNET.C stuff stops here */

#if 0	/* Limit of included code so far */

/* Both need to dump packets out
 */
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

/* Merge DPNI20's arp_myreply() with DPIMP's arp_req() ??
   But one sends out a reply, the other a request...
 */

/* There was an ether_write() for NIT unused in dpni20.c
  whereas dpimp.c still uses it for everything.
*/

#endif /* if 0 - still-excluded code */
