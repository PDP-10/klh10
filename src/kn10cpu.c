/* KN10CPU.C - Main Processor Operations (APR, PI, PAG)
*/
/* $Id: kn10cpu.c,v 2.9 2002/05/21 16:54:32 klh Exp $
*/
/*  Copyright © 1992, 1993, 2001 Kenneth L. Harrenstien
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
 * $Log: kn10cpu.c,v $
 * Revision 2.9  2002/05/21 16:54:32  klh
 * Add KLH10_I_CIRC to allow any sys to have CIRC
 *
 * Revision 2.8  2002/05/21 16:25:26  klh
 * Fix typo
 *
 * Revision 2.7  2002/05/21 10:03:02  klh
 * Fix SYNCH implementation of KL timebase
 *
 * Revision 2.6  2002/03/21 09:50:08  klh
 * Mods for CMDRUN (concurrent mode)
 *
 * Revision 2.5  2001/11/19 10:43:28  klh
 * Add os_rtm_adjust_base for ITS on Mac
 *
 * Revision 2.4  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include <stdio.h>
#include <setjmp.h>	/* For setjmp, longjmp */

#include "klh10.h"
#include "osdsup.h"
#include "kn10def.h"
#include "kn10ops.h"
#include "kn10dev.h"	/* For device PI handling */
#include "dvcty.h"	/* For cty_ stuff */

#ifdef RCSID
 RCSID(kn10cpu_c,"$Id: kn10cpu.c,v 2.9 2002/05/21 16:54:32 klh Exp $")
#endif

/* Exported functions */
void apr_init(void);
void apr_init_aprid(void);
int apr_run(void);
void pi_devupd(void);
void apr_check(void);
void pxct_undo(void);	/* Stuff needed by KN10PAG for page fail trap */
void trap_undo(void);
#if KLH10_ITS_1PROC
void a1pr_undo(void);
#elif KLH10_CPU_KI || KLH10_CPU_KL
void afi_undo(void);
#endif

/* Imported functions */
extern void fe_begpcfdbg(FILE *);
extern void fe_endpcfdbg(FILE *);
extern void pishow(FILE *);
extern void pcfshow(FILE *, h10_t flags);
extern void pinstr(FILE *, w10_t w, int flags, vaddr_t e);
extern void fe_traceprint (register w10_t instr, vaddr_t e);

/* Pre-declarations */
static void trap_xct(void);
static int pi_check(void);
static void pi_xct(int lev);
static void pi_init(void);
static int tim_init(void);
#if KLH10_EXTADR
 static int apr_creep(void);
 static void apr_hop(void);
#else
 static int apr_walk(void);
 static void apr_fly(void);
#endif
#if KLH10_ITS_1PROC
 static void apr_1proc(void);
#endif
#if KLH10_CPU_KI || KLH10_CPU_KL	/* AFI Handling */
 static void apr_afi(void);
#endif
#if KLH10_CPU_KS
 static void tim_rdtim(dw10_t *);
 static void tim_wrtim(dw10_t *);
# if KLH10_RTIME_SYNCH
  static void tim_basefreeze(void);
  static void tim_baseunfreeze(void);
# endif
#endif /* KLH10_CPU_KS */

jmp_buf aprhaltbuf;	/* Use this jump buffer to halt CPU, return to FE */
jmp_buf aprloopbuf;	/* Use this jump buffer to return to main APR loop */

/* APR_INIT - Called at startup to initialize APR "device" stuff
*/
void
apr_init(void)
{
    INSBRK_INIT();		/* Ensure insbreak cleared */

    cpu.aprf.aprf_set = cpu.aprf.aprf_ena = cpu.aprf.aprf_lev = 0;
    cpu.mr_pcflags = 0;
    PC_SET30(0);
    PCCACHE_RESET();
    cpu.mr_serialno = KLH10_APRID_SERIALNO;
    apr_init_aprid();

    cpu.mr_usrmode = cpu.mr_inpxct = cpu.mr_intrap =
			cpu.mr_injrstf = cpu.mr_inpi = 0;
#if KLH10_ITS_1PROC
    cpu.mr_in1proc = 0;
#elif KLH10_CPU_KI || KLH10_CPU_KL
    cpu.mr_inafi = 0;
#endif
    cpu.mr_dotrace = cpu.mr_1step = 0;

    op10m_setz(cpu.mr_dsw);	/* Clear data switches initially */

    pi_init();			/* Init the PI system */
    pag_init();			/* Init the pager */
    tim_init();			/* Init the timers/clocks */
}


/* APR_INIT_APRID - Called to set APRID value
*/
void
apr_init_aprid(void)
{
#if KLH10_CPU_KS
# if KLH10_SYS_ITS
    LRHSET(cpu.mr_hsb, 0, 0500);	/* Initial HSB base addr */
    LRHSET(cpu.mr_aprid,		/* Set APR ID word */
		AIF_ITS | KLH10_APRID_UCVER,	/* ITS ucode */
		cpu.mr_serialno);
# else /* DEC */
    LRHSET(cpu.mr_hsb, 0, 0376000);	/* Initial HSB base addr */
    LRHSET(cpu.mr_aprid,		/* Set APR ID word */
		AIF_UBLT		/* Have BLTUB, BLTBU */
	/* The INHCST and NOCST bits shouldn't be on for T20, but T20
	** ignores them while T10 expects them with KL paging, so always
	** have them on. Sigh.
	*/
		| AIF_INHCST | AIF_NOCST	/* For T10 */
		| (KLH10_PAG_KI ? AIF_KIPG : 0)
		| (KLH10_PAG_KL ? AIF_KLPG : 0)
		| KLH10_APRID_UCVER,
		cpu.mr_serialno);
# endif /* DEC */

#elif KLH10_CPU_KL
    LRHSET(cpu.mr_aprid,
	 (			   AIF_PMV
		| (KLH10_PAG_KL  ? AIF_T20 : 0)
		| (KLH10_EXTADR  ? AIF_EXA : 0)
		| (KLH10_I_CIRC  ? AIF_SEX : 0)
		| (KLH10_CPU_KLX ? AIF_KLB : 0)
		|	 KLH10_APRID_UCVER),
	(			   AIF_CCA
		|		   AIF_CHN
		|		   AIF_OSC
		| (KLH10_MCA25   ? AIF_MCA : 0)
		| (KLH10_CPU_KLX ? AIF_KLX : 0)
		|     cpu.mr_serialno));	/* SN # */
#endif
}

/* APR_RUN - Invoked by FE to start PDP-10 running!
**	All this routine does is select the actual main loop to use,
**	based on the debugging or operating parameters in effect.
**
** Note that there are two different setjmp points.  One is to allow
** halting the CPU loop; the other implements traps (PI interrupts etc) by
** re-entering the start of the CPU loop.  The latter used to be inside
** the loop functions but has been moved outside because some compilers
** refuse to optimize anything that contains a setjmp!
*/
int
apr_run(void)
{
    register int haltval;

    /* Save return point for APR halt */
    if (haltval = _setjmp(aprhaltbuf)) {	/* If longjmp back, */
	clk_suspend();				/* stop internal clock */
	return haltval;				/* return from running APR */
    }
    clk_resume();				/* Resume internal clock */

    /* Determine which APR loop to start or re-enter! */
    if (cpu.mr_bkpt || cpu.mr_dotrace || cpu.mr_1step
#if KLH10_SYS_T20 && KLH10_CPU_KS
		|| cpu.fe.fe_iowait || cpu.io_ctydelay
#endif
			) {
	/* Debug loop of some sort - invoke slow loop */

	/* Save point to return to for trap/interrupt */
	if (_setjmp(aprloopbuf)) {
	    if (cpu.mr_bkpt && PC_INSECT == cpu.mr_bkpt)
		return HALT_BKPT;
	}
#if KLH10_EXTADR
	return apr_creep();	/* Extended (KL) */
#else
	return apr_walk();	/* Non-extended (KS) */
#endif
    } else {
	/* No debugging - invoke normal fast loop, max speed */

	_setjmp(aprloopbuf);	/* Save return point for trap/interrupt */
#if KLH10_EXTADR
	apr_hop();		/* Extended (KL) */
#else
	apr_fly();		/* Non-extended (KS) */
#endif
	/* Fast loop shouldn't ever return, but if it does then we must
	   be doing some kind of manual debugging.
	*/
	return HALT_STEP;
    }
}

#if !KLH10_EXTADR
/* APR_WALK - Routine to use when debugging or some other kind of
**	slow checking within the main loop may be needed.
**	NON-EXTENDED operation.
*/
static int
apr_walk(void)
{
    register w10_t instr;

    for (;;) {
	register vaddr_t ea;

	/* Check for possible interrupt, trap, or special handling */
	if (INSBRKTEST()) {	/* Check and handle all possible "asynchs" */
	    apr_check();
	    if (cpu.mr_bkpt && PC_INSECT == cpu.mr_bkpt)
		return HALT_BKPT;
	}

	/* Fetch next instruction from new PC (may page fault) */
	instr = vm_fetch(PC_VADDR);

	/* Compute effective address (may page fault) */
	ea = ea_calc(instr);	/* Find it with inline macro */

	/* Now dispatch after setting up AC (may page fault or set trap).
	** During execution, cpu.mr_PC contains PC of this instruction.  It
	** is not incremented (if at all) until execution finishes.
	*/
#if 1
	if (cpu.mr_dotrace)	/* Show instruction about to be XCT'd */
	    fe_traceprint(instr, ea);
#endif

	/* EXECUTE! */
	PC_ADDXCT(op_xct(iw_op(instr), iw_ac(instr), ea));

#if KLH10_SYS_T20 && KLH10_CPU_KS
	/* Gross hack to emulate I/O device delays for buggy 10 software */
	if (cpu.io_ctydelay && (--cpu.io_ctydelay <= 0)) {
	    cty_timeout();
	}
#endif
	CLOCKPOLL();			/* Update clock if necessary */

#if 1	/* Hack to support FE single-stepping */
	if (cpu.mr_1step && (--cpu.mr_1step <= 0)) {
	    cpu.mr_1step = 0;
	    return HALT_STEP;
	}
	if (cpu.mr_bkpt && PC_INSECT == cpu.mr_bkpt)
	    return HALT_BKPT;
#endif
    }
}


/* APR_FLY - Primary loop when no debugging or special checks
**	are necessary.
**	NON-EXTENDED operation.
*/
static void
apr_fly(void)
{
    register w10_t instr;
#if KLH10_PCCACHE
    register vmptr_t vp;
    register paddr_t pc;
    register paddr_t cachelo = 1;	/* Lower & upper bounds of PC */
    register paddr_t cachehi = 0;
#endif

    PCCACHE_RESET();		/* Robustness: invalidate cached PC info */
    for (;;) {
	if (INSBRKTEST())	/* Check and handle all possible "asynchs" */
	    apr_check();
#if KLH10_PCCACHE
	/* See if PC is still within cached page pointer */
	if ((pc = PC_INSECT) <= cachehi && cachelo <= pc
	  && cpu.mr_cachevp) {
	    vp = cpu.mr_cachevp + (pc & PAG_MASK);	/* Win, fast fetch */
	} else {
	    vp = vm_xeamap(PC_VADDR, VMF_FETCH);  /* Do mapping, may fault */
	    /* Remember start of page or ac block */
	    cpu.mr_cachevp = vp - (pc & PAG_MASK);
	    if (cachelo = (pc & (H10MASK & ~PAG_MASK))) {
		/* Normal page reference */
		cachehi = cachelo | PAG_MASK;
	    } else if (pc > AC_17) {	/* Page 0, special handling */
		cachelo = AC_17+1; 
		cachehi = PAG_MASK;
	    } else
		cachehi = AC_17;	/* Running in ACs */
	}
	instr = vm_pget(vp);
#else
	instr = vm_fetch(PC_VADDR);	/* Fetch next instr */
#endif
	PC_ADDXCT(op_xct(iw_op(instr), iw_ac(instr), ea_calc(instr)));

#if 0	/* Turn on to help debug this loop */
	if (cpu.mr_1step && (--cpu.mr_1step <= 0)) {
	    cpu.mr_1step = 0;
	    return HALT_STEP;
	}
#endif

	CLOCKPOLL();			/* Update clock if necessary */
    }
}
#endif /* !KLH10_EXTADR */


/* APR_INT - Abort current instruction; interrupt processor by returning
**	to main loop.
** Someday might pass optional arg to longjmp for special dispatching
** into main loop.
*/
void
apr_int(void)
{
#if KLH10_ITS_1PROC
    if (cpu.mr_in1proc) a1pr_undo();	/* Undo if in one-proceed */
#elif KLH10_CPU_KI || KLH10_CPU_KL
    if (cpu.mr_inafi) afi_undo();	/* Undo if in AFI */
#endif
    if (cpu.mr_intrap) trap_undo();	/* Undo if in trap instruction */
    if (cpu.mr_inpxct) pxct_undo();	/* Undo if PXCT operand */
    _longjmp(aprloopbuf, 1);
}

/* APR_HALT - Halt processor.
**	Passes reason back up to apr_run().
*/
void apr_halt(enum haltcode haltval)	/* A HALT_xxx value */
{
    _longjmp(aprhaltbuf, haltval);
}

#if KLH10_EXTADR

/* APR_CREEP - EXTENDED version of APR_WALK.
**	Slow debug checking, slower with XA operation.
*/
static int
apr_creep(void)
{
    register w10_t instr;

    for (;;) {
	register vaddr_t ea;

	/* Check for possible interrupt, trap, or special handling */
	if (INSBRKTEST()) {	/* Check and handle all possible "asynchs" */
	    apr_check();
	    if (cpu.mr_bkpt && PC_INSECT == cpu.mr_bkpt)
		return HALT_BKPT;
	}

	/* Fetch next instruction from new PC (may page fault) */
	instr = vm_PCfetch();

	/* Compute effective address (may page fault) */
	ea = xea_calc(instr, PC_SECT);	/* Find it with inline macro */

	/* Now dispatch after setting up AC (may page fault or set trap).
	** During execution, cpu.mr_PC contains PC of this instruction.  It
	** is not incremented (if at all) until execution finishes.
	*/
#if 1
	if (cpu.mr_dotrace)	/* Show instruction about to be XCT'd */
	    fe_traceprint(instr, ea);
#endif

	/* EXECUTE! */
	PC_ADDXCT(op_xct(iw_op(instr), iw_ac(instr), ea));
	CLOCKPOLL();			/* Update clock if necessary */

#if 1	/* Hack to support FE single-stepping */
	if (cpu.mr_1step && (--cpu.mr_1step <= 0)) {
	    cpu.mr_1step = 0;
	    return HALT_STEP;
	}
	if (cpu.mr_bkpt && PC_INSECT == cpu.mr_bkpt)
	    return HALT_BKPT;
#endif
    }
}

/* APR_HOP - EXTENDED version of APR_FLY.
**	Fast loop, hobbled by XA operations.
**	No PC Cache stuff to begin with -- keep simple for now.
*/
static void
apr_hop(void)
{
    register w10_t instr;

    for (;;) {
	if (INSBRKTEST())	/* Check and handle all possible "asynchs" */
	    apr_check();
	instr = vm_PCfetch();	/* Fetch next instr */

	PC_ADDXCT(op_xct(iw_op(instr), iw_ac(instr),
				xea_calc(instr, PC_SECT)));

#if 0	/* Turn on to help debug this loop */
	if (cpu.mr_1step && (--cpu.mr_1step <= 0)) {
	    cpu.mr_1step = 0;
	    return HALT_STEP;
	}
#endif
	CLOCKPOLL();		/* Update clock if necessary */
    }
}
#endif /* EXTADR */


/* APR_CHECK - Check all possible "interrupt" conditions to see what
**	needs to be done, and do it.
**	Clears mr_insbreak if nothing needed doing.
*/
int apr_intcount = 0;

