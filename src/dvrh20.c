/* DVRH20.C - Emulates RH20 disk devices for KL10
*/
/* $Id: dvrh20.c,v 2.4 2003/02/23 18:16:54 klh Exp $
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
 * $Log: dvrh20.c,v $
 * Revision 2.4  2003/02/23 18:16:54  klh
 * Add <string.h> to predeclare memset.
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include "klh10.h"

#if !KLH10_DEV_RH20 && CENV_SYS_DECOSF
	/* Stupid gubbish needed to prevent OSF/1 AXP compiler from
	** halting merely because compiled file is empty!
	*/
static int decosfcclossage;
#endif

#if KLH10_DEV_RH20		/* Moby conditional for entire file */

#include <stddef.h>	/* For size_t etc */
#include <string.h>	/* For memset etc */
#include <errno.h>
#include <stdio.h>

#include "kn10def.h"	/* This includes OSD defs */
#include "kn10dev.h"
#include "dvuba.h"
#include "dvrh20.h"

#ifdef RCSID
 RCSID(dvrh20_c,"$Id: dvrh20.c,v 2.4 2003/02/23 18:16:54 klh Exp $")
#endif

struct rh20 {
    struct device rh_dv;	/* Generic 10 device structure */

    /* RH20-specific vars */
    int rh_no;		/* RH20 Unit number (0-7) */
    uint18 rh_cond;	/* CONI bits */
    int rh_pilev;	/* PI level mask (0 if PIA=0) */

			/* "Prep reg" settings */
    uint18 rh_prep;	/* Prep reg LH */
    int rh_rs;		/* Register select */
    int rh_ds;		/* Drive select */
    struct device *rh_dsptr;

    uint32 rh_reg[8];	/* RH20 Internal registers */
    int rh_sbarf;	/* TRUE if SBAR was set */
    int rh_bcnt;	/* Current block count from PTCR */

    /* Channel vars */
    int     rh_eptoff;	/* Offset of channel area in EPT */
    paddr_t rh_clp;	/* DC "PC" - Current Command List Pointer */
    uint18  rh_dcsts;	/* DC status flag bits (LH) */
    w10_t   rh_dcccw;	/* DC current cmd word */
    int     rh_dcwcnt;	/* DC cmd word count */
    paddr_t rh_dcbuf;	/* DC cmd data buffer pointer */
    int	    rh_dchlt;	/* TRUE if halt when current wordcount done */
    int	    rh_dcrev;	/* TRUE if doing reverse data xfer */
    int     rh_dcwrt;	/* TRUE if doing device write */


    /* Drive bindings */
    struct device *rh_drive[8];
    int rh_attn;	/* ATTN summary for all drives */
};

/* Macro for simpler access to RH20 internal regs */
#define RH20REG(c,r) ((c)->rh_reg[(r)-RH20_1ST])


static int nrh20s = 0;
/* static */ struct rh20 dvrh20[RH20_NSUP];

#define RHDEBUG(rh) ((rh)->rh_dv.dv_debug)
#define RHDBF(rh)   ((rh)->rh_dv.dv_dbf)


/* Function predecls */

/* Functions provided to device vector */

static w10_t rh20_pifnwd(struct device *d);
static void  rh20_cono(struct device *d, h10_t erh);
static w10_t rh20_coni(struct device *d);
static void  rh20_datao(struct device *d, w10_t w);
static w10_t rh20_datai(struct device *d);
static int   rh20_bind(struct device *d, FILE *of, struct device *u, int num);
static int   rh20_init(struct device *d, FILE *of);
static void  rh20_reset(struct device *d);
#if 0
static int   rh20_readin();	/* Readin boot, if supported */
#endif

/* Functions provided to slave device */

static void rh20_attn(struct device *drv, int on);
static void rh20_drerr(struct device *drv);
static int  rh20_iobeg(struct device *drv, int wflg);
static int  rh20_iobuf(struct device *drv, register int wc, vmptr_t *avp);
static void rh20_ioend(struct device *drv, int bc);

/* Completely internal functions */

static void rh_clear(struct rh20 *rh);
static void rh_picheck(struct rh20 *rh);
static void rh_pi(struct rh20 *rh);
static void rh_xfrbeg(struct rh20 *rh);
static void rh_clrattn(struct rh20 *rh, int msk);

static void rhdc_init(struct rh20 *rh, int mbc);
static void rhdc_clear(struct rh20 *rh);
static int  rhdc_ccwget(struct rh20 *rh);

/* RH20 interface routines to KLH10 */

struct device *
dvrh20_create(FILE *f, char *s)
{
    register struct rh20 *rh;

    /* Parse string to determine which device to use, config, etc etc
    ** But for now, just allocate sequentially.  Hack.
    */ 
    if (nrh20s >= RH20_NSUP) {
	fprintf(f, "Too many RH20s, max: %d\n", RH20_NSUP);
	return NULL;
    }
    rh = &dvrh20[nrh20s++];		/* Pick unused RH20 */

    /* Initialize generic device part of RH20 struct */
    iodv_setnull(&rh->rh_dv);	/* Initialize as null device */

    rh->rh_dv.dv_dflags = DVFL_CTLR;
    rh->rh_dv.dv_pifnwd = rh20_pifnwd;
    rh->rh_dv.dv_cono = rh20_cono;
    rh->rh_dv.dv_coni = rh20_coni;
    rh->rh_dv.dv_datao = rh20_datao;
    rh->rh_dv.dv_datai = rh20_datai;

    rh->rh_dv.dv_bind = rh20_bind;	/* Controller, so can bind. */
    rh->rh_dv.dv_init = rh20_init;	/* Set up own post-bind init */
    rh->rh_dv.dv_reset = rh20_reset;	/* System reset (clear stuff) */

    return &rh->rh_dv;
}

