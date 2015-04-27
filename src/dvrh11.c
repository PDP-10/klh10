/* DVRH11.C - Emulates RH11 controller for KS10
*/
/* $Id: dvrh11.c,v 2.4 2002/03/14 06:26:22 klh Exp $
*/
/*  Copyright © 1997, 2001 Kenneth L. Harrenstien
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
 * $Log: dvrh11.c,v $
 * Revision 2.4  2002/03/14 06:26:22  klh
 * Fixed rh11_bind to handle case of binding drive that's already been
 * selected by CS2, even if by default.
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include "klh10.h"

#if !KLH10_DEV_RH11 && CENV_SYS_DECOSF
	/* Stupid gubbish needed to prevent OSF/1 AXP compiler from
	** halting merely because compiled file is empty!
	*/
static int decosfcclossage;
#endif

#if KLH10_DEV_RH11		/* Moby conditional for entire file */

#include <stddef.h>	/* For size_t etc */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "kn10def.h"	/* This includes OSD defs */
#include "kn10dev.h"
#include "dvuba.h"
#include "dvrh11.h"
#include "dvrh20.h"	/* Temp: for RHR ext reg defs */
#include "dvrpxx.h"	/* Temp: for RH_DTNBA def */
#include "prmstr.h"	/* For parameter parsing */

#ifdef RCSID
 RCSID(dvrh11_c,"$Id: dvrh11.c,v 2.4 2002/03/14 06:26:22 klh Exp $")
#endif

struct rh11 {
    struct device rh_dv;	/* Generic 10 device structure */

    /* RH11-specific vars */

    int rh_no;			/* RH number - use UBA # here */

    /* RH11 internal registers */
    uint18 rh_cs1;	/* Only RH11 bits are set here, no drive bits */
    uint18 rh_cs2;
    uint18 rh_wc;
    uint18 rh_ba;

    /* Drive bindings & selection */

    int rh_ds;		/* Drive select (0-7) */
    struct device *rh_dsptr;

    int rh_attn;	/* ATTN summary for all drives */
    struct device *rh_drive[8];
    uint18 rh_dt[8];	/* Drive type for each known drive */

    /* Pseudo "Data Channel" vars */
    int     rh_dcwcnt;	/* DC current xfer word count */
    paddr_t rh_dcbuf;	/* DC current xfer data buffer pointer */
    int     rh_bcnt;	/* Initial block count */
    int	    rh_dcrev;	/* TRUE if doing reverse data xfer */
    int     rh_dcwrt;	/* TRUE if doing device write */

    w10_t   rh_dcccw;	/* DC current cmd word */
    int     rh_eptoff;	/* Offset of channel area in EPT */
    paddr_t rh_clp;	/* DC "PC" - Current Command List Pointer */
    uint18  rh_dcsts;	/* DC status flag bits (LH) */
    int	    rh_dchlt;	/* TRUE if halt when current wordcount done */

};

static int nrh11s = 0;
/* static */ struct rh11 dvrh11[RH11_NSUP];

#define RHDEBUG(rh) ((rh)->rh_dv.dv_debug)
#define RHDBF(rh)   ((rh)->rh_dv.dv_dbf)


/* Function predecls */

static int  rh11_conf(FILE *f, char *s, struct rh11 *rh);

/* Functions provided to device vector */

static int  rh11_bind(struct device *d, FILE *of, struct device *slv, int num);
static int  rh11_init(struct device *d, FILE *of);
static void rh11_reset(struct device *d);
static dvureg_t rh11_pivec(struct device *d);
static dvureg_t rh11_read(struct device *d, uint18 addr);
static void rh11_write(struct device *d, uint18 addr, dvureg_t val);

/* Functions provided to slave device */

static void rh11_attn(struct device *drv, int on);
static void rh11_drerr(struct device *drv);
static int  rh11_iobeg(struct device *drv, int wflg);
static int  rh11_iobuf(struct device *drv, int wc, vmptr_t *avp);
static void rh11_ioend(struct device *drv, int bc);

/* Completely internal functions */

static void rh_picheck(struct rh11 *rh);
static void rh_pi(struct rh11 *rh);
static void rh_clrattn(struct rh11 *rh, int msk);
static void rh_clear(struct rh11 *rh);

static void rhdc_clear(struct rh11 *rh);
static int  rhdc_ccwget(struct rh11 *rh);

/* Configuration Parameters */

#define DVRH11_PARAMS \
    prmdef(RH11P_DBG, "debug"),	/* Initial debug value */\
    prmdef(RH11P_BR,  "br"),	/* BR priority */\
    prmdef(RH11P_VEC, "vec"),	/* Interrupt vector */\
    prmdef(RH11P_ADDR,"addr")	/* Unibus address */

enum {
# define prmdef(i,s) i
	DVRH11_PARAMS
# undef prmdef
};

static char *rh11prmtab[] = {
# define prmdef(i,s) s
	DVRH11_PARAMS
# undef prmdef
	, NULL
};

