/* DVRPXX.C - Emulates RP/RM disk drives under RH20 for KL10
*/
/* $Id: dvrpxx.c,v 2.4 2002/05/21 09:52:07 klh Exp $
*/
/*  Copyright © 1993, 2001 Kenneth L. Harrenstien
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
 * $Log: dvrpxx.c,v $
 * Revision 2.4  2002/05/21 09:52:07  klh
 * Change protos for vdk_read, vdk_write
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include "klh10.h"

#if !KLH10_DEV_RPXX && CENV_SYS_DECOSF
	/* Stupid gubbish needed to prevent OSF/1 AXP compiler from
	** halting merely because compiled file is empty!
	*/
static int decosfcclossage;
#endif

#if KLH10_DEV_RPXX		/* Moby conditional for entire file */

#include <stddef.h>	/* For size_t etc */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>	/* For malloc */

#include "kn10def.h"	/* This includes OSD defs */
#include "kn10dev.h"
#include "dvuba.h"
#include "dvrh20.h"
#include "dvrpxx.h"
#include "prmstr.h"	/* For parameter parsing */

#if KLH10_DEV_DPRPXX
# include "dpsup.h"	/* Using device subproc! */
# include "dprpxx.h"	/* Define stuff shared with subproc */
#endif
#if 1 /* !KLH10_DEV_DPRPXX */	/* For now, include these too */
# include "wfio.h"	/* For word-based file i/o */
# include "vdisk.h"	/* Virtual Disk facilities */
#endif

#ifdef RCSID
 RCSID(dvrpxx_c,"$Id: dvrpxx.c,v 2.4 2002/05/21 09:52:07 klh Exp $")
#endif

#ifndef DVRP_NSUP		/* Max # of drives can create */
# define DVRP_NSUP (8*8)
#endif

#ifndef DVRP_MAXPATH		/* Length of pathname for diskfile */
# define DVRP_MAXPATH 63
#endif

#if 0
#define DVDEBUG(d) ((d)->rp_dv.dv_debug)
#define DVDBF(d)   ((d)->rp_dv.dv_dbf)
#endif


int rp_format = VDK_FMT_RAW;	/* VDK_DFxxx value */
			/* External needed until becomes config param */

#if KLH10_DEV_DPRPXX	/* Max I/O operation in bytes */
# define DPRP_MAXRECSIZ (sizeof(w10_t)*128*DPRP_NSECS_MAX)
#endif

/* Disk type configuration params.
**	All of these numbers assume drives using 18-bit formatting.
**	(16-bit formatting has more sectors per track)
*/
static struct diskconf {
	char dcf_name[8];	/* Drive type name (RP06, etc) */
	int dcf_type;		/* Type bits for type register */
	int dcf_nwds;		/* Words/Sector */
	int dcf_nsec;		/* Sectors/Track */
	int dcf_ntrk;		/* Tracks/Cylinder */
	int dcf_ncyl;		/* Cylinders/Drive (includes maint cyls) */
} diskconfs[] = {
		/*	wrd  sec trk cyl       Cyl+maint totsec totmaintsec */
    { "RP02", 0,	128, 10, 20, 203 },	/* 200+3  40000	 40,600 */
    { "RP03", 0,	128, 10, 20, 403 },	/* 400+3  80000	 80,600 */

    { "RP04", RH_DTRP04, 128, 20, 19, 411 },	/* 406+5 154280 156,180 */
    { "RP05", RH_DTRP05, 128, 20, 19, 411 },	/*   "      "	  "     */
    { "RP06", RH_DTRP06, 128, 20, 19, 815 },	/* 810+5 307800 309,700 */
    { "RP07", RH_DTRP07, 128, 43, 32, 630 },	/* 629+1 865504 866,880 */
    { "RM03", RH_DTRM03, 128, 30,  5, 823 },	/* 821+2 123150 123,450 */
    { "RM02", RH_DTRM02, 128, 30,  5, 823 },	/*    "     "      "    */
    { "RM05", RH_DTRM05, 128, 30, 19, 823 },	/* 821+2 467970	469,110 */
    { "RM80", RH_DTRM80, 128, 30, 14, 561 },	/* 559+2 234780 235,620 */

    { "" }
};
/* RP07 doc p.6-23 says addressing params are:
**	Mode		Cyl Head/Track	Sector(18) Sector(16)
**	Functional:	630	32	43	    50
**	Diagnostic:	632	32	43	    50
*/

/* In ITS:
	RP06 was 812+3
	RP07 was 627+3
	RM03 was 821+2
	RM05 was 820
	RM80 was 556+3
*/


#ifndef DVRP_DEFAULT_DISK
# define DVRP_DEFAULT_DISK "RP06"
#endif


struct rpdev {
    struct device rp_dv;

	/* Drive-specific stuff */
    int rp_bit;			/* Attention bit */
    int rp_scmd;		/* Pending command if any */
# define NRH20DRVREGS 040		/* 0-037 */
    dvureg_t rp_reg[NRH20DRVREGS];

	/* Drive config */
    struct diskconf rp_dcf;	/* Disk configuration, incl dev type */
    long rp_totsecs;		/* Total # sectors on disk */
    int rp_isdyn;		/* TRUE if dynamically sized */
    int rp_fmt;			/* Data format on real disk, VDK_FMT_xxx */
    int rp_iswrite;		/* TRUE if writable, else RO */

    /* I/O transfer vars, updated to track progress */
    int rp_blkcnt;		/* # sectors in total transfer */
    int rp_xfrcnt;		/* # subtransfers so far */
    long rp_blkwds;		/* # words left to transfer */
    long rp_blklim;		/* # words actually allowed to xfer */
    long rp_cyl;		/* Current seek pos - must hold > 16 bits */
    int  rp_trk, rp_sec;	/* Current position in cyl - 8 bits each */
    long rp_blkadr;		/* Disk loc as a sector number */
    int rp_isdirect;		/* TRUE if current xfer is direct */
    vmptr_t rp_xfrvp;		/* If direct, holds ptr to 10-mem */

    int rp_bufwds;		/* Size of buffer in words */
    int rp_bufsec;		/* Size of buffer in sectors */
    unsigned char *rp_buff;	/* Ptr to buffer (may be in shared mem) */

    clkval_t rp_iodly;		/* Optional usec to delay I/O ops */
    clktmr_t rp_iotmr;		/* Timer for above */

#if KLH10_DEV_DPRPXX
    char *rp_dpname;		/* Pathname of executable subproc */
    int rp_dpdma;		/* TRUE to use DP DMA if possible */
    struct dp_s rp_dp;		/* Handle on dev subprocess */
    struct dprpxx_s *rp_sdprp;	/* Ptr to shared memory segment */

    int rp_state;
# define RPXX_ST_OFF	0	/* Turned off - no subproc */
# define RPXX_ST_READY	1	/* On and ready for command */
# define RPXX_ST_BUSY	2	/* Executing some command */
    int rp_dpdbg;		/* Initial DP debug value */

#else
    long rp_rescnt;		/* I/O result sector count */
    long rp_reserr;		/* I/O result error (if nonzero) */
    struct vdk_unit rp_vdk;	/* Virtual Disk unit */
#endif
    char rp_spath[DVRP_MAXPATH+1];
};

#define RPREG(d,r) ((d)->rp_reg[r])

static int nrps = 0;		/* # of RPs defined */
struct rpdev			/* External for easier debug */
	*dvrpxx[DVRP_NSUP];	/* Table of pointers, for easier debug */

static struct rpdev dvrp;	/* Make #0 be static, for easier debug */

/* Functions provided to device vector */

static int  rpxx_init(struct device *d, FILE *of);
static void rpxx_reset(struct device *d);
static uint32 rpxx_rdreg(struct device *d, int reg);
static int  rpxx_wrreg(struct device *d, int reg, dvureg_t val);
static void rpxx_powoff(struct device *d);
static int  rpxx_mount(struct device *d, FILE *f, char *path, char *argstr);

/* Other exported vectors */

#if KLH10_DEV_DPRPXX
static void rpxx_evhsdon(struct device *d, struct dvevent_s *evp);
static void rpxx_evhrwak(struct device *d, struct dvevent_s *evp);
#endif
static int  rpxx_timeout(void *);

/* Completely internal functions */

static int  rp_conf(FILE *f, char *s, struct rpdev *rp);
static int  rp_xmount(struct rpdev *rp);
static void rp_attn(struct rpdev *);
static void rp_clear(struct rpdev *rp);
static void rp_cmdxct(struct rpdev *, unsigned int);
static void rp_delayop(struct rpdev *, int);

static int rp_ioxfr(struct rpdev *, int);
static void rp_ioend(struct rpdev *);
static int rp_updxfr(struct rpdev *);
static int rp_wrfilbuf(struct rpdev *);
static int rp_rdflsbuf(struct rpdev *);
static void rp_showbuf(struct rpdev *, unsigned char *, vmptr_t, int, int);

#if KLH10_DEV_DPRPXX
static void rp_dpcmd(struct rpdev *rp, int cmd, size_t arg);
static int  rp_dpstart(struct rpdev *rp);
static void rp_dpcmddon(struct rpdev *);
#endif

/* Configuration Parameters */

#define DVRPXX_PARAMS \
    prmdef(RPP_DBG,  "debug"),	/* Initial debug value */\
    prmdef(RPP_TYP,  "type"),	/* Drive type (eg RP06) */\
    prmdef(RPP_CYL,  "cyl"),	/* Drive size: # cylinders/drive */\
    prmdef(RPP_TRK,  "trk"),	/* Drive size: # tracks/cyl */\
    prmdef(RPP_SEC,  "sec"),	/* Drive size: # sectors/trk */\
    prmdef(RPP_SN,   "sn"),	/* Drive Serial number */\
    prmdef(RPP_PATH, "path"),	/* Pack pathname - OS file or raw device */\
    prmdef(RPP_FMT,  "format"),	/* Pack format */\
    prmdef(RPP_RO,   "ro"),	/* Pack is Read-Only */\
    prmdef(RPP_RW,   "rw"),	/* Pack is Read/Write (default) */\
    prmdef(RPP_BUF,  "bufsiz"),	/* Buffer size in words */\
    prmdef(RPP_IODLY,"iodly"),	/* Usec to delay I/O operations */\
    prmdef(RPP_DPDBG,"dpdebug"), /* Initial DP debug value */\
    prmdef(RPP_DMA,  "dpdma"),	/* True to use subproc DMA if possible */\
    prmdef(RPP_DP,   "dppath")	/* Device subproc pathname */

enum {
# define prmdef(i,s) i
	DVRPXX_PARAMS
# undef prmdef
};

static char *rpprmtab[] = {
# define prmdef(i,s) s
	DVRPXX_PARAMS
# undef prmdef
	, NULL
};


static int partyp(struct rpdev *, char *);	/* Local parsing routines */
static int parfmt(char *cp, int *afmt);