void
apr_check(void)
{
#if 0
    apr_intcount++;
#endif
    for (;;) {		/* Start of outer synchronous-handling loop */

	INSBRK_ACTBEG();	/* Start inner asynch-handling loop */

	/* Check for various interrupt-driven stuff */
	if (INTF_TEST(cpu.intf_fecty)) {
	    INTF_ACTBEG(cpu.intf_fecty);
	    INTF_ACTEND(cpu.intf_fecty);	/* Immediately reset flag */
	    apr_halt(fe_haltcode());		/* And halt processor! */
	}

#if KLH10_EVHS_INT
	if (INTF_TEST(cpu.intf_evsig)) {
	    INTF_ACTBEG(cpu.intf_evsig);
	    dev_evcheck();
	    INTF_ACTEND(cpu.intf_evsig);
	}
#endif
#if KLH10_CTYIO_INT
	if (INTF_TEST(cpu.intf_ctyio)) {
	    INTF_ACTBEG(cpu.intf_ctyio);
	    cty_incheck();
	    INTF_ACTEND(cpu.intf_ctyio);
	}
#endif
	if (INTF_TEST(cpu.intf_clk)) {
	    INTF_ACTBEG(cpu.intf_clk);
	    clk_synctimeout();			/* Invoke clock timeouts */
	    INTF_ACTEND(cpu.intf_clk);
	}

	/* Check for PI requests */
	{
	    register int pilev;
	    if (pilev = pi_check())	/* If PI interrupt requested, */
		pi_xct(pilev);		/* handle it! */
	}

	/* Make sure no further asynchs have come in.  This macro will
	** either loop or clear the insbreak flag.
	*/
	INSBRK_ACTEND();

	/* OK, the insbreak flag is clear!  Now OK to handle synchronous
	** trap conditions, where instructions must be executed.  This cannot
	** be done within the inner asynch loop because iterative instructions
	** that do an INSBRKTEST would abort prematurely.
	**
	** This problem also potentially afflicts PI execution if the
	** interrupt instruction uses an indirect address, but the EA calc
	** code allows up to 1 indirection for speed before checking INSBRK.
	** If any monitor does double indirections then PI will hang
	** forever and this code must be re-thought.  Just clearing INSBRK
	** is not good enough because some asynch event could turn INSBRK
	** back on during PI execution.  Sigh.
	*/
#if KLH10_ITS_1PROC
	/* Any pending PI has been handled, now look for one-proceed trap */
	if (PCFTEST(PCF_1PR)) {
	    apr_1proc();		/* One-proceed next instruction */
	    if (INSBRKTEST())		/* If it tickled the insbreak flag, */
		continue;		/* restart outer loop! */
	} else
#elif KLH10_CPU_KI || KLH10_CPU_KL
	/* Any pending PI has been handled, now see if hacking AFI  */
	if (PCFTEST(PCF_AFI)) {
	    apr_afi();			/* Exert AFI on next instruction */
	    if (INSBRKTEST())		/* If it tickled the insbreak flag, */
		continue;		/* restart outer loop! */
	} else
#endif
	/* Check for trap flags set.  One-proceed/AFI also checks, so if
	** we did either, this is skipped.
	*/
	if (PCFTEST(PCF_TR1|PCF_TR2) && cpu.mr_paging) {
	    trap_xct();			/* Execute trap instruction */
	    if (INSBRKTEST())		/* It may have tickled insbreak */
		continue;
	}

	/* If we're here, nothing else to do, so resume normal execution */
	break;
    }
}

/* Effective Address Calculation.
**	The routines here are full-blown functions called when the
**	inline macros give up.
*/

w10_t
ea_wcalc(register w10_t iw,
	 register acptr_t acp,
	 register pment_t *map)
{
    register h10_t tmp;

    for (;;) {
	if ((tmp = LHGET(iw)) & IW_X) {		/* Indexing? */
	    register w10_t wea;
	    wea = ac_xget(tmp & IW_X, acp);	/* Get c(X) */
	    LHSET(iw, LHGET(wea));
	    RHSET(iw, (RHGET(iw)+RHGET(wea))&H10MASK);	/* Add previous Y */
	}
	if (!(tmp & IW_I))			/* Indirection? */
	    return iw;				/* Nope, return now! */
	iw = vm_pget(vm_xmap(RHGET(iw),		/* Yup, do it */
				VMF_READ,
				acp, map));
	if (iw_i(iw)) {			/* If new word is indirect too */
	    CLOCKPOLL();
	    if (INSBRKTEST()) apr_int();	/* Stop infinite @ loops */
	}
    }
}

#if 1

vaddr_t
ea_fncalc(register w10_t iw,
	  register acptr_t acp,
	  register pment_t *map)
{
    register vaddr_t ea;
    register h10_t tmp;

    tmp = LHGET(iw);
    for (;;) {
	if (tmp & IW_X) {		/* Indexing? */
	    /* Could optimize here by adding both words before masking;
	    ** another job for word10.h or kn10ops.h.
	    */
	    va_lmake(ea, 0,
		(RHGET(iw) + ac_xgetrh(tmp & IW_X, acp)) & H10MASK);
	} else					/* Not indexing, just use Y */
	    va_lmake(ea, 0, RHGET(iw));

	if (!(tmp & IW_I))			/* Indirection? */
	    return ea;				/* Nope, return now! */

	/* Indirection, do it */
	iw = vm_pget(vm_xmap(ea, VMF_READ, acp, map));
	if ((tmp = LHGET(iw)) & IW_I) {		/* If new word also indirect */
	    CLOCKPOLL();
	    if (INSBRKTEST()) apr_int();	/* Stop infinite @ loops */
	}
    }
}
#endif

#if KLH10_EXTADR

/* XEA_XCALC - Extended Addressing EA calc!!
**	This is it -- the monster that combines with vm_xmap to
**	suck all the CPU cycles out of a KL emulation!
*/
vaddr_t
xea_xcalc(register w10_t iw,	/* IFIW to evaluate */
	register unsigned sect,	/* Current section */
	register acptr_t acp,	/* AC block mapping */
	register pment_t *map)	/* Page table mapping */
{
    register vaddr_t e;		/* Address to return */

    for (;;) {
	if (op10m_tlnn(iw, IW_X)) {		/* Indexing? */
	    register w10_t xw;
	    xw = ac_xget(iw_x(iw), acp);	/* Get c(X) */
	    /* Check for type of indexing.
	    ** Do global only if NZS E, X>0, and X<6:17> NZ
	    */
	    if (op10m_skipge(xw) && sect && op10m_tlnn(xw, VAF_SMSK)) {
		/* Note special hackery for global indexing: Y becomes
		** a signed displacement.
		*/
		va_gmake30(e, VAF_30MSK & (va_30frword(xw)
			+ (op10m_trnn(iw, H10SIGN)
				? (RHGET(iw) | (VAF_SMSK<<H10BITS))
				: RHGET(iw))));
	    } else {
		va_lmake(e, sect, (RHGET(xw)+RHGET(iw)) & VAF_NMSK);
	    }
	} else {
	    va_lmake(e, sect, RHGET(iw));	/* No X, just use Y */
	}
	if (!op10m_tlnn(iw, IW_I))		/* Indirection? */
	    return e;				/* Nope, done! */

	/* Indirection, enter subloop.
	** Allow one indirect fetch before checking for PI.
	** Apart from speeding up the most common case (single indirect),
	** this also allows PI instructions to have a single indirect 
	** fetch.  Double indirects will hang forever unless INSBRK is
	** somehow safely cleared prior to the pi_xct, and kept clear
	** despite any asynchronous INSBRKSETs.
	**
	** An alternative would be to explicitly check for cpu.mr_inpi if
	** about to do an apr_int(), and check further to see if an abort
	** is really necessary or not.  Decision gets hairy though.
	*/
	for (;;) {
	    iw = vm_pget(vm_xmap(e,		/* Get c(E), may fault */
				VMF_READ,
				acp, map));
	    /* Note must test E via va_isext rather than using "sect"
	    ** because the new E built by indexing may have a different
	    ** section #.
	    */
	    if (!va_isext(e)) {
		sect = 0;		/* Not NZS, back to outer loop */
		break;
	    }
	    if (op10m_skipl(iw)) {		/* Check bit 0 */
		if (!op10m_tlnn(iw, IW_EI)) {	/* Bits 0,1 == 10 or 11? */
		    sect = va_sect(e);
		    break;			/* = 10, back to local loop */
		}
		/* Generate page fail trap for bits 11 */
		/* This is failure code 24 (PRM 3-41) */
		pag_iifail(e, map);	/* Not sure what args to give */
	    }

	    /* Extended Format Indirect Word */
	    if (op10m_tlnn(iw, IW_EX)) {	/* Indexing? */
		register w10_t xw;
		xw = ac_xget(iw_ex(iw), acp);	/* Get c(X)+Y */
		va_gmake30(e, (va_30frword(xw)+va_30frword(iw)) & VAF_30MSK);
	    } else {
		va_gmake30(e, va_30frword(iw));	/* No X, just Y */
	    }
	    if (!op10m_tlnn(iw, IW_EI))	/* Bits 0,1 == 00 or 01? */
		return e;		/* = 00, Done!  Global E */
					/* = 01, get another indirect word */

	    /* Looping with another EFIW. */
	    CLOCKPOLL();
	    if (INSBRKTEST()) apr_int();
	}

	/* Returning to IFIW loop */
	if (op10m_tlnn(iw, IW_I)) {	/* If another indirection */
	    CLOCKPOLL();		/* Check to avoid infinite @ */
	    if (INSBRKTEST()) apr_int();
	}
    }
}

#endif /* KLH10_EXTADR */

/* Device APR - Instructions & support */

/* APRID (BLKI APR,) - Returns processor ID word
*/
ioinsdef(io_aprid)
{
    vm_write(e, cpu.mr_aprid);	/* Read Processor ID */
    return PCINC_1;
}

/* RDAPR (CONI APR,) - Stores APR status word in E.
*/
ioinsdef(io_rdapr)
{
    register w10_t w;
    LRHSET(w, cpu.aprf.aprf_ena,	/* LH set to flags enabled, */
	      cpu.aprf.aprf_set);	/* RH to flags actually set */
    vm_write(e, w);			/* Store word into E */
    return PCINC_1;
}

/*  CONSZ APR, - Skips if all APR status bits in E are zero
**	KS10 doc doesn't mention this instr, but it evidently works.
*/
ioinsdef(io_sz_apr)
{
#if 1
    return (va_insect(e) & cpu.aprf.aprf_set)
		? PCINC_1 : PCINC_2;

#else	/* No longer considered correct */
/*	Subtle point: must check for some bits in E being set,
**	otherwise CONSZ APR,0 would always skip!
*/
    return (va_insect(e) && !(va_insect(e) & cpu.aprf.aprf_set))
		? PCINC_2 : PCINC_1;
#endif
}

/*  CONSO APR, - Skips if any APR status bits in E are set
**	KS10 doc doesn't mention this instr, but it evidently works.
*/
ioinsdef(io_so_apr)
{
    return (va_insect(e) & cpu.aprf.aprf_set)
		? PCINC_2 : PCINC_1;
}


/* WRAPR (CONO APR,) - Sets APR status from bits in E (not c(E)!)
**	Some APR flags conflict with each other.  In all cases, the emulation
**	here does the OFF case before the ON case, on the assumption that
**	the programmer didn't intend to turn things on and then off.
**	Off and then on may be equally pointless but is sometimes useful.
*/
ioinsdef(io_wrapr)
{
    register h10_t ebits = va_insect(e);
    register int flgs = (ebits & APRF_MASK);

    if (ebits & APRW_CLR)		/* Clear flags? */
	cpu.aprf.aprf_set &= ~flgs;
    if (ebits & APRW_SET)		/* Set flags? */
	cpu.aprf.aprf_set |= flgs;

    if (ebits & APRW_DIS)		/* Disable flags? */
	cpu.aprf.aprf_ena &= ~flgs;
    if (ebits & APRW_ENA)		/* Enable flags? */
	cpu.aprf.aprf_ena |= flgs;
    
    /* Set new PI channel assignment */
    cpu.aprf.aprf_set &= ~APRF_CHN;
    cpu.aprf.aprf_set |= (ebits & APRF_CHN);
    cpu.aprf.aprf_lev = pilev_bits[ebits & APRF_CHN];	/* Find PI bit mask */

#if KLH10_CPU_KS
    /* Check for pulsing the FE interrupt */
    if (cpu.aprf.aprf_set & APRF_INT80) {
	cpu.aprf.aprf_set &= ~APRF_INT80;	/* Turn back off */
	fe_intrup();			/* Do whatever needed to alert FE */
    }
#endif /* KS */

    /* Flags munged, now check to see if any action needed. */
    apr_picheck();

    return PCINC_1;
}

/* APR_PICHECK - check APR flags and trigger interrupt if one is called for.
**	Note that the change test also detects any change in the PI
**	level assignment and updates things correctly.
*/
void
apr_picheck(void)
{
    if (cpu.aprf.aprf_set & cpu.aprf.aprf_ena) { /* Any enabled flags set? */
	cpu.aprf.aprf_set |= APRR_INTREQ;	/* Say APR int requested! */
	if (cpu.pi.pilev_aprreq != cpu.aprf.aprf_lev) {	/* PI change? */
	    cpu.pi.pilev_aprreq = cpu.aprf.aprf_lev;	/* Change request! */
	    pi_devupd();			/* Tell PI system! */
	}
    } else {
	cpu.aprf.aprf_set &= ~APRR_INTREQ;	/* No int, clear this flag */
	if (cpu.pi.pilev_aprreq) {		/* If PI request active, */
	    cpu.pi.pilev_aprreq = 0;		/* clear it */
	    pi_devupd();			/* Tell PI system */
	}
    }
}

/* APR_PCFCHECK - Check PC flags for switching between EXEC/USER modes & maps.
**	This must be called anytime the PC flags are changed in a
**	way that may change either PCF_USR or PCF_UIO.
**	Likewise either of the trap flags PCF_TR1 or PCF_TR2, although
**	there is a special SETTRAP macro for them when they are being
**	generated rather than the flags merely being set.
** The cpu.mr_usrmode variable still reflects the previous mode prior to the
** flag change.  Note the checks in Exec mode to make sure that the
** "previous context" mapping (cpu.vmap.prev) is correctly set based on the
** setting of PCF_UIO.
**	During User mode, both maps are set to User even though it
** shouldn't really matter, since none of PXCT or UMOVE/M can be executed
** in User mode anyway.
**	Note that PCCACHE_RESET must be called if the current map is changed,
** since any information cached about the current PC becomes invalid.
*/
void
apr_pcfcheck(void)
{
    if (PCFTEST(PCF_USR)) {		/* Want to be in USER mode? */
	if (!cpu.mr_usrmode) {		/* Leaving Exec mode? */
	    vmap_set(cpu.vmap.user, cpu.vmap.user); /* Set all maps to User */
	    cpu.mr_usrmode = TRUE;
	    PCCACHE_RESET();
#if KLH10_JPC
	    cpu.mr_ejpc = cpu.mr_jpc;	/* Update last EXEC JPC */
	    cpu.mr_jpc = cpu.mr_ujpc;	/* Restore previous user JPC */
# if KLH10_ITS_JPC
	    cpu.pag.pr_ejpc = cpu.mr_jpc; /* Update most recent Exec JPC */
	    cpu.mr_jpc = cpu.pag.pr_ujpc; /* And restore prev user JPC */
# endif	/* KLH10_ITS_JPC */
#endif /* KLH10_JPC */
	}
    } else {				/* Want to be in EXEC mode */
	if (cpu.mr_usrmode) {		/* Leaving User mode? */
	    cpu.mr_usrmode = FALSE;
	    vmap_set(cpu.vmap.exec,
			(PCFTEST(PCF_UIO) ? cpu.vmap.user : cpu.vmap.exec));
	    PCCACHE_RESET();
#if KLH10_JPC
	    cpu.mr_ujpc = cpu.mr_jpc;	/* Update last User JPC */
	    cpu.mr_jpc = cpu.mr_ejpc;	/* Restore previous Exec JPC */
# if KLH10_ITS_JPC
	    cpu.pag.pr_ujpc = cpu.mr_jpc; /* Update most recent User JPC */
	    cpu.mr_jpc = cpu.pag.pr_ejpc; /* And restore previous Exec JPC */
# endif /* KLH10_ITS_JPC */
#endif /* KLH10_JPC */
	} else				/* Already in Exec mode */
	    cpu.vmap.prev =
			(PCFTEST(PCF_UIO) ? cpu.vmap.user : cpu.vmap.exec);
    }
    /* Check to see if new flags require special handling outside insn loop.
    ** These are traps plus one-proceed/AFI.
    */
    if (PCFTEST(PCF_TR1|PCF_TR2|PCF_1PR|PCF_AFI))
	INSBRKSET();			/* Ensure they get handled. */
}

#if KLH10_CPU_KS

/* RDHSB ( = CONSZ TIM,)
** WRHSB ( = CONSZ MTR,)
**	Read and write phys addr of Halt Status Block.
*/
ioinsdef(io_rdhsb)		/* CONSZ TIM, */
{
    vm_write(e, cpu.mr_hsb);	/* Read Halt Status Block */
    return PCINC_1;
}
ioinsdef(io_wrhsb)		/* CONSZ MTR, */
{
    cpu.mr_hsb = vm_read(e);	/* Write Halt Status Block */
    return PCINC_1;
}
#endif /* KLH10_CPU_KS */


#if KLH10_SYS_ITS

/* RDPCST (70144 = DATAI CCA,)
** WRPCST (70154 = DATAO CCA,)
**	Some random ITS KS10 instructions for doing
**	PC sampling.  Hope nothing in the system depends on them.
*/
ioinsdef(io_rdpcst)
{	return PCINC_1;
}
ioinsdef(io_wrpcst)
{	return PCINC_1;
}
#endif /* KLH10_SYS_ITS */


/* I_XCT.  Duplicate of main instruction loop, except returns if execution
**	completed successfully.
** NOTE: This routine is responsible for making sure that code execution
**	proceeds no deeper; that is, once i_xct() is called, *nothing*
**	must result in a further call to i_xct, or the KLH10 stack is
**	susceptible to overflow.
**	There are only two possible instructions that could cause this:
**	another XCT, or a LUUO.  Thus we check for both, and execute them
**	within the loop if necessary rather than using op_xct.
*/
pcinc_t i_pxct(int, int, vaddr_t);