/* RH11_CONF - Parse configuration string and set defaults.
**	At this point, device has just been created, but not yet bound
**	or initialized.
** NOTE that some strings are dynamically allocated!  Someday may want
** to clean them up nicely if config fails or device is uncreated.
*/
static int
rh11_conf(FILE *f, char *s, struct rh11 *rh)
{
    int i, ret = TRUE;
    struct prmstate_s prm;
    char buff[200];
    long lval;

    /* First set defaults for all configurable parameters
	Unfortunately there's currently no way to access the UBA # that
	we're gonna be bound to, otherwise could set up defaults.  Later
	fix this by giving rh11_create() a ptr to an arg structure, etc.
     */
    DVDEBUG(rh) = FALSE;

    prm_init(&prm, buff, sizeof(buff),
		s, strlen(s),
		rh11prmtab, sizeof(rh11prmtab[0]));
    while ((i = prm_next(&prm)) != PRMK_DONE) {
	switch (i) {
	case PRMK_NONE:
	    fprintf(f, "Unknown RH11 parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;
	case PRMK_AMBI:
	    fprintf(f, "Ambiguous RH11 parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;
	default:	/* Handle matches not supported */
	    fprintf(f, "Unsupported RH11 parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;

	case RH11P_DBG:		/* Parse as true/false boolean or number */
	    if (!prm.prm_val)	/* No arg => default to 1 */
		DVDEBUG(rh) = 1;
	    else if (!s_tobool(prm.prm_val, &DVDEBUG(rh)))
		break;
	    continue;

	case RH11P_BR:		/* Parse as octal number */
	    if (!prm.prm_val || !s_tonum(prm.prm_val, &lval))
		break;
	    if (lval < 4 || lval > 7) {
		fprintf(f, "RH11 BR must be one of 4,5,6,7\n");
		ret = FALSE;
	    } else
		rh->rh_dv.dv_brlev = lval;
	    continue;

	case RH11P_VEC:		/* Parse as octal number */
	    if (!prm.prm_val || !s_tonum(prm.prm_val, &lval))
		break;
	    if (lval < 4 || lval > 0400 || (lval&03)) {
		fprintf(f, "RH11 VEC must be valid multiple of 4\n");
		ret = FALSE;
	    } else
		rh->rh_dv.dv_brvec = lval;
	    continue;

	case RH11P_ADDR:	/* Parse as octal number */
	    if (!prm.prm_val || !s_tonum(prm.prm_val, &lval))
		break;
	    if (lval < (RH11R_N<<1) || (lval&037)) {
		fprintf(f, "RH11 ADDR must be valid Unibus address\n");
		ret = FALSE;
	    } else
		rh->rh_dv.dv_addr = lval;
	    continue;

	}
	ret = FALSE;
	fprintf(f, "RH11 param \"%s\": ", prm.prm_name);
	if (prm.prm_val)
	    fprintf(f, "bad value syntax: \"%s\"\n", prm.prm_val);
	else
	    fprintf(f, "missing value\n");
    }

    /* Param string all done, do followup checks or cleanup */
    if (!rh->rh_dv.dv_brlev || !rh->rh_dv.dv_brvec || !rh->rh_dv.dv_addr) {
	fprintf(f, "RH11 missing one of BR, VEC, ADDR params\n");
	ret = FALSE;
    }
    /* Set 1st invalid addr */
    rh->rh_dv.dv_aend = rh->rh_dv.dv_addr + (RH11R_N * 2);

    return ret;
}

/* RH11 interface routines to KLH10 */

struct device *
dvrh11_create(FILE *f, char *s)
{
    register struct rh11 *rh;

    /* Allocate a RH11 device structure */
    if (nrh11s >= RH11_NSUP) {
	fprintf(f, "Too many RH11s, max: %d\n", RH11_NSUP);
	return NULL;
    }
    rh = &dvrh11[nrh11s++];		/* Pick unused RH11 */

    /* Various initialization stuff */
    memset((char *)rh, 0, sizeof(*rh));

    iodv_setnull(&rh->rh_dv);		/* Init as null device */

    rh->rh_dv.dv_dflags = DVFL_CTLR;
    rh->rh_dv.dv_bind = rh11_bind;	/* Controller, so can bind. */
    rh->rh_dv.dv_init = rh11_init;	/* Set up own post-bind init */
    rh->rh_dv.dv_reset = rh11_reset;	/* System reset (clear stuff) */

    /* Unibus stuff */
    rh->rh_dv.dv_pivec = rh11_pivec;	/* Return PI vector */
    rh->rh_dv.dv_read  = rh11_read;	/* Read unibus register */
    rh->rh_dv.dv_write = rh11_write;	/* Write unibus register */

    /* Configure from parsed string and remember for init
    */
    if (!rh11_conf(f, s, rh))
	return NULL;

    return &rh->rh_dv;
}

static int
rh11_init(struct device *d, FILE *of)
{
    register struct rh11 *rh = (struct rh11 *)d;

    /* Clear RH-internal registers */
    rh->rh_cs1 = 0;
    rh->rh_cs2 = 0;
    rh->rh_wc = 0;
    rh->rh_ba = 0;
    rh->rh_attn = 0;		/* Clear ATTN summary */
    rh->rh_dsptr = NULL;	/* No drive */

    rh->rh_no = rh->rh_dv.dv_uba->ubn;	/* Find Unibus # we're on */

    return TRUE;
}

static int
rh11_bind(register struct device *d,
	  FILE *of,
	  register struct device *slv,
	  int num)
{
    register struct rh11 *rh = (struct rh11 *)d;

    if (0 <= num && num < 8) ;
    else {
	if (of) fprintf(of, "RH11 can only bind up to 8 drives (#%d invalid).\n", num);
	return FALSE;
    }

    /* Backpointer and device # (dv_ctlr, dv_num) are already set up. */
    slv->dv_attn  = rh11_attn;	/* Set attention handler */
    slv->dv_drerr = rh11_drerr;	/* Set exception handler */
    slv->dv_iobeg = rh11_iobeg;	/* IO xfer beg */
    slv->dv_iobuf = rh11_iobuf;	/* IO xfer buffer setup */
    slv->dv_ioend = rh11_ioend;	/* IO xfer end */

    rh->rh_drive[num] = slv;
    rh->rh_dt[num] = 0;		/* Drive not yet inited, so can't ask it */

    /* Check to see if this drive is already being selected by CS2.
     * If so, must set it up now, or will get spurious RAE's if PDP-10
     * tries to read regs before it sets CS2 again.
     */
    if (rh->rh_ds == num) {
	rh->rh_dsptr = slv;	/* Yes, set it up! */
	rh->rh_cs2 &= ~RH_YNED;	/* Turn off non-ex bit */
    }

    return TRUE;
}

/* RH11_RESET - Clear controller and drives
**	Also does equivalent of asserting Massbus INIT, by invoking
**	the reset routine for all drives.
*/
static void
rh11_reset(struct device *d)
{
    register struct rh11 *rh = (struct rh11 *)d;
    register int i;

    rh_clear(rh);		/* Clear RH-only controller stuff */

    /* Clear all drives for this controller */
    for (i = 0; i < 8; ++i)
	if (rh->rh_drive[i])
	    (*(rh->rh_drive[i]->dv_reset))(rh->rh_drive[i]);
}

/* PI: Return interrupt vector */

static dvureg_t
rh11_pivec(register struct device *d)
{
    (*d->dv_pifun)(d, 0);	/* Turn off interrupt request */
    return d->dv_brvec;		/* Return vector to use */
}


/* Table for mapping RH11 registers into RH20-drive registers.
** Note certain RH regs are mapped into special internal-reg numbers.
** Also note that one RH11 reg address (BUF) has no counterpart and is mapped
** into a no-op read-only reg.
*/
#define RHRX_WC  RHR_N+1
#define RHRX_BA  RHR_N+2
#define RHRX_CS2 RHR_N+3

static int rh11regmap[RH11R_N] = {
	RHR_CSR,	/*   0 RH11R_CS1 - (RH/DR) CTRL AND STATUS 1. */
	RHRX_WC,	/*   1 RH11R_WC  - (RH) WORD COUNT. */
	RHRX_BA,	/*   2 RH11R_BA  - (RH) UNIBUS ADDRESS. */
	RHR_BAFC,	/*   3 RH11R_ADR - (DR) DESIRED ADDRESS. */
	RHRX_CS2,	/*   4 RH11R_CS2 - (RH) CTRL AND STATUS 2. */
	RHR_STS,	/*   5 RH11R_STS - (DR) DRIVE STATUS. */
	RHR_ER1,	/*   6 RH11R_ER1 - (DR) ERROR 1. */
	RHR_ATTN,	/*   7 RH11R_ATN - (DR*) ATTENTION SUMMARY. */
	RHR_LAH,	/* 010 RH11R_LAH - (DR) LOOK AHEAD. */
	RHR_SN,		/* 011 RH11R_BUF - (DR) DATA BUFFER. */
	RHR_MNT,	/* 012 RH11R_MNT - (DR) MAINTENANCE. */
	RHR_DT,		/* 013 RH11R_TYP - (DR) DRIVE TYPE. */
	RHR_SN,		/* 014 RH11R_SER - (DR) SERIAL NUMBER. */
	RHR_OFTC,	/* 015 RH11R_OFS - (DR) OFFSET. */
	RHR_DCY,	/* 016 RH11R_CYL - (DR) DESIRED CYLINDER. */
	RHR_CCY,	/* 017 RH11R_CCY - (DR) CURRENT CYLINDER  */
	RHR_ER2,	/* 020 RH11R_ER2 - (DR) ERROR 2 */
	RHR_ER3,	/* 021 RH11R_ER3 - (DR) ERROR 3 */
	RHR_EPOS,	/* 022 RH11R_POS - (DR) ECC POSITION. */
	RHR_EPAT	/* 023 RH11R_PAT - (DR) ECC PATTERN. */
};

static char *rh11regnam[RH11R_N] = {
	"CS1",		/*   0 RH11R_CS1 - (RH/DR) CTRL AND STATUS 1. */
	"WC",		/*   1 RH11R_WC  - (RH) WORD COUNT. */
	"BA",		/*   2 RH11R_BA  - (RH) UNIBUS ADDRESS. */
	"ADR",		/*   3 RH11R_ADR - (DR) DESIRED ADDRESS. */
	"CS2",		/*   4 RH11R_CS2 - (RH) CTRL AND STATUS 2. */
	"STS",		/*   5 RH11R_STS - (DR) DRIVE STATUS. */
	"ER1",		/*   6 RH11R_ER1 - (DR) ERROR 1. */
	"ATN",		/*   7 RH11R_ATN - (DR*) ATTENTION SUMMARY. */
	"LAH",		/* 010 RH11R_LAH - (DR) LOOK AHEAD. */
	"BUF",		/* 011 RH11R_BUF - (DR) DATA BUFFER. */
	"MNT",		/* 012 RH11R_MNT - (DR) MAINTENANCE. */
	"TYP",		/* 013 RH11R_TYP - (DR) DRIVE TYPE. */
	"SER",		/* 014 RH11R_SER - (DR) SERIAL NUMBER. */
	"OFS",		/* 015 RH11R_OFS - (DR) OFFSET. */
	"CYL",		/* 016 RH11R_CYL - (DR) DESIRED CYLINDER. */
	"CCY",		/* 017 RH11R_CCY - (DR) CURRENT CYLINDER  */
	"ER2",		/* 020 RH11R_ER2 - (DR) ERROR 2 */
	"ER3",		/* 021 RH11R_ER3 - (DR) ERROR 3 */
	"POS",		/* 022 RH11R_POS - (DR) ECC POSITION. */
	"PAT"		/* 023 RH11R_PAT - (DR) ECC PATTERN. */
};


static dvureg_t
rh11_read(struct device *d, register uint18 addr)
{
    register struct rh11 *rh = (struct rh11 *)d;
    register int reg;
    register uint32 dregval;
    register dvureg_t val;

    reg = (addr - rh->rh_dv.dv_addr) >> 1;
    if (reg < 0 || reg >= RH11R_N) {
	/* In theory ought to generate illegal IO register page-fail here,
	    but this should never happen - all addresses for this device
	    are being handled.
	 */
	panic("rh11_read: Unknown register %o", addr);
    }

    switch (reg) {

	/* Controller register stuff */
    case RH11R_CS2: val = rh->rh_cs2;	break;	/* CTRL AND STATUS 2 */
    case RH11R_WC:  val = rh->rh_wc;	break;	/* WORD COUNT */
    case RH11R_BA:  val = rh->rh_ba;	break;	/* UNIBUS ADDRESS */
    case RH11R_ATN: val = rh->rh_attn;	break;	/* ATTENTION SUMMARY */

    case RH11R_CS1:	/* CTRL AND STATUS 1 - special combo of RH and drive */
	/* Special handling is required for this one as it combines
	    both the RH and a selected drive.  If no drive is selected,
	    then this access attempt must cause a Massbus Parity Error!
	*/
	if (!rh->rh_dsptr) {
	    rh->rh_cs2 |= RH_YNED;	/* Non-existent drive - set on ref */
	    rh->rh_cs1 |= RH_XMCP;	/* Shd we trigger PI?  For now, no */
	    val = rh->rh_cs1;
	    break;
	}

	if ((MASK16 < (dregval =
		(*rh->rh_dsptr->dv_rdreg)(rh->rh_dsptr, RHR_CSR)))) {
	    dregval = 0;
	    rh->rh_cs1 |= RH_XMCP;	/* Shd we trigger PI?  For now, no */
	}
	val = (rh->rh_cs1 | (dregval & RH_CS1_DRIVE));
	break;

    default:
	/* Get value from drive. */
	if (rh->rh_dsptr && (MASK16 >= (dregval =
		(*rh->rh_dsptr->dv_rdreg)(rh->rh_dsptr, rh11regmap[reg])))) {
	    val = dregval;
	    break;
	}
	/* Non-existent drive or drive register access error.
	** Unclear what should be done for this case; IO page fail?
	*/
	if (RHDEBUG(rh)) {
	    fprintf(RHDBF(rh), "[rh11_read: %o.%o RAE for %s: ",
			    rh->rh_no, rh->rh_ds, rh11regnam[reg]);
	    if (!rh->rh_dsptr)
		fprintf(RHDBF(rh), "no drive]\r\n");
	    else
		fprintf(RHDBF(rh), "%lo]\r\n", (long)dregval);
	}
	rh->rh_cs1 |= RH_XMCP;	/* Shd we trigger PI?  For now, no */
	return 0;
    }

    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "[rh11_read: %o.%o %s: %lo]\r\n",
			rh->rh_no, rh->rh_ds, rh11regnam[reg], (long)val);
    return val;
}

/* Note from T20:

THE RH11 REQUIRES THAT A CLEAR INSTRUCTION (setting RH_YCLR in CS2) BE
FOLLOWED IMMEDIATELY BY A SELECTION OF AN EXISTANT UNIT NUMBER
OTHERWISE ANY ATTEMPT TO ACCESS A REGISTER THAT IS NOT IN THE RH (CS1)
WILL CAUSE MASSBUS PARITY ERRORS.

- Apparently CS2 CLEAR cannot simultaneously do a drive select; it causes no
	drive to be selected, and another CS2 write without the clear bit
	is required.
- If no drive is selected (or an invalid one is picked) apparently RH_YNED
	is not set until an actual attempt is made to access (read or write)
	a drive register (of which CS1 counts as one!).

- In ER1, T20 only checks RH_1PAR (Control Bus Parity Error) and
	RH_1AOE (Address Overflow / Frame Count Error)

*/

static void
rh11_write(struct device *d, uint18 addr, register dvureg_t val)
{
    register struct rh11 *rh = (struct rh11 *)d;
    register int reg;

    reg = (addr - rh->rh_dv.dv_addr) >> 1;
    if (reg < 0 || reg >= RH11R_N) {
	/* In theory ought to generate illegal IO register page-fail here,
	    but this should never happen - all addresses for this device
	    are being handled.
	 */
	panic("rh11_write: Unknown register %o", addr);
    }

    val &= MASK16;
    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "[rh11_write: %o.%o %s: %lo]\r\n",
			rh->rh_no, rh->rh_ds, rh11regnam[reg], (long)val);
    switch (reg) {

    case RH11R_CS1:	/* R/W      CS1 CS1 Control/command */
	/* Set RH bits first */
	if (val & RH_XTRE)		/* If turning off xfer err bit */
	    rh->rh_cs1 &= ~RH_XTRE;	/* do so. */
	rh->rh_cs1 &= ~(RH_XA17|RH_XA16|RH_XIE);	/* Bits can set */
	rh->rh_cs1 |= (val & (RH_XA17|RH_XA16|RH_XIE)); /* Set em */

	/* Now set drive bits */
	if (!rh->rh_dsptr) {
	    rh->rh_cs2 |= RH_YNED;	/* Set Non-Existent Drive */
	} else {
	    /* Give command to drive! */
	    if ((*rh->rh_dsptr->dv_wrreg)(rh->rh_dsptr,
				RHR_CSR, (int)(val & RH_XCMD))) {
		return;			/* Success, all done here */
	    }
	    goto wregerr;	/* Ugh, report error */
	}
	break;		/* Go set RH_XMCP Control Bus Parity Error */

    case RH11R_CS2:	/* CTRL AND STATUS 2 */
    {
	register int drvno = val & RH_YDSK;

	if (val & RH_YCLR)
	    rh_clear(rh);	/* Clear controller (not drive) */
				/* Also turns off any PI request! */

	rh->rh_cs2 = ((rh->rh_cs2)&~RH_YDSK) | drvno;
	rh->rh_ds = drvno;
	if (rh->rh_dsptr = rh->rh_drive[drvno]) {
	    rh->rh_cs2 &= ~RH_YNED;	/* Drive exists */
	} else {
	    rh->rh_cs2 |= RH_YNED;	/* Non-ex drive */
	}
	return;
    }

    case RH11R_WC:	/* WORD COUNT */
	rh->rh_wc = val;
	return;

    case RH11R_BA:	/* UNIBUS ADDRESS */
	rh->rh_ba = val;
	return;

    case RH11R_ATN:	/* R/W [I2] ATN AS  Attention Summary */
	rh_clrattn(rh, (int)val);	/* Turn off ATTN for all bits set */
	return;

    default:		/* All other registers are given to drive */
	if (rh->rh_dsptr &&
	    (*rh->rh_dsptr->dv_wrreg)(rh->rh_dsptr, rh11regmap[reg], (int)val))
	    return;		/* External reg write succeeded */
	/* Error drops through */

    wregerr:
	if (RHDEBUG(rh)) {
	    fprintf(RHDBF(rh), "[rh11_write: %o.%o RAE for %s: ",
			    rh->rh_no, rh->rh_ds, rh11regnam[reg]);
	    if (!rh->rh_dsptr)
		fprintf(RHDBF(rh), " (no drive)]");
	    else
		fprintf(RHDBF(rh), "]");
	}
	break;
    }


    /* External reg write failed, set RH_XMCP and maybe do PI. */
    rh->rh_cs1 |= RH_XMCP;		/* Set Control Bus Parity Error */
    if (rh->rh_cs1 & RH_XIE) {
	rh_pi(rh);			/* Trigger PI */
    }
}

/* RH11 internal routines (internal registers etc) */

/* RH_CLEAR - Clear controller only (not drives)
*/
static void
rh_clear(register struct rh11 *rh)
{
    if (rh->rh_dv.dv_pireq)		/* If PI request outstanding, */
	(*rh->rh_dv.dv_pifun)(&rh->rh_dv, 0);	/* clear it. */

    /* Stop and flush any in-progress xfer? */

    /* Need to figure out just which bits get cleared; for now, all RH bits */
    rh->rh_cs1 = RH_XRDY;	/* CTRL AND STATUS 1 */
    rh->rh_cs2 = 0;		/* CTRL AND STATUS 2 */
    rh->rh_wc = 0;
    rh->rh_ba = 0;

    rh->rh_dsptr = NULL;	/* No drive selected */
    rh->rh_ds = 0;
}


/* RH_PICHECK - Check RH11 conditions to see if PI should be attempted.
**	Possible problem: RH11 has no "done" flag, so using this routine
**	may not be a good idea as it could turn off the PI for command-done.
**	RH_XSC is not the equivalent as it is only set for ATTN or error,
**	not for transfer-done.
*/
static void
rh_picheck(register struct rh11 *rh)
{
    /* If any possible interrupt bits are set */
    if (rh->rh_cs1 & (RH_XSC | RH_XTRE | RH_XMCP)) {
	rh_pi(rh);
	return;
    }
    /* Here, shouldn't be requesting PI, so if our request bit is set,
    ** turn it off.
    */
    if (rh->rh_dv.dv_pireq) {		/* If set while shouldn't be, */
	(*rh->rh_dv.dv_pifun)(&rh->rh_dv, 0);	/* Clear it! */
    }
}


/* RH11_PI - trigger PI for selected RH11.
**	This could perhaps be an inline macro, but for now
**	having it as a function helps debug.
*/
static void
rh_pi(register struct rh11 *rh)
{
    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "[rh11_pi: %o]", rh->rh_dv.dv_brlev);

    if (rh->rh_dv.dv_brlev		/* If have non-zero PIA */
      && !(rh->rh_dv.dv_pireq)) {	/* and not already asking for PI */
	(*rh->rh_dv.dv_pifun)(&rh->rh_dv, rh->rh_dv.dv_brlev);	/* then do it! */
    }
}

