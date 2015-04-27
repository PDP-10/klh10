/* DPTM03.H - Device sub-Process defs for TM03
*/
/* $Id: dptm03.h,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: dptm03.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#ifndef DPTM03_INCLUDED
#define DPTM03_INCLUDED 1

#ifdef RCSID
 RCSID(dptm03_h,"$Id: dptm03.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

#ifndef DPSUP_INCLUDED
# include "dpsup.h"
#endif

#ifndef DPTM_MAXRECSIZ
# define DPTM_MAXRECSIZ (1L<<16)	/* 16 bits worth of record length */
#endif

/* Version of DPTM03-specific shared memory structure */

#define DPTM03_VERSION DPC_VERSION(1,2,0)	/* V1.2.0 */


/* DPTM03-specific stuff */
			/* C = controlling parent sets, D = Device proc sets */
struct dptm03_s {
    struct dpc_s dptm_dpc;	/* CD Standard DPC portion */
    int dptm_ver;		/* C  Version of shared struct */
    int dptm_blkopen;	/* C  Max times to try blocking open before recheck */
    int dptm_type;	/*  D Tape type (MTYP_xxx) */

    /* Tape mount args */
    int dptm_pathx;	/* C  Index into buffer for pathname */
    int dptm_argsx;	/* C  Index into buffer for general arg string */

    /* Tape status */
    int dptm_col;	/* CD TRUE if drive came online (cleared by any cmd) */
    int dptm_mol;	/* CD TRUE if medium online (tape mounted) */
    int dptm_pip;	/*  D TRUE if operation in progress */
    int dptm_wrl;	/*  D TRUE if tape write-locked */
    int	dptm_bot;	/*  D TRUE if BOT seen */
    int dptm_eot;	/*  D TRUE if EOT seen */
    int dptm_eof;	/*  D TRUE if EOF (tapemark) seen */
    int dptm_err;	/* CD Non-zero if error */
    int dptm_res;	/*  D Operation result (currently unused) */
    unsigned int
	dptm_frms;	/*  D # frames (bytes) read in record */

    /* Statistical info */
    int dptm_recs,	/*  D # recs in tape so far */
	dptm_frecs,	/*  D # recs in current file so far */
	dptm_files,	/*  D # files (tapemarks) seen so far */
	dptm_herrs,	/*  D Hard errors (unrecoverable) */
	dptm_serrs;	/*  D Soft errors (includes retries) */


    /* Record buffer of size DPTM_MAXRECSIZ is allocated after this struct
    ** by dp_init().  The following depends on this and reserves space
    ** to prefix the buffer with an aligned quantity of zero bytes, so that
    ** read-reverse will safely pad out the last word with zeros.
    ** See tm_flsbuf() code in dvtm03.
    */
    char dptm_revpad[sizeof(double)];
};

#define DPTM_RES_FAIL 0
#define DPTM_RES_SUCC 1

/* Tape device type byte specs
	* - ANY
		If file doesn't exist, is assumed to be RW virtual and created.
		If exists, is checked for type.
			Normal file - RW if access allows, else RO.
			Special device - hard-mount device
	R - Virtual RO
	W - Virtual RW
	4 - 4mm
	8 - 8mm
*/


/* Values for d_istape */
enum dptmmtype {
	MTYP_NONE=0,	/* Non-mounted */
	MTYP_NULL,	/*	Null device - special hack */
	MTYP_VIRT,	/* Soft-mount: Virtual magtape */
	MTYP_HALF,	/* Hard-mount: Half-inch reel magtape */
	MTYP_QIC,	/*	Quarter-inch cartridge streaming tape */
	MTYP_8MM,	/*	8mm cartridge magtape */
	MTYP_4MM	/*	4mm DDS/DAT cartridge magtape */
};

/* Commands to and from DP and KLH10 TM03 driver */

enum {
	DPTM_RESET=0,	/* 0 - Reset DP */
	DPTM_MOUNT,	/* Mount R/W tape specified by string of N bytes */
	DPTM_ROMNT,	/* Mount RO  tape specified by string of N bytes */

	DPTM_NOP,	/* No operation */
	DPTM_UNL,	/* Unload (implies rewind & unmount) */
	DPTM_REW,	/* Rewind tape */
	DPTM_WTM,	/* Write Tape Mark (EOF) */
	DPTM_SPF,	/* Space N records forward */
	DPTM_SPR,	/* Space N records reverse */
	DPTM_SFF,	/* Space N files forward */
	DPTM_SFR,	/* Space N files reverse */
	DPTM_ER3,	/* Erase 3 inches */
	DPTM_WRT,	/* Write forward N bytes */
	DPTM_RDF,	/* Read forward up to N bytes */
	DPTM_RDR,	/* Read reverse up to N bytes */
	DPTM_SNS	/* Sense? */
};


#endif /* ifndef DPTM03_INCLUDED */