insdef(i_xct)
{
    register w10_t instr;

    for (;;) {
	if (ac && !cpu.mr_usrmode)	/* If non-zero AC, becomes PXCT, */
	    return i_pxct(op, ac, e);	/* but only in EXEC mode. */

	instr = vm_fetch(e);		/* Fetch instr (may page fault) */
#if KLH10_EXTADR
	/* Compute E (may page fault), using the XCT's E for default section */
	e = xea_calc(instr, va_sect(e));
#else
	e = ea_calc(instr);		/* Compute E (may page fault) */
#endif
	ac = iw_ac(instr);
	op = iw_op(instr);

	/* Check fetched instruction; try to optimize most common case */
	if ( (cpu.opdisp[op] != i_xct)
	  && (cpu.opdisp[op] != i_luuo))
	    return op_xct(op, ac, e);	/* Do it!  May page fault */

	/* Ah.  If XCT just continue loop;
	** if LUUO, must duplicate the LUUO code here (sigh).
	*/
	if (cpu.opdisp[op] == i_luuo) {
#if KLH10_EXTADR
	    /* Note special extended case, where it's safe to invoke the
	    ** complex handling in i_luuo() because it won't call i_xct.
	    */
	    if (PC_ISEXT)
		return i_luuo(op, ac, e);
#endif
	    LRHSET(instr,		/* Build clean instruction wd */
			((h10_t)op<<9)|(ac<<5),
			va_insect(e));
	    va_lmake(e, 0, 040);	/* Get vaddr_t for loc 40 */
	    vm_write(e, instr);		/* Store the word */
	    va_linc(e);			/* Bump new fetch address to 41 */
	    ac = 0;			/* Ensure not mistaken for PXCT */
	}

	CLOCKPOLL();			/* Guard against infinite XCT chain */
	if (INSBRKTEST()) apr_int();
    }					/* Continue for another XCT/LUUO */
}

/* PXCT, XCTR, XCTRI - Previous Context Execute.
**	There is a possible ambiguity with respect to PXCT'd instructions
**	that set the trap flags.
**	- Are the trap flags actually set?  User or Exec?
**	- If exec, are the trap instructions executed?
** The current implementation allows the current (exec mode) trap flags
** to be set, which means they will take effect just before the next
** exec-mode instruction (after the PXCT) is fetched, and the exec's trap
** instructions will be executed.
*/
insdef(i_pxct)
{
    register w10_t instr;

    if (cpu.mr_usrmode)			/* Must be in EXEC mode */
	return i_muuo(op, ac, e);	/* If not, treat as MUUO */

    /* Not all instructions are supported.  Check a bit first. */
    instr = vm_fetch(e);		/* Fetch instr (may page fault) */
    op = iw_op(instr);
#if !KLH10_EXTADR
    if (op == I_EXTEND) {		/* Special-case hairy handling */
	panic("i_pxct: PXCT of EXTEND not supported");
    }
#endif

    /* Fake out mem refs temporarily.
    ** Any abnormal return to main loop (instr abort or page fault)
    ** must back out by undoing this fakeout!  Hence the set of
    ** "cpu.mr_inpxct" to indicate what's going on.
    */
    cpu.mr_inpxct = ac;			/* Now faking out map ptrs */

    if (ac & 010)
	{ cpu.vmap.xea  = cpu.vmap.prev; cpu.acblk.xea  = cpu.acblk.prev; }
    if (ac &  04)
	{ cpu.vmap.xrw  = cpu.vmap.prev; cpu.acblk.xrw  = cpu.acblk.prev; }
    if (ac &  02)
	{ cpu.vmap.xbea = cpu.vmap.prev; cpu.acblk.xbea = cpu.acblk.prev; }
    if (ac &  01)
	{ cpu.vmap.xbrw = cpu.vmap.prev; cpu.acblk.xbrw = cpu.acblk.prev;
#if KLH10_CPU_KLX
	/* In order to adequately fake out PXCT of PUSH and POP when the stack
	** is remapped by bit 12, it's necessary to fake out the test of
	** PC section when determining stack pointer format.
	** This is done here by temporarily inserting
	** the pager PrevContextSection into the PC section field.
	** This screws any jumps, but that's OK as PXCTd jumps are undefined.
	** This may also affect PXCT of MOVSLJ; worry about that later.
	** Note that [Uhler83] claims bit 12 is unused and the test is always
	** made based on PC section, but that may only be for the KC10, and
	** the KL diagnostics (DFKED) clearly do expect it to work.
	** Even if no monitor ever does a PXCT of PUSH or POP.  Sigh.
	*/
	cpu.mr_pxctpc = cpu.mr_PC;	/* Save PC */
	PC_SET30(pag_pcsfget());	/* Set to just section */
#endif
    }

    ac = iw_ac(instr);		/* Now can set to new instr's AC */
#if KLH10_EXTADR
    /* Special hair is needed here beyond the usual extended EA stuff.
    ** This is implemented by the xea_pxctcalc facility.
    */
    e = xea_pxctcalc(instr, (unsigned)va_sect(e),
		     cpu.acblk.xea, cpu.vmap.xea, 010, 04);
#else
    e = ea_calc(instr);		/* Compute E (may page fault) */
#endif

  {				/* Try to help compiler optimize */
    register pcinc_t pci;	/* the temporary use of this reg */

    pci = op_xct(op, ac, e);	/* Do it!  May page fault */
    pxct_undo();		/* Done, clean up after ourselves */
    return pci;			/* Return whatever the instr did */
  }
}

void
pxct_undo(void)
{
    acmap_set(cpu.acblk.cur, cpu.acblk.prev);	/* Restore normal AC maps */
    vmap_set(cpu.vmap.cur, cpu.vmap.prev);	/* And normal VM map */
#if KLH10_CPU_KLX
    if (cpu.mr_inpxct & 01)			/* If bit 12 was set, */
	cpu.mr_PC = cpu.mr_pxctpc;		/* restore original PC */
#endif
    cpu.mr_inpxct = FALSE;			/* No longer doing PXCT */
}

#if KLH10_EXTADR
/* XEA_PXCTCALC - Implements EA calculation for PXCT
**	as described by [Uhler83 12.6 (p.46)].
**
** This involves two differences from the normal EA calculation:
** (1) Pre-processing to use PCS if E is using previous context.
** (2) Post-processing to use PCS if:
**	E is local and E computation didn't use previous context, but
**	memory reference *is* using previous context.
**
** NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE NOTE
** !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
**	It has been proven empirically that this algorithm is **NOT** how
**	a real KL operates under at least the following circumstance:
**			PCS=0  X/ <NZ>,,Y
**		XCT 4,[MOVE 1,(X)]
**	According to the doc, the EA is computed in current context, which
**	results in a GLOBAL address of <NZ>,,Y that is then used for a ref
**	in previous context (and bombs if section <NZ> doesn't exist!).
**
**	However, on a real KL, the reference is made to 0,,Y.  Why?
**	Don't know.  Until someone figures out from prints
**	exactly what is going on, the following hack is used to modify
**	the algorithm to match the KL more closely:
**		For case (2), if PCS=0, locality of E is ignored; mem ref
**		is always made using PCS.
*/
vaddr_t
xea_pxctcalc(register w10_t iw,
	register unsigned sect,	/* Current section */
	register acptr_t acp,	/* AC block mapping */
	register pment_t *map,	/* Page table mapping */
	int ebit, int dbit)	/* E-calc and Data-ref bits from PXCT */
{
    register vaddr_t e;
    register int pxbits = cpu.mr_inpxct;

    if (ebit & pxbits) {		/* If mapping E calc */
	/* E-bit set, do pre-proc but never post-proc */
	sect = pag_pcsget();			/* Make default section PCS */
	return xea_xcalc(iw, sect, acp, map);	/* Compute E normally */
    }

    /* E-bit not set, may do post-proc */
    e = xea_xcalc(iw, sect, acp, map);	/* Compute E normally */
    if ((pxbits & dbit)			/* D set? (prev ctxt mem ref) */
      && (!(sect = pag_pcsget())	/* PCS==0? (SEE NOTE ABOVE!!) */
	    || va_islocal(e))) {	/* or E is local? */
	va_lmake(e, sect, va_insect(e));	/* Yup, apply PCS post-proc */
    }
    return e;
}
#endif /* KLH10_EXTADR */

#if KLH10_CPU_KS

/* UMOVE, UMOVEM - Move from and to user memory.
**	Even though these are nominally identical to PXCT 4,[MOVE/M]
**	they are implemented with their own routines, partly because
**	there *is* one difference (legal in User-IOT mode) and partly
**	because they are used in lots of places.
** Note the use of vm_xmap() which directly specifies the mapping to
** use, thus avoiding any need to set cpu.mr_inpxct.
**	Unfortunately, these instructions are only defined for the KS,
**	although they would be very handy for the KL as well.
*/
insdef(i_umove)
{
    if (cpu.mr_usrmode && !PCFTEST(PCF_UIO))	/* Must be EXEC or User-IOT */
	return i_muuo(op, ac, e);
    ac_set(ac, vm_pget(				/* Do it, may page fault */
		vm_xmap(e, VMF_READ, cpu.acblk.prev, cpu.vmap.prev)));
    return PCINC_1;
}

insdef(i_umovem)
{
    if (cpu.mr_usrmode && !PCFTEST(PCF_UIO))	/* Must be EXEC or User-IOT */
	return i_muuo(op, ac, e);
    vm_pset(vm_xmap(e, VMF_WRITE, cpu.acblk.prev, cpu.vmap.prev),
		ac_get(ac));			/* Do it, may page fault */
    return PCINC_1;
}
#endif /* KS */

/* TRAP_XCT - Called when about to execute next instruction and notice
**	that trap flags are set.  Paging must be ON.
** PC points to not-yet-executed new instruction.  In order to fake out
** instruction execution routines, we back this up by 1, which makes
** normal return work properly.
** However, any abnormal return to main loop (page fault or PI interrupt)
** must back out by undoing this!
**
** KLX notes:
**	There is a serious problem here with extended addressing.
** In order to properly determine E for the trap instruction in an
** extended program, we must know the section # of the instruction that
** caused the trap (this constitutes the default section for EA computation).
**	However, in the current KLH10 architecture, by the time we get here
** we can't know for sure where the guilty instruction was!  PC has already
** been changed and if it was a jump of some kind then we can't tell 
** what it was before (note normal increments and skips are always local,
** so for those we always know what PC section was).
** It may be possible to use the JPC feature to help out here.
**		The only jumps that set Trap 1 are SOJ and AOJ.
**		The only jumps that set Trap 2 are POPJ and PUSHJ.
**	Finally, it's not clear which section is the default section if
** the trap occured as a result of an XCT chain.  Hopefully it is *NOT*
** that of the final instruction word, since while we might be able to
** determine the correct section, there's no way we can find out from PC
** where the original XCT was.
**	For the time being, we'll use PC section, and hope nothing loses
**	too badly.
*/
static void
trap_xct(void)
{
    register w10_t instr;
    register paddr_t loc;

    /* Find location of trap instruction and fetch it.
    ** This is a physical memory address, so no page fault is possible.
    */
    switch (cpu.mr_pcflags & (PCF_TR1|PCF_TR2)) {	/* Find trap offset */
	case 0:		return;			/* Not trapping?? */
	case PCF_TR1:	loc = UPT_TR1;	break;
	case PCF_TR2:	loc = UPT_TR2;	break;
	case (PCF_TR1|PCF_TR2): loc = UPT_TR3; break;
    }
    loc += (cpu.mr_usrmode ? cpu.mr_ubraddr : cpu.mr_ebraddr);	/* Find loc in PT */
    instr = vm_pget(vm_physmap(loc));		/* Fetch the trap instr */

#if KLH10_DEBUG
    if (cpu.mr_debug) {
	fe_begpcfdbg(stderr);
	fprintf(stderr,"TRAP:{");
	pinstr(stderr, instr, 0, 0L);	/* 0L not right but ok for now */
    }
#endif

    cpu.mr_intrap = cpu.mr_pcflags & (PCF_TR1|PCF_TR2);	/* Remember traps */
    cpu.mr_pcflags &= ~(PCF_TR1|PCF_TR2);		/* Clear from flags */
    PC_ADD(-1);						/* Fake 'em out */

    /* Now execute it */
#if KLH10_EXTADR
    PC_ADDXCT(op_xct(iw_op(instr), iw_ac(instr), xea_calc(instr, PC_SECT)));
#else
    PC_ADDXCT(op_xct(iw_op(instr), iw_ac(instr), ea_calc(instr)));
#endif
    cpu.mr_intrap = 0;		/* Normal return, no longer doing trap instr */

#if KLH10_DEBUG
    if (cpu.mr_debug) {
	putc('}', stderr);
	fe_endpcfdbg(stderr);
    }
#endif
}

/* TRAP_UNDO - Called by abnormal instruction termination (abort or pagefault)
**	while executing a trap instruction, to undo stuff properly.
*/
void
trap_undo(void)
{
    if (cpu.mr_intrap) {
	cpu.mr_pcflags &= ~(PCF_TR1|PCF_TR2);	/* Clear from PC flags */
	cpu.mr_pcflags |= cpu.mr_intrap;	/* Add old flags */
	cpu.mr_intrap = 0;
	PC_ADD(1);				/* Undo trap's backup of PC */
	INSBRKSET();				/* Want attention later */
    }
}

#if KLH10_CPU_KS

/* KS10 Clock and Interval Timing system
**
** See the PRM p.4-37 for a description of KS system timing.  The original
** code here attempted to emulate the KS millisecond counter behavior by
** calling tim_ksupdate() at each millisecond countdown, but there is no
** real reason to do a timer update at anything less than the interval
** timer interrupt frequency, since all of the interaction between the
** monitor and the hardware is done via specific instructions that
** can take care to fake things out correctly.
**
**	The KS10 basically has two ways of tracking time, a 71-bit
** time base and a 35-bit interval timer, both of which are
** run at exactly 4.1 MHz.  The reason for this odd frequency appears to
** be an attempt to have the 12-bit internal counter count out at
** almost exactly one millisecond (1<<12 == 4096), but it really is
** exactly 4.1 MHz and not 4.096MHz!!  Sigh, but close enough.
**
** 	This means each tick (low-order bit) represents 1/4.1 == 0.243902439...
** usec, and 4 ticks represent what ITS calls a "short usec" of
** 1/1.025 == .975 usec.  Thus ignoring the low-order 2 bits gives
** a count of short usecs, and ignoring the low-order 12 bits gives a
** count of milliseconds (to be precise, 999.02439 usec).
**
** This code relies on the CLK facilities to provide either of two
** emulation methods: synchronous countdown, or OS run-time interrupts.
** See KN10CLK.C for more details.  When using the synchronous method,
** each cycle tick should be considered equal to 1 virtual usec since
** that's roughly how fast a real KS10 executes instructions.
**
** The ITS pager's "quantum timer" is also driven off this counter so
** as to emulate a 1 MHz clock rate.  On a KA the quantum timer returned by
** SPM is a 4.096/4 usec quantity.  On a KL there is no quantum timer but
** it is approximated by performance counters to provide similar 4.096-usec
** units to ITS.  On a KS (which implements SPM) the units are 3.9/16 usec
** (same as the time base) which ITS converts to 3.9-usec.
** Theoretically the quantum timer is continuously
** incremented whenever there is no PI in Progress.  Since it is hacked
** in native mode, only 32 bits are supported, but that should be sufficient
** since the original KA-10 counter was only 18 bits.
** To emulate this with a low-resolution clock, the quantum counter is
** only updated whenever:
**	PIP status changes:
**		Enter PIP: freeze quant
**		Leave PIP: unfreeze quant
**	counter is read by ITS SPM instruction: freeze, read, unfreeze
**	counter is set by ITS LMPR instruction: set, unfreeze
**	millisecond clock ticks: add 1000 if unfrozen (not in PIP)
** The mechanics of un/freezing are:
**	To unfreeze (start, so it can be ticked up by 1ms clock):
**		quant = quant + <usec-left-in-interval>
**	To freeze value (stop, so it can be read):
**		quant = quant - <usec-left-in-interval>
**
** The basic idea is to normalize the quant counter whenever it is
** decoupled from the interval clock increment, by seeing how far it is in usec
** to the next interval tick.
**
** The same principle applies to maintaining the time base.
**
** Note that the code makes a simplifying assumption that each counter tick
** (ctick) is equivalent to 1/4 usec, rather than exactly 1/4.1 usec.
** This allows usec quantities to be added and subtracted from the counters
** simply by shifting them up by 2 bits.
*/

static void tim_ksupdate(void *);

/* The main loop countdown is considered to be in units of short microseconds.
** Each such unit is equivalent to 4 timebase units.  The countdown starts
** at a value calculated to reach zero after 1 millisecond, whereupon all
** other counters are updated.
*/