/* RP_CONF - Parse configuration string and set defaults.
**	At this point, device has just been created, but not yet bound
**	or initialized.
** NOTE that some strings are dynamically allocated!  Someday may want
** to clean them up nicely if config fails or device is uncreated.
*/
static int
rp_conf(FILE *f, char *s, struct rpdev *rp)
{
    int i, ret = TRUE;
    struct prmstate_s prm;
    char buff[200];
    long lval;

    /* First set defaults for all configurable parameters */
    DVDEBUG(rp) = FALSE;
    rp->rp_fmt = rp_format;		/* For now, use external default */
    rp->rp_iswrite = TRUE;
    partyp(rp, DVRP_DEFAULT_DISK);	/* Default disk config */
    RPREG(rp, RHR_SN) =			/* Serial Number register (BCD) */
		  (((1    / 1000)%10) << 12)
		| (((6    /  100)%10) <<  8)
		| (((nrps /   10)%10) <<  4)
		| (((nrps       )%10)      );
    rp->rp_bufsec = 4;			/* # sectors in buffer */
    rp->rp_bufwds = rp->rp_bufsec	/* # words in buffer */
		* rp->rp_dcf.dcf_nwds;
    rp->rp_iodly =			/* I/O delay */
#if KLH10_CPU_KS
		CLK_USECS_PER_MSEC;	/* on KS, default I/O delay to 1ms */
#else
		0;			/* else none */
#endif
    rp->rp_iotmr = NULL;
    rp->rp_spath[0] = '\0';		/* No path (default it later) */
#if KLH10_DEV_DPRPXX
    rp->rp_dpdma = TRUE;		/* Default is DO use DMA if possible */
    rp->rp_dpname = "dprpxx";		/* Subproc executable */
    rp->rp_dpdbg = FALSE;
#endif

    prm_init(&prm, buff, sizeof(buff),
		s, strlen(s),
		rpprmtab, sizeof(rpprmtab[0]));
    while ((i = prm_next(&prm)) != PRMK_DONE) {
	switch (i) {
	case PRMK_NONE:
	    fprintf(f, "Unknown RPXX parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;
	case PRMK_AMBI:
	    fprintf(f, "Ambiguous RPXX parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;
	default:	/* Handle matches not supported */
	    fprintf(f, "Unsupported RPXX parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;

	case RPP_DBG:		/* Parse as true/false boolean or number */
	    if (!prm.prm_val)	/* No arg => default to 1 */
		DVDEBUG(rp) = 1;
	    else if (!s_tobool(prm.prm_val, &DVDEBUG(rp)))
		break;
	    continue;

	case RPP_TYP:		/* Parse as disk type string */
	    if (!prm.prm_val)
		break;
	    if (!partyp(rp, prm.prm_val))
		break;
	    continue;

	case RPP_CYL:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    if (lval <= 0 || lval > MASK16) {
		fprintf(f, "RPXX CYL must be 0 < x < %ld\n", (long)MASK16);
		ret = FALSE;
	    }
	    rp->rp_dcf.dcf_ncyl = lval;
	    rp->rp_isdyn = TRUE;
	    continue;

	case RPP_TRK:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    if (lval <= 0 || lval > 0377) {
		fprintf(f, "RPXX TRK must be 0 < x < %d\n", 0377);
		ret = FALSE;
	    }
	    rp->rp_dcf.dcf_ntrk = lval;
	    rp->rp_isdyn = TRUE;
	    continue;

	case RPP_SEC:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    if (lval <= 0 || lval > 0377) {
		fprintf(f, "RPXX SEC must be 0 < x < %d\n", 0377);
		ret = FALSE;
	    }
	    rp->rp_dcf.dcf_nsec = lval;
	    rp->rp_isdyn = TRUE;
	    continue;

	case RPP_SN:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    if (lval < 0) {
		fprintf(f, "RP SN must be >= 0\n");
		ret = FALSE;
	    } else
		/* Turn last 4 digits into BCD */
		RPREG(rp, RHR_SN) =
			  (((lval / 1000)%10) << 12)
			| (((lval /  100)%10) <<  8)
			| (((lval /   10)%10) <<  4)
			| (((lval       )%10)      );
	    continue;

	case RPP_FMT:		/* Parse as disk format string */
	    if (!prm.prm_val)
		break;
	    if (!parfmt(prm.prm_val, &rp->rp_fmt))
		break;
	    continue;

	case RPP_RO:
	    rp->rp_iswrite = FALSE;
	    continue;

	case RPP_RW:
	    rp->rp_iswrite = TRUE;
	    continue;

	case RPP_BUF:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    if (lval <= 0 || (lval % rp->rp_dcf.dcf_nwds)) {
		fprintf(f, "RPXX bufsiz must be multiple of %d\n",
						rp->rp_dcf.dcf_nwds);
		ret = FALSE;
	    } else {
		rp->rp_bufsec = lval / rp->rp_dcf.dcf_nwds;
		rp->rp_bufwds = rp->rp_bufsec * rp->rp_dcf.dcf_nwds;
	    }
	    continue;

	case RPP_IODLY:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    if ((lval < 0) || (lval > CLK_USECS_PER_SEC)) {
		fprintf(f, "RPXX I/O delay invalid: %ld\n", lval);
		ret = FALSE;
	    } else {
		rp->rp_iodly = lval;
	    }
	    continue;

	case RPP_PATH:		/* Parse as simple string */
	    if (!prm.prm_val)
		break;
	    if (strlen(prm.prm_val) > DVRP_MAXPATH) {
		fprintf(f, "RPXX path too long (max %d)\n", DVRP_MAXPATH);
		ret = FALSE;
	    } else
		strcpy(rp->rp_spath, prm.prm_val);
	    continue;

	case RPP_DPDBG:		/* Parse as true/false boolean or number */
#if KLH10_DEV_DPRPXX
	    if (!prm.prm_val)	/* No arg => default to 1 */
		rp->rp_dpdbg = 1;
	    else if (!s_tobool(prm.prm_val, &(rp->rp_dpdbg)))
		break;
#endif
	    continue;

	case RPP_DMA:		/* Parse as true/false boolean */
#if KLH10_DEV_DPRPXX
	    if (!prm.prm_val)
		break;
	    if (!s_tobool(prm.prm_val, &rp->rp_dpdma))
		break;
#endif
	    continue;

	case RPP_DP:		/* Parse as simple string */
#if KLH10_DEV_DPRPXX
	    if (!prm.prm_val)
		break;
	    rp->rp_dpname = s_dup(prm.prm_val);
#endif
	    continue;
	}
	ret = FALSE;
	fprintf(f, "RPXX param \"%s\": ", prm.prm_name);
	if (prm.prm_val)
	    fprintf(f, "bad value syntax: \"%s\"\n", prm.prm_val);
	else
	    fprintf(f, "missing value\n");
    }

    /* Param string all done, do followup checks or cleanup */
#if KLH10_DEV_DPRPXX
    if (!cpu.mm_shared)		/* If no shared 10 mem, */
	rp->rp_dpdma = FALSE;	/* force no DMA. */
#endif

    /* Set default path for diskfile if none given */
    if (!rp->rp_spath[0]) {
	sprintf(rp->rp_spath, "RH20.%s.%d",
			rp->rp_dcf.dcf_name, nrps);
    }

    /* Helpful checks to avoid shooting self in foot. */

    /* If using dynamically sized drive, make sure all geometry
       parameters were provided!
     */
    if (rp->rp_isdyn) {
	if (!rp->rp_dcf.dcf_nsec
	 || !rp->rp_dcf.dcf_ntrk
	 || !rp->rp_dcf.dcf_ncyl) {
	    fprintf(f, "RPXX incomplete dynamic geometry: DT %o, %dc %dt %ds\n",
		    rp->rp_dcf.dcf_type, rp->rp_dcf.dcf_ncyl,
		    rp->rp_dcf.dcf_ntrk, rp->rp_dcf.dcf_nsec);
	    return FALSE;
	}
	fprintf(f, "RPXX dynamic geom: DT %o, %dc %dt %ds\n",
		    rp->rp_dcf.dcf_type, rp->rp_dcf.dcf_ncyl,
		    rp->rp_dcf.dcf_ntrk, rp->rp_dcf.dcf_nsec);
    }

    /*  Ensure the drive serial # isn't duplicated, otherwise TOPS-20
	will think it's a dual-ported drive and get very confused.
	Do similar check for diskfile path as well.
    */
    for (i = 0; i < nrps; ++i) {	/* Step thru all known RP devs */
	struct rpdev *ckrp;

	if (!(ckrp = dvrpxx[i]) || (ckrp == rp))
	    continue;
	if (RPREG(ckrp, RHR_SN) == RPREG(rp, RHR_SN)) {
	    fprintf(f, "RPXX serial num duplicated! %d%d%d%d\n",
			(RPREG(rp, RHR_SN) >> 12) & 017,
			(RPREG(rp, RHR_SN) >>  8) & 017,
			(RPREG(rp, RHR_SN) >>  4) & 017,
			(RPREG(rp, RHR_SN)      ) & 017);
	    ret = FALSE;
	    break;
	}
	if (strcmp(ckrp->rp_spath, rp->rp_spath) == 0) {
	    fprintf(f, "RPXX path duplicated! \"%s\"\n", rp->rp_spath);
	    ret = FALSE;
	    break;
	}
    }

    return ret;
}

static int
partyp(struct rpdev *rp, char *cp)
{
    register struct diskconf *dc;
    long lval;

    for (dc = diskconfs; dc->dcf_name[0]; ++dc) {
	if (s_match(cp, dc->dcf_name) == 2) {
	    rp->rp_dcf = *dc;			/* Found match, won */
	    rp->rp_isdyn = FALSE;		/* Not dyn sized */
	    return TRUE;
	}
    }
    /* Type name not found.  See if it's actually a number;
       if so, allow using that but make it dynamic. 
       Parse as octal.
     */
    if (!s_tonum(cp, &lval) || (((unsigned long)lval) > RH_DTTYP)) {
	return FALSE;			/* Unknown disk type */
    }
    /* Specified arbitrary type number.  Assume dynamically sized as
       well; will barf later if no sizes were provided.
    */
    rp->rp_isdyn = TRUE;
    sprintf(rp->rp_dcf.dcf_name, "DT%lo", lval);
    rp->rp_dcf.dcf_type = lval;
    rp->rp_dcf.dcf_nwds = 128;
    rp->rp_dcf.dcf_nsec = 0;
    rp->rp_dcf.dcf_ntrk = 0;
    rp->rp_dcf.dcf_ncyl = 0;
    return TRUE;
}


/* Parse disk format -- something that VDISK understands.
*/
static char *fmttab[] = {
# define vdk_fmt(i,n,c,s,f,t) n
	VDK_FORMATS
# undef vdk_fmt
};

static int
parfmt(char *cp, int *afmt)
{
    register int i;

    for (i = 0; i < VDK_FMT_N; ++i) {
	if (s_match(cp, fmttab[i]) == 2) {
	    *afmt = i;			/* Found match, won */
	    return TRUE;
	}
    }
    return FALSE;			/* Unknown disk format */
}


struct device *
dvrp_create(FILE *f, char *s)
{
    register struct rpdev *rp;

    /* Allocate an RP device structure */
    if (nrps >= DVRP_NSUP) {
	fprintf(f, "Too many RPs, max: %d\n", DVRP_NSUP);
	return NULL;
    }
    if (nrps == 0)			/* Special-case first RP */
	rp = &dvrp;
    else {
	if (!(rp = (struct rpdev *)malloc(sizeof(struct rpdev)))) {
	    fprintf(f, "Cannot allocate RP device!  (out of memory)\n");
	    return NULL;
	}
    }
    dvrpxx[nrps++] = rp;

    /* Various initialization stuff */
    memset((char *)rp, 0, sizeof(*rp));

    /* Configure drive externals */
    iodv_setnull(&rp->rp_dv);		/* Init as nulldev */
    rp->rp_dv.dv_dflags = DVFL_CTLIO | DVFL_DISK;
    rp->rp_dv.dv_init   = rpxx_init;
    rp->rp_dv.dv_reset  = rpxx_reset;
    rp->rp_dv.dv_rdreg  = rpxx_rdreg;
    rp->rp_dv.dv_wrreg  = rpxx_wrreg;
    rp->rp_dv.dv_powoff = rpxx_powoff;
    rp->rp_dv.dv_mount  = rpxx_mount;

    /* Configure drive internals from parsed string and remember for
    ** setting up disk during init.
    */
    if (!rp_conf(f, s, rp))
	return NULL;

    return &rp->rp_dv;
}

/* Init RP-specific stuff
**	This is called as final stage of binding device to controller.
*/
static int
rpxx_init(struct device *d, FILE *of)
{
    register struct rpdev *rp = (struct rpdev *)d;

    rp->rp_bit = 1 << rp->rp_dv.dv_num;		/* Set attention bit mask */

    rp->rp_totsecs = rp->rp_dcf.dcf_ncyl *
			 (rp->rp_dcf.dcf_ntrk * rp->rp_dcf.dcf_nsec);
    RPREG(rp, RHR_OFTC) = 0;			/* OFFSET */
    RPREG(rp, RHR_DT) = RH_DTMH			/* DRIVE TYPE */
			| rp->rp_dcf.dcf_type;
    if (rp->rp_isdyn) {
	RPREG(rp, RHR_DT) |= RH_DTDYNGEOM;
	RPREG(rp, RHR_EPOS) = rp->rp_dcf.dcf_ncyl-1;
	RPREG(rp, RHR_EPAT) = RH_ADRSET(rp->rp_dcf.dcf_ntrk-1,
					rp->rp_dcf.dcf_nsec-1);
    }
    rp->rp_scmd = -1;

#if KLH10_DEV_DPRPXX
  {
    register struct dprpxx_s *dprp;
    struct dvevent_s ev;

    rp->rp_state = RPXX_ST_OFF;

    if (!dp_init(&rp->rp_dp, sizeof(struct dprpxx_s),
		DP_XT_MSIG, SIGUSR1, 0,				/* in fr dp */
		DP_XT_MSIG, SIGUSR1,				/* out to dp */
				(size_t)rp->rp_bufwds*sizeof(w10_t))) {
	if (of) fprintf(of, "RPXX subproc init failed!\n");
	return FALSE;
    }
    rp->rp_buff = dp_xsbuff(&(rp->rp_dp.dp_adr->dpc_todp), (size_t *)NULL);

    rp->rp_dv.dv_dpp = &(rp->rp_dp);	/* Tell CPU where our DP struct is */

    /* Set up RPXX-specific part of shared DP memory */
    dprp = (struct dprpxx_s *) rp->rp_dp.dp_adr;
    rp->rp_sdprp = dprp;
    if (rp->rp_dpdma) {			/* If have shared mem and want DMA, */
	dprp->dprp_shmid = cpu.mm_physegid;	/* tell DP where it is */
	dprp->dprp_dma = TRUE;
    } else {
	dprp->dprp_shmid = 0;
	dprp->dprp_dma = FALSE;
    }
    dprp->dprp_dpc.dpc_debug = rp->rp_dv.dv_debug;	/* Init debug flag */
    if (cpu.mm_locked)				/* Lock DP mem if CPU is */
	dprp->dprp_dpc.dpc_flags |= DPCF_MEMLOCK;

    /* Set up config vars */
    dprp->dprp_fmt = rp->rp_fmt;
    strncpy(dprp->dprp_devname, rp->rp_dcf.dcf_name,
			sizeof(dprp->dprp_devname)-1);
    dprp->dprp_ncyl = rp->rp_dcf.dcf_ncyl;
    dprp->dprp_ntrk = rp->rp_dcf.dcf_ntrk;
    dprp->dprp_nsec = rp->rp_dcf.dcf_nsec;
    dprp->dprp_nwds = rp->rp_dcf.dcf_nwds;


    /* Register ourselves with main KLH10 loop for DP events */

    ev.dvev_type = DVEV_DPSIG;		/* Event = Device Proc signal */
    ev.dvev_arg.eva_int = SIGUSR1;
    ev.dvev_arg2.eva_ip = &(rp->rp_dp.dp_adr->dpc_todp.dpx_donflg);
    if (!(*rp->rp_dv.dv_evreg)((struct device *)rp, rpxx_evhsdon, &ev)) {
	if (of) fprintf(of, "RPXX event reg failed!\n");
	return FALSE;
    }

    ev.dvev_type = DVEV_DPSIG;		/* Event = Device Proc signal */
    ev.dvev_arg.eva_int = SIGUSR1;
    ev.dvev_arg2.eva_ip = &(rp->rp_dp.dp_adr->dpc_frdp.dpx_wakflg);
    if (!(*rp->rp_dv.dv_evreg)((struct device *)rp, rpxx_evhrwak, &ev)) {
	if (of) fprintf(of, "RPXX event reg failed!\n");
	return FALSE;
    }
  }

#else

    if (!vdk_init(&(rp->rp_vdk), (void (*)())NULL, (char *)NULL))
	return FALSE;

    if (!(rp->rp_buff = (unsigned char *)
			malloc(rp->rp_bufwds * sizeof(w10_t)))) {
	if (of) fprintf(of, "RPXX alloc of %d-word buffer failed!\n",
					    rp->rp_bufwds);
	return FALSE;
    }

    /* Set up config vars */
    rp->rp_vdk.dk_format = rp->rp_fmt;
    rp->rp_vdk.dk_filename = rp->rp_spath;
    rp->rp_vdk.dk_dtype = rp->rp_dcf.dcf_type;
    strcpy(rp->rp_vdk.dk_devname, rp->rp_dcf.dcf_name);
    rp->rp_vdk.dk_ncyls = rp->rp_dcf.dcf_ncyl;
    rp->rp_vdk.dk_ntrks = rp->rp_dcf.dcf_ntrk;
    rp->rp_vdk.dk_nsecs = rp->rp_dcf.dcf_nsec;
    rp->rp_vdk.dk_nwds = rp->rp_dcf.dcf_nwds;

#endif

    /* Do standard drive clear - sets "Drive Available" */
    rp_clear(rp);

    /* Mount pack here if specified as init arg? */
    if (rp->rp_spath[0]) {

#if KLH10_DEV_DPRPXX
	if (!rp_dpstart(rp)) {		/* Fire up the subproc! */
	    if (of) fprintf(of, "RPXX subproc \"%s\" startup failed!\n",
					rp->rp_dpname);
	    return FALSE;
	}
#endif
	if (!rp_xmount(rp)) {		/* Special initial mount */
	    if (of) fprintf(of, "RPXX initial mount of \"%s\" failed!\n",
					rp->rp_spath);
	    return FALSE;
	}
    }

    /* Set up I/O delay timer if needed */
    if (rp->rp_iodly) {
	rp->rp_iotmr = clk_tmrget(rpxx_timeout, (void *)rp, rp->rp_iodly);
	clk_tmrquiet(rp->rp_iotmr);	/* Immediately make it quiescent */
    }

    return TRUE;
}


/* RPXX_RESET - reset RP, clear stuff
*/
static void
rpxx_reset(struct device *d)
{
    register struct rpdev *rp = (struct rpdev *)d;

    if (DVDEBUG(rp))
	fprintf(DVDBF(rp), "[rpxx_reset]");
    rp_clear(rp);
}

/* RPXX_POWOFF - Handle "power-off" which usually means the KLH10 is
**	being shut down.  This is important if using a dev subproc!
*/
static void
rpxx_powoff(struct device *d)
{
    register struct rpdev *rp = (struct rpdev *)d;

    /* Later could add stuff to pretend controller/slave is off, but for
    ** now it suffices just to clean up.
    */
#if KLH10_DEV_DPRPXX
    (*rp->rp_dv.dv_evreg)(	/* Flush all event handlers for device */
		(struct device *)rp,
		NULL,		/* No event handler proc */
		(struct dvevent_s *)NULL);
    dp_term(&(rp->rp_dp), 0);	/* Flush all subproc overhead */

    rp->rp_state = RPXX_ST_OFF;
    rp->rp_sdprp = NULL;	/* Clear pointers no longer meaningful */
    rp->rp_buff = NULL;
#endif
}

/* RPXX_MOUNT - Mount or dismount a disk pack.
**	If path is NULL, wants to dismount; argstr is ignored.
**	If path is "", just wants status report.
**	Else mounting diskfile; argstr if present has keyword params:
**	"ro" - Read-Only
**	"rw" - Read/Write (default)
**	<fmt> - "raw", "dbh8", etc - indicate disk word format
** Returns:
**	0 - error.  Error message already output to stream, if one.
**	1 - action succeeded.
*/
static int
rpxx_mount(struct device *d,
	   FILE *f, char *path, char *argstr)
{
    register struct rpdev *rp = (struct rpdev *)d;
    int res;
    char *opath;

#if KLH10_DEV_DPRPXX
    opath = rp->rp_spath[0] ? rp->rp_spath : NULL;
#else
    int prevmount = vdk_ismounted(&(rp->rp_vdk));	/* Get state */
    int vstate = 0;

    if (prevmount) {
	opath = rp->rp_vdk.dk_filename;
	vstate = vdk_iswritable(&(rp->rp_vdk)) ? 2 : 1;
    }
#endif

    if (path && !*path) {
	/* Just wants mount status report */
	if (!opath) {
	    fprintf(f, "No pack mounted.\n");
	    return TRUE;
	}
	fprintf(f, "Current disk pathname is \"%s\", status ", opath);

#if KLH10_DEV_DPRPXX
	switch (rp->rp_state) {
	    case RPXX_ST_OFF:
		fprintf(f, "OFF\n");
		return TRUE;
	    case RPXX_ST_BUSY:
		fprintf(f, "BUSY");
		break;
	    case RPXX_ST_READY:
		fprintf(f, "READY");
		break;
	    default:
		fprintf(f, "<\?\?%d\?\?>", rp->rp_state);
		break;
	}
	if (rp->rp_sdprp->dprp_mol)
	    fprintf(f, " ONLINE");
	if (rp->rp_sdprp->dprp_wrl)
	    fprintf(f, " WRITELOCKED");
#else
	switch (vstate) {
	    case 1:
		fprintf(f, "ONLINE WRITELOCKED");
		break;
	    case 2:
		fprintf(f, "ONLINE");
		break;
	    default:
		fprintf(f, "<\?\?%d\?\?>", vstate);
		break;
	}
#endif
	fprintf(f, "\n");
	return TRUE;
    }

    /* Unmount any existing tape, and mount new tape if one provided */

#if KLH10_DEV_DPRPXX
    /* Should this kill the subproc, or wait its turn to send a command?
    ** Don't want to hang waiting for rewind to complete!
    */
    /* For now, return error if busy (sigh)
    */
    if (rp->rp_state == RPXX_ST_BUSY) {
	fprintf(f, "Cannot %smount: drive busy\n", (path ? "" : "un"));
	return FALSE;
    }
    if (rp->rp_state == RPXX_ST_OFF) {
	/* Subproc not running.  If call is just unmounting, that's all,
	** else must start it up so it can handle the mount.
	*/
	if (!path) {
	    rp->rp_spath[0] = '\0';	/* Make sure no current pack */
	    fprintf(f, "No pack mounted.\n");
	    return TRUE;		/* OK, no pack mounted */
	}

	if (!rp_dpstart(rp))		/* Fire up the subproc! */
	    return FALSE;
    }
#endif /* KLH10_DEV_DPRPXX */

    /* At this point, state should be READY... */

    if (path) {
	/* Process argstr to determine optional params for mount */
	int roflag = FALSE;
	int fmt = rp_format;	/* For now, default to external */
	int err = FALSE;
	size_t plen;
	char tokbuf[100];

	while (s_eztoken(tokbuf, sizeof(tokbuf), &argstr)) {
		 if (s_match(tokbuf, "ro")==2) roflag = TRUE;
	    else if (s_match(tokbuf, "rw")==2) roflag = FALSE;
	    else if (parfmt(tokbuf, &fmt));
	    else {
		fprintf(f, "Unknown mount option: \"%s\"\n", tokbuf);
		err++;
	    }
	}
	if (err)
	    return FALSE;

	/* Plug params into RP struct for use by xmount */
	rp->rp_fmt = fmt;
	rp->rp_iswrite = !roflag;

	plen = strlen(path);
	if (plen > DVRP_MAXPATH-1)
	    plen = DVRP_MAXPATH-1;
	memcpy(rp->rp_spath, path, plen);	/* Remember pathname */
	rp->rp_spath[plen] = '\0';

	res = rp_xmount(rp);			/* Do it! */

#if KLH10_DEV_DPRPXX
	fprintf(f, "Mount requested: \"%s\"\n", path);
#else
	if (res)
	    fprintf(f, "Pack mounted: \"%s\"\n", path);
	else
	    fprintf(f, "Mount failed for: \"%s\"\n", path);
#endif

    } else {
	/* Just unmounting current pack? */
#if KLH10_DEV_DPRPXX
	rp->rp_buff[0] = '\0';		/* Tell DP to unmount */
	rp->rp_scmd = RH_MNOP;		/* Conspire with rp_dpcmddon */
	rp_dpcmd(rp, DPRP_MOUNT, (size_t)0);
	fprintf(f, "Unmount requested\n");
	return TRUE;
#else
	res = vdk_unmount(&(rp->rp_vdk));
	rp_clear(rp);		/* Clear drive status, set regs */

	if (res)
	    fprintf(f, "Pack unmounted\n");
	else
	    fprintf(f, "Unmount failed\n");
#endif /* !KLH10_DEV_DPRPXX */
    }
    return res;
}

static int
rp_xmount(register struct rpdev *rp)
{
#if KLH10_DEV_DPRPXX
    register size_t cnt;

    /* Copy new path into DP comm buffer, including a terminating nul.
    */
    cnt = strlen(rp->rp_spath);
    rp->rp_buff[0] = (rp->rp_iswrite ? 'W' : 'R');	/* Special prefix */

    memcpy((char *)(rp->rp_buff+1), rp->rp_spath, cnt);
    rp->rp_buff[++cnt] = '\0';
    rp->rp_sdprp->dprp_fmt = rp->rp_fmt;	/* Set desired format */

    /* Do command!  And hope for the best... */
    rp->rp_scmd = RH_MNOP;			/* Conspire with rp_dpcmddon */
    rp_dpcmd(rp, DPRP_MOUNT, cnt);
    return TRUE;
#else
    int res;

    rp->rp_vdk.dk_format = rp->rp_fmt;
    res = vdk_mount(&(rp->rp_vdk), rp->rp_spath, rp->rp_iswrite);

    rp_clear(rp);		/* Clear drive status, set regs */

    return res;
#endif /* !KLH10_DEV_DPRPXX */
}

#if KLH10_DEV_DPRPXX


/* RP_DPCMD - Carry out an asynchronous command
*/
static void
rp_dpcmd(register struct rpdev *rp, int cmd, size_t arg)
{
    register struct dpx_s *dpx = &(rp->rp_dp.dp_adr->dpc_todp);

    if (DVDEBUG(rp))
	fprintf(DVDBF(rp), "[rp_dpcmd: DP %d, %ld]\r\n", cmd, (long)arg);

    /* First, double-check to be sure it's OK to send a command */
    if (rp->rp_state != RPXX_ST_READY) {
	/* Says not ready -- check DP to see if true */
	if (!(rp->rp_state == RPXX_ST_BUSY) || !dp_xstest(dpx)) {
	    /* Yep, really can't send command now.
	    ** This shouldn't happen; what to do?
	    */
	    panic("[rp_dpcmd: can't send cmd %d]", cmd);
	}
	/* Hmmm, state is BUSY but dp_xstest thinks we're OK, so go ahead */
    }
    rp->rp_state = RPXX_ST_BUSY;

    dp_xsend(dpx, cmd, arg);		/* Send command! */
}


/* RPXX_EVHSDON - Invoked by INSBRK event handling when
**	signal detected from DP saying "done" in response to something
**	we sent it.
**	Basically this means the DP should be ready to accept another
**	command.
*/
static void
rpxx_evhsdon(struct device *d,
	     register struct dvevent_s *evp)
{
    register struct rpdev *rp = (struct rpdev *)d;

    if (DVDEBUG(rp))
	fprintf(DVDBF(rp), "[rpxx_evhsdon: %d]",
		(int)dp_xstest(&(rp->rp_dp.dp_adr->dpc_todp)));

    rp->rp_state = RPXX_ST_READY;	/* Say ready for cmd again */
    rp_dpcmddon(rp);
}

/* RPXX_EVHRWAK - Invoked by INSBRK event handling when
**	signal detected from DP saying "wake up"; the DP is sending
**	us something.
**	The RPXX will use this to receive notice of unexpected manual events,
**	specifically tape being mounted or unmounted.
*/
static void
rpxx_evhrwak(struct device *d,
	     register struct dvevent_s *evp)
{
    register struct rpdev *rp = (struct rpdev *)d;
    register struct dpx_s *dpx = &(rp->rp_dp.dp_adr->dpc_frdp);

    if (DVDEBUG(rp))
	fprintf(DVDBF(rp), "[rpxx_evhrwak: %d]", (int)dp_xrtest(dpx));

    if (dp_xrtest(dpx)) {		/* Verify there's a message for us */
	switch (dp_xrcmd(dpx)) {
	case DPRP_MOUNT:
	    if (DVDEBUG(rp))
		fprintf(DVDBF(rp), "[rpxx_evhrwak: Disk Online!]\r\n");

#if 0	/* Any equivalent for this? */
	    RPREG(rp, RHR_STS) |= RP_SSSC;	/* Set Slave Status Change */
#endif
	    rp_attn(rp);
	    break;

	default:
	    break;
	}
	dp_xrdone(dpx);			/* just ACK it */
    }
}


static int
rp_dpstart(register struct rpdev *rp)
{
    if (rp->rp_state != RPXX_ST_OFF) {
	fprintf(DVDBF(rp), "[rp_dpstart: Already running\?\?]\r\n");
	return FALSE;
    }
    if (DVDEBUG(rp))
	fprintf(DVDBF(rp), "[rp_dpstart: Starting DP \"%s\"...",
				rp->rp_dpname);
    if (!dp_start(&rp->rp_dp, rp->rp_dpname)) {
	if (DVDEBUG(rp))
	    fprintf(DVDBF(rp), " failed!]\r\n");
	else
	    fprintf(DVDBF(rp), "[rp_dpstart: Start of DP \"%s\" failed!]\r\n",
				rp->rp_dpname);
	return FALSE;
    }
    if (DVDEBUG(rp))
	fprintf(DVDBF(rp), " started!]\r\n");

    rp->rp_state = RPXX_ST_READY;
    return TRUE;
}

#endif /* KLH10_DEV_DPRPXX */

/* RP_SSTA - Set Status bits from slave info
*/
static void
rp_ssta(register struct rpdev *rp)
{
    register unsigned int sts = RPREG(rp, RHR_STS);

    /* Clear bits we'll check */
    sts &= ~(RH_SPIP|RH_SMOL|RH_SWRL|RH_SLBT|RH_SPGM
		    |RH_SDPR|RH_SDRY|RH_SVV);

    sts |= RH_SDPR;		/* Assume drive always there */

#if KLH10_DEV_DPRPXX
  {
    register struct dprpxx_s *dprp = rp->rp_sdprp;

    if (rp->rp_state != RPXX_ST_OFF && dprp) {
	/* Drive present, see if ready for commands */
	if (rp->rp_state == RPXX_ST_READY)
	    sts |= RH_SDRY;	/* Drive present & ready for commands */

	if (dprp->dprp_mol) sts |= RH_SMOL|RH_SVV;	/* Medium online */
	if (dprp->dprp_wrl) sts |= RH_SWRL;		/* Write-locked */
    }
  }
#else
	/* MOL,DPR,RDY, and VV must all be present for the drive to be
	** considered "good" by T20.
	*/
    sts |= RH_SDPR|RH_SDRY;		/* Drive Present & Ready */
    if (vdk_ismounted(&(rp->rp_vdk)))
	sts |= RH_SMOL|RH_SVV;		/* Online and Valid */
    if (!vdk_iswritable(&(rp->rp_vdk)))
	sts |= RH_SWRL;			/* Write-locked */
#endif

    RPREG(rp, RHR_STS) = sts;	/* Set new value of status register! */
}

/* RP_ATTN - Send special attention interrupt
*/
static void
rp_attn(register struct rpdev *rp)
{
    RPREG(rp, RHR_STS) |= RH_SATN;
    RPREG(rp, RHR_ATTN) |= rp->rp_bit;		/* For our drive # */
    if (DVDEBUG(rp))
	fprintf(DVDBF(rp), "[rp_attn: ON]");
    (*rp->rp_dv.dv_attn)(&(rp->rp_dv), 1);	/* Assert ATTN */
}

/* RP_CLEAR - clear RP drive
*/
static void
rp_clear(register struct rpdev *rp)
{

    /* Turn off any attention bit. */
    if (DVDEBUG(rp))
	fprintf(DVDBF(rp), "[rp_attn: off]");
    (*rp->rp_dv.dv_attn)(&rp->rp_dv, 0);

    rp->rp_scmd = -1;
    if (rp->rp_iodly && rp->rp_iotmr)	/* Ensure any timer is quiescent */
	clk_tmrquiet(rp->rp_iotmr);

    /* Clear and set Drive bits in CS1. */
    RPREG(rp, RHR_CSR) = RH_XDVA;	/* Drive Available */

    /* Clearing errors may be tricky.  Have to ensure that slave
    ** errors are reset as well -- if this involves a command to
    ** the DP then how to wait for synchronization to happen?
    ** May need to have a shared "clear-before-executing-cmd" flag which can
    ** be set anytime.
    */
#if KLH10_DEV_DPRPXX
    rp->rp_sdprp->dprp_err = 0;	/* For now, so rp_ssta doesn't spill beans */
#endif
    RPREG(rp, RHR_STS) =	/* RO  FS Formatter Status */
		RH_SDPR;	/*	Drive/formatter Present */
    rp_ssta(rp);		/* Set status bits for drive */
#if 0
    RPREG(rp, RHR_STS) =
		RH_SMOL|RH_SDPR|RH_SDRY|RH_SVV;	/* Drive Status */
#endif

    RPREG(rp, RHR_ER1) = 0;	/* ERROR 1. */
    switch (rp->rp_dcf.dcf_type) {
	case RH_DTRP06:		/* RP06 or RP07 */
	case RH_DTRP07:

	case RH_DTRM03:		/* These too? Does ER2 even exist? */
	case RH_DTRM80:

	    RPREG(rp, RHR_ER2) = 0;	/* ERROR 2. */
	    RPREG(rp, RHR_ER3) = 0;	/* ERROR 3. */
	    break;
    }
    RPREG(rp, RHR_MNT) &= ~(1<<15);	/* Clears bit 15 of MNT */
    if (!rp->rp_isdyn) {		/* These are re-used for dynsizing */
	RPREG(rp, RHR_EPOS) = 0;	/* ECC POSITION. */
	RPREG(rp, RHR_EPAT) = 0;	/* ECC PATTERN. */
    }
}

static uint32
rpxx_rdreg(struct device *d, int reg)
{
    register struct rpdev *rp = (struct rpdev *)d;

    switch (reg) {

	/* In general, device registers are just read directly */
    case RHR_CSR:	/* R/W      CS1 Control/command */
    case RHR_STS:	/* RO  [I2] STS Status */
    case RHR_ER1:	/* R/W	    ER1 Error 1 */
    case RHR_MNT:	/* R/W [-2] MNT	Maintenance */
    case RHR_ATTN:	/* R/W [I2] ATN? Attention Summary */
    case RHR_BAFC:	/* R/W [I2] ADR Block Address or Frame Count */
    case RHR_DT:	/* RO  [I2] TYP	Drive Type */
    case RHR_ER2:	/* R/W	    ER2	Error 2 */
    case RHR_OFTC:	/* R/W	    OFS	Offset or TapeControl */
    case RHR_DCY:	/* R/W	    CYL	Desired Cylinder Addr */
    case RHR_CCY:	/* RO  [-2] CCY	Current Cylinder Addr */
    case RHR_SN:	/* RO  [-2] SER	Serial Number */
    case RHR_ER3:	/* R/W	    ER3	Error 3 */
    case RHR_EPOS:	/* RO	    POS	ECC Position */
    case RHR_EPAT:	/* RO	    PAT	ECC Pattern */
	break;

/* According to ITS, only RP06/7 have CCY,ER2,ER3. */
/* The RM03 and RM80 have ER3, not clear on CCY and ER2. */

	/* Like above but with special hack for stupid T20 init,
	** which wants to see Look-Ahead changing so it knows that the
	** disk really is spinning.  Little does it know...
	*/
    case RHR_LAH:	/* RO	    LAH	Current BlkAdr or R/W ChkChr */
	RPREG(rp, reg) += 0100;		/* Use RP07 sector field & mask */ 
	RPREG(rp, reg) &= 07700;	/* Bump field and return it */
	break;

    default:			/* Unknown register */
	if (rp->rp_dv.dv_debug)
	    fprintf(rp->rp_dv.dv_dbf, "[rpxx_rdreg: unknown reg %o]\r\n", reg);
	return -1;		/* Return error, caller will handle */
    }

    if (DVDEBUG(rp))
	fprintf(DVDBF(rp), "[rpxx_rdreg: r%o/ %o]\r\n",
				reg, RPREG(rp, reg));
    return RPREG(rp, reg);
}

static int
rpxx_wrreg(struct device *d,
	   int reg,
	   register dvureg_t val)
{
    register struct rpdev *rp = (struct rpdev *)d;

    val &= MASK16;

    if (DVDEBUG(rp))
	fprintf(DVDBF(rp), "[rpxx_wrreg: r%o/ %o = %o]\r\n",
				reg, RPREG(rp, reg), val);


    /* If GO bit is still set, all reg mods are refused except for ATTN */
    if ((RPREG(rp, RHR_CSR) & RH_XGO) && (reg != RHR_ATTN)) {
	/* Set RMR error bit (register modif refused); drive is busy.
	** No ATTN generated.
	*/
	if (DVDEBUG(rp))
	    fprintf(DVDBF(rp), "[rpxx_wrreg: reg mod refused: %o]\r\n", reg);

	RPREG(rp, RHR_ER1) |= RH_1RMR;	/* Set RMR error bit */
	RPREG(rp, RHR_STS) |= RH_SERR;	/* And composite error */
	return 1;
    }

    switch (reg) {

    case RHR_CSR:	/* R/W      CS1 Control/command */
	/*  Set any permissible drive bits */
	RPREG(rp, RHR_CSR) &= ~(RH_XCMD);	/* Bits can set */
	RPREG(rp, RHR_CSR) |= (val & RH_XCMD);	/* Set em */
	val &= RH_XCMD;
	if (val & RH_XGO)
	    rp_cmdxct(rp, val);			/* Perform drive command */
	break;

    case RHR_ATTN:	/* R/W [I2] ATN? Attention Summary */
	/* This register is actually intercepted and handled specially
	** by the controller.  At this point, only this specific drive
	** is being addressed, so only one bit of information
	** is meaningful; consider the entire value to be either zero
	** or non-zero.
	** If non-zero, turns off the ATTN bit for this drive.
	*/
	/* Ignores state of GO bit */
	if (val) {				/* Any live bits set? */
	    RPREG(rp, RHR_ATTN) = 0;		/* Yep, turn them off! */
	    RPREG(rp, RHR_STS) &= ~RH_SATN;	/* Clear status bit */
	    if (DVDEBUG(rp))
		fprintf(DVDBF(rp), "[rp_attn: off]");
	    (*rp->rp_dv.dv_attn)(&rp->rp_dv, 0); /* Tell controller it's off */
	}
	break;

	/* Regs that can write */
    case RHR_BAFC:	/* R/W [I2] ADR Block Address or Frame Count */
    case RHR_OFTC:	/* R/W	    OFS	Offset or TapeControl */
    case RHR_DCY:	/* R/W	    CYL	Desired Cylinder Addr */
    case RHR_MNT:	/* R/W [-2] MNT	Maintenance (not supported) */
	RPREG(rp, reg) = val;
	break;

	/* Not clear on these regs.  RP07 claims RO, but others say R/W */
    case RHR_ER1:	/* R/W?	    ER1 Error 1 */
    case RHR_ER2:	/* R/W?	    ER2	Error 2 */
    case RHR_ER3:	/* R/W?	    ER3	Error 3 */
	RPREG(rp, reg) = val;
	break;

	/* Read-Only registers, write is no-op */
    case RHR_STS:	/* RO  [I2] STS Status */
    case RHR_DT:	/* RO  [I2] TYP	Drive Type */
    case RHR_LAH:	/* RO		Current BlockAddr or R/W CheckChar */
    case RHR_SN:	/* RO  [-2] SER	Serial Number */
    case RHR_CCY:	/* RO  [-2] CCY	Current Cylinder Addr */
    case RHR_EPOS:	/* RO	    POS	ECC Position */
    case RHR_EPAT:	/* RO	    PAT	ECC Pattern */
	break;

/* According to ITS, only RP06/7 have CCY,ER2,ER3. */
/* The RM03 and RM80 have ER3, not clear on CCY and ER2. */

    default:			/* Unknown register */
	if (DVDEBUG(rp))
	    fprintf(DVDBF(rp), "[rpxx_wrreg: unknown reg %o]\r\n", reg);

	/* If illegal register was selected, sets ILR bit in error reg, but
	** doesn't generate ATTN.
	*/
	RPREG(rp, RHR_ER1) |= RH_1ILR;	/* Set ILR error bit */
	RPREG(rp, RHR_STS) |= RH_SERR;	/* And composite error */
	return 0;		/* Return error, caller will handle */

    }

    return 1;
}


/* RP_CMDXCT - RP drive command execution
**	Note that rp_cmdxct cannot be called unless the GO bit is off!
**
**	Only one command can be given
*/
static void
rp_cmdxct(register struct rpdev *rp,
	  unsigned int cmd)
{
    if (DVDEBUG(rp))
	fprintf(DVDBF(rp), "[RP cmd: %o]\r\n", cmd);

    /* The RP07 doc (p. 6-20) claims that giving a command with GO
    ** while an error condition exists will always fail (the operation
    ** is "inhibited" and GO is not set).
    ** The only exceptions are a Drive Clear or Microdiagnostic command.
    */
    /* Check for always-legal CLR command */
    if (cmd == RH_MCLR) {
	rp_clear(rp);
	return;
    }

    /* Now check for pre-existing error */
    if (RPREG(rp, RHR_STS) & RH_SERR) {		/* Check composite bit */
	RPREG(rp, RHR_CSR) &= ~RH_XGO;		/* Turn off GO */
	rp_attn(rp);				/* and set ATA to interrupt */
	return;
    }

    /* Check for medium online.
    **	RP07 doc says MOL must be set prior to initiation of any command
    **	except in Microdiag mode, but doesn't say what happens if you try
    **	anyway.  The following code is similar to that of the TM02/3.
    */
    if (!(RPREG(rp, RHR_STS) & RH_SMOL)) {	/* No "Medium Online"? */
	/* Must distinguish between I/O commands and everything else.
	** I/O xfers must invoke special handler.
	*/
	if ((061 <= cmd && cmd <= 067)		/* Write function? */
	  || (071 <= cmd && cmd <= 077)) {	/* Read function? */
	    (*rp->rp_dv.dv_iobeg)(&rp->rp_dv, (cmd < 070)); /* Set up xfer */
	    (*rp->rp_dv.dv_drerr)(&rp->rp_dv);	/* then say error */
	}
	/* Always add error bit for "Unsafe" -- can't find any
	** other plausible bit for offline medium.
	*/
	RPREG(rp, RHR_CSR) &= ~RH_XGO;	/* Turn off GO */
	RPREG(rp, RHR_ER1) |= RH_1UNS;	/* Unsafe */
	RPREG(rp, RHR_STS) |= RH_SERR;	/* Error summary */
	rp_attn(rp);			/* Send attention interrupt */
	return;
    }


    /* OK to execute command!
    **	Note Drive Clear (RH_MCLR) has already been checked for.
    */
    switch (cmd) {
    case RH_MNOP:	/* No Operation */
	break;		/* Done, no ATTN */

    case RH_MRDP:	/* Read-In Preset */
	RPREG(rp, RHR_DCY) = 0;		/* DC = Desired Cylinder */
	RPREG(rp, RHR_BAFC) = 0;	/* DA = Desired Sector/Track Address */
	RPREG(rp, RHR_OFTC) = 0;	/* OF = Offset */
	break;		/* Done, no ATTN */

    case RH_MUNL:	/* Unload ("Standby" -- the pack doesn't fly off). */
	break;		/* Could put drive softwarily offline? */

    case RH_MREC:	/* Recalibrate */
	/* Recalibrate does a head seek to home position (cyl 0) and
	** triggers ATTN when done.
	** RP07 doc p.6-50 says this takes 500ms (?!)
	*/
	RPREG(rp, RHR_CCY) = 0;		/* CC = Current Cylinder */
	rp_attn(rp);
	break;		/* Done, turn off GO */

    case RH_MRLS:	/* Drive release (dual port) */
	break;		/* No-op since we're never hacking dual stuff */


    case RH_MSEK:	/* Seek to Cylinder */
	if (RPREG(rp, RHR_DCY) >= rp->rp_dcf.dcf_ncyl) {
	    RPREG(rp, RHR_ER1) |= RH_1IAE;	/* Illegal Address Error */
	    RPREG(rp, RHR_STS) |= RH_SERR;	/* Error summary */
	} else {
	    RPREG(rp, RHR_CCY) = RPREG(rp, RHR_DCY);	/* Do the seek */
	}
	rp_attn(rp);	/* Send attention interrupt */
	break;		/* Done, turn off GO and return */

    case RH_MSRC:	/* Search to Cylinder, Track, Sector */
	if ((RPREG(rp, RHR_DCY) >= rp->rp_dcf.dcf_ncyl)
	  || (RH_ATRKGET(RPREG(rp, RHR_BAFC)) >= rp->rp_dcf.dcf_ntrk)
	  || (RH_ASECGET(RPREG(rp, RHR_BAFC)) >= rp->rp_dcf.dcf_nsec)) {
	    RPREG(rp, RHR_ER1) |= RH_1IAE;	/* Illegal Address Error */
	    RPREG(rp, RHR_STS) |= RH_SERR;	/* Error summary */
	} else {
	    RPREG(rp, RHR_CCY) = RPREG(rp, RHR_DCY);	/* Do the seek */
	}
	rp_attn(rp);	/* Send attention interrupt */
	break;		/* Done, turn off GO and return */


    case RH_MACK: /* Acknowledge mounting of pack (required before I/O) */
			/* RP07 doesn't support this, but so what */ 
#if KLH10_DEV_DPRPXX
	/* Talk to subproc? */
#endif
	RPREG(rp, RHR_STS) |= RH_SVV;	/* Volume now valid */
	break;		/* Dunno, but assume no ATTN needed */

    case RH_MOFS:	/* Offset Heads Slightly */
    case RH_MCEN:	/* Return Heads To Centerline */
	break;		/* Dunno, but assume no ATTN needed */


#if 0	/* Commented out so will be intepreted as illegal functions */
    case RH_MWCF:	/* Write Check Header and Data (?doesn't work) */
    case RH_MWHD:	/* Write Header And Data (RP07: Format Track) */
    case RH_MWTD:	/* Write Track Descriptor (RP07 only) */
    case RH_MRTD:	/* Read Track Descriptor (RP07 only) */
	break;
#endif



    case RH_MWRT:	/* Write Data */
	if (rp->rp_iodly) {		/* If config setting is for delay, */
	    rp_delayop(rp, RH_MWRT);	/* Do delayed op instead */
	}
	else if (!rp_ioxfr(rp, 1))	/* Errors handled by rp_io now */
	    break;			/* Failed, drop out and turn off GO */
	RPREG(rp, RHR_STS) &= ~RH_SDRY;	/* Drive busy, note GO still on! */
	return;

#if KLH10_SYS_ITS	/* Pretend this works so ITS Salvager will run */
    case RH_MRHD:	/* Read Header and Data */
#endif
    case RH_MWCH:	/* Write Check Data (RP07: Same as Read Data) */
    case RH_MRED:	/* Read Data */
	if (rp->rp_iodly) {		/* If config setting is for delay, */
	    rp_delayop(rp, RH_MRED);	/* Do delayed op instead */
	}
	else if (!rp_ioxfr(rp, 0))	/* Errors handled by rp_io now */
	    break;			/* Failed, drop out and turn off GO */
	RPREG(rp, RHR_STS) &= ~RH_SDRY;	/* Drive busy, note GO still on! */
	return;

    default:
	if (DVDEBUG(rp))
	    fprintf(DVDBF(rp), "[rp_cmdxct: unknown cmd %#o]\r\n", cmd);

	RPREG(rp, RHR_CSR) &= ~RH_XGO;	/* Turn off GO */
	RPREG(rp, RHR_ER1) |= RH_1ILF;	/* Illegal Function code */
	RPREG(rp, RHR_STS) |= RH_SERR;	/* Error summary */
	rp_attn(rp);			/* Send attention interrupt */
	return;
    }

    /* Command done.  Default is NOT to set attention bit. */
    RPREG(rp, RHR_CSR) &= ~RH_XGO;	/* Turn off GO */
}


/* Called to issue a delayed I/O operation.
**	Note that delay applies to STARTING the operation, not to
** reporting its completion via interrupt.  We cannot quietly start
** the read and then delay the interrupt; that would work for some
** monitor timing problems, but would not avoid situations where,
** for example, the bootstrap initiates a disk read that overlays the
** currently executing code!!  This actually happens in an unpatched ITS.
*/
static void
rp_delayop(register struct rpdev *rp, int cmd)
{
    if (rp->rp_scmd >= 0)
	fprintf(DVDBF(rp), "[rp_delayop: cmd overrun: old %o, new %o]\r\n",
			     rp->rp_scmd, cmd);
    rp->rp_scmd = cmd;		/* Save command for rpxx_timeout */
    clk_tmractiv(rp->rp_iotmr);	/* Start timer */
}

static int
rpxx_timeout(void *arg)
{
    register struct rpdev *rp = (struct rpdev *)arg;

    switch (rp->rp_scmd) {
    case -1:		/* No command buffered up?! */
    default:
	fprintf(DVDBF(rp), "[rpxx_timeout: no cmd?]");
	/* Do nothing whatsoever beyond grumbling */
	break;

    case RH_MWRT:	/* Write Data */
	if (!rp_ioxfr(rp, 1)) {
	    RPREG(rp, RHR_CSR) &= ~RH_XGO;	/* Error, turn off GO */
	}
	break;

    case RH_MRED:	/* Read Data */
	if (!rp_ioxfr(rp, 0)) {
	    RPREG(rp, RHR_CSR) &= ~RH_XGO;	/* Error, turn off GO */
	}
	break;
    }
    return CLKEVH_RET_QUIET;		/* Become quiescent */
}


#if KLH10_DEV_DPRPXX

/* RP_DPCMDDON - Command/operation completed, wrap it up.
**	Slave is known to be quiescent at this point.
*/
static void
rp_dpcmddon(register struct rpdev *rp)
{
    register int cmd = rp->rp_scmd;	/* Find command to complete */

    if (DVDEBUG(rp))
	fprintf(DVDBF(rp), "[rp_dpcmddon: %#o]\r\n", cmd);


    switch (cmd) {

    case RH_MNOP:	/* No Operation - actually DPRP_MOUNT! */
	/* This should only happen when a DPRP_MOUNT has completed,
	** because nothing else puts a NOP in rp_scmd.  Hack.
	** See also rpxx_evhrwak.
	*/
	rp_ssta(rp);		/* update all status */
#if 0
	RPREG(rp, RHR_STS) |= RP_SSSC;	/* Set SSC - slave changed state */
#endif
	rp_attn(rp);
	break;

    case RH_MUNL:	/* Unload */
	rp_ssta(rp);	/* Set status to whatever */
	break;

    case RH_MCLR:	/* Formatter clear (reset errors etc.) */
	break;

    /* The following commands are I/O xfer commands */
    case RH_MWRT:	/* Write Data */
	/* Data written from buffer, maybe fill it up again */
	if (rp_wrfilbuf(rp))
	    return;	/* Yep, keep going */
	break;		/* Nope, all done, turn off GO bit */

    case RH_MRED:	/* Read Data */
	/* Data read into buffer, dispose of it and maybe get more */
	if (rp_rdflsbuf(rp))
	    return;	/* Yep, keep going */
	break;		/* Nope, all done, turn off GO bit */


    default:
	if (DVDEBUG(rp))
	    fprintf(DVDBF(rp), "[RPXX unknown scmd %#o]\r\n", cmd);
	/* Let unknown commands clear GO bit, to avoid wedging */
	break;
    }

    rp->rp_scmd = -1;			/* No more pending cmd */
    RPREG(rp, RHR_CSR) &= ~RH_XGO;	/* Turn off GO bit in CSR */
    rp_ssta(rp);			/* Update all status */
}

#endif /* KLH10_DEV_DPRPXX */

/*
	One complete transfer can involve a number of separate
subtransfers as directed by the controller (data channel), which does
scatter-gather I/O.  This makes life harder, because it's not clear
whether to expect a device subproc to be capable of doing transfers of
less than one sector.  It's possible, but may be less efficient than
e.g. simply reading in the whole sector into a temporary buffer and then
distributing it from there.

	Such transfers DO happen -- eg when booting up the monitor,
the T20 boot skips over the first 16 words of the first page, to skip
over the AC addresses.

Ideal case: transferring whole multiple sectors into sequential
	memory addresses.
Minimal direct xfer case: transferring whole sector.

Spiral note: For the RP06/7 at least, T10 expects to be able to do "spiral"
	read/write transfers where the drive will automatically seek to the
	next cylinder if necessary.

*/

static int
rp_ioxfr(register struct rpdev *rp, int wrtf)
{
    /* Find # sectors controller wants us to xfer */
    rp->rp_blkcnt = (*rp->rp_dv.dv_iobeg)(&rp->rp_dv, wrtf);
    if (!rp->rp_blkcnt) {			/* If screwed up somehow, */
	(*rp->rp_dv.dv_ioend)(&rp->rp_dv, 0);	/* Tell ctlr we're done, */
	return 0;				/* and take failure return */
    }
    /* Find resulting total # of words */
    rp->rp_blkwds = rp->rp_blkcnt * rp->rp_dcf.dcf_nwds;

    /* Determine & verify initial disk address to read from or write to */

    rp->rp_cyl = RPREG(rp, RHR_DCY);	/* Use "desired" cyl - implied seek! */
    rp->rp_trk = RH_ATRKGET(RPREG(rp, RHR_BAFC));
    rp->rp_sec = RH_ASECGET(RPREG(rp, RHR_BAFC));
    if ( (rp->rp_cyl >= rp->rp_dcf.dcf_ncyl)
      || (rp->rp_trk >= rp->rp_dcf.dcf_ntrk)
      || (rp->rp_sec >= rp->rp_dcf.dcf_nsec)) {
	RPREG(rp, RHR_ER1) |= RH_1IAE;		/* Invalid Address Error */
	RPREG(rp, RHR_STS) |= RH_SERR;
	(*rp->rp_dv.dv_drerr)(&rp->rp_dv);	/* Complain to ctlr */
	rp_attn(rp);				/* Trigger ATA */
	return 0;
    }

    /* Starting address OK, get it as a sector address */

    rp->rp_blkadr = (((rp->rp_cyl * rp->rp_dcf.dcf_ntrk)
			+ rp->rp_trk) * rp->rp_dcf.dcf_nsec
			+ rp->rp_sec);

    /* Check length of transfer against disk address, to see whether
    ** it will overrun some boundary, and set limit accordingly.
    ** Currently we assume spiral xfer capability, so don't check for
    ** cylinder boundaries other than last one.
    */
    if ((rp->rp_blkadr + rp->rp_blkcnt) <= rp->rp_totsecs)
	rp->rp_blklim = rp->rp_blkwds;		/* OK to xfer all wds */
    else					/* Oops, truncate */
	rp->rp_blklim = (rp->rp_totsecs - rp->rp_blkadr)
				* rp->rp_dcf.dcf_nwds;

    rp->rp_xfrcnt = 0;		/* Init # of subtransfers */
    rp->rp_isdirect = FALSE;	/* Not direct xfer (yet) */
    if (wrtf) {
	rp->rp_scmd = RH_MWRT;
#if KLH10_DEV_DPRPXX
	return rp_wrfilbuf(rp);	/* Writing - Fill up buffer, start I/O */
#else
	while (rp_wrfilbuf(rp));
	return 0;
#endif
    } else {
	rp->rp_scmd = RH_MRED;
#if KLH10_DEV_DPRPXX
	return rp_rdflsbuf(rp);	/* Reading - Start I/O, empty buffer */
#else
	while (rp_rdflsbuf(rp));
	return 0;
#endif
    }
}

/* RP_IOEND - Called when RP xfer done, when either device or channel
**	is out of data, or if error detected in middle.
*/
static void
rp_ioend(register struct rpdev *rp)
{
    /* Wrap up channel xfer, tell controller we're done */

    rp_ssta(rp);		/* Set status in case MOL etc changed */
    if (RPREG(rp, RHR_STS) & RH_SERR) {
	/* Device error - inform controller */
	(*rp->rp_dv.dv_drerr)(&rp->rp_dv);
	rp_attn(rp);

    } else if (rp->rp_blkwds) {
	/* Channel problem, ran out of space from channel too early. */
	(*rp->rp_dv.dv_ioend)(&rp->rp_dv,	/* Say device wanted more */
		(int)((rp->rp_blkwds + rp->rp_dcf.dcf_nwds-1)
				     / rp->rp_dcf.dcf_nwds));
    } else {
	/* Normal termination, device done.
	** Channel will check itself to see whether it had any words left.
	*/
	(*rp->rp_dv.dv_ioend)(&rp->rp_dv, 0);	/* Win, all blks done */
    }
}

/* RP_UPDXFR - Update vars to reflect disk transfer.
**	Returns 0 if should stop (error of some kind)
**	Assumes no partial sector reads or writes.
*/
static int
rp_updxfr(register struct rpdev *rp)
{
    register int i;

#if KLH10_DEV_DPRPXX
    i = rp->rp_sdprp->dprp_scnt;
#else
    i = rp->rp_rescnt;
#endif

    rp->rp_blkadr += i;		/* Update sector address */

    if ((rp->rp_sec += i) >= rp->rp_dcf.dcf_nsec) {
	i          = rp->rp_sec / rp->rp_dcf.dcf_nsec;
	rp->rp_sec = rp->rp_sec % rp->rp_dcf.dcf_nsec;
	if ((rp->rp_trk += i) >= rp->rp_dcf.dcf_ntrk) {
#if 1
	    /* If spiral read/write supported, bump cylinder # as well.
	       This is supported by at least the RP05, RP06, RP07; the others
	       are unknown, but for now assume the same and hope nothing
	       ever depends on non-spiraling.
	    */
	    i          = rp->rp_trk / rp->rp_dcf.dcf_ntrk;
	    rp->rp_trk = rp->rp_trk % rp->rp_dcf.dcf_ntrk;
	    if ((rp->rp_cyl += i) >= rp->rp_dcf.dcf_ncyl) {
		/* If trk/sec NZ, or plan to do more I/O, say AOE */
		if (rp->rp_blkwds || rp->rp_trk || rp->rp_sec) {
		    RPREG(rp, RHR_ER1) |= RH_1AOE;
		    RPREG(rp, RHR_STS) |= RH_SERR;
		}
	    }
	    RPREG(rp, RHR_CCY) = rp->rp_cyl;
	    RPREG(rp, RHR_DCY) = rp->rp_cyl;
#endif
	}
    }
    RPREG(rp, RHR_BAFC) = RH_ADRSET(rp->rp_trk, rp->rp_sec);

    /* Check to see if last write completed successfully */
#if KLH10_DEV_DPRPXX
    if (i = rp->rp_sdprp->dprp_err) {
#else
    if (i = rp->rp_reserr) {
#endif
	/* Ugh, what error bit to use?? */
	if (i < 0) {				/* If -1 assume addr ovfl */
	    RPREG(rp, RHR_ER1) |= RH_1AOE;
	} else {
	    RPREG(rp, RHR_ER1) |= RH_1UNS;	/* Use generic "Unsafe" */
	}
	RPREG(rp, RHR_STS) |= RH_SERR;		/* Set composite error */
    }

    if (RPREG(rp, RHR_STS) & RH_SERR) {
	rp_ioend(rp);				/* Ugh, stop now */
	return 0;				/* Do nothing else */
    }
    return 1;
}

/* RP_WRFILBUF - Set up buffer for writing to disk
*/
static int
rp_wrfilbuf(register struct rpdev *rp)
{
    register int wc;
    register long bwcnt, totw;
    register w10_t *wp;
    vmptr_t vp;

    if (rp->rp_xfrcnt++) {

	/* Invoking after completion of a write; check results, do next
	** subtransfer.
	** Update registers to reflect any actual I/O done, then
	** see if operation complete or need to write another bufferful.
	*/
	if (rp->rp_isdirect) {
#if KLH10_DEV_DPRPXX
	    wc = rp->rp_sdprp->dprp_scnt;	/* Get # sectors written */
#else
	    wc = rp->rp_rescnt;
#endif
	    wc *= rp->rp_dcf.dcf_nwds;	/* Find # words */
	    rp->rp_blkwds -= wc;
	    rp->rp_blklim -= wc;
	    (void) (*rp->rp_dv.dv_iobuf)(&rp->rp_dv, wc, &vp);
	}

	if (!rp_updxfr(rp))	/* Update regs to reflect progress */
	    return 0;		/* Ugh, error of some kind, ATA & DC done  */
	if (!rp->rp_blkwds) {	/* If nothing left to write, */
	    rp_ioend(rp);	/* stop normally. */
	    return 0;
	}
    }

    /* Get 10's initial data channel info - buffer pointer and count.
    ** Use this to determine whether a direct xfer makes sense.
    */
    wc = (*rp->rp_dv.dv_iobuf)(&rp->rp_dv, 0, &vp);
    if (DVDEBUG(rp))
	fprintf(DVDBF(rp), "[rp_wrfilbuf: Write %d (of %ld) words]",
					wc, rp->rp_blkwds);

#if KLH10_DEV_DPRPXX
    if (rp->rp_dpdma && (wc > rp->rp_dcf.dcf_nwds)) {
#else
    if (wc > rp->rp_dcf.dcf_nwds) {
#endif
	/* Direct!  At least one sector's worth.
	*/
	totw = (wc < rp->rp_blklim)		/* Truncate if needed */
			? wc : rp->rp_blklim;
	wc = (totw / rp->rp_dcf.dcf_nwds);	/* Find # sectors */
	rp->rp_isdirect = TRUE;
	rp->rp_xfrvp = vp;

	if (DVDEBUG(rp)) {
	    fprintf(DVDBF(rp), "[RP wrdir: %d sec, %ld <- %#lo]\r\n",
				wc, (long)rp->rp_blkadr,
				(long)(vp - vm_physmap(0)));
	    if (DVDEBUG(rp) & DVDBF_DATSHO)
		rp_showbuf(rp, (unsigned char *)NULL, vp, (int)totw, 0);
	}

#if KLH10_DEV_DPRPXX
	/* Set up shared vars here */
	rp->rp_sdprp->dprp_phyadr = vp - vm_physmap(0);
	rp->rp_sdprp->dprp_scnt = wc;		/* # sectors */
	rp->rp_sdprp->dprp_daddr = rp->rp_blkadr;
	rp_dpcmd(rp, DPRP_WRDMA, (size_t)0 /* wc*128*sizeof(w10_t) */);
#else
	rp->rp_rescnt = vdk_write(&rp->rp_vdk, vp, (uint32)rp->rp_blkadr, wc);

	/* Check for error -- if ran out of space, go offline! */
	if ((rp->rp_reserr = rp->rp_vdk.dk_err) == ENOSPC) {
	    vdk_unmount(&(rp->rp_vdk));		/* Take offline */
	    RPREG(rp, RHR_ER1) |= RH_1UNS;	/* Unsafe */
	    RPREG(rp, RHR_STS) |= RH_SERR;	/* Error summary */
	}
#endif
	return 1;
    }

    /* Non-direct xfer, must use shared buffer.
    ** Xfer is limited by size of shared buffer, but will always
    ** be some multiple of sector size.
    */
    rp->rp_isdirect = FALSE;
    bwcnt = (rp->rp_bufwds < rp->rp_blklim)
		    ? rp->rp_bufwds : rp->rp_blklim;
    totw = bwcnt;
    wp = (w10_t *)(rp->rp_buff);
    while (wc && bwcnt) {

	if (wc < 0) {		/* Channel trying a reverse xfer?? */
	    /* Yuck, don't attempt to support this.  What err to use?
	    ** It's not a device error actually...  just halt chan xfer.
	    */
	    (*rp->rp_dv.dv_ioend)(&rp->rp_dv, 1);	/* Complain to ctlr */
	    return 0;
	}

	/* WC has # words to xfer on this pass.
	** VP will always be set cuz we're writing ("channel skip" uses a
	** pattern of words, hence VP is never null).
	*/
	if (wc > bwcnt)			/* Limit to # wds in buffer */
	    wc = bwcnt;

	if (DVDEBUG(rp))
	    fprintf(DVDBF(rp), "[rp_wrfilbuf: %d %#lo]\r\n",
			wc, (long)(vp - vm_physmap(0)));

	memcpy((char *)wp, (char *)vp, sizeof(w10_t)*wc);
	if (DVDEBUG(rp) & DVDBF_DATSHO)
	    rp_showbuf(rp, (unsigned char *)(wp - wc), vp, wc, 0);
	bwcnt -= wc;
	wp += wc;

	/* Update controller/datachannel's notion of transfer, and set up
	** for next pass.  Loop test will fail if WC set 0.
	*/
	wc = (*rp->rp_dv.dv_iobuf)(&rp->rp_dv, wc, &vp);
    }

    /* See if buffer has anything and write it out if so */
    totw -= bwcnt;		/* Find # words put into buffer */
    if (totw <= 0) {
	rp_ioend(rp);	/* Nothing in buffer, stop entire xfer now */
	return 0;
    }

    /* Something in buffer -- ensure rounded up to a sector boundary */
    if (bwcnt
      && (wc = bwcnt % rp->rp_dcf.dcf_nwds)) {	/* Find # words left */
	/* Last sector not completely filled out.  Fill it up with
	** zero words, which is what real hardware would do.
	*/
	memset((char *)wp, 0, wc*sizeof(w10_t));	/* Clear rem */
	bwcnt = totw + wc;		/* Find new rounded-up total */
    } else			/* Rounded fine, just use totw as is */
	bwcnt = totw;


    /* Set up for indirect xfer and do it! */
    rp->rp_blkwds -= totw;	/* Reflect gobble from channel */
    rp->rp_blklim -= totw;

    wc = (bwcnt / rp->rp_dcf.dcf_nwds);	/* Find # sectors */

#if KLH10_DEV_DPRPXX
    /* Set up shared mem vars here */
    rp->rp_sdprp->dprp_scnt = wc;	/* # sectors to write from buffer */
    rp->rp_sdprp->dprp_daddr = rp->rp_blkadr;
    rp_dpcmd(rp, DPRP_WRITE, (size_t)0 /* wc*128*sizeof(w10_t) */);
#else

    /* Set up WC and VP for indirect xfer */
    vp = (vmptr_t) rp->rp_buff;		/* Pointer to buffer */
    if (DVDEBUG(rp))
	fprintf(DVDBF(rp), "[RP wrbuf: %d sec, %ld <- 0x%lx]\r\n",
				wc, (long)rp->rp_blkadr, (long)vp);
    rp->rp_rescnt = vdk_write(&rp->rp_vdk, vp, (uint32)rp->rp_blkadr, wc);

    /* Check for error -- if ran out of space, go offline! */
    if ((rp->rp_reserr = rp->rp_vdk.dk_err) == ENOSPC) {
	vdk_unmount(&(rp->rp_vdk));		/* Take offline */
	RPREG(rp, RHR_ER1) |= RH_1UNS;		/* Unsafe */
	RPREG(rp, RHR_STS) |= RH_SERR;		/* Error summary */
    }
#endif

    return 1;			/* Say to keep going... */
}

/* RP_RDFLSBUF - Flush record buffer by copying it into 10's memory,
**	and ask for another bufferful.
*/
static int
rp_rdflsbuf(register struct rpdev *rp)
{
    register int wc;
    register long bwcnt, totw;
    register w10_t *wp;
    vmptr_t vp;

    if (rp->rp_xfrcnt++ == 0) {

	/* First time - Get initial WC request from channel.
	** WC will have # words to xfer (may be 0 if failed or all done, or
	** negative if channel is trying to do a reverse transfer.)
	** If VP is set, then it points to phys mem to xfer to/from.
	*/
	wc = (*rp->rp_dv.dv_iobuf)(&rp->rp_dv, 0, &vp);

    } else {

	/* Invoking after completion of a read.
	** Examine result, copy into 10 mem, and set up for next.
	*/
#if KLH10_DEV_DPRPXX
	totw = rp->rp_sdprp->dprp_scnt;	/* Find # sectors read */
#else
	totw = rp->rp_rescnt;		/* Find # sectors read */
#endif
	totw *= rp->rp_dcf.dcf_nwds;	/* Find # words read */
	if (DVDEBUG(rp))
	    fprintf(DVDBF(rp), "[rp_rdflsbuf: Read %ld words]", totw);

	if (rp->rp_isdirect) {
	    /* Just completed a direct transfer, needn't copy from buffer!
	    ** Can simply update channel vars.
	    */
	    if (DVDEBUG(rp) & DVDBF_DATSHO)
		rp_showbuf(rp, (unsigned char *)NULL, rp->rp_xfrvp, (int)totw, 0);
	    wc = (*rp->rp_dv.dv_iobuf)(&rp->rp_dv, (int)totw, &vp);

	} else {
	    /* Indirect transfer, must copy from buffer into 10's memory.
	    ** Get 10's current buffer pointer and count
	    */
	    wc = (*rp->rp_dv.dv_iobuf)(&rp->rp_dv, 0, &vp);
	    wp = (w10_t *)rp->rp_buff;		/* Get ptr to buffer */
	    bwcnt = totw;
	    for (; wc && bwcnt; ) {
		if (wc < 0) {		/* Channel trying a reverse xfer?? */
		    /* Yuck, don't attempt to support this.  What err to use?
		    ** It's not a device error actually...  just stop channel.
		    */
		    if (!rp_updxfr(rp))		/* Update xfer so far */
			return 0;
		    (*rp->rp_dv.dv_ioend)(&rp->rp_dv, 1);	/* Tell ctlr */
		    return 0;
		}

		if (wc > bwcnt)		/* Apply block wdcount limit */
		    wc = bwcnt;

		if (vp) {		/* VP may be NULL if skipping */
		    if (DVDEBUG(rp))
			fprintf(DVDBF(rp), "[rp_rdflsbuf: %d %#lo]\r\n",
					    wc, (long)(vp - vm_physmap(0)));
		    memcpy((char *)vp, (char *)wp, sizeof(w10_t)*wc);
		    if (DVDEBUG(rp) & DVDBF_DATSHO)
			rp_showbuf(rp, (unsigned char *)wp, vp, wc, 0);
		} else
		    if (DVDEBUG(rp))
			fprintf(DVDBF(rp), "[rp_rdflsbuf: skip %d]\r\n", wc);

		bwcnt -= wc;		/* Update block wdcnt */
		wp += wc;		/* and read pointer from buffer */

		/* Update controller/datachannel's notion of transfer, and set
		** up for next pass.  Loop test will fail if WC set 0.
		*/
		wc = (*rp->rp_dv.dv_iobuf)(&rp->rp_dv, wc, &vp);
	    }

	    totw -= bwcnt;			/* Find total # words copied */
	}

	/* Disk input all copied into 10 mem, or no more space from channel.
	** See if need to read more from disk.
	*/
	rp->rp_blkwds -= totw;
	rp->rp_blklim -= totw;

	if (!rp_updxfr(rp))		/* Update regs, check for error */
	    return 0;

	if (!wc || !rp->rp_blkwds) {	/* Ran out in either chan or dev? */
	    rp_ioend(rp);		/* Yup, stop xfer */
	    return 0;
	}
    }

    /* First time, or more to do.
    ** Set up and initiate read, using remaining block count.
    ** WC has # words channel wants for a direct xfer; VP is set if xfer
    ** is to memory (else skipping).
    */
    if ((bwcnt = rp->rp_blklim) == 0) {	/* If no more to read, */
	rp_ioend(rp);			/* terminate normally */
	return 0;
    }

    /* See if OK to do a direct xfer, and
    ** see how many words we can xfer on this pass.
    */
    if (vp && (wc > rp->rp_dcf.dcf_nwds)
#if KLH10_DEV_DPRPXX
	&& rp->rp_dpdma
#endif
					) {
	/* Yup, at least one sector's worth. */
	rp->rp_isdirect = TRUE;
	wc = (wc < bwcnt) ? wc : bwcnt;		/* Truncate if needed */
	wc = (wc / rp->rp_dcf.dcf_nwds);	/* Find # sectors */
	rp->rp_xfrvp = vp;			/* Remember loc reading into */

#if KLH10_DEV_DPRPXX
	rp->rp_sdprp->dprp_phyadr = vp - vm_physmap(0);
#endif
	if (DVDEBUG(rp))
	    fprintf(DVDBF(rp), "[RP rddir: %d sec, %ld -> %#lo]\r\n",
			wc, (long)rp->rp_blkadr, (long)(vp - vm_physmap(0)));

    } else {

	/* Non-direct xfer, must use shared buffer.
	** Xfer is limited by size of shared buffer, but will always
	** be some multiple of sector size.
	** Set up WC and VP for indirect xfer.
	*/
	rp->rp_isdirect = FALSE;
	bwcnt = (rp->rp_bufwds < bwcnt) ? rp->rp_bufwds : bwcnt;
	wc = (bwcnt / rp->rp_dcf.dcf_nwds);	/* Find # sectors */
	vp = (vmptr_t) rp->rp_buff;		/* Pointer to buffer */

	if (DVDEBUG(rp))
	    fprintf(DVDBF(rp), "[RP rdbuf: %d sec, %ld -> 0x%lx]\r\n",
			wc, (long)rp->rp_blkadr, (long)vp);
    }

#if KLH10_DEV_DPRPXX
    rp->rp_sdprp->dprp_scnt = wc;
    rp->rp_sdprp->dprp_daddr = rp->rp_blkadr;
    rp_dpcmd(rp, (rp->rp_isdirect ? DPRP_RDDMA : DPRP_READ),
					(size_t)0 /* wc*128*sizeof(w10_t) */);
# if 0
    dp_xswait(&(rp->rp_dp.dp_adr->dpc_todp));	/* Synch hack */
# endif
#else

    rp->rp_rescnt = vdk_read(&rp->rp_vdk, vp, (uint32)rp->rp_blkadr, wc);
    rp->rp_reserr = rp->rp_vdk.dk_err;
#endif

    return 1;	
}


static void
rp_showbuf(register struct rpdev *rp,
	   register unsigned char *ucp,
	   register vmptr_t vp,
	   register int wc,
	   int fmt)
{
    register w10_t w;
    int fpw;

    fpw = fmt;			/* for now */

    for (; --wc >= 0; ++vp) {
	w = vm_pget(vp);
	fprintf(DVDBF(rp), "[RPXX: %#lo/ %6lo,,%6lo",
		(long)(vp - vm_physmap(0)), (long)LHGET(w), (long)RHGET(w));
	if (fpw) {
	    register int i = fpw;

	    fprintf(DVDBF(rp), "   ");
	    while (--i >= 0)
		fprintf(DVDBF(rp), " %3o", *ucp++);
	}
	fprintf(DVDBF(rp), "]\r\n");
    }
}

#endif /* KLH10_DEV_RPXX */