/* RH_CLRATTN - Clear ATTN bit for all masked drives.
**	Forces call even if it seems unnecessary, just to be safe.
*/
static void
rh_clrattn(register struct rh11 *rh, int msk)
{
    register int i;
    register struct device *drv;

    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "[rh11_clrattn: %o]", msk);
    msk &= 0377;		/* Allow only 8 bits for 8 drives */
    for (i = 0; msk; ++i, msk >>= 1) {
	if ((msk & 01) && (drv = rh->rh_drive[i]))
	    (*drv->dv_wrreg)(drv, RHR_ATTN, 1);	/* Tell drv to clr its ATTN */
    }
}

/* The following rh11_ functions are all called by the drive via
** the controller/drive vectors.
*/

/* RH11_ATTN - Called by drive to assert or clear its attention bit.
**	Note arg is pointer to drive, not controller.
*/
static void
rh11_attn(register struct device *drv, int on)
{
    register struct rh11 *rh = (struct rh11 *)(drv->dv_ctlr);
    register int rbit = (1 << drv->dv_num);	/* Attention bit to use */

    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "[rh11_attn: bit %o %s]",
			rbit, on ? "on" : "off");

    /* If turning ATN bit on, always interrupt if enabled, even if ATN
	was already on.  This is because it could have been previously
	turned on while RH_XIE was off, and thus is not a reliable sign
	of whether an interrupt is already pending.
    */
    if (on) {
	rh->rh_attn |= rbit;		/* Add new attention bit! */
	rh->rh_cs1 |= RH_XSC;		/* Assert Special Condition */
	if (rh->rh_cs1 & RH_XIE)	/* If OK for attn to int */
	    rh_pi(rh);		/* then try to trigger PI! */
    } else {
	if (rh->rh_attn & rbit) {
	    rh->rh_attn &= ~rbit;	/* Turn off attention bit */
	    if (!rh->rh_attn		/* If no longer have any ATTNs */
	      && (rh->rh_cs1 & RH_XSC)) {	/* ensure Spec Cond is off */
		/* Changing Spec Cond state... may have to clean up PI */
		rh->rh_cs1 &= ~RH_XSC;		/* ensure Spec Cond is off */
		if (rh->rh_dv.dv_pireq)		/* If had PI request, */
		    rh_picheck(rh);		/* Sigh, must re-check stuff */
	    }
	}
    }
}