static int
rh20_bind(struct device *d,		/* Our device (controller) */
	FILE *of,
	register struct device *slv,	/* Slave device */
	int num)
{
    register struct rh20 *rh = (struct rh20 *)d;

    if (0 <= num && num < 8) ;
    else {
	if (of) fprintf(of, "RH20 can only bind up to 8 drives (#%d invalid).\n", num);
	return FALSE;
    }

    /* Backpointer and device # (dv_ctlr, dv_num) are already set up. */
    slv->dv_attn  = rh20_attn;	/* Set attention handler */
    slv->dv_drerr = rh20_drerr;	/* Set exception handler */
    slv->dv_iobeg = rh20_iobeg;	/* IO xfer beg */
    slv->dv_iobuf = rh20_iobuf;	/* IO xfer buffer setup */
    slv->dv_ioend = rh20_ioend;	/* IO xfer end */

    rh->rh_drive[num] = slv;

    return TRUE;
}

static int
rh20_init(register struct device *d, FILE *of)
{
    register struct rh20 *rh = (struct rh20 *)d;

    /* Transform device # into RH20 unit # */
    if (DEVRH20(0) <= rh->rh_dv.dv_num && rh->rh_dv.dv_num < DEVRH20(8)) {
	rh->rh_no = (rh->rh_dv.dv_num - DEVRH20(0)) >> 2;
    } else {
	if (of) fprintf(of, "Trying to init RH20 with non-Massbus device #\n");
	return FALSE;
    }
    rhdc_init(rh, rh->rh_no);	/* Initialize data channel for this MBC */

    rh->rh_cond = 0;		/* Clear all CONI bits */
    rh->rh_prep = 0;		/* Clear prep reg */
    rh->rh_rs = rh->rh_ds = 0;	/* Default selections 0 */
    rh->rh_dsptr = NULL;	/* No drive */
    memset((char *)&(rh->rh_reg[0]), 0, sizeof(rh->rh_reg));
    rh->rh_sbarf = 0;
    rh->rh_attn = 0;		/* Clear ATTN summary */

    return TRUE;
}

static void
rh20_reset(struct device *d)
{
    rh_clear((struct rh20 *)d);
}

/* PI: Get function word */
static w10_t
rh20_pifnwd(struct device *d)
{
    register w10_t w;

#if 0
    /* Clear PI request?
    ** This is probably not correct; I suspect PI stays on until condition
    ** is explicitly turned off with CONO, etc.
    */
    if (rh->rh_dv.dv_pireq)		/* If PI request outstanding, */
	(*rh->rh_dv.dv_pifun)(rh, 0);	/* clear it. */
#endif

    /* Return PI function word; only low 9 bits of addr are used. */
    LRHSET(w, PIFN_ASPEPT | PIFN_FVEC,
		RH20REG((struct rh20 *)d, RH20_IVIR) & H10MASK);
    return w;
}

/* CONO 18-bit conds out
**	Args D, ERH
** Returns nothing
*/
static insdef_cono(rh20_cono)
{
    register struct rh20 *rh = (struct rh20 *)d;
    register uint18 cond = erh;

    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "[rh20_cono: %lo]\r\n", (long)erh);


    /* B24 - Clears RAE and associated PI req */
    if (cond & RH20CO_CRAE) {
	rh->rh_cond &= ~RH20CI_RAE;
    }
    
    /* B25 - Clear Massbus Controller.  Resets RH20 and all drives to
    ** power-up state.  Will hang channel if xfer in progress,
    ** unless RCLP also set.
    */
    if (cond & RH20CO_CMC) {
	rh_clear(rh);
    }

    /* B26 - Clear all xfer error indicators (CONI 18-23, 26) */
    if (cond & RH20CO_TEC) {
	rh->rh_cond &= ~(
		  RH20CI_DPE	/* B18 - Data Parity Error */
		| RH20CI_DEE	/* B19 - Drive Exception (xfer error) */
		| RH20CI_LWCE	/* B20 - Long Word Count Error */
		| RH20CI_SWCE	/* B21 - Short Word Count Error */
		| RH20CI_CE	/* B22 - Channel Error */
		| RH20CI_DR	/* B23 - Drive Response Error (ie no-ex) */
		| RH20CI_DOE	/* B26 - Data Overrun */
		);
    }

    /* B27 - MassBus Enable
    ** Note this doesn't actually do anything - code always assumes it's
    ** enabled, to avoid time-wasting checks.
    */
    if (cond & RH20CO_MBE) {
	rh->rh_cond |= RH20CI_MBE;	/* Same bit, could just mask in */
    }

    /* B28 - Reset Cmd List Ptr (inits channel) */
    if (cond & RH20CO_RCLP) {
	rhdc_clear(rh);
    }

    /* B29 - Clears cmd file xfer logic.
    ** (Used if previous data xfer caused xfer error, CONI bits 18-23, 26).
    */
    if (cond & RH20CO_DSCRF) {
	rh->rh_cond &= ~(RH20CI_SCRF | RH20CI_PCRF);	/* No regs loaded */
	rh->rh_sbarf = 0;				/* No SBAR either */
    }

    /* B30 - Allow Massbus ATTN to generate PI.
    **	Unlike most of the other CONO bits, the state of this one is
    **	always significant; MBAE is turned off if not set.
    */
    if (cond & RH20CO_MEAE) {
	rh->rh_cond |= RH20CI_MBAE;	/* B30 - Enable PI on RH20CI_MBA */
    } else
	rh->rh_cond &= ~RH20CI_MBAE;	/* B30 - Enable PI on RH20CI_MBA */

    /* B31 - Stop Transfer. (nothing cleared) */
    if (cond & RH20CO_ST) {
	/* Ugh, nothing we can really do about this. */
    }

    /* B32 - Clear Cmd Done, clears CONI bit 32 */
    if (cond & RH20CO_CCMD) {
#if 1	/* Avoid T20 ILLGO BUGHLTs for stacked xfers! */
	/* Start xfer if turning off a live CMD bit and STCR has a pending
	** xfer command; assume T20 int handler has fired.
	*/
	if ((rh->rh_cond & (RH20CI_CMD|RH20CI_PCRF|RH20CI_SCRF))
		== (RH20CI_CMD|RH20CI_SCRF)) {
	    /* Both CMD and SCRF are on, and PCRF is off -- start new xfer! */
	    if (RHDEBUG(rh))
		fprintf(RHDBF(rh), "[rh20_cono 2nd xfrbeg!]");
	    rh->rh_cond &= ~RH20CI_CMD;		/* Clear it */
	    rh_xfrbeg(rh);
	} else
#endif
	rh->rh_cond &= ~RH20CI_CMD;	/* Clear it */
    }

    /* B33-35 - PI channel assignment (0 = none) */
    rh->rh_cond = (rh->rh_cond & ~RH20CI_PIA)
			| (cond & RH20CO_PIA);
    rh->rh_pilev = (1 << (7-(cond & RH20CO_PIA))) & 0177; /* 0 if PIA=0 */
    if (rh->rh_dv.dv_pireq && (rh->rh_dv.dv_pireq != rh->rh_pilev)) {
	/* Changed PIA while PI outstanding; flush it, let picheck re-req */
	(*rh->rh_dv.dv_pifun)(&rh->rh_dv, 0);	/* clear it. */
    }

    /* Check for any changes to PI status */
    rh_picheck(rh);
}