/* TIM_INIT - Initialize time stuff.  KS version.
*/
static int
tim_init(void)
{
    /* Init new internal clock */
    clk_init();

#if KLH10_ITIME_SYNCH
    clk_itickset(CLK_USECS_PER_SEC/60);		/* Default to 60HZ */
#elif KLH10_ITIME_INTRP
    clk_itickset(CLK_USECS_PER_SEC/30);		/* Default to 30HZ */
#endif						/* (60 still too fast) */

    clk_itmrget(tim_ksupdate, (char *)NULL);	/* Put in 1st itick entry */

#if KLH10_RTIME_SYNCH
    cpu.tim.tim_base[0] = 0;		/* Initialize time base */
    cpu.tim.tim_base[1] = 0;
    cpu.tim.tim_base[2] = 0;
    tim_baseunfreeze();			/* Start it going */

#elif KLH10_RTIME_OSGET
#  if KLH10_SYS_ITS	/* ITS uses timebase as date/time clock if value OK */
  {
    FILE *f;

    if ((f = fopen(TIMEBASEFILE, "r")) == NULL) {
	LRHSET(cpu.tim.wrbase.w[0], 0, 0);
	LRHSET(cpu.tim.wrbase.w[1], 0, 0);
	os_rtmget(&cpu.tim.osbase);
	return FALSE;
    }
    fread((char *)&cpu.tim.wrbase, sizeof(cpu.tim.wrbase), 1, f);
    fread((char *)&cpu.tim.osbase, sizeof(cpu.tim.osbase), 1, f);
    os_rtm_adjust_base(&cpu.tim.osbase, &cpu.tim.osbase, 0);
    if (ferror(f)) {
	fclose(f);
	return FALSE;
    }
    fclose(f);
  }
#  else		/* T20 (and T10?) use timebase as uptime clock, must clear */
    LRHSET(cpu.tim.wrbase.w[0], 0, 0);
    LRHSET(cpu.tim.wrbase.w[1], 0, 0);
    os_rtmget(&cpu.tim.osbase);
#  endif /* !ITS */
#endif

    return TRUE;
}


#if KLH10_QTIME_OSREAL || KLH10_QTIME_OSVIRT
static osrtm_t qcbase;

/* Freeze - get realtime and compute # quantums used since last qcbase. */
int32
quant_freeze(int32 qc)
{
    osrtm_t rtm;

#if KLH10_QTIME_OSREAL
    os_rtmget(&rtm);		/* Find current realtime */
#else
    os_vrtmget(&rtm);		/* Find current virtual time used */
#endif
    os_rtmsub(&rtm, &qcbase);	/* Get diff since last freeze */

    /* Paranoia check - last line of defense against systems that
    ** don't report runtime in a monotonically increasing way.
    ** See os_vrtmget() for more detail.
    */
#if CENV_SYSF_BSDTIMEVAL
    if (rtm.tv_sec < 0 || rtm.tv_usec < 0) {
# if 0	/* Disable output for now.  Perhaps bump a meter later */
	fprintf(stderr, "[Neg quantum! %ld,%ld]\r\n",
		(long)rtm.tv_sec, (long)rtm.tv_usec);
# endif
	return qc;
    }
#endif	/* CENV_SYSF_BSDTIMEVAL */

    return qc + (int32)os_rtm_toqct(&rtm);	/* Convert to quantum ticks! */
}

/* Unfreeze - get realtime and remember as qcbase for current quantum count. */
int32
quant_unfreeze(int32 qc)
{
#if KLH10_QTIME_OSREAL
    os_rtmget(&qcbase);		/* Find current realtime */
#else
    os_vrtmget(&qcbase);	/* Find current virtual time used */
#endif
    return qc;
}
#endif /* OSREAL || OSVIRT */

#if KLH10_RTIME_SYNCH

static void
tim_basefreeze(void)		/* Freeze to read actual value */
{
    register uint32 adjtim;
    adjtim = CLK_USEC_UNTIL_ITICK() << 2;
    cpu.tim.tim_base[0] = (cpu.tim.tim_base[0] - adjtim) & MASK32;
    if (cpu.tim.tim_base[0] > -adjtim) {	/* Check for underflow */
	if ((cpu.tim.tim_base[1] = (cpu.tim.tim_base[1]-1)&MASK32) == MASK32)
	    cpu.tim.tim_base[2]--;
    }
}

static void
tim_baseunfreeze(void)
{
    register uint32 adjtim;
    adjtim = (CLK_USEC_UNTIL_ITICK() << 2);
    cpu.tim.tim_base[0] = (cpu.tim.tim_base[0] + adjtim) & MASK32;
    if (cpu.tim.tim_base[0] < adjtim) {		/* Check for overflow */
	if ((cpu.tim.tim_base[1] = (cpu.tim.tim_base[1]+1)&MASK32)==0)
	    cpu.tim.tim_base[2]++;
    }
}
#endif /* KLH10_RTIME_SYNCH */

/* TIM_KSUPDATE - Invoked when KS10 interval timer goes off;
**	either OS timer fired, or internal synch counter counted out.
*/
static void
tim_ksupdate(void *ignored)
{
#if KLH10_RTIME_SYNCH
    /* Update time base. */
  {
    int32 intv4 = CLK_USECS_PER_ITICK << 2;	/* Interval in 1/4 us units */

    if (((cpu.tim.tim_base[0] += intv4) & MASK32) < intv4)
	if ((cpu.tim.tim_base[1] = (cpu.tim.tim_base[1]+1)&MASK32) == 0)
	    ++cpu.tim.tim_base[2];
  }
#endif

#if KLH10_SYS_ITS && KLH10_QTIME_SYNCH
    /* Update quantum timer if not PIP. */
    if (!cpu.pi.pilev_pip)
	cpu.pag.pr_quant += (CLK_USECS_PER_ITICK<<2);
#endif

    /* Attempt PI if interval done.
    ** For now assume that call to tim_ksupdate() always implies interval done,
    ** so always fire if permitted.
    */
    if (op10m_skipn(cpu.tim.tim_intreg)) {	/* If interval cntr is set */
	cpu.aprf.aprf_set |= APRF_TIM;		/* "Timer Interval Done" */
	apr_picheck();				/* Check to trigger PI */
    }
}

#if KLH10_DEBUG
int tim_debug = 0;	/* Hack for KS timebase debugging */
#endif

/* TIM_RDTIM - Get time base using OS real time.
*/
static void
tim_rdtim(register dw10_t *tbase)
{
#if KLH10_RTIME_SYNCH
    register w10_t hi, lo;

    /* Freeze/normalize the time-base quantity at this point */
    tim_basefreeze();

#define TB cpu.tim.tim_base
    RHSET(lo, TB[0] & (H10MASK&~03));	 /* Low 18 bits in RH */
    LHSET(lo, (TB[0] >> 18) & (((h10_t)1<<(32-18))-1));	/* 14 bits in LH */
    LHSET(lo, (LHGET(lo) | (TB[1] << 14)) & MASK17);	/* 3 more */
	/* Note sign bit of low word must be clear, ditto bottom 2 bits */
    RHSET(hi, (TB[1] >> 3) & H10MASK);			/* Shove 18 into RH */
    LHSET(hi, (TB[1] >> 21) & (((h10_t)1<<(32-21))-1));	/* 11 more bits */
    LHSET(hi, (LHGET(hi) | (TB[2] << 11)) & H10MASK);	/* All rest */
#undef TB
    tim_baseunfreeze();
    tbase->w[0] = hi;
    tbase->w[1] = lo;

#elif KLH10_RTIME_OSGET
    osrtm_t rtm;

    if (!os_rtmget(&rtm)) {
	op10m_setz(tbase->w[0]);
	op10m_setz(tbase->w[1]);
	return;
    }

    /* Now find difference between current time and old systime */
    os_rtmsub(&rtm, &cpu.tim.osbase);
#if KLH10_DEBUG
    if (tim_debug)
	printf("OS time diff: %ld sec, %ld usec\r\n",
	       (long)OS_RTM_SEC(rtm), (long)OS_RTM_USEC(rtm));
#endif
    /* Now convert this quantity to KS10 ticks, add that to old timebase,
    ** and return the new timebase value.
    ** A real pain on these crippled 32-bit machines.
    */
    os_rtm_tokst(&rtm, tbase);

#if KLH10_DEBUG
    if (tim_debug)
	printf("KS time diff: %#lo,,%#lo %#lo,,%#lo\r\n",
		(long) LHGET(HIGET(*tbase)), (long) RHGET(HIGET(*tbase)), 
		(long) LHGET(LOGET(*tbase)), (long) RHGET(LOGET(*tbase)) );
    if (tim_debug)
	printf("KS  old time: %#lo,,%#lo %#lo,,%#lo\r\n",
	    (long) LHGET(HIGET(cpu.tim.wrbase)), (long) RHGET(HIGET(cpu.tim.wrbase)), 
	    (long) LHGET(LOGET(cpu.tim.wrbase)), (long) RHGET(LOGET(cpu.tim.wrbase)) );
#endif

    /* Slightly faster double addition.  Assumes low signs always 0 */
    op10m_udaddw(*tbase, cpu.tim.wrbase.w[1]);	/* First add low word */
    op10m_add(tbase->w[0], cpu.tim.wrbase.w[0]); /* Then add high word */

#if KLH10_DEBUG
    if (tim_debug)
	printf("KS  new time: %#lo,,%#lo %#lo,,%#lo\r\n",
		(long) LHGET(HIGET(*tbase)), (long) RHGET(HIGET(*tbase)), 
		(long) LHGET(LOGET(*tbase)), (long) RHGET(LOGET(*tbase)) );
#endif

#endif
}

/* TIM_WRTIM - Set time base, relative to OS real time.
**	Makes permanent record of offset so that the next KLH10
**	startup can pretend that the clock kept running.
*/
static void
tim_wrtim(register dw10_t *tbase)
{
#if KLH10_RTIME_SYNCH
    dw10_t d;

    d = *tbase;
    tim_basefreeze();		/* Stop, to get valid current time */
#define TB cpu.tim.tim_base
    TB[0] &= ~MASK12;		/* Keep bottom 12 bits of current time! */
    TB[0] |= ((uint32)LHGET(LOGET(d)) << 18) | (RHGET(LOGET(d))&~MASK12);
    TB[1] = (LHGET(LOGET(d)) >> (18-(35-32))) & 07;	/* High 3 */
    TB[1] |= (((uint32)LHGET(HIGET(d)) << 18) | RHGET(HIGET(d))) << 3;
    TB[2] = LHGET(HIGET(d)) >> (32-(18+3));
#undef TB
    tim_baseunfreeze();

#elif KLH10_RTIME_OSGET
    FILE *f;
    osrtm_t absolute_osbase;

    cpu.tim.wrbase = *tbase;
    op10m_andcmi(cpu.tim.wrbase.w[1], MASK12);	/* Zap low 12 bits */

    if (!os_rtmget(&cpu.tim.osbase)) {
	return;
    }
#  if KLH10_SYS_ITS	/* ITS wants timebase to endure over reloads */
    if ((f = fopen(TIMEBASEFILE, "w")) == NULL)
	return;
    fwrite((char *)&cpu.tim.wrbase, sizeof(cpu.tim.wrbase), 1, f);
#  if 1
    os_rtm_adjust_base(&cpu.tim.osbase, &absolute_osbase, 1);
    fwrite((char *)&absolute_osbase, sizeof(absolute_osbase), 1, f);
#  else
    fwrite((char *)&cpu.tim.osbase, sizeof(cpu.tim.osbase), 1, f);
#  endif
    fclose(f);
#  endif /* ITS */
#endif
}


/* IO_RDTIM -  (CONO TIM,)
*/
ioinsdef(io_rdtim)
{
    register vmptr_t p0, p1;
    dw10_t d;

    p0 = vm_xrwmap(e, VMF_WRITE);	/* Check access for 1st word */
    va_inc(e);				/* Bump E by 1, may wrap funnily */
    p1 = vm_xrwmap(e, VMF_WRITE);	/* Check access for 2nd word */

    tim_rdtim(&d);			/* Read time double-word into here */
    vm_pset(p0, HIGET(d));		/* OK, set the double-word */
    vm_pset(p1, LOGET(d));
    return PCINC_1;
}

/* IO_WRTIM -  (CONO MTR,)	Set Time Base
**	Note that the bottom 12 bits cannot be set.  They are left as whatever
**	they already were in the time base registers.
*/
ioinsdef(io_wrtim)
{
    dw10_t d;
    vm_dread(e, d);		/* Fetch the double */
    tim_wrtim(&d);		/* Store it as time base! */
    return PCINC_1;
}

/* IO_RDINT -  (CONI TIM,)
*/
ioinsdef(io_rdint)
{
    vm_write(e, cpu.tim.tim_intreg);	/* Store interval register into E */
    return PCINC_1;
}

/* IO_WRINT -  (CONI MTR,)	Set KS10 interval counter period from c(E)
**
**	The interval counter counts down in the same units as the time
** base (at 4.1MHz, each tick represents 1/4.1 usec)
** This instruction is only invoked once during monitor startup
** so doesn't need to be fast, and only a few specific cases are provided
** for.
**	ITS uses a   60 Hz period (0205355)
**	T20 uses a 1000 Hz period ( 010004)
**	T10 uses a   60 Hz period (0200000) but twiddles it incessantly
**				for leap jiffy, leap msec, etc.
**
** It isn't clear what happens if the period is set to 0.  This code
** assumes it turns the counter off.  It's possible a real KS might
** interpret this as interrupting the next time the counter overflows,
** which could be anywhere from 1 usec to 140 minutes.
*/
ioinsdef(io_wrint)
{
    register w10_t w;

    w = vm_read(e);	/* Read new interval register from E */
    cpu.tim.tim_intreg = w;

#if KLH10_ITIME_INTRP
    INTF_INIT(cpu.intf_clk);
    if (op10m_skipn(w)) {
	register uint32 usec;

	if (!LHGET(w)) {
	    if (RHGET(w) == 0205355)		/* ITS: 1/60 sec? */
		usec = CLK_USECS_PER_SEC/60;
	    else if (RHGET(w) == 01004)		/* T20: 1ms? */
		usec = CLK_USECS_PER_MSEC;
	    else if ((0200000 <= RHGET(w))	/* T10: within "60Hz" range? */
		 && (RHGET(w) <= 0220000))
		usec = CLK_USECS_PER_SEC/60;
	    else
		goto gencase;
	}
	else if (LHGET(w) < 037777) {			/* Ensure no ovfl */
	  gencase:
	    /* General case: usec = (ticks * 1/4.1)
	    ** Compute this as ((ticks * 10) / 41)
	    */
	    usec = (LHGET(w) << H10BITS) | RHGET(w);
	    usec = (usec * 10) / 41;
	} else {
	    fprintf(stderr, "[io_wrint: Unsupported interval: %lo,,%lo]\r\n",
				(long)LHGET(w), (long)RHGET(w));
	    return PCINC_1;	/* Do nothing */
	}

	/* Ask to change interval timer.  Normally this request will
	** be ignored, unless KLH10 user has cleared clk_ithzfix.
	** See corresponding KL code in io_co_tim().
	*/
	clk_itickset(usec);	/* Set timer going! */

    } else {
#if 0
	/* Don't allow KS to get any more interval timer ints, but
	** must keep internal CLK timer alive.
	*/
#endif
    }
#endif /* KLH10_ITIME_INTRP */

    return PCINC_1;
}
#endif /* KLH10_CPU_KS */

#if KLH10_CPU_KL

/* KL timer and meter code */

/*
The KL10 has two different time counters, a time base and an interval counter,
very similar to those for the KS.

INTERVAL COUNTER:
	The interval counter is a 12-bit 100kHz counter (each unit is
exactly 10 usec).  The minimum interval is thus 10 usec to 40.950
msec.  TOPS-20 sets its interval to 0144 (100.) so that it interrupts
every 1ms.
	Whenever the countup reaches a value equal to the interval
period, it sets its "Done" flag (which may trigger a PI), clears
itself, and starts over.  If the count reaches its maximum value
without matching the set period (this can only happen if the period is
set at a time when the count is already greater than the period), then
both the "Done" and "Overflow" flags are set.
	PI for the interval counter is specially handled -- it
executes the instruction at EPT+514.  Hence it needs to be checked for
in a slightly different way.

	Synchronous mode emulation: each instruction is interpreted as 1 usec
		and the interval period used for the clock poll countdown.
		If the interval counter is off, a default period of 1ms
		is used for clock polling.
	Interrupt mode emulation: the interval period is translated into
		suitable host platform units.  Currently there may be a
		check for T20's 1ms period to silently substitute a more
		manageable 10ms period instead.

TIME BASE:
	The time base is one of the peculiar KL double-word counts.
The 16-bit hardware count portion is incremented at 1MHz (one tick = 1
usec); note this is shifted up 12 bits from the low end of the doubleword.
(TOPS-20 basically ignores those low 12 bits.)

	Synchronous mode emulation: As for the interval counter, each
		instruction is interpreted as 1 usec and the time base
		maintained from the clock poll countdown (which has a
		period corresponding to either the interval counter or
		the default polling count).
	OS mode emulation: every RDTIME request instigates a host platform
		system call for the current time-of-day in units as small
		as feasible.  From this is subtracted the OS time base
		as of the last KL timebase check, and the result
		translated into 1MHz units.

Beware: Only 32 bits of hardware counter are guaranteed, which at 1MHz is
	approximately 4K sec == 1 hour.  There will be a time loss for
	KLH10_RTIME_OSGET if execution is suspended for that long!
*/

static void tim_klupdate(void *);