/* RH11_DRERR - Called by drive to assert an exception error during an
**	I/O transfer.
*/
static void
rh11_drerr(register struct device *drv)
{
    register struct rh11 *rh = (struct rh11 *)(drv->dv_ctlr);

    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "[rh11_drerr]");

    if (!(rh->rh_cs1 & RH_XTRE)) {
	rh->rh_cs1 |= RH_XTRE;		/* Assert Drive Transfer Error */
	rh_pi(rh);			/* Then try to trigger PI */
    }

    /* If I/O transfer in progress, abort it gracefully */
    if (1)
	rh11_ioend(drv, 1);	/* Assume always have a block left */
}


/* RH11_IOBEG - Called by drive to set up data channel just prior to
**	starting an I/O xfer.
** Returns 0 if failure; drive should stop immediately and not
**	invoke rh11_iobuf, but must still call rh11_ioend.
** Else returns # of block units (sectors or records) to do I/O for.
*/
static int
rh11_iobeg(struct device *drv, int wflg)
{
    register struct rh11 *rh = (struct rh11 *)(drv->dv_ctlr);
    register int nblks;
    register int18 wc;

    rh->rh_dcwrt = wflg;		/* Remember if writing */

    /* Paranoia - consistency check */
    if (rh->rh_ds != drv->dv_num) {
	panic("rh11_iobeg: drv/ctrlr mismatch: %d/%d",
					rh->rh_ds, drv->dv_num);
    }

    /* HACK HACK HACK - need to know drive type before we know whether
	to return a meaningful block count.  Block-addressed drives
	are assumed to have 128 words per block (sector); later this
	needs to be fixed up with better drive/controller communication.
    */
    if (rh->rh_dt[drv->dv_num] == 0) {
	rh->rh_dt[drv->dv_num] = (*drv->dv_rdreg)(drv, RHR_DT);
    }

    /* Find word count in PDP10 words (rounds down) */
    wc = (-(rh->rh_wc | ~MASK16))>>1;		/* Find # of PDP10 words */
    if (wc == 0)
	nblks = 0;
    else if (rh->rh_dt[drv->dv_num] & RH_DTNBA)
	nblks = 1;				/* Not a block device */
    else
	nblks = (wc + 0177) >> 7;		/* 128-wd block (sector) */

    /* Do mapping and set up internal vars from the xfer registers */
    if (!rhdc_ccwget(rh)) {
	rh->rh_cs1 |= RH_XTRE;		/* Some kind of xfer error */
	rh_pi(rh);			/* Try to trigger PI */
	return 0;			/* Tell drive we failed */
    }

    rh->rh_cs1 &= ~RH_XRDY;		/* Channel active now! */
    return rh->rh_bcnt = nblks;
}


