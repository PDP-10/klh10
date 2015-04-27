/* DVTM03.C - Emulates TM03 tape controller under RH20 for KL10
*/
/* $Id: dvtm03.c,v 2.8 2002/05/21 09:40:26 klh Exp $
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
 * $Log: dvtm03.c,v $
 * Revision 2.8  2002/05/21 09:40:26  klh
 * Fix duplicate path check - only applies if using subprocs
 *
 * Revision 2.7  2002/04/24 07:50:06  klh
 * Turn max rec-space cmd into a file-space cmd
 * Add blocking wait on DP when booting, to avoid DEC boot races
 *
 * Revision 2.6  2002/04/10 16:06:20  klh
 * Fix spurious complaint about duplicate paths.
 *
 * Revision 2.5  2002/03/28 16:50:30  klh
 * Uniquize tape device serial numbers (just as for disks)
 *
 * Revision 2.4  2002/03/13 12:36:17  klh
 * Changed to allow NOP to be executed while rewinding without sticking
 * in CSR (ITS depends on this).
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include "klh10.h"

#if !KLH10_DEV_TM03 && CENV_SYS_DECOSF
	/* Stupid gubbish needed to prevent OSF/1 AXP compiler from
	** halting merely because compiled file is empty!
	*/
static int decosfcclossage;
#endif

#if KLH10_DEV_TM03	/* Moby conditional for entire file */

#include <errno.h>
#include <string.h>
#include <stdlib.h>	/* For malloc */
#include <stdio.h>

#include "kn10def.h"
#include "kn10dev.h"
#include "dvuba.h"
#include "dvtm03.h"
#include "prmstr.h"

#if KLH10_DEV_DPTM03
# include "dpsup.h"	/* Using device subproc! */
# include "dptm03.h"	/* Define stuff shared with subproc */
#else
# include "wfio.h"	/* For word-based file i/o */
# include "vmtape.h"	/* For virtual magtape facilities */
#endif

#ifdef RCSID
 RCSID(dvtm03_c,"$Id: dvtm03.c,v 2.8 2002/05/21 09:40:26 klh Exp $")
#endif

/* Some notes on TM03 support:

From the viewpoint of a RH11 or RH20 controller, the TM02/3 is a "drive";
however, by itself it's known as a "formatter" which can control up to 8
"slave" transport units -- sort of like a subcontroller.

The best documentation on the TM02/3 is the DEC Technical
Manual titled "TM03 Magnetic Tape Formatter", part EK-0TM03-TM-003.
The last known edition was Dec 1983.  Having the prints also would
be nice...

Some notes on PDP-10 expectations:
---------------------------------

	ITS driver: nmtape >
	T10 driver: tm2kon.mac
	T20 driver: phym2.mac

Tapemark status:

	The TM03 tech doc is clear that status bit 04 (Tape Mark Detected)
is set whenever passing over a tapemark in the forward direction.
It is not as clear whether this applies to the reverse direction, or
if a tapemark was just written.

All 3 systems DO expect to see this bit when moving back
over a tapemark, and rely on it.

Regarding setting if just written: the hardware appears to be set up
so that anything written will immediately pass under the read head.
This implies that a written tapemark will be immediately detected.

The TOPS-10 driver (tm2kon.mac) does expect to see EOF set when it just
wrote one - this is used to help maintain tape position information -
although it avoids passing the flag (as RB.STM) to the user program
TAPUUO in that case.

The other 2 drivers don't have similarly explicit comments and it
doesn't appear to matter to them.

	CONCLUSION: try to set the EOF/TM flag in all those situations.


Frame Count: 

	ITS never reads the frame count register except when reading
a data record.  Even then, it checks the tape status reg for
EOF/tapemark before looking at the frame count to see how much
data it grabbed.

T10 similarly pays no attention to FC except when reading.

T20 does check the result on tape movement, but the exact value
seems not to matter.  If FC is 0 then it is assumed the operation
succeeded; if it's non-zero then the other bits are checked to
make sure it stopped early for a good reason (BOT,EOT,TM).

	CONCLUSION: Must maintain an accurate frame count for reading,
		but otherwise it's OK to settle for just ensuring it's
		0 on full success and non-zero on partial completion.

*/

#ifndef DVTM_NSUP
# define DVTM_NSUP (8*8)
#endif
#ifndef DVTM_MAXPATH		/* Length of pathname for tapefile */
# define DVTM_MAXPATH 127
#endif

#ifndef DVTM_MAXRECSIZ
# ifdef DPTM_MAXRECSIZ
#  define DVTM_MAXRECSIZ DPTM_MAXRECSIZ
# else
#  define DVTM_MAXRECSIZ (1L<<16)	/* 16 bits worth of record length */
# endif
#endif

#if 0
#define DVDEBUG(d) ((d)->tm_dv.dv_debug)
#define DVDBF(d)   ((d)->tm_dv.dv_dbf)
#endif

struct tmdev {
    struct device tm_dv;

	/* Drive(formatter)-specific stuff */
    unsigned int tm_reg[040];	/* Formatter registers (16 bits each) */
    int tm_typ;			/* Drive type (TM02/TM03 bit in DT) */
    int tm_bit;			/* Attention bit */
    int tm_wc;			/* I/O current word count */
    vmptr_t tm_vp;		/* I/O current word pointer into phys mem */
    int tm_slv;			/* Current selected slave (only 0 used) */

    /* Stuff for slave 0 */
    int tm_styp;		/* Slave device type (RH_DTxx value) */
    int tm_sfmt;		/* Slave format select */
    int tm_sfpw;		/* Frames per word of selected format */
    int tm_sden;		/* Slave density select */
    int tm_srew;		/* TRUE if slave is rewinding */
    int tm_scmd;		/* Command being executed, with GO bit */

    unsigned char *tm_buff;	/* Start ptr into buffer (local or shared) */
    size_t tm_bchs;		/* # bytes used in buffer */

#if KLH10_DEV_DPTM03
    char *tm_dpname;		/* Pathname of executable subproc */
    struct dp_s tm_dp;		/* Handle on dev subprocess */
    struct dptm03_s *tm_sdptm;	/* Ptr to shared memory segment */

    int tm_state;
# define TM03_ST_OFF	0	/* Turned off - no subproc */
# define TM03_ST_READY	1	/* On and ready for command */
# define TM03_ST_BUSY	2	/* Executing some command */
				/* Note "on" doesn't imply tape mounted! */
    int tm_dpdbg;		/* Initial DP debug val */
    char tm_spath[DVTM_MAXPATH+1];
#else
    unsigned char *tm_bufp;	/* Handle on malloced buffer */
    struct vmtape tm_vmt;
#endif
};

#define TMREG(d,r) ((d)->tm_reg[r])

static int ntms = 0;
struct tmdev			/* External for easier debug */
	*dvtm03[DVTM_NSUP];	/* Table of pointers, for easier debug */
static struct tmdev dvtm;	/* First one static for easier debug */


/* Handy macros to eliminate some conditionals */

#if KLH10_DEV_DPTM03
# define TM03_FRMS(tm) ((tm)->tm_sdptm->dptm_frms)
# define TM03_ERRS(tm) ((tm)->tm_sdptm->dptm_err)
#else
# define TM03_FRMS(tm) (vmt_framecnt(&(tm)->tm_vmt))
# define TM03_ERRS(tm) (vmt_errors(&(tm)->tm_vmt))
#endif

/* Internal variables and defs */

static int  tm03_conf(FILE *f, char *s, struct tmdev *tm);

static int  tm03_init(struct device *d, FILE *of);
static int  tm03_readin(struct device *d, FILE *of, w10_t, w10_t *, int);
static int  tm03_mount(struct device *d, FILE *f, char *path, char *argstr);
static void tm03_powoff(struct device *d);
static void tm03_reset(struct device *d);
static uint32 tm03_rdreg(struct device *d, int reg);
static int  tm03_wrreg(struct device *d, int reg, unsigned int val);
#if KLH10_DEV_DPTM03
static void tm03_run(struct tmdev *tm);
static void tm03_evhsdon(struct device *d, struct dvevent_s *evp);
static void tm03_evhrwak(struct device *d, struct dvevent_s *evp);
#endif

static void tm_clear(struct tmdev *tm);
static void tm_cmdxct(struct tmdev *tm, int cmd);
static void tm_cmddon(struct tmdev *tm);
static void tm_nxfn(struct tmdev *tm);
static void tm_attn(struct tmdev *tm);
static void tm_ssint(struct tmdev *tm);
static void tm_space(struct tmdev *tm, int revf);
static int  tm_io(struct tmdev *tm, int dirf);
static void tm_ssel(struct tmdev *tm), tm_ssta(struct tmdev *tm);
static int  tm_filbuf(struct tmdev *tm);
static int  tm_flsbuf(struct tmdev *tm, int revf);
static void tm_showbuf(struct tmdev *tm,
		       unsigned char *ucp, vmptr_t vp, int wc, int revf);

static unsigned char *wdstofcd(unsigned char *ucp, vmptr_t vp, int wc);
static unsigned char *wdstofic(unsigned char *ucp, vmptr_t vp, int wc);
static void fcdtowds(vmptr_t vp, unsigned char *ucp, int wc);
static void fictowds(vmptr_t vp, unsigned char *ucp, int wc);
static void revfcdtowds(vmptr_t vp, unsigned char *ucp, int wc, int revf);
static void revfictowds(vmptr_t vp, unsigned char *ucp, int wc, int revf);

/* Configuration Parameters */

#define DVTM03_PARAMS \
    prmdef(TMP_DBG, "debug"),	/* Initial debug value */\
    prmdef(TMP_FMTR,"fmtr"),	/* Formatter type (eg TM03) */\
    prmdef(TMP_TYP, "type"),	/* Slave type (eg TU45) */\
    prmdef(TMP_PATH,"path"),	/* Initial mount path of file or raw device */\
    prmdef(TMP_SN,  "sn"),	/* Formatter Serial number */\
    prmdef(TMP_DPDBG,"dpdebug"), /* Initial DP debug value */\
    prmdef(TMP_DP,  "dppath")	/* Device subproc pathname */

enum {
# define prmdef(i,s) i
	DVTM03_PARAMS
# undef prmdef
};

static char *tmprmtab[] = {
# define prmdef(i,s) s
	DVTM03_PARAMS
# undef prmdef
	, NULL
};


static int partyp(char *cp, int *atyp);	/* Local parsing routines */
static int parfmtr(char *cp, int *afmtr);