/* TIM_INIT - Initialize time stuff.  KL version.
*/
static int
tim_init(void)
{
    /* Initialize interval counter */
    cpu.tim.tim_on = 0;
    cpu.tim.tim_flgs = 0;
    cpu.tim.tim_intper = 0;
    cpu.tim.tim_intcnt = 0;

    /* Initialize time base */
#if KLH10_RTIME_SYNCH
    cpu.tim.tim_ibased = 0;
    cpu.tim.tim_ibaser = 0;
#elif KLH10_RTIME_OSGET
    if (!os_rtmget(&cpu.tim.tim_osbase)) {	/* Get OS realtime */
	fprintf(stderr, "tim_init: Cannot get OS realtime!\n");
	return FALSE;
    }
#endif

    /* Init internal clock */
    clk_init();
    clk_itickset(CLK_USECS_PER_SEC/30);		/* Default to 30HZ */
						/* (60 still too fast) */
    clk_itmrget(tim_klupdate, (char *)NULL);	/* Put in 1st itick entry */

    return TRUE;
}

/* TIM_KLUPDATE - Invoked once per ITICK (interval timer tick)
**	KL version.
*/
static void
tim_klupdate(void *ignored)		/* Arg is unused */
{
#if KLH10_RTIME_SYNCH
    /* Update time base by adding # of usec since last update, which
     * may have been due to either this function or a RDTIME.
     * Avoid bogus value in case interval period changed.
     */
    cpu.tim.tim_ibased +=
	  (CLK_USECS_PER_ITICK < cpu.tim.tim_ibaser) ? 0
	: (CLK_USECS_PER_ITICK - cpu.tim.tim_ibaser);
    cpu.tim.tim_ibaser = 0;	/* Reset usec since last itick */
#endif

    /* Attempt PI if interval done.
    ** For now assume that call to tim_klupdate() always implies interval done.
    */
    if (cpu.tim.tim_on) {
	cpu.tim.tim_intcnt = 0;			/* Reset countup */

	cpu.tim.tim_flgs |= TIM_RDONE;		/* "Timer Interval Done" */
	if (cpu.pi.pilev_timreq |= cpu.tim.tim_lev)	/* Add PI if any */
	    pi_devupd();			/* Check to trigger intrupt */
    }
}

/* General-purpose utility to update counter doublewords
**	Note hair to ensure that "hardware" counter is shifted up by 12.
*/
#define DWCNT_UPDATE(pa, cntvar) \
  { vp = vm_physmap(pa);	\
    d = vm_pgetd(vp);		/* Get double (EPT/UPT is known safe ref) */\
    if ((cntvar) & ~MASK23)	/* Add high bits if any */\
	op10m_addi(d.w[0], ((cntvar)>>23) & H10MASK);	\
    LRHSET(w, ((cntvar) >> (H10BITS-12))&H10MASK, ((cntvar)<<12) & H10MASK); \
    op10m_udaddw(d, w);		/* Add W into D */\
    vm_psetd(vp, d);		/* Store back into original loc as update */\
    (cntvar) = 0;		/* Clear "hardware" counter */\
  }

static pcinc_t
mtr_store(register unsigned int pa,
	  register uint32 *acnt,
	  register vaddr_t e)
{
    register w10_t w;	/* Vars needed for MTR_DWUPDATE */
    register dw10_t d;
    register vmptr_t vp;

    if (cpu.mr_paging) {
	DWCNT_UPDATE(pa, *acnt)		/* Do update, leave dword in d */
    } else {
	/* If no paging, can't refer to EPT or UPT.  Just pass on
	** value of hardware counter, and don't reset it.
	*/
	LRHSET(d.w[0], 0, (*acnt >> 23) & H10MASK);  /* Hi bits into hi wd */
	LRHSET(d.w[1], (*acnt >> (H10BITS-12))&H10MASK,
			(*acnt << 12) & H10MASK);
    }

    /* Now store in desired location */
    vm_dwrite(e, d);		/* Can pagefail after all this, sigh */
    return PCINC_1;
}


/* RDPERF (70200 = BLKI TIM,) Read Performance Analysis Count
**	Read process execution time doubleword from EPT+512,
**	add current hardware counter, store result in E, E+1.
** NOTE: PRM 3-61 is *INCORRECT*.  Description there is a duplicate of
**	that for RDEACT.  EPT map on p.3-30 is right.
*/
ioinsdef(io_rdperf)
{
    return mtr_store((cpu.mr_ebraddr + EPT_PRF),	/* EPT+512 */
		&cpu.tim.tim_perf,
		e);
}

/* RDTIME (70204 = DATAI TIM,) Read Time Base
**	Read time base doubleword from EPT+510,
**	add current hardware time base counter, store result in E, E+1.
*/
/*
	The KL10 time base is a 59-bit doubleword integer (low sign is
0, low 12 bits are ignored) that increments at exactly 1MHz (1 each usec).
It's OK to set the last 12 bits if the extra precision is available from
the host platform.

Note that 20 bits are needed to store a 1M value.  Thus 32 bits is
exactly enough to hold the result of left-shifting a value up to 1M
by 12 bits (for adding into low part of word).
*/
/* NOTE:
	It appears that the only place the T20 monitor actually reads the
EPT timebase contents (rather than using RDTIME) is a place in APRSRV
called GETMID where the low word is used as a unique structure media ID.
(extremely bogusly, if you ask me).
	It would be VERY nice if the EPT update could be totally flushed
and the timebase merely returned in response to a RDTIME.  But if GETMID
is to be supported (and it looks suspiciously important), then EPT+510
needs to be updated every so often just on the .000001% probability it
might be needed.
	Perhaps a closer examination of the caller of GETMID (WRTHOM in
DSKALC.MAC) might reveal something else we could trigger the update on.
The best candidate seems to be a WRTIME (CONO MTR,) that resets the
time base; by setting the low 12 bits (otherwise unused) of the location
to some random fixed value, can ensure that the initial structure always gets
that value as its media ID.

*/
ioinsdef(io_rdtime)
{
    uint32 ibase;

#if KLH10_RTIME_SYNCH
    /* Find # usec since last RDTIME and add them into EPT timebase.
    ** tim_ibased is always added directly - it accounts for usec
    **    since last RDTIME, up to the most recent interval tick.
    ** tim_ibaser however is only used to remember the last value of
    **    CLK_USEC_SINCE_ITICK(), ie usec since the most recent interval tick.
    **    The difference between the new value and the old one is the
    **    # of usec elapsed since the last update of tim_ibased.
    */
    uint32 cticks = CLK_USEC_SINCE_ITICK();

    if (cticks >= cpu.tim.tim_ibaser) {
	ibase = cpu.tim.tim_ibased + (cticks - cpu.tim.tim_ibaser);
    } else {
	/* Shouldn't happen, but be prepared if weird change to interval
	** period causes this.
	*/
	fprintf(stderr, "[io_rdtime: tim_ibaser skew, %ld < %ld]\r\n",
		(long)cticks, (long)cpu.tim.tim_ibaser);
	ibase = cpu.tim.tim_ibased;
    }
    cpu.tim.tim_ibaser = cticks;	/* Remember last subinterval cnt */
    cpu.tim.tim_ibased = 0;

#elif KLH10_RTIME_OSGET
    osrtm_t rtm, rtmbase;

    if (!os_rtmget(&rtm))			/* Get OS realtime */
	panic("io_rdtime: Cannot get OS realtime!");
    rtmbase = rtm;				/* Remember for new base */
    os_rtmsub(&rtm, &cpu.tim.tim_osbase);	/* Find elapsed realtime  */
    cpu.tim.tim_osbase = rtmbase;		/* Set base for next time */
    ibase = os_rtm_toklt(&rtm);			/* Convert diff to KL ticks */
#else
    ibase = 0;
#endif

    return mtr_store((cpu.mr_ebraddr + EPT_TBS),	/* EPT+510 */
		&ibase,
		e);
}

/* WRPAE (70210 = BLKO TIM,) Write Performance Analysis Enables
**	Select counting method and conditions for PERF counter, based
**	on c(E).
**	Makes a read reference but otherwise is a nop for now.
*/
ioinsdef(io_wrpae)
{
    (void) vm_redmap(e);	/* Do a read reference */
    return PCINC_1;
}

/* CONO TIM, (70220) Conditions Out, Interval Counter
**	Set up interval counter according to E.
**	It's unclear what a zero interval might mean.  For the time
**	being I'm interpreting it as 1<<12 (4096.)
*/
ioinsdef(io_co_tim)
{
    register h10_t ebits = va_insect(e);
    register unsigned int newper = ebits & TIM_PERIOD;	/* 12 bits */

    if (!newper)		/* Interpret a zero period as 4096 */
	newper = 1<<12;

    /* Check for clearing TIM_RDONE and TIM_ROVFL flags */
    if (ebits & TIM_WFCLR) {
	cpu.tim.tim_flgs = 0;		/* Clear Done & Overflow flags */
	if (cpu.pi.pilev_timreq) {	/* If PI was set, */
	    cpu.pi.pilev_timreq = 0;	/* clear it, and */
	    pi_devupd();		/* propagate the change */
	}
    }

    /* Not clear (:-) how to clear counter in middle of a countdown.
    ** Assume it only happens when turning things off (in which case
    ** value is irrelevant) or on (in which case we simply restart).
    */
    if (ebits & TIM_WCLR)
	cpu.tim.tim_intcnt = 0;		/* Clear countup */

    /* See if changing either the period or the on/off state.
    ** Generally they are changed only once at monitor startup and never
    ** thereafter, although CONO TIM, must be done after every interrupt
    ** in order to turn off the DONE bit.
    */
    if (newper != cpu.tim.tim_intper
      || (cpu.tim.tim_on != (ebits & TIM_ON))) {
	if (ebits & TIM_ON) {	/* Turn interval counter on? */
	    /* Changing period can glitch all meters (and timebase) unless
	    ** they are partially incremented to compensate.  But ignore that
	    ** for the time being since no known monitor ever changes in
	    ** midstream.
	    ** Note previous code that prevents newper from being 0.
	    */
#if 0 /* KLH10_ITIME_SYNCH */
	    /* For now, ignore "minor" change attempts.  T10 tries to
	    ** twiddle this incessantly for a "leap jiffy" and it will
	    ** never do any good.  So avoid wasting the overhead...
	    ** This check ignores attempts to reduce the period or increase it
	    ** by less than 3.
	    */
	    if (((int)(newper - cpu.tim.tim_intper)) > 2)
		clk_itickset(((uint32)newper)*10);

#elif 1 /* KLH10_ITIME_INTRP */
	    /* Go ahead and ask the clock code for a new timer period.
	    ** Normally this request will be ignored in favor of a fixed
	    ** runtime parameter (clk_ithzfix), unless the user has cleared it.
	    ** This both cures T10's twiddling (centered around 60Hz)
	    ** and avoids T20's too-fast 1ms tick.
	    */
	    clk_itickset(((uint32)newper)*10);
#endif
	} else {		/* Turning counter off */
	    /* No explicit turnoff of CLK internal timer, cuz other things
	    ** like peripheral device timeouts may depend on it.
	    ** Clearing TIM_ON bit will suffice to ensure that 10 doesn't
	    ** receive unexpected interval interrupts.
	    **
	    ** Doesn't keep interval counter state from changing; oh well.
	    */
	}
	cpu.tim.tim_on = (ebits & TIM_ON);	/* Set flag to new state */
	cpu.tim.tim_intper = newper;
    }

    return PCINC_1;
}

/* CONI TIM, (70224) Conditions In, Interval Counter
**	Read status of interval counter into c(E).
*/
ioinsdef(io_ci_tim)
{
    register w10_t w;

    if (cpu.tim.tim_on) {
#if KLH10_ITIME_SYNCH		/* Get 10usec units */
	cpu.tim.tim_intcnt = CLK_USEC_SINCE_ITICK() / 10;
#elif KLH10_ITIME_INTRP
	cpu.tim.tim_intcnt++;	/* Can't tell, so just bump each time */
#endif
    }
    LRHSET(w, (cpu.tim.tim_intcnt & MASK12),
		cpu.tim.tim_on | cpu.tim.tim_flgs | cpu.tim.tim_intper);

    vm_write(e, w);
    return PCINC_1;
}

/* CONSZ TIM, (70230) Skip if all TIM status bits in non-zero E are zero.
*/
ioinsdef(io_sz_tim)
{
#if 1
    return (va_insect(e) &
		(cpu.tim.tim_on | cpu.tim.tim_flgs | cpu.tim.tim_intper))
	? PCINC_1 : PCINC_2;
#else
    return (va_insect(e)
	&& !(va_insect(e) &
		(cpu.tim.tim_on | cpu.tim.tim_flgs | cpu.tim.tim_intper)))
	? PCINC_2 : PCINC_1;
#endif
}

/* CONSO TIM, (70234) Skip if any TIM status bits in E are set.
*/
ioinsdef(io_so_tim)
{
    return (va_insect(e) &
		(cpu.tim.tim_on | cpu.tim.tim_flgs | cpu.tim.tim_intper))
	? PCINC_2 : PCINC_1;
}
#endif /* KL */

#if KLH10_CPU_KL

/* Device MTR */


/* MTR_UPDATE - Carry out the "update accounts" operation
**	described by PRM 3-57 for the Execution and Memory accounts.
**	Called by pager code when UBR is set.
*/
void
mtr_update(void)
{
    register w10_t w;	/* Vars needed for DWCNT_UPDATE */
    register dw10_t d;
    register vmptr_t vp;

    if (cpu.tim.mtr_on && cpu.mr_paging) {
	DWCNT_UPDATE(cpu.mr_ubraddr + UPT_MRC, cpu.tim.mtr_mact)
	DWCNT_UPDATE(cpu.mr_ubraddr + UPT_PXT, cpu.tim.mtr_eact)
    }
}


/* RDMACT (70240 = BLKI MTR,) Read Memory Account
**	Read memory reference doubleword from UPT+506,
**	add current hardware counter, store result in E, E+1.
*/
ioinsdef(io_rdmact)
{
    return mtr_store(
		(cpu.mr_ubraddr + UPT_MRC),	/* UPT+506 */
		&cpu.tim.mtr_mact,
		e);
}

/* RDEACT (70244 = DATAI MTR,) Read Execution Account
**	Read process execution time doubleword from UPT+504,
**	add current hardware counter, store result in E, E+1.
*/
ioinsdef(io_rdeact)
{
    return mtr_store(
		(cpu.mr_ubraddr + UPT_PXT),	/* UPT+504 */
		&cpu.tim.mtr_eact,
		e);
}

/* WRTIME (70260 = CONO MTR,) Conditions Out, Meters
**	Set up meters and timing.
**	See comments for io_rdtime() regarding time base setup.
*/
ioinsdef(io_wrtime)
{
    register uint18 erh = va_insect(e);

    if (erh & MTR_SUA) {
	/* Set up Accounts */

	/* Reflect new flag bits in test words */
	cpu.tim.mtr_on      = (erh & MTR_AON);
	cpu.tim.mtr_ifexe   = (erh & MTR_AEPI);
	cpu.tim.mtr_ifexepi = (erh & MTR_AENPI);

	/* Actually do something about it? */
    }

    /* Remember flags for CONI */
    cpu.tim.mtr_flgs = erh & (MTR_AEPI|MTR_AENPI|MTR_AON|MTR_TBON|MTR_ICPIA);

    /* Set PIA for interval counter */
    cpu.tim.tim_lev = pilev_bits[erh & MTR_ICPIA];
    if (cpu.pi.pilev_timreq) {		/* If PI already being requested */
	cpu.pi.pilev_timreq = cpu.tim.tim_lev;	/* may need to change lev */
	pi_devupd();			/* and re-check priority */
    }

    /* Take care of time base */
    if (erh & MTR_TBOFF)
	cpu.tim.tim_tbon = 0;
    if (erh & MTR_TBON)
	cpu.tim.tim_tbon = MTR_TBON;
    if (erh & MTR_TBCLR) {	/* Reset time base hardware ctr */
#if KLH10_RTIME_SYNCH
	cpu.tim.tim_ibased = 0;
	cpu.tim.tim_ibaser = CLK_USEC_SINCE_ITICK();
#elif KLH10_RTIME_OSGET
	(void) os_rtmget(&cpu.tim.tim_osbase);
#endif
	/* Special hack.  See comments at io_rdtime for explanation. */
	if (cpu.mr_paging) {
	    vmptr_t vp = vm_physmap(cpu.mr_ebraddr + EPT_TBS + 1);
	    vm_psetrh(vp, vm_pgetrh(vp) | 01367);	/* 759. */
	}
    }

    return PCINC_1;
}

/* CONI MTR, (70264) Conditions In, Meters
**	Read status of meters & timers into c(E).
*/
ioinsdef(io_ci_mtr)
{
    register w10_t w;

    LRHSET(w, 0, cpu.tim.mtr_flgs);
    vm_write(e, w);
    return PCINC_1;
}

/* CONSZ MTR, (70270) Skip if all MTR status bits in non-zero E are zero.
*/
ioinsdef(io_sz_mtr)
{
#if 1
    return (va_insect(e) & cpu.tim.mtr_flgs)
		? PCINC_1 : PCINC_2;
#else
    return (va_insect(e) && !(va_insect(e) & cpu.tim.mtr_flgs))
		? PCINC_2 : PCINC_1;
#endif
}