/* RH11_IOBUF - Called by drive to query data channel to find source
**	or dest buffer for I/O xfer.  Can be called repeatedly for one xfer.
**	Caller must provide # of words used from the last call (0 if none).
**	In particular, final transfer MUST invoke this to tell the channel
**	how many words were actually used.
**
**	If returns 0, no data or space left.
**	If returns +, OK, *avp NULL if skipping, else vmptr to mem.
**	If returns -, wants to do reverse transfer.
**		Not clear whether buffer pointer points to LAST word of
**		block to write, or ONE PAST the last word.  Assuming
**		the former, based on DEC DC doc (p. DATA/2-7) desc of address.
*/
static int
rh11_iobuf(struct device *drv,
	   register int wc,	/* Max 11 bits of word count, so int OK */
	   vmptr_t *avp)
{
    register struct rh11 *rh = (struct rh11 *)(drv->dv_ctlr);

    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "[rh11_iobuf: %d. => ", wc);

    /* If drive is updating our IO xfer status, do it. */
    if (wc) {
	register uint18 ba;

	/* Special hacking needed for BA to update extension bits.
	** Code knows that A7 and A6 are contiguous.
	*/
	ba = rh->rh_ba + (wc << 2);	/* Get updated BA value (byte addr) */
	if (ba > MASK16) {		/* Overflowed 16 bits? */
	    /* Add overflow bit in-place into extension bits */
	    rh->rh_cs1 = (rh->rh_cs1 & ~(RH_XA17|RH_XA16))
			| ((rh->rh_cs1 + RH_XA16) & (RH_XA17|RH_XA16));
	    ba &= MASK16;
	}
	rh->rh_ba = ba;

	if (wc < 0) {			/* Verify direction consistent */
	    if (!rh->rh_dcrev)
		panic("rh11_iobuf: Neg wc for fwd xfer!");
	    wc = -wc;			/* Make positive */
	} else if (rh->rh_dcrev)
		panic("rh11_iobuf: Pos wc for rev xfer!");
	if (wc > rh->rh_dcwcnt)		/* Verify not too big */
	    panic("rh11_iobuf: drive overran chan!");

	rh->rh_wc = (rh->rh_wc + (wc << 1)) & MASK16;	/* Add to 11-wd cnt */

	if (!rhdc_ccwget(rh)) {		/* Do mapping, set up our vars */
	    if (RHDEBUG(rh))
		fprintf(RHDBF(rh), "failed]");
	    return 0;			/* Map error! */
	}      
    }

    /* See if word count ran out */
    if (rh->rh_wc == 0) {
	/* Yep, RH11 has no real DC so all done now! */
	if (RHDEBUG(rh))
	    fprintf(RHDBF(rh), "0]\r\n");
	return 0;		/* Done, or no more */
    }

    /* Here, we still have a positive count, so return that. */
    wc = rh->rh_dcwcnt;		/* Set up by ccwget */

    /* Also return addr of buffer.  RH11 has no DC so no skip/fill hackery */
    *avp = vm_physmap(rh->rh_dcbuf);
    if (rh->rh_dcrev)		/* If reverse xfer, negate count */
	wc = -wc;

    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "%d. (%lo)]\r\n", wc, (long)(rh->rh_dcbuf));

    return wc;
}