/* CONI 36-bit conds in
**	Args D
** Returns condition word
*/
static insdef_coni(rh20_coni)
{
    register w10_t w;

    if (RHDEBUG((struct rh20 *)d))
	fprintf(RHDBF((struct rh20 *)d), "[rh20_coni: %lo]",
		 (long)((struct rh20 *)d)->rh_cond);

    LRHSET(w, 0, ((struct rh20 *)d)->rh_cond);
    return w;
}

/* DATAO word out
**	Args D, W
** Returns nothing
*/
static insdef_datao(rh20_datao)
{
    register struct rh20 *rh = (struct rh20 *)d;

    /* First make sure it's OK to do anything at all, by checking RAE.
    ** Note that the setting of DRAES in the current DATAO has no effect on
    ** the initial test -- although it does affect the handling of RAE if
    ** it is raised during the current DATAO.
    */
    if ((rh->rh_cond & RH20CI_RAE) && !(rh->rh_prep & RH20DO_DRAES)) {
	if (RHDEBUG(rh))
	    fprintf(RHDBF(rh), "[rh20_datao: %o.? %lo,,%lo RAE-ignored]\r\n",
		rh->rh_no, (long)LHGET(w), (long)RHGET(w));

	return;			/* Ugh, RAE but no DRAES, so do nothing */
    }

    /* Now always set prep reg values.
    **	Note that CBEP is ignored.
    */
    rh->rh_prep = LHGET(w) &
		(RH20DO_RS | RH20DO_LR | RH20DO_DRAES | RH20DO_DS);
    rh->rh_rs = (rh->rh_prep >> 12) & 077;
    rh->rh_ds = rh->rh_prep & RH20DO_DS;
    rh->rh_dsptr = rh->rh_drive[rh->rh_ds];

    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "[rh20_datao: %o.%o RS%o <= %lo,,%lo]\r\n",
		rh->rh_no, rh->rh_ds, rh->rh_rs,
		(long)LHGET(w), (long)RHGET(w));

    /* Now see whether to actually load anything else */
    if (!(rh->rh_prep & RH20DO_LR))
	return;				/* Nope, just prep reg, we're done. */
	
    switch (rh->rh_rs) {
    default:
	if (rh->rh_rs <= 037) {
	    /* External register.
	    ** In the real RH20, external registers cannot be updated if
	    ** a command file transfer (using PBAR&PTCR) is in progress;
	    ** the RH20 waits until the massbus is free.
	    ** This may become a consideration for future asynch operation.
	    ** Hopefully no monitor tries to do this.
	    */
	    /* Note RHR_ATTN needs special handling, just as for DATAI.
	    */
	    if (rh->rh_rs == RHR_ATTN) {	/* Attn summary? */
		/* Turn off ATTN bit for all drive bits provided.
		*/
		rh_clrattn(rh, (int)RHGET(w)&MASK16);
		return;
	    }
	    if (rh->rh_dsptr &&
		(*rh->rh_dsptr->dv_wrreg)(rh->rh_dsptr,
					rh->rh_rs, ((int)RHGET(w))&MASK16))
		return;		/* External reg write succeeded */

	    if (RHDEBUG(rh)) {
		fprintf(RHDBF(rh), "[rh20_datao: %o.%o RAE %o",
				rh->rh_no, rh->rh_ds, rh->rh_rs);
		if (!rh->rh_dsptr)
		    fprintf(RHDBF(rh), " (no drive)]");
		else
		    fprintf(RHDBF(rh), "]");
	    }

	    /* External reg write failed, set RAE and maybe do PI. */
	    rh->rh_cond |= RH20CI_RAE;		/* Set RAE */
	    if (!(rh->rh_prep & RH20DO_DRAES)) {
		rh_pi(rh);			/* Trigger PI */
	    }
	}
	return;	 	/* Unspecified RH20 reg (040-067), just do nothing */

    /* Internal RH20 registers! */

	/* Big hairy case -- this is what ultimately starts a data xfer!! */
    case RH20_STCR:	/* R/W Secondary Transfer Control Reg */
	RH20REG(rh, RH20_STCR) = W10_U32(w);	/* First load STCR */
	if (rh->rh_cond & RH20CI_PCRF) {	/* Primary regs loaded? */
	    rh->rh_cond |= RH20CI_SCRF;		/* Yep, say STCR loaded */
	    return;
	}
#if 1	/* Avoid T20 ILLGO BUGHLTs for stacked xfers! */
	/* Don't start xfer if CMD bit still set -- means T20 hasn't yet
	** responded to the xfer-done interrupt!
	*/
	if (rh->rh_cond & RH20CI_CMD) {
	    if (RHDEBUG(rh)) {
	      fprintf(RHDBF(rh), "[rh20_datao postponed 2nd xfer!]");
	    }
	    rh->rh_cond |= RH20CI_SCRF;		/* Yep, say STCR loaded */
	    return;
	}
#endif
	rh_xfrbeg(rh);	/* Nothing in PTCR, start xfer! */
	return;

    case RH20_SBAR:	/* R/W Secondary Block Address Reg */
	rh->rh_sbarf = TRUE;	/* Say SBAR loaded */
	break;			/* Then go load it */

	/* Simple can't-write cases */
    case RH20_PBAR:	/* RO Primary Block Address Reg */
    case RH20_PTCR:	/* RO Primary Transfer Control Reg */
    case RH20_RR:	/* RO Read Reg (Diagnostic only) */
	return;		/* Can't write, so no-op these */

	/* Simple write-register cases */
    case RH20_IVIR:	/* R/W Interrupt Vector Index Reg */
    case RH20_WR:	/* WO Write Reg (Diagnostic only) */
    case RH20_DCR:	/* WO Diagnostic Control Reg (Diags only) */
	break;		/* Just do simple write */
    }

    /* Do a simple internal register write */
    RH20REG(rh, rh->rh_rs) = W10_U32(w);	/* Simple write */
}

