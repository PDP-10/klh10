/* DPCHAOS.H - Definitions for CHAOS process
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

#ifndef DPCHAOS_INCLUDED
#define DPCHAOS_INCLUDED 1

#ifndef DPSUP_INCLUDED
# include "dpsup.h"
#endif
#ifndef OSDNET_INCLUDED
# include "osdnet.h"
#endif

/* Version of DPCHAOS-specific shared memory structure */
#define DPCHAOS_VERSION DPC_VERSION(2,0,0)	/* 2.0.0 */

#define IFNAM_LEN	PATH_MAX	/* at least IFNAMSIZ! */

#ifndef DPCHAOS_CHIP_MAX
# define DPCHAOS_CHIP_MAX 10
#endif

/* Chaos ARP list */
// @@@@ implement something to show the table
#define CHARP_MAX 16
#define CHARP_MAX_AGE (60*5)	// ARP cache limit
struct charp_ent {
  u_char charp_eaddr[ETHER_ADDR_LEN];
  u_short charp_chaddr;
  time_t charp_age;
};

/* If a dynamically added CHIP entry is older than this (seconds), it can get updated */
#ifndef DPCHAOS_CHIP_DYNAMIC_AGE_LIMIT
# define DPCHAOS_CHIP_DYNAMIC_AGE_LIMIT (60*5)
#endif

/* Chaos/IP mapping entry - see ch_chip */
struct dpchaos_chip {
    unsigned int dpchaos_chip_chaddr; /* Chaos address */
    struct in_addr dpchaos_chip_ipaddr; /* IP address */
  in_port_t dpchaos_chip_ipport;	/* IP port */
  time_t dpchaos_chip_lastrcvd;	/* When last received, if dynamically added */
};

/* DPCHAOS-specific stuff */
			/* C = controlling parent sets, D = Device proc sets */
			/*       If both, 1st letter indicates inital setter */
struct dpchaos_s {
    struct dpc_s dpchaos_dpc;	/* CD Standard DPC portion */
    int dpchaos_ver;		/* C  Version of shared struct */
    int dpchaos_attrs;		/* C  Attribute flags */
    char dpchaos_ifnam[IFNAM_LEN];	/* CD Interface name if any */
    char dpchaos_ifmeth[16];	/* C  Interface method */
    int dpchaos_ifmeth_chudp;	/* C  Interface method is CHUDP? */
    unsigned short dpchaos_myaddr;  /* C  my Chaos address  */
    unsigned char dpchaos_eth[6];	/* CD Ethernet address of interface */
  /* probably not used */
    int dpchaos_inoff;		/* C Offset in buffer of input (I->H) data */
    int dpchaos_outoff;		/* D Offset in buffer of output (H->I) data */
    int dpchaos_backlog;	/* C Max sys backlog of rcvd packets */
    int dpchaos_dedic;		/* C TRUE if dedicated ifc, else shared */
  in_port_t dpchaos_port;	/* C port for CHUDP protocol */
  /* Chaos/IP mapping */
  int dpchaos_chip_tlen;	/* C table length */
  struct dpchaos_chip dpchaos_chip_tbl[DPCHAOS_CHIP_MAX];
  // ARP table
  struct charp_ent charp_list[CHARP_MAX];  /* D arp table */
  int charp_len;		/* D arp table length */
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
#define DPCHAOS_CHUDP_DATAOFFSET (sizeof(struct chudp_header)) // 4 bytes
#define DPCHAOS_ETHER_DATAOFFSET (sizeof(struct ether_header)) // 6+6+2=16 bytes
// room for protocol header + Chaos header + max Chaos data + Chaos hw trailer
#define DPCHAOS_MAXLEN (DPCHAOS_ETHER_DATAOFFSET+CHAOS_HEADERSIZE+CHAOS_MAXDATA+CHAOS_HW_TRAILERSIZE+42) /* some slack */

#define DPCHAOS_CH_DESTOFF 4	/* offset to dest addr in chaos pkt */
#define DPCHAOS_CH_FC 2		/* offset to forwarding count */

/* Commands to and from DP and KLH10 CH11 driver */

	/* From 10 to DP */
#define DPCHAOS_RESET	0	/* Reset DP */
#define DPCHAOS_SPKT	1	/* Send data packet to ethernet */

	/* From DP to 10 */
#define DPCHAOS_INIT	1	/* DP->10 Finished init */
#define DPCHAOS_RPKT	2	/* DP->10 Received data packet from net */

#endif	/* ifndef DPCHUDP_INCLUDED */