/* RH11_IOEND - Called by drive when I/O finished, terminate data channel
**	I/O xfer.  Assumes that rh11_iobuf has been called to update
**	status after each transfer or sub-transfer, so channel word count
**	is accurate reflection of data xfer.
** State may be one of:
**	Error - either in drive, or from data channel.  Assumption is
**		that error has set all appropriate error bits (specifically
**		Channel Error if channel status must be stored), and 
**		triggered PI as well.
**
**	Drive stopped early due to no more DC buffer space.  Indicated by
**		non-zero returned block count.
**	Drive stopped normally, block count 0.
**
**	Checks to see whether to set Short/Long WC Error.
**	Stores channel status if requested by PTCR bit RH11DO_SES.
**
**	May initiate next RH11 data transfer.
*/
static void
rh11_ioend(register struct device *drv,
	   int bc)		/* Drive's final block count */
{
    register struct rh11 *rh = (struct rh11 *)(drv->dv_ctlr);

    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "[rh11_ioend: %d.]\r\n", bc);


    /* Update final IO xfer status */
    if (bc || rh->rh_dcwcnt) {
	/* Drive still has stuff it wanted to do.
	** Set channel Short WC if WC==0, or Long WC if WC != 0,
	** and RH Chan Err.
	*/
#if 0	/* Apparently for RH11 nothing happens in either case? */
	rh->rh_dcsts |= (rh->rh_dcwcnt ? CSW_LWCE : CSW_SWCE);
	rh->rh_cond |= RH11CI_CE;
#endif
    }

    /* Now say command done, whatever happened. */
    rh->rh_cs1 |= RH_XRDY;	/* Done, channel ready */
    rh_pi(rh);			/* Attempt triggering PI */
}