/* DATAI word in
**	Args D
** Returns data word
*/
static insdef_datai(rh20_datai)
{
    register struct rh20 *rh = (struct rh20 *)d;
    register w10_t w;

    /* For the RH20 the register & unit selected is specified by the
    ** preparation register, set by a previous DATAO.
    */
    if (rh->rh_rs >= RH20_1ST) {
	/* Internal register, simple and always succeeds */
	LRHSET(w, (RH20REG(rh, rh->rh_rs)>>H10BITS)&H10MASK,
			(RH20REG(rh, rh->rh_rs)&H10MASK));

    } else if (rh->rh_rs <= 037) {
	/* External register.
	** Note RHR_ATTN needs to be handled specially, as it must collect
	** attention status of all drives!
	*/
	register uint32 erdata;

	if (rh->rh_rs == RHR_ATTN) {
	    LRHSET(w, RH20DI_TRA, rh->rh_attn);	/* Special summary */

	} else if (rh->rh_dsptr && (MASK16 >= (erdata =
		(*rh->rh_dsptr->dv_rdreg)(rh->rh_dsptr, rh->rh_rs)))) {
	    LRHSET(w, RH20DI_TRA, erdata);

	} else {
	    /* Non-existent drive or register access error.
	    ** Set the RAE CONI bit and possibly interrupt.
	    */
	    if (RHDEBUG(rh)) {
		fprintf(RHDBF(rh), "[rh20_datai: %o.%o RAE %o: ",
				rh->rh_no, rh->rh_ds, rh->rh_rs);
		if (!rh->rh_dsptr)
		    fprintf(RHDBF(rh), "no drive]");
		else
		    fprintf(RHDBF(rh), "%lo]", (long)erdata);
	    }
	    LRHSET(w, RH20DI_CBPE, 0);
	    rh->rh_cond |= RH20CI_RAE;		/* Set RAE */
	    if (!(rh->rh_prep & RH20DO_DRAES)) {
		rh_pi(rh);			/* Trigger PI */
	    }
	}

    } else {
	/* Unspecified RH20 register (040-067) just does nothing */
	LRHSET(w, 0, 0);
    }

    /* Always stuff in RS and LR fields from prep reg */
    LHSET(w, (LHGET(w) & ~(RH20DO_RS|RH20DO_LR))
		| ((RH20DO_RS|RH20DO_LR) & rh->rh_prep));

    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "[rh20_datai: %o.%o RS%o => %lo,,%lo]\r\n",
		rh->rh_no, rh->rh_ds, rh->rh_rs,
		(long)LHGET(w), (long)RHGET(w));
    return w;
}

/* RH20 internal routines (internal registers etc) */