/* CONSO MTR, (70274) Skip if any MTR status bits in E are set.
*/
ioinsdef(io_so_mtr)
{
    return (va_insect(e) & cpu.tim.mtr_flgs)
		? PCINC_2 : PCINC_1;
}

#endif /* KL */

/* PI "device" code. */

/* Later make these EXTDEFs in kn10def.h and initialize both at runtime */
int pilev_bits[8] = {
	0, PILEV1, PILEV2, PILEV3, PILEV4, PILEV5, PILEV6, PILEV7
};
unsigned char pilev_nums[PILEVS+1] = { 0 };		/* 1st entry zero */

/* PI_INIT - Called at startup to initialize PI "device" stuff
*/
static void
pi_init(void)
{
    register int i;

    /* Set up pilev_nums array, skipping 1st entry which is 0 */
    for (i = 1; i <= PILEVS; ++i) {
	register unsigned num = 8, r = i;
	while (r) --num, r >>= 1;
	pilev_nums[i] = num;
    }

    /* Clear PI system */
    cpu.pi.pisys_on = 0;
    cpu.pi.pilev_on = 0;
    cpu.pi.pilev_pip = 0;
    cpu.pi.pilev_preq = 0;
    cpu.pi.pilev_dreq = 0;

    /* Clear all device requests too */
    cpu.pi.pilev_aprreq = 0;
#if KLH10_CPU_KS
    cpu.pi.pilev_ub1req = 0;
    cpu.pi.pilev_ub3req = 0;
#elif KLH10_CPU_KL
    cpu.pi.pilev_timreq = 0;
    cpu.pi.pilev_rhreq = 0;
    cpu.pi.pilev_dtereq = 0;
    cpu.pi.pilev_diareq = 0;
#endif
}

/* RDPI (CONI PI,) - Stores PI status word in E.
**	The only PI data subject to asynch update is cpu.pi.pilev_dreq which
**	isn't returned in the status, so we don't need to lock anything.
*/
ioinsdef(io_rdpi)
{
    register w10_t w;
    LRHSET(w, cpu.pi.pilev_preq,
	      ((cpu.pi.pilev_pip<<PIR_PIPSHIFT) | cpu.pi.pisys_on | cpu.pi.pilev_on));
    vm_write(e, w);			/* Store word at E */
    return PCINC_1;
}

/*  CONSZ PI, - Skips if all PI status bits in nonzero E are zero
**	KS10 doc doesn't mention this instr, but it evidently works.
*/
ioinsdef(io_sz_pi)
{
#if 1
    return (va_insect(e) &
	    (((cpu.pi.pilev_pip<<PIR_PIPSHIFT)
	    | cpu.pi.pisys_on | cpu.pi.pilev_on)))
		? PCINC_1 : PCINC_2;
#else
	/* Same confusion as for CONSZ APR */
    return (va_insect(e)
      && !(va_insect(e) &
	    (((cpu.pi.pilev_pip<<PIR_PIPSHIFT)
	    | cpu.pi.pisys_on | cpu.pi.pilev_on))))
		? PCINC_2 : PCINC_1;
#endif
}

/*  CONSO PI, - Skips if any PI status bits in E are set
**	KS10 doc doesn't mention this instr, but it evidently works.
*/
ioinsdef(io_so_pi)
{
    return (va_insect(e) &
	    (((cpu.pi.pilev_pip<<PIR_PIPSHIFT)
	    | cpu.pi.pisys_on | cpu.pi.pilev_on)))
		? PCINC_2 : PCINC_1;
}


/* WRPI (CONO PI,) - Sets PI status from bits in E (not c(E)!)
**	The only PI data subject to asynch update is cpu.pi.pilev_dreq which
**	isn't affected by these bits, so we don't need to lock anything.
**	However, PI_CHECK should be called to see whether a new interrupt
**	now needs to be taken.
**
**	Some PI flags conflict with each other.  In all cases, the emulation
**	here does the OFF case before the ON case, on the assumption that
**	the programmer didn't intend to turn things on and then off.
**	Off and then on may be equally pointless but is sometimes useful.
*/
ioinsdef(io_wrpi)
{
    register h10_t ebits = va_insect(e);
    register int levs = (ebits & PILEVS);

    if (ebits & PIW_CLR) {		/* Clear PI system? */
#if KLH10_SYS_ITS
	if (cpu.pi.pilev_pip)		/* If about to go out of PIP, */
	    cpu.pag.pr_quant =		/* adjust ctr */
		quant_unfreeze(cpu.pag.pr_quant);
#endif /* ITS */
	cpu.pi.pilev_on = cpu.pi.pilev_pip =
		cpu.pi.pilev_preq = cpu.pi.pisys_on = 0;
    }				/* NOTE: Must leave cpu.pi.pilev_dreq alone! */

    if (ebits & PIW_OFF)		/* Turn PI system off? */
	cpu.pi.pisys_on = 0;
    if (ebits & PIW_ON)			/* Turn PI system on? */
	cpu.pi.pisys_on = PIR_ON;

    if (ebits & PIW_LDRQ)		/* Drop programmed requests? */
	cpu.pi.pilev_preq &= ~levs;
    if (ebits & PIW_LIRQ)		/* Initiate programmed requests? */
	cpu.pi.pilev_preq |= levs;

    if (ebits & PIW_LOFF)		/* Disable levels? */
	cpu.pi.pilev_on &= ~levs;
    if (ebits & PIW_LON)		/* Enable (activate) levels? */
	cpu.pi.pilev_on |= levs;

    if (pi_check())		/* Done, now check for new ints! */
	INSBRKSET();		/* Yup, something wants to interrupt */
    return PCINC_1;
}


/* PI_DEVUPD - Update known state of pending device PI requests.
**	This is still kludgy (needs locking etc), but serves as a
**	focus for any device emulators that do things that might change
**	the PI state.
**	An interrupt check is triggered if it looks like attention may be
**	needed.  This can be redundant if an int is already in progress, but
**	doesn't hurt.
*/
void
pi_devupd(void)
{
    if (cpu.pi.pilev_dreq = (cpu.pi.pilev_aprreq
#if KLH10_CPU_KS
	| cpu.pi.pilev_ub1req | cpu.pi.pilev_ub3req
#elif KLH10_CPU_KL
	| cpu.pi.pilev_timreq
	| cpu.pi.pilev_rhreq
	| cpu.pi.pilev_dtereq
	| cpu.pi.pilev_diareq
#endif
						)) {
	INSBRKSET();
    }
}



/* PI_CHECK - Check to see whether PI interrupt should be taken.
**	This is called from main loop whenever "mr_insbreak" indicates that
**	something needs attention, or by a "CONO PI,".
**	It returns the PI level bits that are now interrupting.
**	If depending on the return value to decide whether mr_insbreak should
**	be turned off, then the "pilev_dreq" variable must be locked to
**	prevent munging by asynchronous devices between the time we test it
**	here and the time mr_insbreak is cleared!
*/
static int
pi_check(void)
{
    register int levs;

    if (!cpu.pi.pisys_on)
	return 0;		/* PI off, do nothing */

    /* Find levels that are:
    **		Active/enabled and requested by device,
    **		or requested by program (whether enabled or not!),
    **		and are NOT already in progress.
    ** Any winners then need to be checked to verify that their priority
    ** is higher than any current PI in progress.  Otherwise, we return
    ** 0 to indicate no new interrupts should be taken.
    */
    levs = ((cpu.pi.pilev_dreq & cpu.pi.pilev_on)
			| cpu.pi.pilev_preq) & ~cpu.pi.pilev_pip;
    return (levs > cpu.pi.pilev_pip) ? levs : 0;
}

#if KLH10_CPU_KS

/* PI_XCT - Take PI interrupt - KS10 version.
**
**	Instruction loop aborted due to PI interrupt on given level.
**	Level mask is furnished, as returned by pi_check().
**	Save context, set up new, then return to main loop.
**
** No cleanup needs to be done here as whatever was needed has already
** been done -- this routine is only called from the main loop between
** instructions.  The next instruction (what PC points to) might actually
** have just been aborted and backed out of.
**
** Taking an interrupt involves executing the "interrupt vector"
** dispatch instruction for that particular interrupt.  This must be
** either a JSR or XPCW (ITS only uses JSR); anything else causes an
** "illegal interrupt" halt.  A JSR automatically enters Exec mode;
** an XPCW uses the new flags.
**
** Normally the PI dispatch instruction for level N is found at location
** 		EPT+040+(2*N)
** However, for an interrupt caused by a Unibus device with vector
** V on adapter C, the PI dispatch is located at:
**		c(EPT+0100+C) + (V/4)
** That is, C indexes into the EPT to get a table pointer; V is used
** as an offset from that pointer to get the dispatch instruction location.
**
** NOTE!  The vector table address is mapped, not physical!  PRM doesn't
** mention this, but this is what TOPS-20 expects.
**
** (NOTE: The DEC Proc Ref Man (6/82 p.4-4) claims adapter #s (1 and 3)
** are not equivalent to the controller #s used in IO addrs (0 and 1).  However
** this appears to be wrong and conflicts with the description of IO instrs
** on p. 2-128 of the same manual.  Assumption here is controller==adapter.)
**
** Lower-numbered levels have higher priority.
** Within a level, lowest adapter # has highest priority.
** Within an adapter, it appears to be a free-for-all, but presumably
**	whichever device is physically closer on the Unibus wins.
**	This is represented here by the order in which the devices are
**	checked.
**
** Regarding the actual PI level that a Unibus device interrupts on:
** a device may interrupt the Unibus on one of BR4-BR7 (higher number
** has higher priority).  BR4&BR5 become KS10 PI requests on the "low" level,
** BR6&BR7 become KS10 PI requests on the "high" level.  The actual
** specification of the high and low levels is made by the setting of
** the Unibus Adapter Status register (at UB_UBASTA).
*/
static void
pi_xct(register int lev)
{
    register w10_t w;
    register vaddr_t e;
    register paddr_t intvec, pa;	/* Note phys addr */
    register int num;

    /* First do a JFFO equivalent to return the PI level number.
    ** This hack uses a 128-byte array for speed (32 wds on most machines).
    */
    if (!lev || (lev & ~PILEVS))	/* Error checking */
	panic("pi_xct: bad PI level");
    num = pilev_nums[lev];		/* Presto! */
    lev = pilev_bits[num];		/* Get clean single bit */

    /* Find dispatch info, set up new context, PC, etc. */
    if (lev & cpu.pi.pilev_dreq) {		/* Device request? */
	if (lev & cpu.pi.pilev_aprreq) {		/* If APR device, do */
	    intvec = cpu.mr_ebraddr + EPT_PI0 + (2 * num);  /* normal stuff. */
	    /* Assume APR interrupt does not automatically turn itself off
	    ** after being granted, so aprreq remains set.
	    */
	    w = vm_pget(vm_physmap(intvec));	/* Fetch from phys mem */
	} else {
	    if ((lev & cpu.pi.pilev_ub1req)		/* Unibus #1? */
	      && (intvec = ub1_pivget(lev)))
		    pa = EPT_UIT+1;
	    else if ((lev & cpu.pi.pilev_ub3req)	/* Unibus #3? */
	      && (intvec = ub3_pivget(lev)))
		    pa = EPT_UIT+3;
	    else
		panic("pi_xct: Spurious pilev_dreq: %o", cpu.pi.pilev_dreq);
		/* If this panic is hit, there is a BUG someplace in the
		** KLH10 code.  You can proceed by using the "Set pilev_dreq 0"
		** command, but the bug really should be tracked down.
		*/

	    pa = vm_pgetrh(vm_physmap(cpu.mr_ebraddr+pa)); /* Get table ptr */
	    if (!pa) panic("pi_xct: no PI dev table");
	    intvec = pa + (intvec/4);		/* Finally derive addr */

	    /* Danger zone -- use exec mapping, can page fault!
	    ** Uses phys mapping if paging off, will fail if NXM.
	    */
	    if (cpu.mr_paging) {
		va_lmake(e, 0, intvec & H10MASK); /* Convert to vaddr_t */
		w = vm_pget(vm_execmap(e, VMF_FETCH));	/* Map and get */
	    } else
		w = vm_pget(vm_physmap(intvec)); /* Fetch from phys mem */
	}
    } else {
	/* Must be non-device program-requested PI, do normal stuff */
	/* Find addr of int vector instr */
	intvec = cpu.mr_ebraddr + EPT_PI0 + (2 * num);
	w = vm_pget(vm_physmap(intvec));	/* Fetch from phys mem */
    }

    /* Have dispatch vector address and fetched interrupt instr */

    /* Now do it!
    ** Note potential screwiness with EA calculation and mem refs.  EA
    ** is computed using current context (which might not be EXEC!), and
    ** the memory refs are made to exec space with whatever the current AC
    ** block is.
    ** Also, it is POSSIBLE for the JSR/XPCW to cause a page failure while
    ** trying to store the PC word.  If this happens, the processor
    ** should halt (per DEC Proc Ref Man (12/82) p.4-14 footnote).
    ** For the time being we use another silly kludge flag (cpu.mr_inpi).
    */
#if KLH10_DEBUG
    if (cpu.mr_debug) {
	fe_begpcfdbg(stderr);
    }
#endif
    switch (iw_op(w)) {
	case I_JSR:
	    /* Interrupt JSR differs from a normal JSR in three ways:
	    ** (1) Saved PC is as-is, not PC+1
	    ** (2) E ref is always to EXEC memory (phys, if paging off)
	    ** (3) PC flags are munged (in addition to normal clearing)
	    */
	    e = ea_calc(w);	/* Figure eff addr (hope I,X not set!) */

	    LRHSET(w, cpu.mr_pcflags, PC_INSECT);	/* Cons up PC word */
	    cpu.mr_inpi = TRUE;			/* Tell pager we're in PI */
	    vm_pset(vm_execmap(e,VMF_WRITE), w);	/* Store PC word */
	    cpu.mr_inpi = FALSE;			/* Now out of PI */
	    PCFCLEAR(PCF_FPD|PCF_AFI|PCF_TR2|PCF_TR1);	/* Clear normally */
#if KLH10_ITS_1PROC
	    /* Note that the above PCFCLEAR also clears the PFC_1PR
	    ** one-proceed flag, in guise of PCF_AFI.  This is proper.
	    */
#endif
	    /* Won, now safe to mung processor state and go all the way */
	    PCFCLEAR(PCF_USR);		/* Switch to exec mode */
	    apr_pcfcheck();		/* Make right things happen */
	    va_inc(e);			/* Increment new PC to E+1 */
	    PC_JUMP(e);			/* Jump there! */
 	    break;

	case I_JRST:
	    if (iw_ac(w) != 07) {	/* Verify XPCW (JRST 7,) */
	default:
		panic("Illegal Interrupt Halt!  Bad int instr: %o,,%o",
			LHGET(w), RHGET(w));
	    }
	    /* Handle XPCW.  Can't use ij_xpcw() since it checks the flags,
	    ** and uses current mapping for mem refs.  Here, we ignore flags
	    ** and always use exec mapping once E is obtained.
	    ** Note mapping is physical if paging off.
	    */
	{
	    e = ea_calc(w);	/* Figure eff addr (hope I,X not set!) */
	    cpu.mr_inpi = TRUE;			/* Tell pager we're in PI */
	    LRHSET(w, cpu.mr_pcflags, 0);	/* Cons up flag word */
	    vm_pset(vm_execmap(e,VMF_WRITE), w);	/* Store */
	    LRHSET(w, 0, PC_INSECT);			/* Cons up PC word */
	    va_inc(e);					/* E+1 */
	    vm_pset(vm_execmap(e,VMF_WRITE), w);	/* Store */
	    va_inc(e);					/* E+2 */
	    w = vm_pget(vm_execmap(e,VMF_READ));	/* Get flags */
	    va_inc(e);					/* E+3 */
	    pa = vm_pgetrh(vm_execmap(e,VMF_READ));	/* Get PC */
	    cpu.mr_inpi = FALSE;			/* Now out of PI */

	    /* Won, now safe to mung processor state and go all the way */
	    cpu.mr_pcflags = LHGET(w) & PCF_MASK;	/* Set new flags */
	    apr_pcfcheck();		/* Make right things happen */
	    va_lmake(e, 0, pa);		/* Get vaddr_t */
	    PC_JUMP(e);			/* Jump to E */
	}
	    break;
    }

    /* Fix up PI vars to account for taking interrupt */
#if KLH10_SYS_ITS
    if (!cpu.pi.pilev_pip)			/* Entering PIP? */
	cpu.pag.pr_quant =			/* Yes, no more quantums! */
		quant_freeze(cpu.pag.pr_quant);
#endif /* ITS */
    cpu.pi.pilev_pip |= lev;		/* PI now in progress on this level */
#if KLH10_DEBUG
    if (cpu.mr_debug)
	fe_endpcfdbg(stderr);
#endif
}
#endif /* KLH10_CPU_KS */