/* TM03_CONF - Parse configuration string and set defaults.
**	At this point, device has just been created, but not yet bound
**	or initialized.
** NOTE that some strings are dynamically allocated!  Someday may want
** to clean them up nicely if config fails or device is uncreated.
*/
static int
tm03_conf(FILE *f, char *s, struct tmdev *tm)
{
    int i, ret = TRUE;
    struct prmstate_s prm;
    char buff[200];
    long lval;

    /* First set defaults for all configurable parameters */
    DVDEBUG(tm) = FALSE;
    tm->tm_typ = TM_DTTM03;		/* Say formatter is TM03 for now */
    tm->tm_styp = TM_DT45;		/* Say slave is TU45 for now */
    TMREG(tm, RHR_SN) =			/* Serial Number register (BCD) */
		  (((9    / 1000)%10) << 12)
		| (((9    /  100)%10) <<  8)
		| (((ntms /   10)%10) <<  4)
		| (((ntms       )%10)      );
#if KLH10_DEV_DPTM03
    tm->tm_dpname = "dptm03";		/* Subproc executable */
    tm->tm_spath[0] = '\0';		/* Nothing mounted yet */
    tm->tm_dpdbg = FALSE;
#endif


    prm_init(&prm, buff, sizeof(buff),
		s, strlen(s),
		tmprmtab, sizeof(tmprmtab[0]));
    while ((i = prm_next(&prm)) != PRMK_DONE) {
	switch (i) {
	case PRMK_NONE:
	    fprintf(f, "Unknown TM03 parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;
	case PRMK_AMBI:
	    fprintf(f, "Ambiguous TM03 parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;
	default:	/* Handle matches not supported */
	    fprintf(f, "Unsupported TM03 parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;

	case TMP_DBG:		/* Parse as true/false boolean or number */
	    if (!prm.prm_val)	/* No arg => default to 1 */
		DVDEBUG(tm) = 1;
	    else if (!s_tobool(prm.prm_val, &DVDEBUG(tm)))
		break;
	    continue;

	case TMP_TYP:		/* Parse as slave type */
	    if (!prm.prm_val)
		break;
	    if (!partyp(prm.prm_val, &tm->tm_styp))
		break;
	    continue;

	case TMP_FMTR:		/* Parse as formatter type */
	    if (!prm.prm_val)
		break;
	    if (!parfmtr(prm.prm_val, &tm->tm_typ))
		break;
	    continue;

	case TMP_SN:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    if (lval < 0) {
		fprintf(f, "TM03 SN must be >= 0\n");
		ret = FALSE;
	    } else
		/* Turn last 4 digits into BCD */
		TMREG(tm, RHR_SN) =
			  (((lval / 1000)%10) << 12)
			| (((lval /  100)%10) <<  8)
			| (((lval /   10)%10) <<  4)
			| (((lval       )%10)      );
	    continue;

	case TMP_PATH:		/* Parse as simple string */
#if KLH10_DEV_DPTM03
	    if (!prm.prm_val)
		break;
	    if (strlen(prm.prm_val) > DVTM_MAXPATH) {
		fprintf(f, "TM03 path too long (max %d)\n", DVTM_MAXPATH);
		ret = FALSE;
	    } else
		strcpy(tm->tm_spath, prm.prm_val);
#endif
	    continue;

	case TMP_DPDBG:		/* Parse as true/false boolean or number */
#if KLH10_DEV_DPTM03
	    if (!prm.prm_val)	/* No arg => default to 1 */
		tm->tm_dpdbg = 1;
	    else if (!s_tobool(prm.prm_val, &(tm->tm_dpdbg)))
		break;
#endif
	    continue;

	case TMP_DP:		/* Parse as simple string */
#if KLH10_DEV_DPTM03
	    if (!prm.prm_val)
		break;
	    tm->tm_dpname = s_dup(prm.prm_val);
#endif
	    continue;
	}
	ret = FALSE;
	fprintf(f, "TM03 param \"%s\": ", prm.prm_name);
	if (prm.prm_val)
	    fprintf(f, "bad value syntax: \"%s\"\n", prm.prm_val);
	else
	    fprintf(f, "missing value\n");
    }

    /* Param string all done, do followup checks or cleanup */

    /* Helpful checks to avoid shooting self in foot. */

    /*  Ensure the drive serial # isn't duplicated, otherwise TOPS-10/20
	will think it's a dual-ported drive and get very confused.
	Do similar check for hard-mount device path as well.
    */
    for (i = 0; i < ntms; ++i) {	/* Step thru all known TM devs */
	struct tmdev *cktm;

	if (!(cktm = dvtm03[i]) || (cktm == tm))
	    continue;
	if (TMREG(cktm, RHR_SN) == TMREG(tm, RHR_SN)) {
	    fprintf(f, "TM03 serial num duplicated! %d%d%d%d\n",
			(TMREG(tm, RHR_SN) >> 12) & 017,
			(TMREG(tm, RHR_SN) >>  8) & 017,
			(TMREG(tm, RHR_SN) >>  4) & 017,
			(TMREG(tm, RHR_SN)      ) & 017);
	    ret = FALSE;
	    break;
	}
#if KLH10_DEV_DPTM03
	if (tm->tm_spath[0] && (strcmp(cktm->tm_spath, tm->tm_spath) == 0)) {
	    fprintf(f, "TM03 path duplicated! \"%s\"\n", tm->tm_spath);
	    ret = FALSE;
	    break;
	}
#endif /* KLH10_DEV_DPTM03 */
    }

    return ret;
}

static int
partyp(char *cp, int *atyp)
{
	 if (s_match(cp, "TU45") == 2) *atyp = TM_DT45;
    else if (s_match(cp, "TE16") == 2) *atyp = TM_DT16;
    else if (s_match(cp, "TU77") == 2) *atyp = TM_DT77;
    else
	return FALSE;

    return TRUE;
}


/* Parse formatter type
*/
static int
parfmtr(char *cp, int *afmtr)
{
	 if (s_match(cp, "TM03") == 2) *afmtr = TM_DTTM03;
    else if (s_match(cp, "TM02") == 2) *afmtr = TM_DTTM02;
    else
	return FALSE;

    return TRUE;
}


struct device *
dvtm03_create(FILE *f, char *s)
{
    register struct tmdev *tm;

    /* Allocate an TM device structure */
    if (ntms >= DVTM_NSUP) {
	fprintf(f, "Too many TMs, max: %d\n", DVTM_NSUP);
	return NULL;
    }
    if (ntms == 0)			/* Special-case first TM */
	tm = &dvtm;
    else {
	if (!(tm = (struct tmdev *)malloc(sizeof(struct tmdev)))) {
	    fprintf(f, "Cannot allocate TM device!  (out of memory)\n");
	    return NULL;
	}
    }
    dvtm03[ntms++] = tm;

    /* Various initialization stuff */
    memset((char *)tm, 0, sizeof(*tm));

    iodv_setnull(&tm->tm_dv);		/* Set up as null device */
    tm->tm_dv.dv_dflags = DVFL_CTLIO | DVFL_NBA | DVFL_TAPE;
    tm->tm_dv.dv_init   = tm03_init;
    tm->tm_dv.dv_readin = tm03_readin;
    tm->tm_dv.dv_reset  = tm03_reset;
    tm->tm_dv.dv_rdreg  = tm03_rdreg;
    tm->tm_dv.dv_wrreg  = tm03_wrreg;
    tm->tm_dv.dv_powoff = tm03_powoff;
    tm->tm_dv.dv_mount  = tm03_mount;

    /* TM-specific stuff */

    /* Configure drive from parsed string.
    */
    if (!tm03_conf(f, s, tm))
	return NULL;

    return &tm->tm_dv;
}

static int
tm03_init(struct device *d, FILE *of)
{
    register struct tmdev *tm = (struct tmdev *)d;

    tm->tm_bit = 1 << tm->tm_dv.dv_num;		/* Set attention bit mask */

    /* Set up stuff for slave 0 */
    TMREG(tm, RHR_DT) = TM_DTNS | TM_DTTA	/* Not sector, and tape */
		| TM_DTSS			/* Slave 0 always there */
		| tm->tm_typ | tm->tm_styp;	/* Formatter & slave type */
    tm->tm_sfmt = 0 /* TM_FCD */;		/* PDP-10 Core-Dump format */
    tm->tm_sfpw = 5;
    tm->tm_sden = 0 /* TM_D02 */;		/* 200bpi */
    tm->tm_srew = FALSE;			/* Not rewinding */
    tm->tm_scmd = 0;				/* No command */

#if KLH10_DEV_DPTM03
  {
    register struct dptm03_s *dptm;
    struct dvevent_s ev;

    tm->tm_state = TM03_ST_OFF;

    if (!dp_init(&tm->tm_dp, sizeof(struct dptm03_s),
		DP_XT_MSIG, SIGUSR1, 0,				/* in fr dp */
		DP_XT_MSIG, SIGUSR1, (size_t)DPTM_MAXRECSIZ)) {	/* out to dp */
	if (of) fprintf(of, "TM03 subproc init failed!\n");
	return FALSE;
    }
    tm->tm_buff = dp_xsbuff(&(tm->tm_dp.dp_adr->dpc_todp), (size_t *)NULL);
    memset(tm->tm_buff - 4, 0, 4);	/* Clear prefix padding */
					/* See dptm_revpad and tm_flsbuf */

    /* Set up TM03-specific part of shared DP memory */
    dptm = (struct dptm03_s *) tm->tm_dp.dp_adr;
    tm->tm_sdptm = dptm;
    tm->tm_dv.dv_dpp = &(tm->tm_dp);	/* Tell CPU where our DP struct is */

    dptm->dptm_dpc.dpc_debug = tm->tm_dpdbg;	/* Init debug flag */
    if (cpu.mm_locked)				/* Lock DP mem if CPU is */
	dptm->dptm_dpc.dpc_flags |= DPCF_MEMLOCK;

    dptm->dptm_blkopen = 10;		/* Use retry of 10 for now */

    /* Register ourselves with main KLH10 loop for DP events */

    ev.dvev_type = DVEV_DPSIG;		/* Event = Device Proc signal */
    ev.dvev_arg.eva_int = SIGUSR1;
    ev.dvev_arg2.eva_ip = &(tm->tm_dp.dp_adr->dpc_todp.dpx_donflg);
    if (!(*tm->tm_dv.dv_evreg)((struct device *)tm, tm03_evhsdon, &ev)) {
	if (of) fprintf(of, "TM03 event reg failed!\n");
	return FALSE;
    }

    ev.dvev_type = DVEV_DPSIG;		/* Event = Device Proc signal */
    ev.dvev_arg.eva_int = SIGUSR1;
    ev.dvev_arg2.eva_ip = &(tm->tm_dp.dp_adr->dpc_frdp.dpx_wakflg);
    if (!(*tm->tm_dv.dv_evreg)((struct device *)tm, tm03_evhrwak, &ev)) {
	if (of) fprintf(of, "TM03 event reg failed!\n");
	return FALSE;
    }

    /* Mount hard device here if specified as init arg? */
    if (tm->tm_spath[0]) {
      if (!tm03_mount((struct device *)tm,
		      of, tm->tm_spath, "hard")) { /* Assume hard, R/W */
	    if (of) fprintf(of, "TM03 initial mount of \"%s\" failed!\n",
			tm->tm_spath);
	    return FALSE;
	}
    }
  }

#else
    /* Note following buffer allocation includes extra "revpad" bytes
       at the start to handle read-reverse transfers; see tm_flsbuf().
    */
    tm->tm_bufp = (unsigned char *)malloc(sizeof(double)+
					DVTM_MAXRECSIZ);
    memset(tm->tm_bufp, 0, sizeof(double));
    tm->tm_buff = tm->tm_bufp + sizeof(double);
    tm->tm_bchs = 0;

    vmt_init(&(tm->tm_vmt), "TM03");
#endif

    tm_clear(tm);

    return TRUE;
}

/* TM03_POWOFF - Handle "power-off" which usually means the KLH10 is
**	being shut down.  This is important if using a dev subproc!
*/
static void
tm03_powoff(struct device *d)
{
    register struct tmdev *tm = (struct tmdev *)d;

    /* Later could add stuff to pretend controller/slave is off, but for
    ** now it suffices just to clean up.
    */
#if KLH10_DEV_DPTM03
    (*tm->tm_dv.dv_evreg)(	/* Flush all event handlers for device */
		(struct device *)tm,
		NULL,		/* Null handler to flush */
		(struct dvevent_s *)NULL);
    dp_term(&(tm->tm_dp), 0);	/* Flush all subproc overhead */

    tm->tm_state = TM03_ST_OFF;
    tm->tm_sdptm = NULL;	/* Clear pointers no longer meaningful */
    tm->tm_buff = NULL;
#endif
}

#if KLH10_DEV_DPTM03

static int
tm_cmdwait(register struct tmdev *tm,
	   FILE *f,
	   register struct dpx_s *dpx,
	   int secs)
{
    int cnt;
    osstm_t stm;
    
    OS_STM_SET(stm, secs);

    cnt = secs - 2;
    while (!dp_xstest(dpx)) {
	if (os_msleep(&stm) <= 0)
	    return 0;
	dev_evcheck();		/* See if got any device completion ints */

	/* Every other sec print progress */
	if (OS_STM_SEC(stm) < cnt) {
	    if (f) fprintf(f, "[TM03 busy, waiting...]\n");
	    cnt = OS_STM_SEC(stm) - 2;
	}
    }
    if (f) fprintf(f, "[TM03 ready]\n");
    return 1;
}

static int
tm_blkcmd(register struct tmdev *tm,
	  FILE *f,
	  int cmd, size_t arg)
{
    register struct dpx_s *dpx = &(tm->tm_dp.dp_adr->dpc_todp);

    if (!tm_cmdwait(tm, f, dpx, 10)) {
	if (f) fprintf(f, "[TM03 still busy, giving up before cmd %o]\n", cmd);
	return FALSE;			/* Barf if not ready in time */
    }
    dp_xsend(dpx, cmd, arg);		/* Send command! */
    if (!tm_cmdwait(tm, f, dpx, 10)) {
	if (f) fprintf(f, "[TM03 still busy, giving up after cmd %o]\n", cmd);
	return FALSE;			/* Barf if not ready in time */
    }
    return TRUE;
}

#endif /* KLH10_DEV_DPTM03 */

/* TM03_READIN - do special readin for boot code
**	Requires special hackery as we are bypassing all of the
**	normal I/O procedures, which assume an initialized controller.
*/
static int
tm03_readin(struct device *d,
	    FILE *f,
	    w10_t blka,		/* Interpreted as file #, 0 = first */
	    w10_t *wp, int wc)
{
    register struct tmdev *tm = (struct tmdev *)d;
    register size_t frmc = wc * 5;	/* Assume core-dump format */
    size_t fskip = W10_U32(blka);

    if (frmc > DVTM_MAXRECSIZ)
	frmc = DVTM_MAXRECSIZ;

    /* If DP, may want to try waiting for response at this point. */

    tm_clear(tm);
    if (!(TMREG(tm, RHR_STS) & TM_SMOL)) {	/* Ensure medium on-line */
	if (f) fprintf(f, "[tm03_readin: tape off-line]\n");
	return 0;		/* Tape not mounted or not ready */
    }

    /* Space forward by given # of files, then read 1 record */
#if KLH10_DEV_DPTM03
    if (fskip) {
	if (f) fprintf(f, "[tm03_readin: skipping %ld]\n", (long)fskip);
	if (!tm_blkcmd(tm, f, DPTM_SFF, fskip))
	    return 0;
    }
    if (!tm_blkcmd(tm, f, DPTM_RDF, frmc)) {
	return 0;
    }
#else
    if (fskip && !vmt_fspace(&(tm->tm_vmt), 0, (long)fskip)) {
	return 0;
    }
    (void) vmt_rget(&(tm->tm_vmt), tm->tm_buff, (long)frmc);
#endif

    frmc = TM03_FRMS(tm);	/* Get # frames in record */
    wc = frmc / 5;		/* Find # whole words */
    if (wc) {
	fcdtowds(wp, tm->tm_buff, wc);
    }
    return wc;
}

#if KLH10_DEV_DPTM03

/* TM03_RUN - Not sure if this one makes sense.
*/
static void
tm03_run(register struct tmdev *tm)
{
}

/* TM_DPCMD - Carry out an asynchronous command
*/
static void
tm_dpcmd(register struct tmdev *tm, int cmd, size_t arg)
{
    register struct dpx_s *dpx = &(tm->tm_dp.dp_adr->dpc_todp);

    if (DVDEBUG(tm))
	fprintf(DVDBF(tm), "[tm_dpcmd: DP %d, %ld]\r\n", cmd, (long)arg);

    /* First, double-check to be sure it's OK to send a command */
    if (tm->tm_state != TM03_ST_READY) {
	/* Says not ready -- check DP to see if true */
	if (!(tm->tm_state == TM03_ST_BUSY) || !dp_xstest(dpx)) {
	    /* Yep, really can't send command now.
	    ** This shouldn't happen; what to do?
	    */
	    fprintf(DVDBF(tm),
		    "[TM03 %s internal error: dpcmd %d blocked, state %d]\r\n",
		    tm->tm_dv.dv_name, cmd, tm->tm_state);
	    /* Try to keep going by ignoring this command */
	    return;
	}
	/* Hmmm, state is BUSY but dp_xstest thinks we're OK, so go ahead */
    }
    tm->tm_state = TM03_ST_BUSY;

    dp_xsend(dpx, cmd, arg);		/* Send command! */

    /* CROCK to get around race problem with DEC boot code (both KL and KS).
     * If PI not turned on, assume we're in boot code, and block here
     * (thus blocking KN10) until tape drive is ready again.
     */
    if (!cpu.pi.pisys_on) {
	/* 15 sec should be plenty!  Any more and probably a real error */
	if (!tm_cmdwait(tm, (DVDEBUG(tm) ? DVDBF(tm) : NULL), dpx, 15)) {
	    if (DVDEBUG(tm))
		fprintf(DVDBF(tm), "[tm_dpcmd: boot-mode wait timed out]\r\n");
	}
    }
}


/* TM03_EVHSDON - Invoked by INSBRK event handling when
**	signal detected from DP saying "done" in response to something
**	we sent it.
**	Basically this means the DP should be ready to accept another
**	command.
*/
static void
tm03_evhsdon(struct device *d,
	     register struct dvevent_s *evp)
{
    register struct tmdev *tm = (struct tmdev *)d;

    if (DVDEBUG(tm))
	fprintf(DVDBF(tm), "[tm03_evhsdon: %d]",
		(int)dp_xstest(&(tm->tm_dp.dp_adr->dpc_todp)));

    tm->tm_state = TM03_ST_READY;	/* Say ready for cmd again */
    tm_cmddon(tm);
}

/* TM03_EVHRWAK - Invoked by INSBRK event handling when
**	signal detected from DP saying "wake up"; the DP is sending
**	us something.
**	The TM03 will use this to receive notice of unexpected manual events,
**	specifically tape being mounted or unmounted.
*/
static void
tm03_evhrwak(struct device *d,
	     register struct dvevent_s *evp)
{
    register struct tmdev *tm = (struct tmdev *)d;
    register struct dpx_s *dpx = &(tm->tm_dp.dp_adr->dpc_frdp);

    if (DVDEBUG(tm))
	fprintf(DVDBF(tm), "[tm03_evhrwak: %d]", (int)dp_xrtest(dpx));

    if (dp_xrtest(dpx)) {		/* Verify there's a message for us */
	switch (dp_xrcmd(dpx)) {
	case DPTM_MOUNT:
	    if (DVDEBUG(tm))
		fprintf(DVDBF(tm), "[tm03_evhrwak: Tape Online!]\r\n");
	    if (tm->tm_slv == 0) {		/* If still right slave */
		tm_ssta(tm);			/* update all status */
	    }
	    TMREG(tm, RHR_STS) |= TM_SSSC;	/* Set Slave Status Change */
	    tm_attn(tm);
	    break;

	default:
	    break;
	}
	dp_xrdone(dpx);			/* just ACK it */
    }
}


static int
tm03_start(register struct tmdev *tm)
{
    if (tm->tm_state != TM03_ST_OFF) {
	fprintf(DVDBF(tm), "[tm03_start: Already running?]\r\n");
	return FALSE;
    }
    if (DVDEBUG(tm))
	fprintf(DVDBF(tm), "[tm03_start: Starting DP \"%s\"...",
				tm->tm_dpname);
    if (!dp_start(&tm->tm_dp, tm->tm_dpname)) {
	if (DVDEBUG(tm))
	    fprintf(DVDBF(tm), " failed!]\r\n");
	else
	    fprintf(DVDBF(tm), "[tm03_start: Start of DP \"%s\" failed!]\r\n",
				tm->tm_dpname);
	return FALSE;
    }
    if (DVDEBUG(tm))
	fprintf(DVDBF(tm), " started!]\r\n");

    tm->tm_state = TM03_ST_READY;
    return TRUE;
}

#endif /* KLH10_DEV_DPTM03 */

/* TM03_MOUNT - Mount or dismount a tape.
**	If path is NULL, wants to dismount; argstr is ignored.
**	If path is "", just wants status report.
**	Else mounting tape; argstr if present has keyword params which
**	are parsed by vmt_attrparse(), e.g.:
**		"hard", "8mm", etc - indicate hardware device or type
**		"ro" - Read-Only
**		"rw" - Create then Read/Write (default)
** Returns:
**	0 - error.  Error message already output to stream, if one.
**	1 - action succeeded.
*/
static int
tm03_mount(struct device *d, FILE *f, char *path, char *argstr)
{
    register struct tmdev *tm = (struct tmdev *)d;

    int err = FALSE;
    char *opath;

#if KLH10_DEV_DPTM03
    register size_t cnt;

    opath = tm->tm_spath[0] ? tm->tm_spath : NULL;
#else
    int prevmount = vmt_ismounted(&(tm->tm_vmt));	/* Get state */
    opath = (prevmount ? vmt_tapepath(&(tm->tm_vmt)) : NULL);
#endif

    if (path && !*path) {
	/* Just wants mount status report */
	if (!f)			/* If no output stream, can't report */
	    return TRUE;
	if (!opath) {
	    fprintf(f, "No tape mounted.\n");
	    return TRUE;
	}
	fprintf(f, "Current tape pathname is \"%s\", status", opath);

#if KLH10_DEV_DPTM03
	switch (tm->tm_state) {
	    case TM03_ST_OFF:
		fprintf(f, " OFF\n");
		return TRUE;
	    case TM03_ST_BUSY:
		fprintf(f, " BUSY");
		break;
	    case TM03_ST_READY:
		fprintf(f, " READY");
		break;
	    default:
		fprintf(f, " <\?\?%d\?\?>", tm->tm_state);
		break;
	}
	if (tm->tm_sdptm->dptm_mol)
	    fprintf(f, " ONLINE");
	if (tm->tm_sdptm->dptm_wrl)
	    fprintf(f, " WRITELOCKED");
#else
	if (prevmount) {
	    fprintf(f, " ONLINE");
	    if (!vmt_iswritable(&(tm->tm_vmt)))
		fprintf(f, " WRITELOCKED");
	} else
	    fprintf(f, " OFFLINE");
#endif
	fprintf(f, "\n");
	return TRUE;
    }

    /* Unmount any existing tape, and mount new tape if one provided */

#if KLH10_DEV_DPTM03
    /* Should this kill the subproc, or wait its turn to send a command?
    ** Don't want to hang waiting for rewind to complete!
    */
    /* For now, return error if busy (sigh)
    */
    if (tm->tm_state == TM03_ST_BUSY) {
	fprintf(f, "Cannot %smount: slave busy\n", (path ? "" : "un"));
	return FALSE;
    }
    if (tm->tm_state == TM03_ST_OFF) {
	/* Subproc not running.  If call is just unmounting, that's all,
	** else must start it up so it can handle the mount.
	*/
	if (!path) {
	    tm->tm_spath[0] = '\0';	/* Make sure no current tapefile */
	    fprintf(f, "No tape mounted.\n");
	    return TRUE;		/* OK, no tape mounted */
	}

	if (!tm03_start(tm))		/* Fire up the subproc! */
	    return FALSE;
    }

    /* At this point, state should be READY... */
    if (!path) {			/* Just unmounting current tape? */
	cnt = 0;			/* Tell DP to unmount */
	tm->tm_sdptm->dptm_pathx = 0;
	tm->tm_sdptm->dptm_argsx = 0;
	tm->tm_buff[0] = '\0';
    } else {
	register unsigned char *cp = tm->tm_buff;
	register size_t acnt;

	cnt = strlen(path);
	if (cnt > DVTM_MAXPATH-1)
	    cnt = DVTM_MAXPATH-1;
	memcpy(tm->tm_spath, path, cnt);	/* Remember pathname */
	tm->tm_spath[cnt] = '\0';

	acnt = argstr ? strlen(argstr) : 0;
	if ((1+cnt+1+acnt+1) > DVTM_MAXRECSIZ) {	/* Buff overflow chk */
	    fprintf(f, "Mount path & args too long! %ld?\n",
		    (long)DVTM_MAXRECSIZ);
	    return FALSE;
	}

	/* Copy path and args into DP comm buffer, including terminators */
	*cp++ = '\0';
	memcpy(cp, path, cnt);
	cp += cnt;
	*cp++ = '\0';
	if (acnt) {
	    memcpy(cp, argstr, acnt);
	}
	cp[acnt] = '\0';
	tm->tm_sdptm->dptm_pathx = 1;
	tm->tm_sdptm->dptm_argsx = 1+cnt+1;
	cnt = 1+cnt+1+acnt+1;
    }

    /* Do command!  And hope for the best... */
    if (!path)
	fprintf(f, "Unmount requested\n");
    else
	fprintf(f, "Mount requested: \"%s\"\n", path);
    tm->tm_scmd = TM_NOP;		/* Conspire with tm_cmddon */
    tm_dpcmd(tm, DPTM_MOUNT, (size_t)cnt);

    return TRUE;

#else	/* !KLH10_DEV_DPTM03 */
  {
    int res;

    if (!path || !*path) {
	/* Wants unmount, args ignored */
	res = vmt_unmount(&tm->tm_vmt);
    } else {
	res = vmt_pathmount(&tm->tm_vmt, path, argstr);
    }

    tm_clear(tm);		/* Clear slave 0 status */
    if (!res || !vmt_ismounted(&(tm->tm_vmt))) {
	if (prevmount)		/* Check - tape previously mounted? */
	    tm_ssint(tm);	/* Yes, say slave status changed */

	fprintf(f, "No tape mounted.\n");
	return (!path ? TRUE : FALSE);	/* OK if dismounting, else failed */
    }
    fprintf(f, "Mount succeeded.\n");

    /* New tape mounted, set up regs appropriately */
    tm_ssta(tm);		/* Set up regs from VMT state */
    TMREG(tm, RHR_STS) |= TM_SSLA;	/* Pretend slave just came online */
    tm_ssint(tm);		/* Say slave status changed */
    return TRUE;
  }
#endif /* !KLH10_DEV_DPTM03 */
}


/* TM03_RESET - clear formatter and selected slave
*/
static void
tm03_reset(struct device *d)
{
    register struct tmdev *tm = (struct tmdev *)d;

    if (DVDEBUG(tm))
	fprintf(DVDBF(tm), "[tm03_reset]");
    tm_clear(tm);
}

/* TM_CLEAR - clear formatter and selected slave
*/
static void
tm_clear(register struct tmdev *tm)
{
    /* Turn off any attention bit. */
    if (DVDEBUG(tm))
	fprintf(DVDBF(tm), "[tm_clear: attn off]");
    (*tm->tm_dv.dv_attn)(&tm->tm_dv, 0);

    /* Clear and set Drive bits in CS1 */
    TMREG(tm, RHR_CSR) = TM_1DA;	/* Drive/formatter Available */
    TMREG(tm, RHR_MNT) &= ~(1<<6);	/* R/W [-2] MNT	Maintenance */
					/* Clears all but bit 6 */

    /* Clearing errors may be tricky.  Have to ensure that slave
    ** errors are reset as well -- if this involves a command to
    ** the DP then how to wait for synchronization to happen?
    ** May need to have a shared "clear-before-executing-cmd" flag which can
    ** be set anytime.
    */
#if KLH10_DEV_DPTM03
    if (tm->tm_sdptm) {		/* Check in case DP startup failed */
	tm->tm_sdptm->dptm_err = 0;	/* So tm_ssta doesn't spill beans */
	tm->tm_sdptm->dptm_col = 0;	/* So TM_SSLA gets turned off! */
      }
#endif
    TMREG(tm, RHR_ER1) = 0;		/* R/W	    ER1 Error 1 */
    TMREG(tm, RHR_STS) =	/* RO  FS Formatter Status */
		TM_SDPR;	/*	Drive/formatter Present */
    tm_ssta(tm);		/* Set status bits per selected slave */

#if 0	/* Clear doesn't touch these */
    TMREG(tm, RHR_BAFC) = 0;	/* R/W [I2] ADR Block Address or Frame Count */
    TMREG(tm, RHR_DT) =		/* RO  [I2] TYP	Drive Type */
	TM_DTNS|TM_DTTA|TM_DTSS|tm->tm_typ|tm->tm_styp;
    TMREG(tm, RHR_LAH) = 0;	/* RO	    LAH	Current BlkAdr or R/W ChkChr */
    TMREG(tm, RHR_OFTC) = 0;	/* R/W	    OFS	Offset or TapeControl */
    TMREG(tm, RHR_SN) = 15414;	/* RO  [-2] SER	Serial Number */
#endif
}


static uint32
tm03_rdreg(struct device *d, int reg)
{
    register struct tmdev *tm = (struct tmdev *)d;

    switch (reg) {

	/* In general, device registers are just read directly.
	** Note that the TM02/TM03 doesn't implement all RH20 registers;
	** attempting to reference unknown regs will fail with an ILR error.
	*/
    case RHR_CSR:	/* R/W      CS1 Control/command */
    case RHR_STS:	/* RO  [I2] STS Status */
    case RHR_ER1:	/* R/W	    ER1 Error 1 */
    case RHR_MNT:	/* R/W [-2] MNT	Maintenance */
    case RHR_ATTN:	/* R/W [I2] ATN Attention Summary */
    case RHR_BAFC:	/* R/W [I2] ADR Block Address or Frame Count */
    case RHR_DT:	/* RO  [I2] TYP	Drive Type */
    case RHR_LAH:	/* RO	    LAH	Current BlkAdr or R/W ChkChr */
    case RHR_SN:	/* RO  [-2] SER	Serial Number */

    case RHR_OFTC:	/* R/W	    OFS	Offset or TapeControl */
	if (DVDEBUG(tm))
	    fprintf(DVDBF(tm), "[tm03_rdreg: r%o/ %o]\r\n",
				reg, TMREG(tm, reg));
	return TMREG(tm, reg);

    default:			/* Unknown register */
	if (DVDEBUG(tm))
	    fprintf(DVDBF(tm), "[tm03_rdreg: unknown reg %o]\r\n", reg);
	break;			/* Return error, caller will handle */
    }

    /* If illegal register was selected, sets ILR bit in error reg, but
    ** doesn't generate ATTN.
    */
    TMREG(tm, RHR_ER1) |= TM_EILR;	/* Set ILR error bit */
    TMREG(tm, RHR_STS) |= TM_SERR;	/* And composite error */

    return -1;			/* Return error, caller will handle */
}

static int
tm03_wrreg(struct device *d,
	   register int reg,
	   register dvureg_t val)
{
    register struct tmdev *tm = (struct tmdev *)d;
    register int gobit;

    val &= MASK16;

    if (DVDEBUG(tm))
	fprintf(DVDBF(tm), "[tm03_wrreg: r%o/ %o = %o]\r\n",
				reg, TMREG(tm, reg), val);


    /* If GO bit is still set, all reg mods are refused
    ** except for the ATTN and MNT registers.
    */
    gobit = TMREG(tm, RHR_CSR) & TM_1GO;

    switch (reg) {

    case RHR_CSR:	/* R/W      CS1 Control/command */
	if (gobit)		/* If formatter is busy, */
	    break;		/* Refuse register modification! */

	/*  Set any permissible drive bits */
	TMREG(tm, RHR_CSR) &= ~(TM_1CM);	/* Bits can set */
	TMREG(tm, RHR_CSR) |= (val & TM_1CM);	/* Set em */
	val &= TM_1CM;
	if (val & TM_1GO) {
	    tm_cmdxct(tm, val);			/* Perform drive command */
	}
	return 1;

    case RHR_ATTN:	/* R/W [I2] ATN? Attention Summary */
	/* This register is actually intercepted and handled specially
	** by the controller.  At this point, only this specific drive
	** is being addressed, so only one bit of information
	** is meaningful; consider the entire value to be either 0 or non-0.
	** If non-zero, turns off the ATTN bit for this drive/formatter.
	*/
	/* Ignores state of GO bit */
	if (val) {				/* Any live bits set? */
	    TMREG(tm, RHR_ATTN) = 0;		/* Yep, turn them off! */
	    TMREG(tm, RHR_STS) &= ~TM_SATA;	/* Clear status bit */
	    if (DVDEBUG(tm))
		fprintf(DVDBF(tm), "[tm_attn: off]");
	    (*tm->tm_dv.dv_attn)(&tm->tm_dv, 0); /* Tell controller it's off */
	}
	return 1;

    case RHR_MNT:	/* R/W [-2] MNT	Maintenance */
	/* Ignores state of GO bit */
	TMREG(tm, reg) = val;	/* Copy value but do nothing */
	return 1;

    /* Frame Count register.  Note TM03 clears this automatically for
    ** a read operation.
    */
    case RHR_BAFC:	/* R/W [I2] ADR Block Address or Frame Count */
	if (gobit)
	    break;
	TMREG(tm, reg) = val;		/* Set reg */
	TMREG(tm, RHR_OFTC) |= TM_TFCS;	/* Set Frame Count Status bit */
	return 1;

	/* Read-Only registers, write is no-op */
    case RHR_STS:	/* RO  [I2] STS Status */
    case RHR_ER1:	/* RO	    ER1 Error 1 */
    case RHR_DT:	/* RO  [I2] TYP	Drive Type */
    case RHR_LAH:	/* RO		Current BlockAddr or R/W CheckChar */
    case RHR_SN:	/* RO  [-2] SER	Serial Number */
	if (gobit)
	    break;
	return 1;

    /* Special - Tape Control register effects slave selection! */
    case RHR_OFTC:	/* R/W	    OFS	Offset or TapeControl */
	if (gobit)
	    break;

	/* Not clear which bits are RO; TM03 doc only identifies
	** ACCL as explicitly RO.  For now, treat FCS as RO also.
	** Don't bother setting SAC (Slave Address Change) as it probably
	**	gets turned off almost immediately by everything.
	**	Treat it as RO also.
	*/
# define TMTCBITS (TM_TEA|TM_TDS|TM_TFS|TM_TTS)	/* R/W bits */

	TMREG(tm,RHR_OFTC) = (TMREG(tm,RHR_OFTC) & ~TMTCBITS)
				| (val & TMTCBITS);

	if (tm->tm_slv != (val & TM_TTS)) {
	    /* Slave selection changed!
	    ** For now, only slave 0 supported, which simplifies code.
	    */
	    tm_ssel(tm);		/* Effect slave selection */
	}

	/* Only hack tape config params for valid slave */
	if (tm->tm_slv == 0) {
	    int oden, ofmt;

	    oden = tm->tm_sden;		/* Remember old values */
	    ofmt = tm->tm_sfmt;
	    tm->tm_sden = (val & TM_TDS)>>8;	/* Get new density */
	    tm->tm_sfmt = (val & TM_TFS)>>4;	/* and format */

	    if ((oden != tm->tm_sden) || (ofmt != tm->tm_sfmt)) {
		/* Something changed, so effect it */
		/* Set format (data mode) */
		switch (tm->tm_sfmt) {
		case TM_FCD: tm->tm_sfpw = 5;	break;
		case TM_FIC: tm->tm_sfpw = 4;	break;
		default:
		    fprintf(DVDBF(tm),
			"[tm03_wrreg: Unsupported data mode %d]\r\n",
					tm->tm_sfmt);
		    tm->tm_sfmt = TM_FCD;	/* Default to core-dump */
		    tm->tm_sfpw = 5;
		}
	    }
	}
	return 1;

    default:			/* Unknown register */
	if (DVDEBUG(tm))
	    fprintf(DVDBF(tm), "[tm03_wrreg: unknown reg %o]\r\n", reg);

	/* If illegal register was selected, sets ILR bit in error reg, but
	** doesn't generate ATTN.
	*/
	TMREG(tm, RHR_ER1) |= TM_EILR;	/* Set ILR error bit */
	TMREG(tm, RHR_STS) |= TM_SERR;	/* And composite error */
	return 0;		/* Return error, caller will handle */
    }

    /* Comes here to set RMR error bit (register modif refused).
    ** As for ILR, doesn't generate ATTN.
    */
    if (DVDEBUG(tm))
	fprintf(DVDBF(tm), "[tm03_wrreg: reg mod refused: %o]\r\n", reg);

    TMREG(tm, RHR_ER1) |= TM_ERMR;	/* Set RMR error bit */
    TMREG(tm, RHR_STS) |= TM_SERR;	/* And composite error */
    return 1;
}


/* TM_SSEL - Do whatever is needed to effect selection of new slave transport.
*/
static void
tm_ssel(register struct tmdev *tm)
{
    tm->tm_slv = (TMREG(tm, RHR_OFTC) & TM_TTS);

    TMREG(tm, RHR_DT) &= ~(TM_DTSS		/* Turn off Slave-Present */
			     | TM_DTDT);	/* and drive type */

    if (tm->tm_slv == 0) {
	/* Set up register values for slave 0 */
	TMREG(tm, RHR_DT) |= TM_DTSS		/* Set Slave-Present */
		| tm->tm_typ | tm->tm_styp;	/* and type */
    } else {
	/* Set up register values for non-existent slave */
	TMREG(tm, RHR_DT) |= 		/* Set type to "none" */
		tm->tm_typ | TM_DT00;	/* plus TM02/3 bit */
    } 
    tm_ssta(tm);			/* Set status register bits */
}

/* TM_SSTA - Set Status bits from slave info
*/
static void
tm_ssta(register struct tmdev *tm)
{
    register unsigned int sts = TMREG(tm, RHR_STS);

    sts &= ~(TM_SPIP|TM_SMOL|TM_SWRL|TM_SEOT	/* Clear bits we'll check */
	  |TM_STM|TM_SDRY|TM_SPES|TM_SDWN|TM_SBOT|TM_SSLA);

    if (tm->tm_slv != 0) {
	TMREG(tm, RHR_STS) = sts;	/* Set new value of status register! */
	return;			/* Non-ex slave selected, no bits to set */
    }

#if KLH10_DEV_DPTM03

  {
    register struct dptm03_s *dptm = tm->tm_sdptm;

    if (tm->tm_state != TM03_ST_OFF && dptm) {
	/* Slave present, see if ready for commands */
	if (tm->tm_state == TM03_ST_READY || tm->tm_srew)
	    sts |= TM_SDRY;	/* Slave present & ready for commands */

	/* SLA is a little peculiar as it is not a static state like MOL; it
	   is set only when the slave comes online (MOL->1) while selected by
	   the TM03.  It is not turned on just by being selected while MOL=1.
	   Cleared by TM03 init, drive clear, and if drive goes off-line.
	*/
	if (dptm->dptm_col) sts |= TM_SSLA;	/* Slave Attn (came online) */

	if (dptm->dptm_pip) sts |= TM_SPIP;	/* Positioning in Progress */
	if (dptm->dptm_mol) sts |= TM_SMOL;	/* Medium online */
	if (dptm->dptm_wrl) sts |= TM_SWRL;	/* Write-locked */

	if (dptm->dptm_bot) sts |= TM_SBOT;	/* Physical BOT */
	if (dptm->dptm_eot) sts |= TM_SEOT;	/* Physical EOT */
	if (dptm->dptm_eof) sts |= TM_STM;	/* At TapeMark (EOF) */
    }
  }
#else
# if 0
    sts |= TM_SSLA;		/* Slave Attention (came online) */
# endif

    sts |= TM_SDRY;		/* Assume "slave" always there */

    if (vmt_ismounted(&(tm->tm_vmt)))	sts |= TM_SMOL;	/* Medium online */
    if (vmt_isatbot(&(tm->tm_vmt)))	sts |= TM_SBOT;	/* Phys BOT */
    if (vmt_isateot(&(tm->tm_vmt)))	sts |= TM_SEOT;	/* Phys EOT */
    if (vmt_isateof(&(tm->tm_vmt)))	sts |= TM_STM;	/* Tapemark (EOF) */
    if (!vmt_iswritable(&(tm->tm_vmt)))	sts |= TM_SWRL;	/* Write-locked */
#endif

    TMREG(tm, RHR_STS) = sts;	/* Set new value of status register! */
}

/* Execute TM02/3 command.
**	Note that tm_cmdxct cannot be called unless the GO bit is off!
**
** There are a variety of funny cases with respect to what commands can
** be executed when.
**	One of the most significant is rewinding, because rewind is the
** only operation that slaves can perform independently of the TM03; that
** is, unselected slaves can continue to rewind while the TM03 pays attention
** to the selected slave.
**	A command can be given to a rewinding slave if DRY (drive ready) is
** true.  However, it will sit in the CSR and not actually be executed until
** the rewind is complete.  The tm_srew flag exists to help support this
** behavior.
**	EXCEPTION: this code permits TM_NOP to be executed immediately,
** without waiting for a rewind to finish.  This appears to violate the
** blanket rule in the TM03 manual, but ITS relies on this working during
** rewinds, so it must have been allowed...
*/
static void
tm_cmdxct(register struct tmdev *tm, int cmd)
{
    if (DVDEBUG(tm))
	fprintf(DVDBF(tm), "[tm_cmdxct: %#o]\r\n", cmd);

    /* The TM03 doc (p. 4-43) claims that giving a command with GO
    ** while an error condition exists will always fail (the operation
    ** is "inhibited").  However, if this were literally true, there
    ** would be NO WAY to clear the error conditions!
    ** Thus, I'm assuming that TM_CLR is specially recognized regardless
    ** of whether any errors exist or not.
    */

    /* Note that only TM_CLR can be executed regardless of whether a
    **	valid slave is selected or not.
    ** TM_RIP requires that slave 0 be valid.
    ** All commands but TM_CLR also require that the selected slave have
    **	its MOL status bit set.
    ** I/O xfer operations to an invalid slave must be aborted specially
    ** so the channel can be stopped and cleaned up.
    */

    /* Check for always-legal CLR command */
    if (cmd == TM_CLR) {
	tm_clear(tm);
	return;
    }

    /* Now check for pre-existing error */
    if (TMREG(tm, RHR_STS) & TM_SERR) {		/* Check composite bit */
	TMREG(tm, RHR_CSR) &= ~TM_1GO;		/* Turn off GO */
	tm_attn(tm);				/* and set ATA to interrupt */
	return;
    }

    /* Now check for being in rewind state - if so, command must be left
    ** in CSR until rewind is done or something else (eg CLR) happens.
    ** Note NOP is specially allowed here.
    */
    if (tm->tm_srew) {
	if (cmd == TM_NOP) {
	    TMREG(tm, RHR_CSR) &= ~TM_1GO;	/* Turn off GO */
	    if (DVDEBUG(tm))
		fprintf(DVDBF(tm), "[TM03 rewinding, NOP executed]\r\n");
	} else {
	    if (DVDEBUG(tm))
		fprintf(DVDBF(tm), "[TM03 rewinding, cmd deferred]\r\n");
	}
	return;
    }

    /* Check for commands that are legal regardless of selected slave.
    ** I'm not entirely certain about these but they seem sensible.
    */
    switch (cmd) {
    case TM_NOP:	/* No Operation */
	TMREG(tm, RHR_CSR) &= ~TM_1GO;		/* Turn off GO */
	return;		/* NOP is not defined as setting ATA when done */

    case TM_RIP:	/* Read-In Preset (not used by T20 or ITS) */
	/* Set tape control reg to slave 0, odd parity, PDP-10 coredump
	** fmt, and 800bpi NRZI
	*/
	TMREG(tm, RHR_OFTC) &= ~(TM_TDS|TM_TFS|TM_TEP|TM_TTS);
	TMREG(tm, RHR_OFTC) |= (TM_D08<<8) | (TM_FCD<<4);
	tm_ssel(tm);	/* Effect the slave 0 selection */

	/* Now attempt to start rewind of slave 0 */
	cmd = TM_REW;	/* Turn current command into "Rewind"! */
	break;

#if 0	/* CLR is now handled by test prior to composite-error check */
    case TM_CLR:	/* Formatter clear (reset errors etc.) */
	tm_clear(tm);
	return;
#endif
    }

    /* Check for valid slave currently selected and online. */
    if (!(TMREG(tm, RHR_STS) & TM_SMOL)) {	/* No "Medium Online"? */
	/* Must distinguish between I/O commands and everything else.
	** I/O xfers must invoke special handler.
	*/
	if ((061 <= cmd && cmd <= 067)		/* Write function? */
	  || (071 <= cmd && cmd <= 077)) {	/* Read function? */
	    (*tm->tm_dv.dv_iobeg)(&tm->tm_dv, (cmd < 070)); /* Set up xfer */
	    (*tm->tm_dv.dv_drerr)(&tm->tm_dv);	/* then say error */
	}
	/* Always add error bit for "Unsafe" -- can't find any
	** other plausible bit for non-existent slave.
	*/
	TMREG(tm, RHR_CSR) &= ~TM_1GO;	/* Turn off GO */
	TMREG(tm, RHR_ER1) |= TM_EUNS;	/* Unsafe */
	TMREG(tm, RHR_STS) |= TM_SERR;	/* Error summary */
	tm_attn(tm);			/* Send attention interrupt */
	return;
    }

    /* Now do all other functions - slave known to exist.
    ** At this point, commands CLR, NOP, and RIP have already been handled.
    */
    tm->tm_scmd = cmd;		/* Remember last cmd executed */
    switch (cmd) {

    case TM_UNL:	/* Unload */
#if KLH10_DEV_DPTM03
	tm->tm_srew = TRUE;		/* Now rewinding */
	tm->tm_sdptm->dptm_mol = FALSE;	/* Say slave went offline */
	TMREG(tm, RHR_STS) &= ~TM_SMOL;	/* Turn off corresponding status bit */
	TMREG(tm, RHR_STS) |= TM_SSSC;	/* Slave Status Change (went offline)*/
	TMREG(tm, RHR_STS) |= TM_SPIP;	/* Positioning in progress */
	tm_dpcmd(tm, DPTM_UNL, (size_t)0);	/* Send UNLOAD command to DP */
#else
	/* Close, dismount */
	/* stdout is not entirely right, but fix up tm03_mount later */
	tm03_mount((struct device *)tm, stdout, (char *)NULL, NULL);
#endif
	break;		/* Turn off GO and send attention interrupt */

    case TM_REW:	/* Rewind */
#if KLH10_DEV_DPTM03
	tm->tm_srew = TRUE;		/* Now rewinding */
	TMREG(tm, RHR_STS) |= TM_SPIP;	/* Positioning in progress */
	tm_dpcmd(tm, DPTM_REW, (size_t)0);	/* Send REWIND command to DP */
	break;		/* Turn off GO, then signal attn */
#else
	vmt_rewind(&tm->tm_vmt);
	tm_cmddon(tm);	/* Say rewind completed, turn off GO, signal ATTN */
	return;
#endif

    case TM_ER3:	/* Erase three inch gap */
#if KLH10_DEV_DPTM03
	TMREG(tm, RHR_STS) &= ~TM_SDRY;	/* Drive busy, note GO still on! */
	tm_dpcmd(tm, DPTM_ER3, (size_t)0);	/* Send ERASE-3 command to DP */
	return;				/* Don't signal attn til done */
#else
	break;		/* No-op for now, just signal attention */
#endif

    case TM_WTM:	/* Write Tape Mark */
	if (TMREG(tm, RHR_STS) & TM_SWRL) {	/* Is it write-locked? */
	    TMREG(tm, RHR_CSR) &= ~TM_1GO;	/* Turn off GO */
	    tm_nxfn(tm);	/* Say non-executable function error */
	    return;
	}
#if KLH10_DEV_DPTM03
	TMREG(tm, RHR_STS) &= ~TM_SDRY;	/* Drive busy, note GO still on! */
	tm_dpcmd(tm, DPTM_WTM, (size_t)0);	/* Send WTM command to DP */
	return;				/* Don't signal attn til done */
#else
	if (!vmt_eof(&tm->tm_vmt)) {
	    /* Either malloc failed or format isn't raw. */
	    fprintf(DVDBF(tm),
			"[TM03: vmt_eof failed, EOF not written]\r\n");
	    TMREG(tm, RHR_CSR) &= ~TM_1GO;	/* Turn off GO */
	    tm_nxfn(tm);
	    return;
	}
	break;		/* Won, signal attn */
#endif

    case TM_SPF:	/* Space Forward records or tapemark */
	tm_space(tm, 0);
#if !KLH10_DEV_DPTM03
	tm_cmddon(tm);	/* Do post-xct stuff, includes clearing GO */
#endif
	return;

    case TM_SPR:	/* Space Reverse records or tapemark */
	tm_space(tm, 1);
#if !KLH10_DEV_DPTM03
	tm_cmddon(tm);	/* Do post-xct stuff, includes clearing GO */
#endif
	return;

    /* The remaining commands are all I/O xfer commands */

    case TM_WRT:	/* Write Forward */
	if (TMREG(tm, RHR_STS) & TM_SWRL) {	/* Is it write-locked? */
	    TMREG(tm, RHR_CSR) &= ~TM_1GO;	/* Turn off GO */
	    tm_nxfn(tm);	/* Give error "Non-Executable Function" */
	    return;
	}
	/* Writes must ensure that frame count reg was set; FCS bit tells
	** us whether a valid count exists.
	*/
	if (!(TMREG(tm, RHR_OFTC)&TM_TFCS)) {
	    if (DVDEBUG(tm))
		fprintf(DVDBF(tm),
			"[tm_cmdxct: NEF - write when FCS=0]\r\n");
	    TMREG(tm, RHR_CSR) &= ~TM_1GO;	/* Turn off GO */
	    tm_nxfn(tm);	/* Give error "Non-Executable Function" */
	    return;
	}
#if KLH10_DEV_DPTM03
	if (!tm_io(tm, 0)) {		/* Errors handled by tm_io now */
	    TMREG(tm, RHR_CSR) &= ~TM_1GO;	/* Turn off GO */
	}
	TMREG(tm, RHR_STS) &= ~TM_SDRY;	/* Drive busy, note GO still on! */
#else
	(void) tm_io(tm, 0);	/* Errors handled by tm_io now */
	tm_cmddon(tm);		/* Do post-xct stuff, includes clearing GO */
#endif
	return;		/* IO operations don't trigger ATTN when done */

    case TM_WCF:	/* Write Check Forward (same as Read Forward) */
    case TM_RDF:	/* Read Forward */
	/* CROCK ALERT!  If operation is reading, formatter clears BAFC
	** at the start of the transfer, so at the end it contains the number
	** of frames read!
	** 0 happens to have the same meaning as "max count" so this reset
	** will never impose a limit on the size of the transfer; that's up to
	** the controller.
	*/
	TMREG(tm, RHR_BAFC) = 0;		/* Reading, force count 0 */
	TMREG(tm, RHR_OFTC) &= ~TM_TFCS;	/* Clear FCS bit */

#if KLH10_DEV_DPTM03
	if (!tm_io(tm, 1)) {		/* Errors handled by tm_io now */
	    TMREG(tm, RHR_CSR) &= ~TM_1GO;	/* Turn off GO */
	}
	TMREG(tm, RHR_STS) &= ~TM_SDRY;	/* Drive busy, note GO still on! */
#else
	(void) tm_io(tm, 1);	/* Errors handled by tm_io now */
	tm_cmddon(tm);		/* Do post-xct stuff, includes clearing GO */
#endif
	return;		/* IO operations don't trigger ATTN when done */

    case TM_WCR:	/* Write Check Reverse (same as Read Reverse) */
    case TM_RDR:	/* Read Data Reverse (not used by ITS) */
			/* Note that T20 may want to use this, argh! */
	TMREG(tm, RHR_BAFC) = 0;		/* Reading, force count 0 */
	TMREG(tm, RHR_OFTC) &= ~TM_TFCS;	/* Clear FCS bit */

#if KLH10_DEV_DPTM03
	if (!tm_io(tm, -1)) {		/* Errors handled by tm_io now */
	    TMREG(tm, RHR_CSR) &= ~TM_1GO;	/* Turn off GO */
	}
	TMREG(tm, RHR_STS) &= ~TM_SDRY;	/* Drive busy, note GO still on! */
#else
	(void) tm_io(tm, -1);	/* Errors handled by tm_io now */
	tm_cmddon(tm);		/* Do post-xct stuff, includes clearing GO */
#endif
	return;


    default:
	if (DVDEBUG(tm))
	    fprintf(DVDBF(tm), "[TM03 unknown cmd %#o]\r\n", cmd);

	TMREG(tm, RHR_CSR) &= ~TM_1GO;	/* Turn off GO */
	TMREG(tm, RHR_ER1) |= TM_EILF;	/* Illegal Function code */
	TMREG(tm, RHR_STS) |= TM_SERR;	/* Error summary */
	tm_attn(tm);			/* Send attention interrupt */
	return;
    }

    /* Command done and wants to set attention bit */
    TMREG(tm, RHR_CSR) &= ~TM_1GO;	/* Turn off GO */
    tm_attn(tm);
}


/* TM_CMDDON - Command/operation completed, wrap it up.
**	Slave is known to be quiescent at this point.
*/
static void
tm_cmddon(register struct tmdev *tm)
{
    register int cmd = tm->tm_scmd;	/* Find command to complete */

    if (DVDEBUG(tm))
	fprintf(DVDBF(tm), "[tm_cmddon: %#o]\r\n", cmd);

    tm->tm_srew = FALSE;		/* Ensure this is flushed */

    switch (cmd) {

    case TM_NOP:	/* No Operation - actually DPTM_MOUNT! */
	/* This should only happen when a DPTM_MOUNT has completed,
	** because nothing else puts a NOP in tm_scmd.  Hack.
	** See also tm03_evhrwak.
	*/
	if (tm->tm_slv == 0) {		/* If still right slave */
	    tm_ssta(tm);		/* update all status */
	}
	if (1) {
	    /* Horrible crock to give feedback on mount/dismount requests */
	    fprintf(DVDBF(tm), "[%s: Tape %s]\r\n",
		    tm->tm_dv.dv_name,
		    (TMREG(tm, RHR_STS) & TM_SMOL) ? "online" : "offline");
	}
	TMREG(tm, RHR_STS) |= TM_SSSC;	/* Set SSC - slave changed state */
	tm_attn(tm);
	break;

    case TM_UNL:	/* Unload */
	/* Note must test for correct slave since unlike most other
	** commands, regs can be written and thus selection can be changed
	** during UNLOAD or REWIND ops!
	*/
	if (tm->tm_slv == 0)			/* If still right slave */
	    TMREG(tm, RHR_STS) &= ~TM_SPIP;	/* say no Pos-in-Progress */
	break;

    case TM_REW:	/* Rewind */
	/* Same check as for UNLOAD above, for same reason */
	if (tm->tm_slv == 0) {			/* If still right slave */
	    tm_ssta(tm);			/* update all status */
	}
	TMREG(tm, RHR_STS) |= TM_SSSC;	/* Rew completed, set SSC */
	tm_attn(tm);
	break;

    case TM_CLR:	/* Formatter clear (reset errors etc.) */
    case TM_RIP:	/* Read-In Preset (not used by T20 or ITS) */
	break;

    case TM_ER3:	/* Erase three inch gap */
	tm_attn(tm);	/* Done, just signal attention */
	break;

    case TM_WTM:	/* Write Tape Mark */
	tm_ssta(tm);	/* Update all status */
	if (TM03_ERRS(tm)) {
	    /* Set some kind of error bit here? */
	    TMREG(tm, RHR_ER1) |= TM_EOPI;	/* What else to use? */
	    TMREG(tm, RHR_STS) |= TM_SERR;
	}
	tm_attn(tm);	/* Done, just signal attention */
	break;

    case TM_SPF:	/* Space Forward records or tapemark */
    case TM_SPR:	/* Space Reverse records or tapemark */
	/* Update BAFC with result */
	TMREG(tm, RHR_BAFC) =
		(TMREG(tm, RHR_BAFC) + TM03_FRMS(tm)) & MASK16;
	if (TMREG(tm, RHR_BAFC) == 0)		/* If counted out, */
	    TMREG(tm, RHR_OFTC) &= ~TM_TFCS;	/* clear FCS */

#if 0	/* Not sure - TM03 ignores data/media errors while spacing */
	if (TM03_ERRS(tm)) {
	    TMREG(tm, RHR_ER1) |= TM_EOPI;	/* What else to use? */
	    TMREG(tm, RHR_STS) |= TM_SERR;
	}
#endif
	tm_ssta(tm);	/* Update all status */
	tm_attn(tm);	/* Done, signal attention */
	break;

    /* The remaining commands are all I/O xfer commands */

    case TM_WRT:	/* Write Forward */
	/* Possibilities:
	**	(1) BAFC != # bytes from data channel sent to slave
	**	(2) # bytes sent != # bytes slave actually wrote
	** (1) should cause a channel short-count error.
	** (2) is problematical.  Channel data was already snarfed, so 
	**	causing a channel long-count error might confuse system;
	**	no TM03 error seems quite right.
	**	I'll make this give an OPI (operation incomplete) error.
	*/
	TMREG(tm, RHR_BAFC) = (TMREG(tm, RHR_BAFC) + tm->tm_bchs) & MASK16;
	if (TMREG(tm, RHR_BAFC)) {
	    /* Frame count didn't match ctlr data count, so complain */
	    if (DVDEBUG(tm))
		fprintf(DVDBF(tm), "[tm_cmddon: WRT!=0: %#o]\r\n",
					TMREG(tm, RHR_BAFC));
	    (*tm->tm_dv.dv_ioend)(&tm->tm_dv, 1);	/* Say stuff left */
	} else {
	    (*tm->tm_dv.dv_ioend)(&tm->tm_dv, 0);	/* Say all's well */
	    TMREG(tm, RHR_OFTC) &= ~TM_TFCS;		/* Clear FCS bit */
	}

	/* Check against # frames actually written */
	if (tm->tm_bchs != TM03_FRMS(tm)) {
	    if (DVDEBUG(tm))
		fprintf(DVDBF(tm), "[tm_cmddon: WRTbf: %#lo != %#lo]\r\n",
			(long)tm->tm_bchs, (long)TM03_FRMS(tm));

	    /* Ugh, set BAFC to -<# frames unwritten> */
	    TMREG(tm, RHR_BAFC) = (TM03_FRMS(tm) - tm->tm_bchs) & MASK16;
	    TMREG(tm, RHR_OFTC) |= TM_TFCS;		/* Restore FCS bit */
	    TMREG(tm, RHR_ER1) |= TM_EOPI;
	    TMREG(tm, RHR_STS) |= TM_SERR;
	}

	/* Check and set general status.
	** No ATTN is signaled for I/O unless some error happened.
	*/
	tm_ssta(tm);
	if (TMREG(tm, RHR_STS) & TM_SERR)	/* If any errors, */
	    tm_attn(tm);			/* signal ATTN */
	break;

    case TM_WCF:	/* Write Check Forward (same as Read Forward) */
    case TM_RDF:	/* Read Forward */
	/* Find # frames read if any, then do channel xfer */
	tm_flsbuf(tm, 0);
	break;

    case TM_WCR:	/* Write Check Reverse (same as Read Reverse) */
    case TM_RDR:	/* Read Data Reverse (not used by ITS) */
	tm_flsbuf(tm, 1);	/* Read in reverse direction */
	break;

    default:
	if (DVDEBUG(tm))
	    fprintf(DVDBF(tm), "[TM03 unknown scmd %#o]\r\n", cmd);
	/* Let unknown commands clear GO bit, to avoid wedging */
	break;
    }

    TMREG(tm, RHR_CSR) &= ~TM_1GO;	/* Turn off GO bit in CSR */
}


static void
tm_nxfn(register struct tmdev *tm)	/* Non-Executable Function */
                          
{
    TMREG(tm, RHR_ER1) |= TM_ENEF;	/* Non Executable function */
    TMREG(tm, RHR_STS) |= TM_SERR;	/* Error summary */
    tm_attn(tm);			/* Send attention interrupt */
}

/* Send special attention interrupt
**	Aside from errors it appears that this may be done whenever a spacing
**	operation (as opposed to I/O) finishes.
*/
static void
tm_attn(register struct tmdev *tm)
{
    TMREG(tm, RHR_STS) |= TM_SATA;
    TMREG(tm, RHR_ATTN) |= tm->tm_bit;		/* For our drive # */
    if (DVDEBUG(tm))
	fprintf(DVDBF(tm), "[tm_attn: ON]");
    (*tm->tm_dv.dv_attn)(&(tm->tm_dv), 1);	/* Assert ATTN */
}

/* Send slave status change and trigger interrupt */
static void
tm_ssint(register struct tmdev *tm)
{
    TMREG(tm, RHR_STS) |= TM_SSSC;	/* Slave status change */
    tm_attn(tm);
}

/* ATTN Checks?
	According to TM03 doc (p.4-44), ATTN is asserted under the
following conditions:

1. At completion of an erase, space, or write-TM operation.
2. Upon initiation of a rewind command.
3. Upon loading a 1 into GO bit of CSR while an error condition exists.
4. Upon termination of an operation during which an error occurred or SSC
	was asserted.
5. Upon termination of any operation during which END PT was asserted.
	(Not clear if this includes BOT in reverse direction).

*/

/* Read/Write data into memory using:
**	Currently selected slave (cs2, tc)
**	Frame count (fc)
** (20)	Controller count & phys addr
**
** (11)	Start addr in memory (ba)	Byte address (4 bytes per PDP10 wd)
** (11)	UBA mapping
**
** NEW REGIME:
**	Asynch I/O operation requires a certain amount of trickery.
** For WRITE operations, the entire record is first acquired via the
** controller and read into a byte array before being sent to the slave.
** If doing asynch, control returns at this point.  When the slave completes
** the record write, the event handler cleans up.
**
** For READ operations, the controller is first initialized (as a check for
** errors in setup), and the entire record is then read in from the slave
** (if doing asynch, control returns during this).  When the slave completes
** the record read, the event handler carries out the controller I/O, possibly
** doing so in reverse order.
*/
/* ITS notes:
**	ITS never writes (and cannot read) records of more than
**	1024. words.  It can however select 32-bit (industry-compatible) format
**	using high 4 8-bit bytes instead of 36-bit core-dump format, which
**	uses 5 frames per word.
**	ITS programs (ie DUMP) generally don't care about record boundaries.
**	They only note tape-marks (file EOFs).
*/

static int
tm_io(register struct tmdev *tm,
      int dirf)		/* +1 = Read Fwd, 0 = Write Fwd, -1 = Read Reverse */
{
    int blkcnt;
    int wrtf = (dirf == 0);	/* TRUE if writing */
    int revf = (dirf < 0);	/* TRUE if read reverse */

#if !KLH10_DEV_DPTM03
    register struct vmtape *t = &tm->tm_vmt;
#endif

    /* Start the tape going! */
    TMREG(tm, RHR_STS) &= ~(TM_SBOT|TM_SEOT|TM_STM);	/* No BOT, EOT, EOF */

    /* Now see if controller can set up transfer OK.
    ** Note that in real life, I/O might already be initiated to device
    ** before any controller problems are discovered!
    */

    /* Find # records controller wants us to xfer.  Better be 1 for tape! */
    blkcnt = (*tm->tm_dv.dv_iobeg)(&tm->tm_dv, wrtf);
    if (blkcnt != 1) {		/* If screwed up somehow, */
	if (DVDEBUG(tm))
	    fprintf(DVDBF(tm), "[tm_io: bad rec cnt (%o)]\r\n", blkcnt);
	if (blkcnt == 0) {
	    (*tm->tm_dv.dv_ioend)(&tm->tm_dv, 0);
	    return 0;
	}
    }

    /* Verify that data xfer has acceptable format */
    switch (tm->tm_sfmt) {
	case TM_FCD:
	case TM_FIC:
	    break;
	default:		/* Fail with a TM_EFMT error */
	    TMREG(tm, RHR_ER1) |= TM_EFMT;
	    TMREG(tm, RHR_STS) |= TM_SERR;
	    tm_attn(tm);
	    (*tm->tm_dv.dv_drerr)(&tm->tm_dv);	/* Stop channel xfer */
	    return 0;
    }

    if (wrtf) {
	/* Writing record (forward only) */
	if (!tm_filbuf(tm))		/* Fill up record buffer */
	    return 0;			/* Some error, return */
#if KLH10_DEV_DPTM03
	tm_dpcmd(tm, DPTM_WRT, tm->tm_bchs);	/* Tell slave to write! */
#else
	vmt_rput(t, tm->tm_buff, tm->tm_bchs);
#endif

    } else {
	/* Reading record (forward or reverse) */
#if KLH10_DEV_DPTM03
	tm_dpcmd(tm,
		(revf ? DPTM_RDR : DPTM_RDF),	/* Tell slave to read! */
		DPTM_MAXRECSIZ);
# if 0	/* Synch hack! */
	dp_xswait(&(tm->tm_dp.dp_adr->dpc_todp));
# endif
# else
	if (revf) {
	    /* Simulate read-reverse by spacing backward one record,
	       reading it in, then backing up again.
	       But no need to read in again if BOT or EOF.
	    */
	    if ((vmt_rspace(t, 1, 1))
		&& (vmt_framecnt(t) == 1)
		&& !vmt_isateof(t)
		&& !vmt_isatbot(t)
		&& !vmt_errors(t)) {
		long savcnt;

		vmt_rget(t, tm->tm_buff, DVTM_MAXRECSIZ);
		savcnt = vmt_framecnt(t);	/* Remember frames read */
		(void) vmt_rspace(t, 1, 1);	/* Back up, clobbers fc */
		vmt_framecnt(t) = savcnt;	/* Ugly hack to restore fc */
	    }
	} else {
	    vmt_rget(t, tm->tm_buff, DVTM_MAXRECSIZ);
	}
#endif /* !KLH10_DEV_DPTM03 */

    }
    return 1;			/* Proceed asynchronously */
}


static int
tm_filbuf(register struct tmdev *tm)
{
    register int wc;
    register unsigned char *buff = tm->tm_buff;
    register unsigned char * (*fmtfunct)(unsigned char *, vmptr_t, int);
    register long bwcnt, fcnt, totw;
    vmptr_t vp;

    /* Find format conversion routine to use.
    ** Note two things: (1) caller has already checked for validity, so
    ** paranoia check defaults to Core-Dump rather than barfing.
    ** (2) This calling style will not suffice for high-density format, which
    ** if implemented will need to know whether it's at start or middle of
    ** a double-word and thus must maintain external state (eg via a 4th arg)
    */
    switch (tm->tm_sfmt) {
	default:
	case TM_FCD:	fmtfunct = wdstofcd;	break;
	case TM_FIC:	fmtfunct = wdstofic;	break;
    }

    /* Find total # words want to xfer.  What we have is just a frame count,
    ** so the # of words depends on how data is being
    ** formatted (ie how many tape frames per word).
    ** Note that the frame cnt is negative; a zero value is interpreted as
    ** the maximum count, so there will always be at least one word to xfer.
    */
    fcnt = -(TMREG(tm, RHR_BAFC) | ~(long)MASK16);	/* Find # frames */
    bwcnt = fcnt / tm->tm_sfpw;		/* Find # whole words */
    if (fcnt % tm->tm_sfpw) {		/* See if partial word */
	++bwcnt;			/* Ugh, bump up */
	fprintf(DVDBF(tm),
		"[tm_filbuf: Frames not mod-word (%ld.)]\r\n", (long)fcnt);
    }
    if (DVDEBUG(tm))
	fprintf(DVDBF(tm), "[tm_filbuf: Write %ld frames]", fcnt);


    /* Get 10's data channel info - buffer pointer and count */
    wc = (*tm->tm_dv.dv_iobuf)(&tm->tm_dv, 0, &vp);

    /* WC has # words to xfer on first pass (may be 0 if initial setup
    ** failed).  VP will always be set cuz writing ("channel skip" uses a
    ** pattern of words, hence VP is never null).
    */
    totw = bwcnt;			/* Remember original count */
    while (wc && bwcnt) {

	if (wc > bwcnt)			/* Apply frame counter limit */
	    wc = bwcnt;

	if (DVDEBUG(tm))
	    fprintf(DVDBF(tm), "[tm_filbuf: %d %#lo]\r\n",
			wc, (long)(vp - vm_physmap(0)));

	buff = (*fmtfunct)(buff, vp, wc);	/* Convert words to bytes */

	if (DVDEBUG(tm) & DVDBF_DATSHO)
	    tm_showbuf(tm, buff - (wc * tm->tm_sfpw), vp, wc, 0);

	bwcnt -= wc;

	/* Update controller/datachannel's notion of transfer, and set up
	** for next pass.  Loop test will fail if WC set 0.
	*/
	wc = (*tm->tm_dv.dv_iobuf)(&tm->tm_dv, wc, &vp);
    }
    tm->tm_bchs = (totw - bwcnt) * tm->tm_sfpw;

    /* Xfer done; tm_bchs has total # of frames xferred and available.
    ** If this is more than fcnt, trim it down; support odd-size writes.
    */
    if (tm->tm_bchs > fcnt)
	tm->tm_bchs = fcnt;

    /* But if this is LESS than fcnt, there's a channel problem - ran out of
    ** data from channel too early.
    ** It's unclear how to handle this at the TM03 end.
    ** For the RH20:
    **	when phys I/O has completed, can tell wordcount was short by
    ** noticing that updating BAFC with tm_bchs doesn't make it zero.
    ** Then need to invoke dv_ioend with a blockcount arg of 1 to indicate
    ** there was still some stuff left the device wanted to do.
    */
    return 1;
}

/* TM_FLSBUF - Flush record buffer by copying it into 10's memory
*/
static int
tm_flsbuf(register struct tmdev *tm, int revf)
{
    register int wc;
    register long bwcnt, fcnt;
    register unsigned char *buff = tm->tm_buff;
    vmptr_t vp;

    /* Find frame count of record just gobbled, then derive total # words
    ** to xfer, if any.  This # depends on how data is being
    ** formatted (ie how many tape frames per word).
    ** Note number may be 0 if a tapemark, BOT/EOT, or error was seen.
    */
    tm->tm_bchs = TM03_FRMS(tm);	/* Get # frames in record */
    fcnt = tm->tm_bchs;			/* Find # frames in record */
    bwcnt = fcnt / tm->tm_sfpw;		/* Find # whole words */
    if (fcnt % tm->tm_sfpw) {		/* See if partial word */
	++bwcnt;			/* Ugh, bump up */
	memset(buff+fcnt, 0, 4);	/* Ensure leftover bytes clear */
	/* This assumes we're reading forward, but is harmless if it turns
	** out we're reading reverse, and should rarely happen anyway.
	** (See the fcdtowds() routine for more comments on this padding)
	*/
    }
    if (DVDEBUG(tm))
	fprintf(DVDBF(tm), "[tm_flsbuf: Read %ld frames]", fcnt);

    /* Report # frames in record, regardless of whether controller
    ** accepts the data.  (What else to do?)
    */
    TMREG(tm, RHR_BAFC) = fcnt & MASK16;	/* Report # frames read */

    /* Set up 10's buffer pointer and count */
    wc = (*tm->tm_dv.dv_iobuf)(&tm->tm_dv, 0, &vp);

    /* WC has # words to xfer on first pass (may be 0 if initial setup
    ** failed, or negative if channel is doing a reverse transfer.)
    ** If VP is set, then it points to phys mem to xfer to/from.
    */

    if (!revf && wc > 0) {
	register void (*fmtfunct)(vmptr_t, unsigned char *, int);

	/* Normal case, reading forward */

	/* Find format conversion routine to use.
	** Note two things: (1) caller has already checked for validity, so
	** paranoia check defaults to Core-Dump rather than barfing.
	** (2) This calling style will not suffice for high-density format,
	** which if implemented will need to know whether it's at start or
	**  middle of a double-word and thus must maintain external state
	** (eg via a 4th arg)
	*/
	switch (tm->tm_sfmt) {
	default:
	case TM_FCD:	fmtfunct = fcdtowds;	break;
	case TM_FIC:	fmtfunct = fictowds;	break;
	}

	for (; wc && bwcnt; ) {

	    if (wc > bwcnt)		/* Apply frame counter limit */
		wc = bwcnt;

	    if (DVDEBUG(tm))
		fprintf(DVDBF(tm), "[tm_flsbuf: %d %#lo]\r\n",
			wc, (vp ? (long)(vp - vm_physmap(0)) : 0L));

	    if (vp) {			/* VP may be NULL if skipping */
		(*fmtfunct)(vp, buff, wc);
		if (DVDEBUG(tm) & DVDBF_DATSHO)
		    tm_showbuf(tm, buff, vp, wc, 0);
	    }
	    buff += tm->tm_sfpw * wc;
	    bwcnt -= wc;

	    /* Update controller/datachannel's notion of transfer, and set up
	    ** for next pass.  Loop test will fail if WC set 0.
	    */
	    wc = (*tm->tm_dv.dv_iobuf)(&tm->tm_dv, wc, &vp);
	}

    } else if (wc) {
	register void (*revfmtfunct)(vmptr_t, unsigned char *, int, int);

	/* Ugh, doing some flavor of reverse read or reverse transfer.
	** Don't try to be super efficient here, do a little more testing
	** within the loops.
	*/
	switch (tm->tm_sfmt) {
	default:
	case TM_FCD:	revfmtfunct = revfcdtowds;	break;
	case TM_FIC:	revfmtfunct = revfictowds;	break;
	}
	if (wc < 0)		/* Invert count if reverse xfer */
	    bwcnt = -bwcnt;
	if (revf)		/* Start at end of buffer if read-reverse */
	    buff += fcnt;
	for (; wc && bwcnt; ) {
	    if ((wc > 0) ? (wc > bwcnt)		/* Apply frame counter limit */
			 : (wc < bwcnt)) {
		wc = bwcnt;
	    }
	    if (DVDEBUG(tm))
		fprintf(DVDBF(tm), "[tm_flsbuf: %d %#lo]\r\n",
			wc, (vp ? (long)(vp - vm_physmap(0)) : 0L));

	    if (vp) {			/* VP may be NULL if skipping */
		(*revfmtfunct)(vp, buff, wc, revf);
		if (DVDEBUG(tm) & DVDBF_DATSHO)
		    tm_showbuf(tm, buff, vp, wc, revf);
	    }
	    buff += (revf ? -tm->tm_sfpw : tm->tm_sfpw) * abs(wc);
	    bwcnt -= wc;
	    wc = (*tm->tm_dv.dv_iobuf)(&tm->tm_dv, wc, &vp);
	}
    }

    tm_ssta(tm);		/* Set slave status bits (TM, BOT, etc) */
    if (TM03_ERRS(tm)) {
	/* Set some kind of error bit here? */
	TMREG(tm, RHR_ER1) |= TM_EOPI;	/* What else to use? */
	TMREG(tm, RHR_STS) |= TM_SERR;
    }

    /* Now wrap up channel xfer, tell controller we're done */
    if (TMREG(tm, RHR_STS) & TM_SERR) {
	(*tm->tm_dv.dv_drerr)(&tm->tm_dv);
	tm_attn(tm);

    } else if (bwcnt) {
	/* Channel problem, ran out of space from channel too early. */
	(*tm->tm_dv.dv_ioend)(&tm->tm_dv, 1);	/* Say device wanted more */

    } else {
	/* Normal termination */
	(*tm->tm_dv.dv_ioend)(&tm->tm_dv, 0);	/* Win, all blks done */
    }

    return 1;
}

static void
tm_showbuf(register struct tmdev *tm,
	   register unsigned char *ucp,
	   register vmptr_t vp,
	   register int wc,
	   int revf)
{
    register w10_t w;
    register int fpw = tm->tm_sfpw;



    /* Back up appropriately if Read Reverse or reverse chan xfer */
    if (wc < 0) {
	wc = -wc;
	vp -= wc;
    }
    if (revf)
	ucp -= fpw * wc;

    for (; --wc >= 0; ++vp, ucp += fpw) {
	w = vm_pget(vp);
	fprintf(DVDBF(tm), "[TM03: %#lo/ %6lo,,%6lo   %3o %3o %3o %3o",
		(long)(vp - vm_physmap(0)), (long)LHGET(w), (long)RHGET(w),
		ucp[0], ucp[1], ucp[2], ucp[3]);
	if (fpw > 4)
	    fprintf(DVDBF(tm), " %3o", ucp[4]);
	fprintf(DVDBF(tm), "]\r\n");
    }
}

/* Copy words to Core-Dump record format
*/
static unsigned char *
wdstofcd(register unsigned char *ucp,
	 register vmptr_t vp,
	 register int wc)
{
    register w10_t w;

    for (; --wc >= 0; ++vp) {
	w = vm_pget(vp);
	*ucp++ = (LHGET(w)>>10) & 0377;
	*ucp++ = (LHGET(w)>> 2) & 0377;
	*ucp++ = ((LHGET(w)&03)<<6) | ((RHGET(w)>>12)&077);
	*ucp++ = (RHGET(w)>>4) & 0377;
	*ucp++ = RHGET(w) & 017;
    }
    return ucp;
}

/* Copy words to Industry-Compatible record format
*/
static unsigned char *
wdstofic(register unsigned char *ucp,
	 register vmptr_t vp,
	 register int wc)
{
    register w10_t w;

    for (; --wc >= 0; ++vp) {
	w = vm_pget(vp);
	*ucp++ = (LHGET(w)>>10) & 0377;
	*ucp++ = (LHGET(w)>> 2) & 0377;
	*ucp++ = ((LHGET(w)&03)<<6) | ((RHGET(w)>>12)&077);
	*ucp++ = (RHGET(w)>>4) & 0377;
	/* Ignore bottom 4 bits */
    }
    return ucp;
}

/* Copy Core-Dump record format to words
**	Note that there is no length check for the bytes; it is assumed
**	that there are always enough valid bytes to build an integral number
**	of words.
**	Since this is not always true for tape record lengths, there is code
**	in tm_flsbuf() that checks for odd lengths and ensures that there
**	are enough extra zero bytes to provide for a nice clean full word
**	of data at the end of a transfer.  4 extra bytes suffice because
**	currently 5-bytes-per-word is the largest valid format.
*/
static void
fcdtowds(register vmptr_t vp,
	 register unsigned char *ucp,
	 register int wc)
{
    register w10_t w;

    for (; --wc >= 0; ++vp, ucp += 5) {
	LRHSET(w,
		(((uint18)(ucp[0] & 0377) << 10)
		 | ((ucp[1] & 0377) << 2)
		 | ((ucp[2] >> 6) & 03)),
		(((uint18)(ucp[2] & 077) << 12)
		 | ((ucp[3] & 0377) << 4)
		 | (ucp[4] & 017))
		);
	vm_pset(vp, w);
    }
}

/* Copy Industry-Compatible record format to words
**	Same comments as for fcdtowds() above.
*/
static void
fictowds(register vmptr_t vp,
	 register unsigned char *ucp,
	 register int wc)
{
    register w10_t w;

    for (; --wc >= 0; ++vp, ucp += 4) {
	LRHSET(w,
		(((uint18)(ucp[0] & 0377) << 10)
		 | ((ucp[1] & 0377) << 2)
		 | ((ucp[2] >> 6) & 03)),
		(((uint18)(ucp[2] & 077) << 12)
		 | ((ucp[3] & 0377) << 4))
			/* No byte for bottom 4 bits */
		);
	vm_pset(vp, w);
    }
}

/* REVERSE Copy Core-Dump record format to words
**	The same issues regarding odd record lengths apply here as for
** fcdtowds().  However, for the read-reverse case, bytes are read out in
** REVERSE order.  To provide for the extra zero padding this requires
** at the start of the record, the dptm_revpad[] array exists in the DPTM
** structure and uses the assumption that the record buffer immediately
** follows this structure, and the bytes are cleared just after the call
** to dp_init in tm03_init.
*/
static void
revfcdtowds(register vmptr_t vp,
	    register unsigned char *ucp,
	    register int wc,	/* Negative if reverse chan xfer */
	    register int revf)	/* TRUE if Read-Reverse */
{
    register w10_t w;

    while (wc) {
	if (revf)
	    ucp -= 5;
	LRHSET(w,
		(((uint18)(ucp[0] & 0377) << 10)
		 | ((ucp[1] & 0377) << 2)
		 | ((ucp[2] >> 6) & 03)),
		(((uint18)(ucp[2] & 077) << 12)
		 | ((ucp[3] & 0377) << 4)
		 | (ucp[4] & 017))
		);
	if (!revf)
	    ucp += 5;
	vm_pset(vp, w);
	if (wc < 0)
	    --vp, ++wc;
	else
	    ++vp, --wc;
    }
}

/* REVERSE Copy Industry-Compatible record format to words
**	Same comments as for revfcdtowds() above.
*/
static void
revfictowds(register vmptr_t vp,
	    register unsigned char *ucp,
	    register int wc,	/* Negative if reverse chan xfer */
	    register int revf)	/* TRUE if Read-Reverse */
{
    register w10_t w;

    while (wc) {
	if (revf)
	    ucp -= 4;
	LRHSET(w,
		(((uint18)(ucp[0] & 0377) << 10)
		 | ((ucp[1] & 0377) << 2)
		 | ((ucp[2] >> 6) & 03)),
		(((uint18)(ucp[2] & 077) << 12)
		 | ((ucp[3] & 0377) << 4))
			/* No byte for bottom 4 bits */
		);
	if (!revf)
	    ucp += 4;
	vm_pset(vp, w);
	if (wc < 0)
	    --vp, ++wc;
	else
	    ++vp, --wc;
    }
}


/* Space forward or reverse by the # of records/tapemarks specified
** by the negative count in UB_TMFC (not WC!)
** Apparently, what stops the spacing is a transition to 0, not a transition
** to a positive state; at least one record/tapemark is always spaced.
**	The TM03 will abort if it encounters a tapemark; the read
** position will be past the tapemark, but the frame count will NOT
** include the tapemark; it only counts valid records.
**	Also, media errors are ignored while spacing; they do not cause
** an error.
**
** As an efficiency hack, we check for the very common case of TMFC==0,
** which means a maximum count and is ordinarily used when the PDP-10 OS
** is trying to do a space-to-file operation; the TM03 does not support
** this operation directly.  Fortunately, in this case no PDP-10 OS cares
** about the resulting frame count, so it doesn't matter if that is
** correct.  This allows us to be much faster when dealing with physical
** tape drives where the actual # of records skipped will not normally
** be available.
*/
static void
tm_space(register struct tmdev *tm, int revf)
{
    register uint18 cnt;

    /* Must ensure that frame count reg was set; FCS bit tells
    ** us whether a valid count exists.
    */
    if (!(TMREG(tm, RHR_OFTC)&TM_TFCS)) {
	if (DVDEBUG(tm))
	    fprintf(DVDBF(tm), "[tm_space: NEF - FCS=0]\r\n");
	tm_nxfn(tm);		/* Give error "Non-Executable Function" */
	return;
    }

    /* Get (negative 16-bit) count in positive form.  If FC was zero this
     * results in 0200000 (1<<16).
     */
    cnt = -(TMREG(tm, RHR_BAFC) | ~MASK16);
    if (cnt > MASK16) {
	/* Ugh, max count */
	cnt = 0;
	if (DVDEBUG(tm))
	    fprintf(DVDBF(tm), "[TM03  Space %s: 0 => File %s 1]\r\n",
		    (revf ? "Rev" : "Fwd"),
		    (revf ? "Rev" : "Fwd"));
    } else {
	if (DVDEBUG(tm))
	    fprintf(DVDBF(tm), "[TM03  Space %s: %ld]\r\n",
		    (revf ? "Rev" : "Fwd"), (long)cnt);
    }

    /* Tape motion initiated, change status bits */
    TMREG(tm, RHR_STS) &= ~(TM_SBOT|TM_STM|TM_SEOT);
#if KLH10_DEV_DPTM03
    TMREG(tm, RHR_STS) &= ~TM_SDRY;	/* Drive busy, note GO still on! */
    TMREG(tm, RHR_STS) |= TM_SPIP;	/* Positioning in progress */
    tm_dpcmd(tm,			/* Send spacing command to DP */
	     (cnt ? (revf ? DPTM_SPR : DPTM_SPF)
	          : (revf ? DPTM_SFR : DPTM_SFF)),
	     (cnt ? (size_t)cnt : (size_t)1));
#else
    if (cnt) {
	if (!vmt_rspace(&tm->tm_vmt, revf, (unsigned long)cnt)) {
	    /* Internal error */
	    tm_nxfn(tm);	
	}
    } else {
	if (!vmt_fspace(&tm->tm_vmt, revf, (unsigned long)1)) {
	    /* Internal error */
	    tm_nxfn(tm);	
	}
    }
#endif /* !KLH10_DEV_DPTM03 */
}

#endif /* KLH10_DEV_TM03 */