/* RH_CLEAR - Clear controller
**	Also does equivalent of asserting Massbus INIT, by invoking
**	the reset routine for all drives.
*/
static void
rh_clear(register struct rh20 *rh)
{
    register int i;

    if (rh->rh_dv.dv_pireq)		/* If PI request outstanding, */
	(*rh->rh_dv.dv_pifun)(&rh->rh_dv, 0);	/* clear it. */

    /* Stop and flush any in-progress xfer? */

    /* Need to figure out just which bits get cleared. */
#if 1	/* For now, all of them */
    rh->rh_cond = 0;		/* Clear all CONI bits */
    rh->rh_prep = 0;		/* Clear prep reg */
    rh->rh_rs = rh->rh_ds = 0;	/* Default selections 0 */
    rh->rh_dsptr = NULL;	/* No drive */
    memset((char *)&(rh->rh_reg[0]), 0, sizeof(rh->rh_reg));
    rh->rh_sbarf = 0;
#endif

    /* Clear all drives for this controller */
    for (i = 0; i < 8; ++i)
	if (rh->rh_drive[i])
	    (*(rh->rh_drive[i]->dv_reset))(rh->rh_drive[i]);
}

/* RH_PICHECK - Check RH20 conditions to see if PI should be attempted.
*/
static void
rh_picheck(register struct rh20 *rh)
{
    /* If any possible interrupt bits are set */
    if (rh->rh_cond & (RH20CI_RAE | RH20CI_MBA | RH20CI_CMD)) {

	if ((rh->rh_cond & RH20CI_CMD)	/* CMD DONE always interrupts */
	  || ((rh->rh_cond & RH20CI_MBA) && (rh->rh_cond & RH20CI_MBAE))
	  || ((rh->rh_cond & RH20CI_RAE) && (!(rh->rh_prep & RH20DO_DRAES)))) {
		rh_pi(rh);
		return;
	}
    }
    /* Here, shouldn't be requesting PI, so if our request bit is set,
    ** turn it off.
    */
    if (rh->rh_dv.dv_pireq) {		/* If set while shouldn't be, */
	(*rh->rh_dv.dv_pifun)(&rh->rh_dv, 0);	/* Clear it! */
    }
}


/* RH20_PI - trigger PI for selected RH20.
**	This could perhaps be an inline macro, but for now
**	having it as a function helps debug.
*/
static void
rh_pi(register struct rh20 *rh)
{
    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "[rh20_pi: %o]", rh->rh_pilev);

    if (rh->rh_pilev			/* If have non-zero PIA */
      && !(rh->rh_dv.dv_pireq)) {	/* and not already asking for PI */
	(*rh->rh_dv.dv_pifun)(&rh->rh_dv, rh->rh_pilev); /* then do it! */
    }
}

/* RH_CLRATTN - Clear ATTN bit for all masked drives.
**	Forces call even if it seems unnecessary, just to be safe.
*/
static void
rh_clrattn(register struct rh20 *rh, int msk)
{
    register int i;
    register struct device *drv;

    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "[rh_clrattn: %o]", msk);
    msk &= 0377;		/* Allow only 8 bits for 8 drives */
    for (i = 0; msk; ++i, msk >>= 1) {
	if ((msk & 01) && (drv = rh->rh_drive[i]))
	    (*drv->dv_wrreg)(drv, RHR_ATTN, 1);	/* Tell drv to clr its ATTN */
    }
}


/*
NOTE:
	This code does not fully emulate the RH20's secondary transfer
registers, for the following reasons:

	- Only TOPS-20 uses this feature (which it calls a "stacked" transfer),
		and only for disk.  TOPS-10 doesn't use it.
	- The T20 code has a timing problem as follows:

T20 is doing a so-called "stacked" transfer using the ability of the
RH20 to store a backup xfer command and start it when the primary xfer
is done.  But then T20 sometimes gets confused -- when it checks the
logout area to see what the last CCW pointed to, it finds a page number
that, while correct (is one past the last phys xfer finished), is NOT what
T20 expects; T20 compares it against the result expected for the FIRST
transfer, not the second one, and promptly dies with an ILLGO BUGHLT.

Somehow, the 2nd transfer is not only being initiated but also
completed before T20 expects it to be.

The odd thing about all this is that this doesn't happen for the
synchronous case, where transfers complete immediately; you can't get
much more extreme than an "instantaneous" disk!  Somehow, the fact
that the "done" interrupt is delayed is making things different.  It's
not even a case of having the wrong "xfer in progress" bits set,
because T20 isn't even doing a CONI in between the two transfer loads
(verified by debug output).

The theory so far is something like this:

        [A1] RH PI suspended.
        [A2] T20 initiates 1st xfer.
        [A3] RH PI allowed.
                ...
        [B1] RH PI suspended.
        [B2] T20 initiates 2nd (stacked) xfer.
        [B3] RH PI allowed.

        If T20 expects to receive a distinct PI int for each xfer, then
        this sequence will lose if:
                - Xfer A completes after [B1]
                - Xfer B completes before [B3]
        Which is what appears to be happening when the asynch disk
        hits ILLGO.  That is, xfer A completes in the middle of B2, and
        xfer B is started as soon as the appropriate B2 DATAO is given,
        but although the RH20 makes a PI request, no interrupt is taken
        until after xfer B is done as well!

If this is correct, this would explain why synch disk wins, cuz the
pending PI would happen in the window between A and B, and T20 is
happy.  This would also explain why fast asynch loses, cuz xfer B
happens much faster than T20 expects.

The solution currently adopted here is for the RH20 to defer the 2nd
xfer until the OS explicitly acknowledges the first xfer by turning
off the DONE bit in the RH20 CONI word.

This is not what a real RH20 does, but a real RH20 is guaranteed of some
non-trivial length of time for a transfer operation to complete.  An
alternative solution, which only makes sense if the following conditions
hold:

        1) T20
        2) disk performance bottleneck
        3) using raw partition
        4) frequent stacked xfers
 THEN:
        Could try a sort of read-ahead or write-ahead hack where the
RP subproc carried out both xfers, so that the data actually gets
transferred at top speed, but the RH side hides that fact from the 10
and the 2nd transfer appears not to be done until CMD-DONE is turned
off for the 1st transfer; then for the 2nd xfer the RH doesn't actually
do I/O but instead just rigs the channel logout area and says 2nd
transfer is done.
	But for now, the wait-for-CMD-clear solution should work.

*/