/* PI_DISMISS - Dismiss current PI interrupt.
**	No-op if no currently active interrupt.
**	Must check for re-interrupting if something else is still
**	making a request on the same level.
*/
void
pi_dismiss(void)
{
    if (cpu.pi.pilev_pip) {
#if KLH10_DEBUG
	if (cpu.mr_debug) {
	    fe_begpcfdbg(stderr);
	}
#endif
	/* Clear leftmost bit */
	cpu.pi.pilev_pip &= ~pilev_bits[pilev_nums[cpu.pi.pilev_pip]];

#if KLH10_SYS_ITS
	if (!cpu.pi.pilev_pip)				/* No longer PIP? */
	    cpu.pag.pr_quant =				/* Let 'er rip */
			quant_unfreeze(cpu.pag.pr_quant);
#endif /* ITS */
	if (pi_check())		/* Check for new interrupt to take */
	    INSBRKSET();	/* This seems to hang us up?? */
#if KLH10_DEBUG
    if (cpu.mr_debug) {
	fprintf(stderr, "(dismiss) ");
	pishow(stderr);
	putc(']', stderr);
    }
#endif
    }
}

#if KLH10_CPU_KL

/* PI_XCT - Take PI interrupt - KL10 version.
**
**	Instruction loop aborted due to PI interrupt on given level.
**	Level mask is furnished, as returned by pi_check().
**	Save context, set up new, then return to main loop.
**
** No cleanup needs to be done here as whatever was needed has already
** been done -- this routine is only called from the main loop between
** instructions.  The next instruction (what PC points to) might actually
** have just been aborted and backed out of.
**
** Taking an interrupt involves executing the interrupt instruction
** for that particular interrupt.  This must be either a JSR or XPCW
** anything else causes an "illegal interrupt" halt.
** (The KL allows BLKI/BLKO but T20 at least doesn't use it.)
** A JSR automatically enters Exec mode; an XPCW uses the new flags.
**
** The KL PI handling is rather complex.  See the PRM.
**
** NOTE!  The vector table address is mapped, not physical!  PRM doesn't
** mention this, but this is what TOPS-20 expects.
**
** Lower-numbered levels have higher priority.
** Within a level, lowest device # has highest priority.
**
*/
static void
pi_xct(register int lev)
{
    register w10_t w;
    register vaddr_t e;
    register paddr_t intvec;	/* Note phys addr */
    register int num;
    register int sect = 0;		/* Default section for int instr */

    /* First do a JFFO equivalent to return the PI level number.
    ** This hack uses a 128-byte array for speed (32 wds on most machines).
    ** PI level 0 is not supported.
    */
    if (!lev || (lev & ~PILEVS))	/* Error checking */
	panic("pi_xct: bad PI level");
    num = pilev_nums[lev];		/* Presto! */
    lev = pilev_bits[num];		/* Get clean single bit */

    /* Determine source of interrupt.
    ** Get its PI function word and derive the interrupt address.
    ** The order checked here is that described by PRM p.3-4
    ** Note:
    **	In order to handle the situation where programmed requests can
    ** be recognized even while their PI level is disabled, without
    ** accidentally handling a device request instead, the programmed flags
    ** are checked ahead of everything but the interval timer (as per
    ** PRM), and the timer check includes a special test to verify that
    ** non-program-request PIs are allowed.
    */
    if ((lev & cpu.pi.pilev_timreq)		/* Check interval timer */
      && (lev & cpu.pi.pilev_on)) {		/* Verify enabled */
	/* Interval Timer - internal device.
	** Not clear if PI request is turned off when int is taken, or
	** when TIM_RDONE bit is cleared by int handler.
	** I'll assume it's similar to APR interrupts and leave it on, so
	** the handler must explicitly turn it off by clearing TIM_RDONE.
	*/
	LRHSET(w, PIFN_FVEC, EPT_ICI);	/* Invent bogus fn word */
	intvec = cpu.mr_ebraddr + EPT_ICI;

    } else if ((lev & cpu.pi.pilev_preq)	/* Check programmed requests */
      || (lev & cpu.pi.pilev_aprreq)) {		/* Check APR request */
	/* Programmed PI requests and APR requests are handled
	** in the same way.  They are not automatically turned off when
	** the interrupt is taken, and both use the standard interrupt.
	*/
	LRHSET(w, 0, 0);		/* Use normal dispatch */
	intvec = cpu.mr_ebraddr + EPT_PI0 + (2*num);

    } else if (lev & cpu.pi.pilev_dreq) {
	/* External device of some kind.  Scan to find which one.
	*/
	extern struct device *devrh20[8], *devdte20[4];
	register int i;
	register struct device *dv = NULL;

	/* First check 8 RH20 channels (maybe rotate order?) */
	if (lev & cpu.pi.pilev_rhreq) {
	    for (i = 0; i < 8; ++i) {
		if ((dv = devrh20[i]) && (dv->dv_pireq & lev))
		    break;
	    }
	    if (i >= 8)
		panic("pi_xct: spurious pilev_rhreq: %o", cpu.pi.pilev_rhreq);

	    w = (*(dv->dv_pifnwd))(dv);		/* Get its PI funct word */
	    LHSET(w, (LHGET(w)&~PIFN_DEV) | (i << 7));	/* Force dev bits */
	    intvec = cpu.mr_ebraddr + (RHGET(w) & 0777);

	} else if (lev & cpu.pi.pilev_dtereq) {
	    /* Then check 4 DTE20s (maybe rotate order?) */
	    for (i = 0; i < 4; ++i) {
		if ((dv = devdte20[i]) && (dv->dv_pireq & lev))
		    break;
	    }
	    if (i >= 4)
		panic("pi_xct: spurious pilev_dtereq: %o", cpu.pi.pilev_dtereq);

	    w = (*(dv->dv_pifnwd))(dv);		/* Get its PI funct word */
	    LHSET(w, (LHGET(w) & ~PIFN_DEV) | ((i+8)<<7));
	    switch (LHGET(w) & PIFN_FN) {
	    case PIFN_F0:	/* Device not interval timer, so do std int */
	    case PIFN_FSTD:
		intvec = cpu.mr_ebraddr + EPT_PI0 + (2*num);
		break;

	    case PIFN_FVEC:
		/* Special DTE20 code for vector interrupt */
		intvec = cpu.mr_ebraddr + EPT_DT0 + (i<<3) + 2;
		break;

	    /* Special cases that only DTE20 should be using */
	    case PIFN_FINC:
	      {
		register vmptr_t vp;
		va_gfrword(e, w);	/* Make global virt addr from 13-35 */
		cpu.mr_inpi = TRUE;
		vp = vm_execmap(e,VMF_WRITE);
		cpu.mr_inpi = FALSE;
		if (LHGET(w) & PIFN_Q)
		    w = vm_pget(vp), op10m_dec(w);
		else
		    w = vm_pget(vp), op10m_inc(w);
		vm_pset(vp, w);
		return;
	      }

	    case PIFN_FEXA:
	    case PIFN_FDEP:
	    case PIFN_FBYT:
		panic("pi_xct: DTE20 function not implemented: %lo,,%lo",
					(long)LHGET(w), (long)RHGET(w));

	    default:
		panic("pi_xct: Illegal function word %lo,,%lo",
					(long)LHGET(w), (long)RHGET(w));
	    }

	} else if (lev & cpu.pi.pilev_diareq) {
	    /* Finally check single DIA20 */

	    /* DIA20 is apparently the only device that can ask for
	    ** a "dispatch interrupt" using bits 13-35 as exec virtual addr.
	    */
	    panic("pi_xct: DIA20 interrupts not supported yet");

	} else {

	    panic("pi_xct: Spurious pilev_dreq: %o", cpu.pi.pilev_dreq);
		/* If this panic is hit, there is a BUG someplace in the
		** KLH10 code.  You can proceed by using the "Set pilev_dreq 0"
		** command, but the bug really should be tracked down.
		*/
	}

#if 0
	/* Determine whether function is special, and execute immediately
	** if so.  Distinguish DTE20s from other devices.
	*/
	switch (LHGET(w) & PIFN_FN) {
	case PIFN_F0:	/* Device is not interval timer, so do std int */
	case PIFN_FSTD:
		intvec = cpu.mr_ebraddr + EPT_PI0 + (2*num);
		sect = 0;			/* Default section is 0 */
		break;
	case PIFN_FVEC:
		break;

	    /* Special cases that only DTE20 should be using */
	case PIFN_FINC:
	case PIFN_FEXA:
	case PIFN_FDEP:
	case PIFN_FBYT:
	default:
		panic("pi_xct: Illegal function word %lo,,%lo",
					(long)LHGET(w), (long)RHGET(w));
	}

	/* Default section is 0 unless function is "dispatch interrupt",
	** i.e. a vector interrupt from DIA20.
	*/
	sect = 0;
#endif


    } else
	panic("pi_xct: Spurious int: %o", lev);

    /* Here, have PI function word in w and phys addr of instr in intvec.
    ** Standard and vector interrupts come here.
    */
    cpu.acblks[7][3] = w;	/* Save function word in AC3, BLK7 */
				/* This works as long as BLK7 is not the
				** current AC block - safe enough.
				*/
    w = vm_pget(vm_physmap(intvec));	/* Fetch int instr from phys mem */


    /* Have dispatch vector address and fetched interrupt instr */

    /* Now do it!
    ** Note potential screwiness with EA calculation and mem refs.
    ** EA is computed using EXEC context with whatever the current AC block
    ** is (which may not be exec ACs), and the default section is normally 0.
    **
    ** Also, it is POSSIBLE for the JSR/XPCW to cause a page failure while
    ** trying to store the PC word or calculating E.  If this happens,
    ** the pager should set the "In-out Page Failure" APR flag
    ** (per DEC PRM p.3-7 top parag)
    ** For the time being we use another silly kludge flag (cpu.mr_inpi).
    */
#if KLH10_DEBUG
    if (cpu.mr_debug) {
	fe_begpcfdbg(stderr);
    }
#endif
    switch (iw_op(w)) {
	case I_JSR:
	    /* Interrupt JSR differs from a normal JSR:
	    ** (1) Saved PC is as-is, not PC+1
	    ** (2) E ref is always to EXEC memory
	    ** (3) PC flags are munged (in addition to normal clearing)
	    ** (4) PC section test is checked from "sect" instead of PC
	    **		section.  If PC had NZ sect, you lose big.  One
	    **		reason why XPCW is "preferred"!
	    */
	    cpu.mr_inpi = TRUE;		/* Tell pager we're in PI */
	    e = xea_calc(w, sect);	/* Figure EA (hope I,X not set!) */

	    if (sect)
		PC_TOWORD(w);		/* Use extended PC word, no flags */
	    else
		LRHSET(w, cpu.mr_pcflags, PC_INSECT);	/* Cons up PC word */
	    vm_pset(vm_execmap(e,VMF_WRITE), w);	/* Store */
	    cpu.mr_inpi = FALSE;			/* Now out of PI */

	    /* Won, now safe to mung processor state and go all the way */
	    PCFCLEAR(PCF_FPD|PCF_AFI|PCF_TR2|PCF_TR1	/* Clear normal flgs*/
			| PCF_USR | PCF_PUB);	/* plus User & Public! */
	    apr_pcfcheck();		/* Make right things happen */
	    va_inc(e);			/* Increment new PC to E+1 */
	    PC_JUMP(e);			/* Jump there! */
 	    break;

	case I_JRST:
	    if (iw_ac(w) != 07) {	/* Verify XPCW (JRST 7,) */
	default:
		panic("Illegal Interrupt Halt!  Bad int instr: %o,,%o",
			LHGET(w), RHGET(w));
	    }
	    /* Handle XPCW.  Can't use ij_xpcw() since it checks the flags,
	    ** and uses current mapping for mem refs.  Here, we ignore flags
	    ** and always use exec mapping once E is obtained.
	    ** Note special hacking of PCS for extended KL.
	    */
/* Other comments
	Note PRM 3-7,3-8 comments on execution in kernel mode, plus
		default section of 0 unless function word is a dispatch.
	Does XPCW always think it's in kernel mode?  Assume yes.  This
		means it always saves PCS in old flag word, and can set
		new flags to whatever it wants.
	New flags are simply taken from new-flag word; PCP and PCU are
		evidently not treated specially.
	PCS doesn't appear to be touched.
		KCIO claims that "ucode sets PCS" but I can't find anything
		of that sort in the PI code.  No mention in PRM.  No reason
		I can imagine for setting PCS, anyway.
*/
	{
	    register w10_t npc;

	    cpu.mr_inpi = TRUE;			/* Tell pager we're in PI */
	    e = xea_calc(w, sect);	/* Figure EA (hope I,X not set!) */
	    LRHSET(w, cpu.mr_pcflags, pag_pcsget());	/* Cons up flag word */
	    vm_pset(vm_execmap(e,VMF_WRITE), w);	/* Store */
	    PC_TOWORD(w);		/* Cons up PC word (not PC+1 !!)*/
	    va_inc(e);					/* E+1 */
	    vm_pset(vm_execmap(e,VMF_WRITE), w);	/* Store */
	    va_inc(e);					/* E+2 */
	    w = vm_pget(vm_execmap(e,VMF_READ));	/* Get flags */
	    va_inc(e);					/* E+3 */
	    npc = vm_pget(vm_execmap(e,VMF_READ));	/* Get PC */
	    cpu.mr_inpi = FALSE;			/* Now out of PI */

	    /* Won, now safe to mung processor state and go all the way */
	    cpu.mr_pcflags = LHGET(w) & PCF_MASK;	/* Set new flags */
	    apr_pcfcheck();		/* Make right things happen */
	    va_lfrword(e, npc);		/* Get 30-bit address from word */
	    PC_JUMP(e);			/* Jump to E */
	}
	    break;

#if 1 /* Diagnostics emulation support */
	/* These instrs are actually UNDEFINED for use as interrupt
	** instructions but the KL diagnostics (DFKAA) expect them to work,
	** so this otherwise unnecessary code is provided to keep diags happy.
	**
	** In the absence of any definition, the algorithms here represent
	** my best guess at their operation.
	** Note that the ACs are whatever the current AC block is.
	*/
	case I_AOSE:
	    /* AOSE is done by doing the increment and falling thru to SKIPE.
	    ** DFKAA doesn't use a NZ AC, so don't bother checking it.
	    ** No flags (especially no trap flags!) are set.
	    */
	{
	    register vmptr_t vp;

	    cpu.mr_inpi = TRUE;		/* Tell pager we're in PI */
	    e = xea_calc(w, sect);	/* Figure EA (hope I,X not set!) */
	    vp = vm_execmap(e,VMF_WRITE);
	    cpu.mr_inpi = FALSE;	/* Now out of PI */
	    w = vm_pget(vp);
	    op10m_inc(w);		/* Increment (don't set flags!) */
	    vm_pset(vp, w);		/* Store back */
	    goto pi_skipe;
	}
	case I_SKIPE:
	    /* Interrupt SKIPE differs from a normal SKIPE:
	    ** DFKAA doesn't use a NZ AC, so don't bother checking it.
	    ** Skip means interrupt is dismissed.
	    ** No-skip means 2nd PI location is executed.  For DFKAA this is
	    **    always a JSP, so we fall through (sort of) to handle it.
	    */
	    cpu.mr_inpi = TRUE;		/* Tell pager we're in PI */
	    e = xea_calc(w, sect);	/* Figure EA (hope I,X not set!) */
	    w = vm_pget(vm_execmap(e,VMF_READ));
	    cpu.mr_inpi = FALSE;	/* Now out of PI */

	pi_skipe:
	    if (op10m_skipe(w)) {
		/* Skipped, so dismiss the interrupt!
		** PIP isn't set -- basically nothing happens!
		*/
#if KLH10_DEBUG
		if (cpu.mr_debug)
		    fe_endpcfdbg(stderr);
#endif
		return;
	    }
	    /* Didn't skip, so handle 2nd PI instr */
	    w = vm_pget(vm_physmap(intvec+1));	/* Fetch from phys mem */
	    if (iw_op(w) != I_JSP) {
		panic("Illegal Interrupt Halt!  Bad int instr: %o,,%o",
			LHGET(w), RHGET(w));
	    }
	    /* Fall through to handle JSP as 2nd PI location instr */

	case I_JSP:
	    /* Interrupt JSP differs from a normal JSP:
	    ** (1) Saved PC is as-is, not PC+1
	    ** (2) E ref is always to EXEC memory
	    ** (3) PC flags are munged (in addition to normal clearing)
	    ** (4) PC section test is checked from "sect" instead of PC
	    **		section.  If PC had NZ sect, you lose big.  One
	    **		reason why XPCW is "preferred"!
	    */
	    cpu.mr_inpi = TRUE;		/* Tell pager we're in PI */
	    e = xea_calc(w, sect);	/* Figure EA (hope I,X not set!) */
	    cpu.mr_inpi = FALSE;	/* Now out of PI */
	    num = iw_ac(w);		/* Remember AC to use */
	    if (sect)
		PC_TOWORD(w);		/* Use extended PC word, no flags */
	    else
		LRHSET(w, cpu.mr_pcflags, PC_INSECT);	/* Cons up PC word */
	    ac_set(num, w);		/* Store in AC */

	    /* Won, now safe to mung processor state and go all the way */
	    PCFCLEAR(PCF_FPD|PCF_AFI|PCF_TR2|PCF_TR1	/* Clear normal flgs*/
			| PCF_USR | PCF_PUB);	/* plus User & Public! */
	    apr_pcfcheck();		/* Make right things happen */
	    PC_JUMP(e);			/* Jump to E! */
 	    break;

#endif /* 1 Diagnostics emulation support */
    }

    /* Fix up PI vars to account for taking interrupt */
    cpu.pi.pilev_pip |= lev;		/* PI now in progress on this level */
#if KLH10_DEBUG
    if (cpu.mr_debug)
	fe_endpcfdbg(stderr);
#endif
}
#endif /* KLH10_CPU_KL */

