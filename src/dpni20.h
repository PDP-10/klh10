/* DPNI20.H - Device sub-Process defs for NI20
*/
/* $Id: dpni20.h,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: dpni20.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#ifndef DPNI20_INCLUDED
#define DPNI20_INCLUDED 1

#ifdef RCSID
 RCSID(dpni20_h,"$Id: dpni20.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

#ifndef DPSUP_INCLUDED
# include "dpsup.h"
#endif

#define DPNI_MCAT_SIZ 16	/* Size of multicast table */
#define DPNI_PTT_SIZ  16	/* Size of protocol type table */

/* Version of DPNI20-specific shared memory structure */

#define DPNI20_VERSION DPC_VERSION(1,1,1)	/* 1.1.1 */


/* DPNI20-specific stuff */
			/* C = controlling parent sets, D = Device proc sets */
struct dpni20_s {
    struct dpc_s dpni_dpc;	/* CD Standard DPC portion */
    int dpni_ver;		/* C  Version of shared struct */
    int dpni_attrs;		/* C  Attribute flags */
# define DPNI20F_LSAP	0x0100	/*	Set if LSAP value specified */
    int dpni_lsap;		/* C  Dest/Source LSAP value if needed */
    char dpni_ifnam[16];	/* CD Interface name if any */
    unsigned char dpni_eth[6];	/* CD Ethernet address of interface */
    unsigned char dpni_ip[4];	/* C 10's IP address to filter on, if shared */
    int dpni_backlog;		/* C Max sys backlog of rcvd packets */
    int dpni_dedic;		/* C TRUE if dedicated ifc, else shared */
    int dpni_decnet;		/* C TRUE to seize DECNET packets, if shared */
    int dpni_doarp;		/* C TRUE to do ARP hackery, if shared */
    int dpni_rdtmo;		/* C # secs to timeout on packetfilter read */
    unsigned char dpni_rqeth[6];		/* C Requested ethernet addr */
    int dpni_nmcats;				/* C # of MCAT entries */
    unsigned char dpni_mcat[DPNI_MCAT_SIZ][6];	/* C Requested MCAT */
    int dpni_nptts;				/* C # of PTT entries */
    unsigned char dpni_ptt[DPNI_PTT_SIZ][6];	/* C Requested PTT */
};

/* Commands to and from DP and KLH10 NI20 driver */

	/* From 10 to DP */
#define DPNI_RESET	0	/* Reset DP */
#define DPNI_SPKT	1	/* Send data packet to ethernet */
#define DPNI_SETETH	2	/* Set hardware ethernet address */
#define DPNI_SETMCAT	3	/* Set hardware multicasts from MCAT table */
#define DPNI_SETPTT	4	/* Set packetfilter using PTT */

	/* From DP to 10 */
#define DPNI_INIT	1	/* DP->10 Finished init */
#define DPNI_RPKT	2	/* DP->10 Received data packet from net */
#define DPNI_NEWETH	3	/* DP->10 Ethernet Address changed */


	/* Feature bits in dpni_doarp */
/*			0x1 */	/* TRUE -- translate into 0xF */
#define DPNI_ARPF_PUBL	0x2	/* Register self, publish */
#define DPNI_ARPF_PERM	0x4	/* Register self, permanent */
#define DPNI_ARPF_OCHK	0x8	/* Check outgoing ARPs for host platform */

#endif /* ifndef DPNI20_INCLUDED */