/* RH_XFRBEG - Called to start a data transfer.
**	Assumes no xfer in progress, and STCR has stuff;
**	takes whatever is there, plus (optionally)
**	whatever is in SBAR, and starts the xfer going.
*/
static void
rh_xfrbeg(register struct rh20 *rh)
{
    register struct device *drv;
    register uint32 bar, tcr;
    register int barf;

    rh->rh_cond &= ~RH20CI_SCRF;	/* Clear SCR Full flag */
    rh->rh_cond |= RH20CI_PCRF;		/* Set PCR Full flag */
    barf = rh->rh_sbarf;
    bar = RH20REG(rh, RH20_PBAR) = RH20REG(rh, RH20_SBAR);
    tcr = RH20REG(rh, RH20_PTCR) = RH20REG(rh, RH20_STCR);

    if (RHDEBUG(rh)) {
	fprintf(RHDBF(rh), "[rh_xfrbeg: TCR %lo", (long)tcr);
	if (rh->rh_sbarf) fprintf(RHDBF(rh), " BAR %lo", (long)bar);
	fprintf(RHDBF(rh), "]\r\n");
    }

    /* What to do about old values of secondary xfer regs?
    ** I doubt the real hardware clears them, but to help debugging,
    ** this seems reasonable.
    */
    RH20REG(rh, RH20_SBAR) = 0;
    RH20REG(rh, RH20_STCR) = 0;
    rh->rh_sbarf = 0;			/* This *must* be cleared */

    if (tcr & (RH20DO_RCLP<<H10BITS))	/* Check flag in LH */
	rhdc_clear(rh);			/* Clear and reset data channel PC */

    /* Get selected device */
    if (!(drv = rh->rh_drive[(tcr >> H10BITS) & RH20DO_DS])) {
	/* Ugh, non-existent drive!  Set CONI DR bit and trigger PI. */
	rh->rh_cond |= RH20CI_DR|RH20CI_CMD;	/* Drive Response Error */
	rh_pi(rh);			/* Trigger PI */
	return;
    }

    /* If SBAR was set, then give that to drive first.
    ** It appears that the drive select field in the BAR reg is ignored;
    ** the actual drive is whatever the TCR specifies!
    */
    if (barf) {
#if 0	/* This was going off constantly */
	/* Do debug check - BAR and TCR better have same drive select! */
	if ((bar & RH20DO_DS) != (tcr & RH20DO_DS)) {
	    if (rh->rh_dv.dv_dbf)
		fprintf(rh->rh_dv.dv_dbf,
		    "[rh_xfrbeg: drive select mismatch, bar=%lo, tcr=%lo]\r\n",
		    (long)bar, (long)tcr);
	}
#endif
	if (!(*drv->dv_wrreg)(drv, RHR_BAFC, (int)(bar & MASK16))) {
	    /* Ugh, drive complained about setting BAR??? */
	    panic("rh_xfrbeg: BAR set failure");
	    rh->rh_cond |= RH20CI_DR|RH20CI_CMD;	/* Drive Resp Err */
	    rh_pi(rh);		/* Trigger PI */
	    return;
	}
    }

    /* Set up positive block count.  Note field is interpreted as negative, and
    ** in the real RH20 the count is done when adding 1 causes overflow.
    ** Thus if the field is all 0s it is interpreted as -2000, not as 0.
    */
    rh->rh_bcnt = - (((tcr>>6)&(RH20DO_NBC>>6)) | ~(RH20DO_NBC>>6));

    /* OK, ready to start xfer... */
    if (!(*drv->dv_wrreg)(drv, RHR_CSR, (int)(tcr & MASK16))) {
	/* Ugh, drive complained about setting CSR??? */
	panic("rh_xfrbeg: CSR set failure");
	rh->rh_cond |= RH20CI_DR|RH20CI_CMD;	/* Drive Resp Err */
	rh_pi(rh);			/* Trigger PI */
	return;
    }

    /* That's all, drive's code does the rest! */
}


/* The following rh20_ functions are all called by the drive via
** the controller/drive vectors.
*/

/* RH20_ATTN - Called by drive to assert or clear its attention bit.
**	Note arg is pointer to rh20drv struct.
*/
static void
rh20_attn(register struct device *drv, int on)
{
    register struct rh20 *rh = (struct rh20 *)(drv->dv_ctlr);
    register int rbit = (1 << drv->dv_num);	/* Attention bit to use */

    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "[rh20_attn: bit %o %s]",
			rbit, on ? "on" : "off");

    /* If turning ATN bit on, always interrupt if enabled, even if ATN
	was already on.  This is because it could have been previously
	turned on while RH20CI_MBAE was off, and thus is not a reliable sign
	of whether an interrupt is already pending.
    */
    if (on) {
	rh->rh_attn |= rbit;		/* Add new attention bit! */
	rh->rh_cond |= RH20CI_MBA;	/* Assert Massbus Attention */
	if (rh->rh_cond & RH20CI_MBAE)	/* If OK for attn to int */
	    rh_pi(rh);		/* then try to trigger PI! */
    } else {
	if (rh->rh_attn & rbit) {
	    rh->rh_attn &= ~rbit;	/* Turn off attention bit */
	    if (!rh->rh_attn		/* If no longer have any ATTNs */
	      && (rh->rh_cond & RH20CI_MBA)) {	/* ensure MB attn is off */
		/* Changing MBA state... may have to clean up PI */
		rh->rh_cond &= ~RH20CI_MBA;	/* ensure MB attn is off */
		if (rh->rh_dv.dv_pireq)		/* If had PI request, */
		    rh_picheck(rh);		/* Sigh, must re-check stuff */
	    }
	}
    }
}