/* RH11 "Data Channel" routines.
**
*/

/* For completeness?  Nothing calls this now */
static void
rhdc_clear(register struct rh11 *rh)
{
    rh->rh_dcwcnt = 0;
    rh->rh_dcbuf = 0;
}

/* Set up vars for next drive-to-memory transfer
*/
static int
rhdc_ccwget(register struct rh11 *rh)
{
    register int wc, wc2;
    register paddr_t mem;
    register h10_t map;		/* Entry from UBA paging RAM */
    register unsigned pagno, pagoff;
    struct ubctl *ub = rh->rh_dv.dv_uba;

    /* If word count reached zero, stop. */
    if ((wc = rh->rh_wc) == 0) {
	rh->rh_dcwcnt = 0;
	return 1;
    }

    /* Find remaining word count in PDP10 words (rounds down) */
    wc = (-(wc | ~MASK16))>>1;		/* Find # of PDP10 words */

    /* Check unibus map to find starting phys address and how many words
	we can handle contiguously in this map before a remap is needed
    */
    
    /* Determine memory address for sector */
    mem = rh->rh_ba;			/* Bus address */
    mem |= (paddr_t)			/* Add extended bits */
		(rh->rh_cs1 & (RH_XA17|RH_XA16)) << 8;
    if (mem & ~(paddr_t)0377774) {	/* High bit or low 2 are no-nos */
	rh->rh_cs1 |= RH_XMCP;		/* Pretend bus error */
					/* (maybe shd use a CS2 bit?) */
	if (RHDEBUG(rh))
	    fprintf(RHDBF(rh), "[rhdc_ccwget: bad bus addr %lo]", (long)mem);
	return 0;
    }
    mem >>= 2;				/* Get PDP10-word address */

    /* High 6 bits (bit 17 known clear) are index into UB map */
    pagno = (mem>>9);
    pagoff = mem & 0777;
    map = ub->ubpmap[pagno];
    if (!(map & UBA_QVAL)) {	/* If map entry not valid, */
	rh->rh_cs1 |= RH_XMCP;	/* Pretend bus error (maybe shd use CS2?) */
	if (RHDEBUG(rh))
	    fprintf(RHDBF(rh), "[rhdc_ccwget: no UB map for page %lo]",
				 (long)pagno);
	return 0;
    }
    mem = ((paddr_t)(map & UBA_QPAG) << 9) | pagoff;	/* Find phys addr */

    /* Determine whether transfer needs to be limited because it crosses
    ** non-contiguous physical pages (owing to unibus map)
    */

    for (wc2 = wc; ; ) {

	/* Find first DEC page boundary */
	if ((pagoff + wc2) <= 01000)
	    break;			/* No boundary crossed */

	/* Check next UBA page map entry for existence & validity */
	if (++pagno >= UBA_UBALEN || (!(ub->ubpmap[pagno] & UBA_QVAL))) {
	    rh->rh_cs1 |= RH_XMCP;	/* Pretend bus error */
	    if (RHDEBUG(rh))
		fprintf(RHDBF(rh), "[rhdc_ccwget: no UB map for page %lo]",
				     (long)pagno);
	    return 0;
	}
	if ((ub->ubpmap[pagno] & UBA_QPAG)
		 != 1+(ub->ubpmap[pagno-1] & UBA_QPAG)) {
	    /* Not contiguous phys page, must limit this xfer. */
	    wc2 -= (01000 - pagoff);	/* Finish rest of current page */
	    wc -= wc2;			/* Limit xfer */
	    break;
	}
	wc2 -= 01000;
    }

    rh->rh_dcwcnt = wc;	/* Set up word count */
    rh->rh_dcbuf = mem;	/* Set up phys PDP10 address */
    return 1;
}

#endif /* KLH10_DEV_RH11 */

