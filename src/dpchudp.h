/* DPCHUDP.H - Definitions for CHUDP process
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

#ifndef DPCHUDP_INCLUDED
#define DPCHUDP_INCLUDED 1

#ifndef DPSUP_INCLUDED
# include "dpsup.h"
#endif
#ifndef OSDNET_INCLUDED
# include "osdnet.h"
#endif

/* Version of DPCHUDP-specific shared memory structure */
#define DPCHUDP_VERSION DPC_VERSION(2,0,0)	/* 2.0.0 */

#define IFNAM_LEN	PATH_MAX	/* at least IFNAMSIZ! */

#ifndef DPCHUDP_CHIP_MAX
# define DPCHUDP_CHIP_MAX 10
#endif

// @@@@ consider ARP age limit
/* If a dynamically added CHIP entry is older than this (seconds), it can get updated */
#ifndef DPCHUDP_CHIP_DYNAMIC_AGE_LIMIT
# define DPCHUDP_CHIP_DYNAMIC_AGE_LIMIT (60*5)
#endif

/* Chaos/IP mapping entry - see ch_chip */
struct dpchudp_chip {
    unsigned int dpchudp_chip_chaddr; /* Chaos address */
    struct in_addr dpchudp_chip_ipaddr; /* IP address */
  in_port_t dpchudp_chip_ipport;	/* IP port */
  time_t dpchudp_chip_lastrcvd;	/* When last received, if dynamically added */
};

/* DPCHUDP-specific stuff */
			/* C = controlling parent sets, D = Device proc sets */
			/*       If both, 1st letter indicates inital setter */
struct dpchudp_s {
    struct dpc_s dpchudp_dpc;	/* CD Standard DPC portion */
    int dpchudp_ver;		/* C  Version of shared struct */
    int dpchudp_attrs;		/* C  Attribute flags */
    char dpchudp_ifnam[IFNAM_LEN];	/* CD Interface name if any */
    char dpchudp_ifmeth[16];	/* C  Interface method */
    int dpchudp_ifmeth_chudp;	/* C  Interface method is CHUDP? */
    unsigned short dpchudp_myaddr;  /* C  my Chaos address  */
    unsigned char dpchudp_eth[6];	/* CD Ethernet address of interface */
  /* probably not used */
    int dpchudp_inoff;		/* C Offset in buffer of input (I->H) data */
    int dpchudp_outoff;		/* D Offset in buffer of output (H->I) data */
    int dpchudp_backlog;	/* C Max sys backlog of rcvd packets */
    int dpchudp_dedic;		/* C TRUE if dedicated ifc, else shared */
  in_port_t dpchudp_port;	/* C port for CHUDP protocol */
  /* Chaos/IP mapping */
  int dpchudp_chip_tlen;	/* C table length */
  struct dpchudp_chip dpchudp_chip_tbl[DPCHUDP_CHIP_MAX];
};


/* Buffer offset:
   CHUDP protocol header is 4 bytes; read/write data after those
*/
struct chudp_header {
  char chudp_version;
  char chudp_function;
  char chudp_arg1;
  char chudp_arg2;
};

/* CHUDP protocol port - should perhaps be registered? */
#define CHUDP_PORT 42042
/* Protocol version */
#define CHUDP_VERSION 1
/* Protocol function codes */
#define CHUDP_PKT 1		/* Chaosnet packet */

#ifndef ETHERTYPE_CHAOS
# define ETHERTYPE_CHAOS 0x0804
#endif
// old names for new, new names for old?
#ifndef ARPOP_RREQUEST
#define ARPOP_RREQUEST ARPOP_REVREQUEST // 3	/* request protocol address given hardware */
#endif
#ifndef ARPOP_RREPLY
#define ARPOP_RREPLY ARPOP_REVREPLY // 4	/* response giving protocol address */
#endif

#include "dvch11.h"
#define DPCHUDP_DATAOFFSET (sizeof(struct chudp_header))
#define DPCHUDP_MAXLEN (CHAOS_MAXDATA+DPCHUDP_DATAOFFSET+42) /* some slack */

#ifndef DPCHUDP_DO_ROUTING
# define DPCHUDP_DO_ROUTING 1
#endif
#define DEFAULT_CHAOS_ROUTER 03040 /* default router (MX-11.LCS.MIT.EDU) */
#define DPCHUDP_CH_DESTOFF 4	/* offset to dest addr in chaos pkt */
#define DPCHUDP_CH_FC 2		/* offset to forwarding count */

/* Commands to and from DP and KLH10 CH11 driver */

	/* From 10 to DP */
#define DPCHUDP_RESET	0	/* Reset DP */
#define DPCHUDP_SPKT	1	/* Send data packet to ethernet */

	/* From DP to 10 */
#define DPCHUDP_INIT	1	/* DP->10 Finished init */
#define DPCHUDP_RPKT	2	/* DP->10 Received data packet from net */

#endif	/* ifndef DPCHUDP_INCLUDED */