/* RH20_DRERR - Called by drive to assert an exception error during an
**	I/O transfer.
*/
static void
rh20_drerr(register struct device *drv)
{
    register struct rh20 *rh = (struct rh20 *)(drv->dv_ctlr);

    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "[rh20_drerr]");

    if (!(rh->rh_cond & RH20CI_DEE)) {
	rh->rh_cond |= RH20CI_DEE;	/* Assert Drive Exception Error */
	rh_pi(rh);			/* Then try to trigger PI */
    }

    /* If I/O transfer in progress, abort it gracefully */
    if (1)
	rh20_ioend(drv, 1);	/* Assume always have a block left */
}


/* RH20_IOBEG - Called by drive to set up data channel just prior to
**	starting an I/O xfer.
** Returns 0 if failure; drive should stop immediately and not
**	invoke rhdv_iobuf, but must still call rhdv_ioend.
** Else returns # of block units (sectors or records) to do I/O for.
*/
static int
rh20_iobeg(struct device *drv, int wflg)
{
    register struct rh20 *rh = (struct rh20 *)(drv->dv_ctlr);

    rh->rh_dcsts = CSW_CLRSET;		/* Clear channel status */
    rh->rh_dcwrt = wflg;		/* Remember if writing */
    if (!rhdc_ccwget(rh)) {
	rh->rh_cond |= RH20CI_CE	/* Say Channel Error */
		| RH20CI_CMD;		/* and Command Done */
	rh_pi(rh);			/* Try to trigger PI */
	return 0;			/* Tell drive we failed */
    }

    rh->rh_cond &= ~RH20CI_CNR;		/* Channel active now! */
    return rh->rh_bcnt;
}


/* RH20_IOBUF - Called by drive to query data channel to find source
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
rh20_iobuf(struct device *drv,
	   register int wc,	/* Max 11 bits of word count, so int OK */
	   vmptr_t *avp)
{
    register struct rh20 *rh = (struct rh20 *)(drv->dv_ctlr);

    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "[rh20_iobuf: %d. => ", wc);

    if (!rh->rh_dcwcnt) {	/* Nothing left */
        if (RHDEBUG(rh))
	    fprintf(RHDBF(rh), "0]\r\n");
	return 0;
    }

    if (wc) {
	/* If drive is updating our IO xfer status, do it. */

	/* Update buffer pointer, assuming wc has correct direction sign */
	if (rh->rh_dcbuf)
	    rh->rh_dcbuf += wc;

	if (wc < 0) {			/* Verify direction consistent */
	    if (!rh->rh_dcrev)
		panic("rh20_iobuf: Neg wc for fwd xfer!");
	    wc = -wc;			/* Make positive */
	} else if (rh->rh_dcrev)
		panic("rh20_iobuf: Pos wc for rev xfer!");

	if ((rh->rh_dcwcnt -= wc) < 0)	/* Update remaining wd cnt */
	    panic("rh20_iobuf: drive overran chan!");

	/* See if word count ran out */
	if (rh->rh_dcwcnt == 0) {
	    /* Yep, was this the last cmd (halt or last xfer?)
	    ** If not, try to get another data xfer CCW.
	    */
	    if (rh->rh_dchlt || !rhdc_ccwget(rh)) {
	        if (RHDEBUG(rh))
		    fprintf(RHDBF(rh), "0]\r\n");
		return 0;		/* Done, or no more */
	    }
	}
    }

    /* Here, we still have a positive count, so return that. */
    wc = rh->rh_dcwcnt;

    /* Also return addr of buffer.  But there are a few special cases
    ** to check for, sigh...
    */
    if (rh->rh_dcbuf)
	*avp = vm_physmap(rh->rh_dcbuf);
    else {			/* Ugh! Special fill or skip hack. */
	if (rh->rh_dcwrt) {	/* If writing, use fill wds from EPT */
	    *avp = vm_physmap(cpu.mr_ebraddr + EPT_CB0);
	    if (wc > 4)
		wc = 4;
	} else {
	    *avp = NULL;	/* Tell drive to skip input */
	}
    }
    if (rh->rh_dcrev)		/* If reverse xfer, negate count */
	wc = -wc;

    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "%d. (%lo)]\r\n", wc, (long)(rh->rh_dcbuf));

    return wc;
}