/* Local UUO (LUUO) handling.
**	On KS10, just use loc 40 & 41 from whichever space we're in.
**	I wonder what happens on a real machine if the instr at 41 screws up?
**	This code will already have clobbered loc 40 by then.
** On extended machine, if zero-section just use 40 & 41 as usual.
**	If non-Z section, use 4-wd block pointed to by UPT+420 (or EPT+420
**	if exec mode).
**	PRM 2-124 appears to be wrong when it asserts that an exec-mode LUUO
**	is treated as a MUUO; this is contradicted by Uhler and V7 T20.
**	Rather, the behavior is just like user-mode LUUOs except the block
**	is pointed to by the EPT instead of UPT.
** Note code in i_xct that duplicates the LUUO code, in order to avoid
**	the risk of C stack overflow if the non-extended case encounters
**	another LUUO in location 41!
*/
insdef(i_luuo)
{
    register w10_t w;
    register vaddr_t va;

#if KLH10_EXTADR
    if (PC_ISEXT) {
	/* Get dispatch vector from physical address of UPT or EPT */
	w = vm_pget(vm_physmap(cpu.mr_usrmode
			? (cpu.mr_ubraddr + UPT_LUU)
			: (cpu.mr_ebraddr + EPT_LUU)));

	va_gfrword(va, w);		/* Convert word to global vaddr_t */

	/* Don't need to mung flags since not changing any modes */
	LRHSET(w, cpu.mr_pcflags, ((h10_t)op << 9) | (ac << 5));
	vm_write(va, w);		/* Store first loc (flags, opcode) */
	va_ginc(va);
	PC_1TOWORD(w);			/* Put full PC+1 into word */
	vm_write(va, w);		/* Store 2nd loc (PC+1) */
	va_ginc(va);
	va_toword_acref(e, w);	/* Store E in word, canonicalizing */
				/* for AC ref just like XMOVEI, XHLLI */
	vm_write(va, w);		/* Store 3rd loc (E) */
	va_ginc(va);
	w = vm_read(va);		/* Fetch new PC from 4th loc */
	va_lfrword(va, w);		/* Make local vaddr_t from it */
	PC_JUMP(va);
	return PCINC_0;
    }
#endif /* KLH10_EXTADR */

    /* Running non-extended, do normal stuff.
    ** Note: later could optimize access to 041 knowing it's within page,
    ** by using a vmptr_t and incrementing that.
    ** If this code is changed, see also the LUUO code in i_xct().
    */
    LRHSET(w, ((h10_t)op<<9)|(ac<<5),	/* Put instr together */
			va_insect(e));
    va_lmake(va, 0, 040);		/* Get vaddr_t for loc 40 */
    vm_write(va, w);			/* Store the word */
    va_linc(va);
    return i_xct(I_XCT, 0, va);		/* Execute instr at 41 */
}


/* Monitor UUO (MUUO) handling.
**	This is also called for any instruction that is illegal
** in user mode.
**
** KLX behavior:
**	PCS is saved in the flag word ONLY if old mode was exec.
**	PCS is always saved in the saved process context word (UBR).
**	The new flags are all cleared except that PCP and PCU are set
**		to reflect the old Public and User mode flags.
**		Since User is cleared, new mode is always exec.
**	New PCS is always set from section # of old PC.
**	New PC is masked to either 30 bits (sez PRM) or 23 (sez ucode).
**
** KS behavior:
**	It appears that the KS10 restores flags from the PC word vector
**	even in T20 mode, just like T10 and single-section KL.
**	USER is always cleared, and PCU set to previous value of USER.
*/				
insdef(i_muuo)
{
    register w10_t w;

#if KLH10_DEBUG
    if (cpu.mr_debug) {
	fe_begpcfdbg(stderr);
	fprintf(stderr,"MUUO %o => ", op);
    }
#endif

    /* First set up return PC from MUUO.
    **	The PC needs to be bumped by one before being saved, so return
    ** will be to the next instruction.
    **
    ** If the MUUO is being executed as a trap instruction (as indicated
    ** by cpu.mr_intrap) then the trap flags are already clear; upon
    ** return, cpu.mr_intrap will be automatically cleared as well.
    **
    ** It used to be the case that KLH10 trap instruction execution (trap_xct)
    ** didn't touch the PC, thus the MUUO code here had to avoid incrementing
    ** the PC for the trap-instruction case.  However, the trap code now
    ** does a PC decrement so as to fake out all instructions (including
    ** MUUO), so no special casing is needed for MUUO's PC increment.
    */
#if 1
    PC_ADD(1);		/* Bump PC, so saved PC is a return to after MUUO */
#else	/* Old regime */
    if (!cpu.mr_intrap)		/* Unless trapping, bump PC */
	PC_ADD(1);		/* so saved PC is return to after MUUO */
#endif

#if KLH10_EXTADR

    LRHSET(w, cpu.mr_pcflags, ((h10_t)op<<9)|(ac<<5));	/* Build op wd */
    if (!cpu.mr_usrmode)			/* If in exec mode, */
	op10m_iori(w, pag_pcsget());		/* add PCS into op word */
    vm_pset(vm_physmap(cpu.mr_ubraddr + UPT_UUO), w);	/* Store op */
    PC_TOWORD(w);
    vm_pset(vm_physmap(cpu.mr_ubraddr + UPT_UPC), w);	/* Store OPC */

    /* Set up and store MUUO E.  This algorithm is the same one
    ** used by XMOVEI.
    */
    if (va_iscvtacref(e))
	LRHSET(w, 1, va_insect(e));	/* Global AC reference */
    else
	LRHSET(w, va_sect(e), va_insect(e));
    vm_pset(vm_physmap(cpu.mr_ubraddr + UPT_UEA), w);	/* Store MUUO E */

    /* Set up and store current UBR.  This includes current PCS
    ** before we clobber it.
    */
    op10m_tlz(cpu.mr_ubr, UBR_PCS);
    op10m_tlo(cpu.mr_ubr, pag_pcsget()&UBR_PCS);
    vm_pset(vm_physmap(cpu.mr_ubraddr + UPT_UCX), cpu.mr_ubr); /* Store UBR */

    /* Now find where new PC will come from, since it depends on
    ** the current state -- whether in user mode, and if trapping.
    ** For the KL and KI, public mode is also tested.
    */
    w = vm_pget(vm_physmap(cpu.mr_ubraddr +
		    (cpu.mr_usrmode
			? (PCFTEST(PCF_PUB)
				? (cpu.mr_intrap ? UPT_UPT : UPT_UPN)
				: (cpu.mr_intrap ? UPT_UUT : UPT_UUN))
			: (PCFTEST(PCF_PUB)
				? (cpu.mr_intrap ? UPT_UST : UPT_USN)
				: (cpu.mr_intrap ? UPT_UET : UPT_UEN))
		)));

    /* Extended MUUO always sets new pager PCS */
    pag_pcsset(PC_SECT);		/* Set it from old PC */

    va_lfrword(e, w);			/* Build local vaddr_t from word */
    PC_SET(e);				/* Set new PC */

    /* Now need to build new PC flags.  All are cleared, except for
    ** PCP and PCU which are set based on the old flags.
    ** This appears to be the only place where PCP and PCU are ever set!
    */
    cpu.mr_pcflags = (PCFTEST(PCF_PUB) ? PCF_PCP : 0)
		   | (PCFTEST(PCF_USR) ? PCF_PCU : 0);

#else	/* not KLH10_EXTADR */

    /* Although MUUO trapping is not logically related to paging,
    ** in practice the MUUO algorithm is a function of the pager being
    ** used, so the different versions here are selected likewise.
    ** Note that a KI10 would need something different from any of this.
    */
#if KLH10_CPU_KLX || (KLH10_CPU_KS && KLH10_PAG_KL)	/* KLX or T20 KS */
    LRHSET(w, cpu.mr_pcflags, ((h10_t)op<<9)|(ac<<5));	/* Build op word */
    vm_pset(vm_physmap(cpu.mr_ubraddr+UPT_UUO), w);	/* Store op word */
    LRHSET(w, 0, PC_INSECT);
    vm_pset(vm_physmap(cpu.mr_ubraddr+UPT_UPC), w);	/* Store old PC */
    LRHSET(w, 0, va_insect(e));
    vm_pset(vm_physmap(cpu.mr_ubraddr+UPT_UEA), w);	/* Store MUUO E */
    
#else	/* KL0, KS/T10, KS/ITS, KI */
    LRHSET(w, ((h10_t)op<<9)|(ac<<5), va_insect(e));	/* Build instr */
    vm_pset(vm_physmap(cpu.mr_ubraddr+UPT_UUO), w);	/* Store instr */
    LRHSET(w, cpu.mr_pcflags, PC_INSECT);
    vm_pset(vm_physmap(cpu.mr_ubraddr+UPT_UPC), w);	/* Store old PC */
#endif

#if KLH10_CPU_KL || KLH10_CPU_KS
    vm_pset(vm_physmap(cpu.mr_ubraddr+UPT_UCX), cpu.mr_ubr);	/* Store UBR */
#endif

    /* Now find where new PC will come from, since it depends on
    ** the current state -- whether in user mode, and if trapping.
    ** For the KL and KI, public mode is also tested.
    */
#if KLH10_CPU_KS
    w = vm_pget(vm_physmap(cpu.mr_ubraddr + (cpu.mr_usrmode
			? (cpu.mr_intrap ? UPT_UUT : UPT_UUN)
			: (cpu.mr_intrap ? UPT_UET : UPT_UEN)) ));

#elif KLH10_CPU_KI || KLH10_CPU_KL
    w = vm_pget(vm_physmap(cpu.mr_ubraddr +
		    (cpu.mr_usrmode
			? (PCFTEST(PCF_PUB)
				? (cpu.mr_intrap ? UPT_UPT : UPT_UPN)
				: (cpu.mr_intrap ? UPT_UUT : UPT_UUN))
			: (PCFTEST(PCF_PUB)
				? (cpu.mr_intrap ? UPT_UST : UPT_USN)
				: (cpu.mr_intrap ? UPT_UET : UPT_UEN))
		)));
#else
# error "KA10 not implemented"
#endif

    PC_SET30(RHGET(w));			/* Set new PC */

    /* Now need to build new PC flags.  Extended KL claims to
    ** always clear the flags since the vector has no room for them.
    ** Systems/machines which use old-format 18-bit PC & flags
    ** do set the flags, however.
    ** Not clear if latter case always sets the PCU flag appropriately.
    ** For now, assume not.
    */
#if KLH10_CPU_KLX
    cpu.mr_pcflags = (cpu.mr_usrmode) ? PCF_PCU : 0;

#elif KLH10_CPU_KL0 || KLH10_CPU_KS || KLH10_CPU_KI
    cpu.mr_pcflags = LHGET(w) & PCF_MASK;
# if KLH10_CPU_KS || KLH10_CPU_KI	/* KS ucode always forces PCU */
    if (cpu.mr_usrmode)			/* (not sure about KI) */
	cpu.mr_pcflags |= PCF_PCU;
    else cpu.mr_pcflags &= ~PCF_PCU;
# endif
#else
# error "KA10 not implemented"
#endif
    
#endif /* !KLH10_EXTADR */

    apr_pcfcheck();			/* Check flags for any changes */

#if KLH10_DEBUG
    if (cpu.mr_debug)
	fe_endpcfdbg(stderr);
    if (cpu.mr_exsafe && (PC_INSECT == 0)) {	/* Hack to catch T10 bug */
	fprintf(stderr, "[KLH10: MUUO going to 0]");
	if (cpu.mr_exsafe >= 2)
	    apr_halt(HALT_EXSAFE);
    }
#endif
    return PCINC_0;
}


#if KLH10_ITS_1PROC
/* APR_1PROC - perform one-proceed.  Routine is here because it's similar
**	to the MUUO code.
*/
static void
apr_1proc(void)
{
    register w10_t w;

#if KLH10_DEBUG
    if (cpu.mr_debug) {
	fe_begpcfdbg(stderr);
	fprintf(stderr,"1PROC ");
    }
#endif

    /* Execute next instruction.  This may actually be a trap instruction,
    ** so check for that (duplicating code in apr_check()).
    ** The 1-proceed flag needs to be turned off during execution so any
    ** saved PC flags don't have it set, but it also needs to be turned
    ** back on if the instruction gets a page fault or something.  Hence
    ** yet another crockish internal flag, cpu.mr_in1proc.
    */
    cpu.mr_in1proc = TRUE;		/* Say hacking 1-proceed */
    PCFCLEAR(PCF_1PR);			/* Then safe to turn off flag */
    if (PCFTEST(PCF_TR1|PCF_TR2) && cpu.mr_paging)
	trap_xct();
    else
	PC_ADDXCT(i_xct(I_XCT, 0, PC_VADDR));	/* XCT next instr */
    cpu.mr_in1proc = FALSE;			/* No longer interruptible */

    /* Now take the 1-proceed trap!  Similar to MUUO handling. */
/* NOTE: May need to mung PC flags for PCP, PCU! */
    LRHSET(w, cpu.mr_pcflags, PC_INSECT);
    vm_pset(vm_physmap(cpu.mr_ubraddr+UPT_1PO), w);	/* Store old PC */
    w = vm_pget(vm_physmap(cpu.mr_ubraddr + UPT_1PN));	/* Get new flgs+PC */
    PC_SET30(RHGET(w));					/* Set new PC */
/* NOTE: May need to mung PC flags for PCP, PCU! */
    cpu.mr_pcflags = LHGET(w) & PCF_MASK;		/* and new flags */
    apr_pcfcheck();				/* Check flags for changes */
#if KLH10_DEBUG
    if (cpu.mr_debug)
	fe_endpcfdbg(stderr);
#endif
}

/* A1PR_UNDO - Recover if page-fault or interrupt out of one-proceed.
*/
void
a1pr_undo(void)
{
    PCFSET(PCF_1PR);		/* Turn flag back on */
    cpu.mr_in1proc = FALSE;
}
#endif /* KLH10_ITS_1PROC */

#if KLH10_CPU_KI || KLH10_CPU_KL	/* AFI Handling */

/* APR_AFI - Execute next instruction under Addr-Failure-Inhibit flag.
**	Very similar to one-proceed stuff.
**	This attempts to emulate the behavior described in PRM 3-52,
**	although that is a bit unclear about odd cases like traps.
*/
static void
apr_afi(void)
{
#if KLH10_DEBUG
    if (cpu.mr_debug) {
	fe_begpcfdbg(stderr);
	fprintf(stderr,"AFI ");
    }
#endif

    /* Execute next instruction.  This may actually be a trap instruction,
    ** so check for that (duplicating code in apr_check()).
    ** The AFI flag needs to be turned off during execution so any
    ** saved PC flags don't have it set, but it also needs to be turned
    ** back on if the instruction gets a page fault or something.  Hence
    ** yet another crockish internal flag, cpu.mr_inafi.
    */
    /* HOWEVER, one exception to this, unlike the one-proceed case:
    ** If taking a trap, leave the flag ON, so it will be saved & cleared
    ** if the trap instruction saves it, and otherwise cleared afterwards.
    ** This will lose if the trap instruction sets AFI itself, but that
    ** seems too improbable to worry about.
    */
    cpu.mr_inafi = TRUE;		/* Say hacking AFI */
    if (PCFTEST(PCF_TR1|PCF_TR2) && cpu.mr_paging) {	/* If trapping, */
	trap_xct();			/* Do trap instr, leave flag on! */
	PCFCLEAR(PCF_AFI);		/* Then safe to turn off flag */
    } else {
	PCFCLEAR(PCF_AFI);		/* Then safe to turn off flag */
	PC_ADDXCT(i_xct(I_XCT, 0, PC_VADDR));	/* XCT next instr */
    }
    cpu.mr_inafi = FALSE;			/* No longer interruptible */

    /* Nothing else to do */
#if KLH10_DEBUG
    if (cpu.mr_debug)
	fe_endpcfdbg(stderr);
#endif
}

/* AFI_UNDO - Recover if page-fault or interrupt out of AFI.
*/
void
afi_undo(void)
{
    PCFSET(PCF_AFI);		/* Turn flag back on */
    cpu.mr_inafi = FALSE;
}
#endif /* KLH10_CPU_KI || KLH10_CPU_KL */


insdef(i_illegal)
{
    if (!cpu.mr_usrmode && cpu.mr_exsafe) {
	/* Barf just in case, but fall thru to let 10 monitor handle it */
	fprintf(stderr,"[KLH10: Illegal exec mode op, PC = %lo: %o %o,%lo]",
			(long)PC_30, op, ac, (long)e);
	if (cpu.mr_exsafe >= 2)
	    apr_halt(HALT_EXSAFE);
    }
    return i_muuo(op, ac, e);
}
