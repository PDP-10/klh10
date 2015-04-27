/* DPRPXX.H - Device sub-Process defs for RPxx
*/
/* $Id: dprpxx.h,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: dprpxx.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#ifndef DPRPXX_INCLUDED
#define DPRPXX_INCLUDED 1

#ifdef RCSID
 RCSID(dprpxx_h,"$Id: dprpxx.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

#ifndef DPSUP_INCLUDED
# include "dpsup.h"
#endif

#ifndef DPRP_NSECS_MAX		/* Max # sectors for single I/O operation */
# define DPRP_NSECS_MAX 4	/* 4*128 = 512 wds */
#endif

/* DPRPXX-specific stuff */

struct dprpxx_s {
    struct dpc_s dprp_dpc;	/* Standard DPC portion */
    int dprp_dma;		/* TRUE if want to use DMA */
    int dprp_shmid;		/* SHM ID for 10-memory, if DMA allowed */
    int dprp_debug;		/* TRUE if want subproc debug output */

    int dprp_res;		/* Operation result */
    int dprp_err;		/* Non-zero if error */
    unsigned long dprp_scnt;	/* # sectors xferred */
    unsigned long dprp_daddr;	/* Disk address as # sectors */
    uint32 dprp_phyadr;		/* Memory word address for DMA */

    /* Unused, maybe later */
    int dprp_cyl,	/* Desired cyl */
	dprp_trk,	/* and track */
	dprp_sec;	/* and sector */

    /* Disk format - set by 10, read by DP */
    int dprp_fmt;
    unsigned long dprp_totsec;
    int dprp_ncyl;
    int dprp_ntrk;
    int dprp_nsec;
    int dprp_nwds;
    char dprp_devname[16];

    /* Disk status - set by DP.  Not really used. */
    int dprp_mol;
    int dprp_wrl;
};

#define DPRP_RES_FAIL 0
#define DPRP_RES_SUCC 1


/* Commands to and from DP and KLH10 RPXX driver */

enum {
	DPRP_RESET=0,	/* 0 - Reset DP */
	DPRP_MOUNT,	/* Mount R/W disk specified by string of N bytes */
	DPRP_ROMNT,	/* Mount RO  disk specified by string of N bytes */

	DPRP_NOP,	/* No operation */
	DPRP_UNL,	/* Unload (go offline) */
	DPRP_SEEK,	/* Seek (basically a nop) */

	DPRP_READ,	/* Read N words */
	DPRP_WRITE,	/* Write N words */

	DPRP_RDSEC,	/* Read N sectors */
	DPRP_WRSEC,	/* Write N sectors */

	DPRP_RDDMA,	/* Read N sectors directly into memory */
	DPRP_WRDMA,	/* Write N sectors directly from memory */

	DPRP_RDDCH,	/* Read N words directly to data channel */
	DPRP_WRDCH,	/* Write N words directly from data channel */

	DPRP_SNS	/* Sense? */
};


#endif /* ifndef DPRPXX_INCLUDED */