/* RH20_IOEND - Called by drive when I/O finished, terminate data channel
**	I/O xfer.  Assumes that rh20_iobuf has been called to update
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
**	Stores channel status if requested by PTCR bit RH20DO_SES.
**
**	May initiate next RH20 data transfer.
*/
static void
rh20_ioend(register struct device *drv,
	   int bc)	/* Drive's final block count */
{
    register struct rh20 *rh = (struct rh20 *)(drv->dv_ctlr);

    if (RHDEBUG(rh))
	fprintf(RHDBF(rh), "[rh20_ioend: %d.]\r\n", bc);


    /* Update final IO xfer status */
    if (bc || rh->rh_dcwcnt) {
	/* Drive still has stuff it wanted to do.
	** Set channel Short WC if WC==0, or Long WC if WC != 0,
	** and RH20 Chan Err.
	*/
	rh->rh_dcsts |= (rh->rh_dcwcnt ? CSW_LWCE : CSW_SWCE);
	rh->rh_cond |= RH20CI_CE;
    }

    /* See whether to store channel status */
    if ((RH20REG(rh, RH20_PTCR)			/* If wanted state stored */
		& (RH20DO_SES<<H10BITS))
      || (rh->rh_cond & RH20CI_CE)) {		/* or any kind of chan error */
	register vmptr_t vp;
	register w10_t w;

	vp = vm_physmap(cpu.mr_ebraddr + rh->rh_eptoff);

	/* Store status bits and current cmd list ptr */
	if (rh->rh_dcwcnt)
	    rh->rh_dcsts |= CSW_NOWC0;
	LRHSET(w, rh->rh_dcsts | ((rh->rh_clp >> H10BITS)&CSW_HIADR),
		rh->rh_clp & H10MASK);
	vm_pset(vp+RH20_EPT_CST(0), w);

	/* Reconstruct current CCW and store it */
	LRHSET(w, (LHGET(rh->rh_dcccw)&CCW_OP)
		| (rh->rh_dcwcnt<<4) | ((rh->rh_dcbuf>>H10BITS)&CCW_ADR),
			rh->rh_dcbuf & H10MASK);
	vm_pset(vp+RH20_EPT_CCW(0), w);
    }

    /* Now say command done, whatever happened. */
    rh->rh_cond &= ~RH20CI_PCRF;	/* Primary regs now free */
    rh->rh_cond |= RH20CI_CMD | RH20CI_CNR;	/* Done, channel ready */
    rh_pi(rh);				/* Attempt triggering PI */

    /* Now if no errors, check for secondary TCR and initiate it? */
    /* NO -- see explanation at start of rh_xfrbeg!! */
}

/* RH20 Data Channel routines.
**
** init/reset - called by RH20
** ccwget - called by RH20 when drive wants to start xfer or needs more
**		data/buffer space from channel.
*/
static void
rhdc_init(register struct rh20 *rh,
	  int mbc)		/* Massbus controller # (0-7) */
{
    rh->rh_eptoff = mbc * 4;	/* Offset of EPT area */
    rhdc_clear(rh);
}

static void
rhdc_clear(register struct rh20 *rh)
{
    /* Reset channel PC to point at 1st wd of channel area in EPT */
    rh->rh_clp = rh->rh_eptoff + cpu.mr_ebraddr;

    rh->rh_dcsts = CSW_CLRSET;		/* Clear status */
    LRHSET(rh->rh_dcccw, 0, 0);
    rh->rh_dcwcnt = 0;
    rh->rh_dcbuf = 0;
    rh->rh_dchlt = TRUE;
}

static int
rhdc_ccwget(register struct rh20 *rh)
{
    register w10_t w;
    register paddr_t pa;
    register int wc;

    /* Grovel down cmd list until hit valid xfer cmd, use that to set up */
    for (;;) {
	/* Fetch current cmd word */
	w = vm_pget(vm_physmap(rh->rh_clp));	/* Get cmd word */
	pa = W10_U32(w) & MASK22;
	wc = (LHGET(w) & CCW_CNT) >> (22-18);

	switch (LHGET(w) & CCW_OP) {
	case CCW_HLT:
	    /* Use addr as CLP for next call, and do stuff to halt.
	    ** Should this cause a "Short Word Count" channel error, or
	    ** something else since transfer was never started?
	    ** For now, pretend zero word count, fail as if short WC.
	    */
	    rh->rh_clp = pa;	/* Set new "PC" */
	    rh->rh_dcbuf = pa;	/* Also set here in case status stored */
	    rh->rh_dcccw = w;	/* Remember current cmd */
	    rh->rh_dcwcnt = 0;
	    rh->rh_dchlt = TRUE;
	    rh->rh_dcrev = 0;
	    return 0;

	case CCW_JMP:
	    rh->rh_clp = pa;	/* Set up CLP and loop to effect jump */
	    continue;

	case CCW_XFR:		/* Forward transfer, no halt */
	    rh->rh_dchlt = 0;
	    rh->rh_dcrev = 0;
	    break;

	case CCW_XFR|CCW_XHLT:	/* Forward transfer, halt */
	    rh->rh_dchlt = TRUE;
	    rh->rh_dcrev = 0;
	    break;

	case CCW_XFR|CCW_XREV|CCW_XHLT:
	    rh->rh_dchlt = 0;
	    rh->rh_dcrev = TRUE;
	    break;

	case CCW_XFR|CCW_XREV:
	    rh->rh_dchlt = TRUE;
	    rh->rh_dcrev = TRUE;
	    break;

	default:
	    /* Perhaps assert Channel Error (RH20CI_CE) here? */
	    panic("rhdc_ccwget: Bad CCW op %lo", (long)(LHGET(w) & CCW_OP));
	    return 0;
	}

	rh->rh_dcccw = w;	/* Remember current cmd */
	rh->rh_dcwcnt = wc;	/* Set up word count */
	rh->rh_dcbuf = pa;	/* Set up address */
	rh->rh_clp = (rh->rh_clp + 1) & MASK22;	/* Bump "PC" */
	return 1;
    }
}

#endif /* KLH10_DEV_RH20 */

