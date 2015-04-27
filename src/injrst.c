/* INJRST.C - Jump (and Stack) instruction routines
*/
/* $Id: injrst.c,v 2.4 2002/03/21 09:50:55 klh Exp $
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
 * $Log: injrst.c,v $
 * Revision 2.4  2002/03/21 09:50:55  klh
 * Fixed refs of fe_debug to use mr_debug
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */


/* See CODING.TXT for guidelines to coding instruction routines. */

#include "klh10.h"
#include "kn10def.h"
#include "kn10ops.h"
#include <stdio.h>	/* For debug output */

#ifdef RCSID
 RCSID(injrst_c,"$Id: injrst.c,v 2.4 2002/03/21 09:50:55 klh Exp $")
#endif

/* Imported functions */

extern void pishow(FILE *);
extern void pcfshow(FILE *, h10_t flags);



insdef(i_jffo)			/* JFFO */
{
    register int i = op10ffo(ac_get(ac));
    if (i < 36) {		/* Found a bit? */
	ac_setlrh(ac_off(ac,1), 0, i);	/* Yep, set AC+1 */
	PC_JUMP(e);		/* and take the jump! */
	return PCINC_0;
    }
    ac_setlrh(ac_off(ac,1), 0, 0);	/* Empty, don't jump */
    return PCINC_1;
}

insdef(i_jfcl)
{
    if (ac) {			/* See which flags we're asking for */
	register h10_t flags;

	flags = (h10_t)ac << (18-4);	/* Shift to match PC flags */
	if (PCFTEST(flags)) {		/* Any of them match? */
	    PCFCLEAR(flags);		/* Yes, clear them */
	    PC_JUMP(e);			/* and take the jump! */
	    return PCINC_0;
	}
    }
    return PCINC_1;
}


insdef(i_jsr)				/* JSR */
{
    register w10_t w;

    PC_1WORD(w);	/* Make PC-word in w (PC+1, plus flags if sect 0) */
    vm_write(e, w);				/* Store it at E */
    PCFCLEAR(PCF_FPD|PCF_AFI|PCF_TR2|PCF_TR1);	/* Clear these flags */
    va_inc(e);				/* Jump to E+1 (local or global) */
    PC_JUMP(e);
    return PCINC_0;
}

insdef(i_jsp)				/* JSP */
{
    register w10_t w;

    PC_1WORD(w);				/* Cons up PC+1 word in w */
    ac_set(ac, w);				/* Store it in AC */
    PCFCLEAR(PCF_FPD|PCF_AFI|PCF_TR2|PCF_TR1);	/* Clear these flags */
    PC_JUMP(e);					/* Jump to E */
    return PCINC_0;
}

/* Note for JSA and JRA that there is no difference in their behavior
** when executed in non-zero sections.
*/
insdef(i_jsa)				/* JSA */
{
    register w10_t w;

    vm_write(e, ac_get(ac));		/* Store c(AC) into E */
    PC_XWDPC1(w, va_insect(e));		/* Cons up word: <e>,,<PC+1> */
    ac_set(ac, w);			/* Store that JSA-word in AC */
    va_inc(e);				/* Jump to E+1 */
    PC_JUMP(e);
    return PCINC_0;
}

insdef(i_jra)				/* JRA */
{
    register vaddr_t va;

    va_lmake(va, PC_SECT,ac_getlh(ac));	/* Make AC.lh a local address */
    ac_set(ac, vm_read(va));		/* Get c(AC.lh) into AC */
    va_lmake(va, PC_SECT, va_insect(e));
    PC_JUMP(va);			/* Jump to PC section,,RH(E) */
    return PCINC_0;
}

/* JRST hackery */

/* Declare subvariants of JRST */
pcinc_t ij_jrstf (int, int, vaddr_t);
pcinc_t ij_halt  (int, int, vaddr_t);
pcinc_t ij_jen   (int, int, vaddr_t);
pcinc_t ij_jrst10(int, int, vaddr_t);
#if !KLH10_SYS_ITS
pcinc_t ij_portal(int, int, vaddr_t);
pcinc_t ij_xjrstf(int, int, vaddr_t);
pcinc_t ij_xjen  (int, int, vaddr_t);
pcinc_t ij_xpcw  (int, int, vaddr_t);
pcinc_t ij_sfm   (int, int, vaddr_t);
#endif
#if KLH10_CPU_KLX
pcinc_t ij_xjrst (int, int, vaddr_t);
#endif


insdef(i_jrst)
{
    switch (ac) {
    case 0:				/* JRST */
	PC_JUMP(e);
	return PCINC_0;

#if !KLH10_SYS_ITS
    case 1: return ij_portal(op, ac, e);	/* PORTAL - not used by ITS */
#endif /* DEC */
    case 2: return ij_jrstf(op, ac, e);		/* JRSTF - Used */
    case 4: return ij_halt(op, ac, e);		/* HALT - Used */
#if !KLH10_SYS_ITS && (KLH10_CPU_KS||KLH10_CPU_KLX)
    case 5: return ij_xjrstf(op, ac, e);	/* XJRSTF - not used by ITS */
    case 6: return ij_xjen(op, ac, e);		/* XJEN - not used by ITS */
    case 7: return ij_xpcw(op, ac, e);		/* XPCW - not used by ITS */
#endif /* DEC KS || KLX */
    case 010: return ij_jrst10(op, ac, e);	/* JRST 10, - used once (?) */
    case 012: return ij_jen(op, ac, e);		/* JEN - used frequently */
#if !KLH10_SYS_ITS && (KLH10_CPU_KS||KLH10_CPU_KLX)
    case 014: return ij_sfm(op, ac, e);		/* SFM - not used by ITS */
# if KLH10_CPU_KLX
    case 015: return ij_xjrst(op, ac, e);	/* XJRST - not used by ITS */
#  endif /* KLX */
#endif /* DEC */
#if KLH10_SYS_ITS
    case 017:	break;				/*  - used once by KL10 ITS */
#endif
    }
    /* Illegal - MUUO (3,11,13,16,17) */
    return i_muuo(op, ac, e);
}


/* DORSTF - Actually carry out flag restoration for JRSTF/XJRSTF/XJEN/XPCW.
**	Restore flags from the top 12 bits of the word in w.
**
** All flags are restored verbatim with these exceptions:
**	USER cannot be cleared by a new setting of 0 (no effect), although
**		a 1 will always set it.
**	UIO can always be cleared, but new setting of 1 will only have
**		an effect if the old mode is EXEC.  (Note that if old
**		mode is user, 1 merely has no effect; the bit may already
**		be set and will remain so.)
**	PUBLIC can always be set by a 1, but a 0 only has effect if
**		old mode is EXEC and new mode is USER.
**
** No special actions are taken for PrevCtxtPublic or PrevCtxtUser.
**
** The wording of the DEC PRM (6/82 p.2-71) is a little misleading, as it
** implies that bit 6 (User-IO) cannot be set to 1 while in user mode; however,
** it appears in reality that it *can* be, if it was *already* 1.
** This is a rather subtle point.
*/
	/* LH(w) contains flags to restore */
#if KLH10_EXTADR
static pcinc_t dorstf(register w10_t w, vaddr_t e, int pcsf)
# define IJ_DO_RSTF(w, e, pcsf) dorstf(w, e, pcsf)
#else
static pcinc_t dorstf(register w10_t w, vaddr_t e)
# define IJ_DO_RSTF(w, e, pcsf) dorstf(w, e)
#endif
{
    register uint18 newf;	/* New flags to restore */

#if KLH10_DEBUG
    if (cpu.mr_debug) {
	putc('[', stderr);
	pishow(stderr); pcfshow(stderr, cpu.mr_pcflags);
#if KLH10_EXTADR
	if (pcsf) fprintf(stderr, "(PCS:%o)", pag_pcsget());
#endif
	fprintf(stderr,"%lo: -> ", (long) PC_30);
    }
#endif
    newf = LHGET(w);		/* Get flags into easy-access reg */

    if (PCFTEST(PCF_USR)) {		/* Currently in user mode? */
	newf |= PCF_USR;		/* Never permit USR clearing */
	if (!PCFTEST(PCF_UIO))		/* If not already in UIO, */
	    newf &= ~PCF_UIO;		/* don't permit UIO setting */
    }
#if KLH10_CPU_KL || KLH10_CPU_KI
    if (PCFTEST(PCF_PUB)		/* If trying to clear Public, */
      && (newf & PCF_PUB)==0) {		/* Must be EXEC, setting USER too. */
	if (PCFTEST(PCF_USR) || !(newf&PCF_USR))
	    newf |= PCF_PUB;		/* Nope, don't clear it */
    }
#endif /* KL || KI */

    /* For XJRSTF/XJEN, a new PCS is loaded from the flag word only if
    ** the new flags specify EXEC mode.
    ** (PRM 2-66, 2nd sentence from bottom).
    */
#if KLH10_EXTADR		/* If going into EXEC mode, restore PCS */
    if (pcsf && !(newf & PCF_USR)) {
	pag_pcsset(RHGET(w) & UBR_PCS);	/* For now, ignore too-big PCS */
    }
#endif

    cpu.mr_pcflags = newf & PCF_MASK;	/* Set new flags!! */
    PC_JUMP(e);				/* Take the jump... */
    apr_pcfcheck();			/* Check flags for any changes. */
	/* NOTE! It is important to check AFTER the PC has changed, so that
	** if there is a user/exec context change, the saved JPC will be the
	** appropriate one for the previous context.
	*/
#if KLH10_DEBUG
    if (cpu.mr_debug) {
	pishow(stderr); pcfshow(stderr, cpu.mr_pcflags);
#if KLH10_EXTADR
	if (pcsf) fprintf(stderr, "(PCS:%o)", pag_pcsget());
#endif
	fprintf(stderr,"%lo:]\r\n", (long) PC_30);
    }
#endif
    return PCINC_0;
}

#if !KLH10_SYS_ITS
/* PORTAL (JRST 1,) - Clear Public and jump to E
**	On KS10, equivalent to JRST.
*/
insdef(ij_portal)
{
#if KLH10_CPU_KI || KLH10_CPU_KL
    PCFCLEAR(PCF_PUB);		/* Always clear public, nothing else needed */
#endif
    PC_JUMP(e);
    return PCINC_0;
}
#endif /* !KLH10_SYS_ITS */

/* JRSTF (JRST 2,)- Jump and Restore Flags from final word used in calc of E.
**
** Because this word isn't saved by the normal instruction loop,
** it needs to be recomputed.  This is painful, but I judged that
** it was better to make JRSTF slower so that every other instruction could
** be slightly faster.
**
** The "mr_injrstf" flag is set solely so any page faults which occur can
** avoid being faked out.  No faults should in fact happen at this point;
** if any do there is a horrible error somewhere.
*/

insdef(ij_jrstf)
{
    register w10_t w;
    register vaddr_t ea;
#if KLH10_EXTADR
    if (PC_ISEXT)		/* A JRSTF in non-zero section is a MUUO */
	return i_muuo(op, ac, e);
#endif
    cpu.mr_injrstf = TRUE;

    /* Assume that our execution started at mr_PC.  This will be untrue
    ** only if we're being executed as a trap or interrupt instruction.
    ** XCT is allowed, but PXCT is fortunately illegal.
    */
    ea = PC_VADDR;			/* Get PC as a virtual address */
    for (;;) {
	w = vm_fetch(ea);		/* Get instr */
	if (iw_op(w) != I_XCT)
	    break;
	ea = ea_calc(w);		/* Is XCT, track chain */
    }
    if (iw_op(w) != op			/* Must match original instr */
      || iw_ac(w) != ac)		/* both in OP and AC */
	panic("ij_jrstf: cannot duplicate op");

    /* Now get to the point of this stupid exercise! */
    w = ea_wcalc(w, cpu.acblk.xea, cpu.vmap.xea);	/* Do full EA calc */
    if (RHGET(w) != va_insect(e))			/* One more check... */
	panic("ij_jrstf: cannot duplicate E");
    cpu.mr_injrstf = FALSE;			/* No more pagefault risk */
    return IJ_DO_RSTF(w, e, FALSE);
}

/* JEN (JRST 12,) - Combination of JRST10 and JRSTF.
**	Lets those routines do the work.  JRSTF's hack to find the PC flags
**	will work, because the opcode & AC are all passed along to it.
**	If user mode, only legal if User-IOT is set.
*/
insdef(ij_jen)
{
#if KLH10_EXTADR
    if (PC_ISEXT)
	return i_muuo(op, ac, e);
#endif
#if KLH10_CPU_KS || KLH10_CPU_KA
    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO))
#elif KLH10_CPU_KL
    if ((PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO))	/* User-IOT or Kernel */
	|| (!PCFTEST(PCF_USR) && PCFTEST(PCF_PUB)))
#elif KLH10_CPU_KI
    if (PCFTEST(PCF_USR | PCF_PUB))		/* Kernel mode only */
#endif
	return i_muuo(op, ac, e);
    pi_dismiss();	/* Tell PI to dismiss current int */
    return ij_jrstf(op, ac, e);
}

/* JRST 10, - Dismisses current interrupt ("restores the level on which the
**	highest priority interrupt is currently being held").
**	If user mode, only legal if User-IOT is set.
*/
insdef(ij_jrst10)
{
#if KLH10_CPU_KS || KLH10_CPU_KA
    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO))	/* User-IOT or Exec */
#elif KLH10_CPU_KL
    if ((PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO))	/* User-IOT or Kernel */
	|| (!PCFTEST(PCF_USR) && PCFTEST(PCF_PUB)))
#elif KLH10_CPU_KI
    if (PCFTEST(PCF_USR | PCF_PUB))		/* Kernel mode only */
#endif
	return i_muuo(op, ac, e);

    pi_dismiss();		/* Tell PI to dismiss current int */
    PC_JUMP(e);			/* Take the jump... */

#if KLH10_DEBUG
    if (cpu.mr_debug) {
	fprintf(stderr,"[ -> ");
	pishow(stderr); pcfshow(stderr, cpu.mr_pcflags);
	fprintf(stderr,"%lo:]\r\n", (long) PC_30);
    }
#endif
    return PCINC_0;
}

/* HALT (JRST 4,) - Halt the processor.
*/
insdef(ij_halt)
{
#if KLH10_CPU_KS || KLH10_CPU_KA
    if (!PCFTEST(PCF_USR))		/* Exec mode can halt */
#elif KLH10_CPU_KL || KLH10_CPU_KI
    if (!PCFTEST(PCF_USR|PCF_PUB))	/* Only Kernel mode can halt */
#endif
    {
	cpu.mr_haltpc = cpu.mr_PC;	/* Remember loc of the HALT instr */
	PC_JUMP(e);			/* Update PC as if jumped */
	apr_halt(HALT_PROG);		/* Halt processor! */
	/* Never returns */
    }
    return i_muuo(op, ac, e);
}

/* DEC extended variants of JRST, never used on ITS or a single-section KL */

#if !KLH10_SYS_ITS && (KLH10_CPU_KS||KLH10_CPU_KLX)

/* XJRSTF (JRST 5,)
**	Pretty much the same as JRSTF except the flags and PC come from
**	a pair of memory locations.  On the KS10 no section addressing
**	is possible (wonder what happens if tried on a real KS10?)
**
**	XJRSTF and XJEN don't save anything.
**	A new PCS is loaded from the flag word only if a KLX and the new
**		flags specify exec mode (PRM 2-66, 2nd sentence from bottom).
**	The new flags are used verbatim, except for the same restrictions
**		as JRSTF (that is, USER, UIO, PUBLIC flags are special-cased).
**		(PRM 2-71)
** See dorstf() for more comments.
*/
insdef(ij_xjrstf)
{
    register dw10_t dpcw;

    vm_dread(e, dpcw);		/* Fetch double-wd from E, E+1 (may pageflt) */
    va_lfrword(e, LOGET(dpcw));			/* New PC from c(E+1) */
    return IJ_DO_RSTF(HIGET(dpcw), e, TRUE);	/* Use flags from c(E) */
}

/* XJEN (JRST 6,)
**	See comments for XJRSTF -- essentially the same.
**	To XJRSTF as JEN is to JRSTF, but only allowed in Exec mode.
**	However, one special precaution is taken; the PCW at E, E+1 is fetched
**	prior to restoring an interrupt level, to ensure any page fault
**	happens before clobbering the PI status!
** This is why the XJRSTF code is duplicated, rather than calling
**	it after dismissing the interrupt.
*/
insdef(ij_xjen)
{
    register dw10_t dpcw;

#if KLH10_CPU_KS
    if (PCFTEST(PCF_USR))	/* Ensure we're in exec mode */
#elif KLH10_CPU_KLX
    if ((PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO))	/* User-IOT or Kernel */
	|| (!PCFTEST(PCF_USR) && PCFTEST(PCF_PUB)))
#endif
	return i_muuo(op, ac, e);

    vm_dread(e, dpcw);		/* Fetch double-wd from E, E+1 (may pageflt) */
    pi_dismiss();		/* Tell PI to dismiss current int */
    va_lfrword(e, LOGET(dpcw));			/* New PC from E+1 */
    return IJ_DO_RSTF(HIGET(dpcw), e, TRUE);	/* Use flags from E */
}

/* XPCW (JRST 7,)
**	Note special precaution as for XJEN to ensure any page faults happen
**	prior to changing flags or PC.
**
**	PCS is saved in the flag word ONLY if old mode was exec (the flag
**		test is on old flags, not new ones).
**	No new PCS is set.
**	The new flags are taken from the new flag word, subject to
**		same restrictions as JRSTF.  It doesn't appear that
**		either PCP or PCU is specially set.
**	PRM says XPCW should only be used as interrupt instr, but T20
**		monitor does use it as regular instr (in SCHED).
**		This code only implements its "regular" behavior; the KLH10
**		PI interrupt code special-cases XPCW itself and never calls
**		this routine.
*/
insdef(ij_xpcw)
{
    register w10_t w;
    register vaddr_t e2;
    dw10_t dpcw;

#if KLH10_CPU_KS
    if (PCFTEST(PCF_USR))	/* Ensure we're in exec mode */
#elif KLH10_CPU_KLX
    if ((PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO))	/* User-IOT or Kernel */
	|| (!PCFTEST(PCF_USR) && PCFTEST(PCF_PUB)))
#endif
	return i_muuo(op, ac, e);

    e2 = e;
    va_add(e2, 2);		/* Get E+2 */
    vm_dread(e2, dpcw);		/* Fetch double from E+2, E+3 (may pageflt) */

    LRHSET(w, cpu.mr_pcflags, 0);	/* Set up 1st word of PCW */
#if KLH10_EXTADR
    if (!PCFTEST(PCF_USR))	/* If in exec mode */
	RHSET(w, pag_pcsget());	/* then OK to add pager's PrevContextSection */
#endif
    vm_write(e, w);		/* Store in E */
    PC_1TOWORD(w);		/* Set up PC+1 word  */
    va_inc(e);
    vm_write(e, w);		/* Store in E+1 */

    va_lfrword(e2, LOGET(dpcw));		/* New PC from E+3 */
    return IJ_DO_RSTF(HIGET(dpcw), e2, FALSE);	/* Use flags from E+2 */
}

/* SFM (JRST 14,)
**	This is now legal in user mode, but the PCS info is only stored if in
**	exec mode. (PRM 2-72)
**	Additionally, PRM 2-73 says on an extended KL, SFM is only legal in
**	NZ section.  However, this was changed as of KL ucode 331 to
**	allow 0-section as well, and there is no IO-legal test;
**	PCS is still only saved if in exec mode, though.
*/
insdef(ij_sfm)
{
    register w10_t w;

#if KLH10_CPU_KS		/* KS10 requires exec mode (kernel) */
    if (PCFTEST(PCF_USR))	/* Ensure we're in exec mode */
	return i_muuo(op, ac, e);
#endif

    /* Don't need to mung flags since not changing modes */
    LRHSET(w, cpu.mr_pcflags, 0);	/* Set up 1st word of PCW */
#if KLH10_EXTADR
    if (!PCFTEST(PCF_USR))		/* If in exec mode, */
	RHSET(w, pag_pcsget());		/* add PCS to the flag word */
#endif
    vm_write(e, w);			/* Store in E */
    return PCINC_1;			/* That's all, no jump! */
}

#if KLH10_CPU_KLX

/* XJRST (JRST 15,)
**	This is yet another late addition to the KL that was never
** documented in the PRM.  It seems to have been added as of ucode
** 301 and is used extensively in the T20 monitor.
**	Basically appears to jump to c(E) rather than E, treating
** the contents of E as a full 30-bit global virtual address.  It's
** not clear whether the high 6 bits matter, but I'll assume they are
** ignored.
**	This also appears to be legal in any section, in any mode.
** Not a bad idea, actually.
*/

insdef(ij_xjrst)
{
    register w10_t w;
    register vaddr_t va;

    w = vm_read(e);		/* Fetch c(E) */
    va_gfrword(va, w);		/* Turn into a virtual address */
    PC_JUMP(va);		/* Jump there! */
    return PCINC_0;
}
#endif /* KLX */

#endif /* !ITS && (KS || KLX) */

/* Stack instructions */

/* Note: on the KA10 the push and pop operations are done by
** adding or subtracting 1,,1 as a word entity (ie not independent halves).
** Also, there are no trap flags, so Pushdown Overflow is set instead.
** These actions are not yet coded for, to avoid clutter.
*/

/* Note special page map context references for stack instructions.
** These are so PXCT can work by twiddling the context pointers.
** For:
**	Computing E			- normal ea_calc() using XEA map
**	R/W of c(E) for PUSH, POP	- normal XRW map
**	R/W of stack data		- special XBRW map
*/
#define vm_stkread(a)     (vm_pget(vm_xbrwmap((a), VMF_READ)))
#define vm_stkwrite(a, w) (vm_pset(vm_xbrwmap((a), VMF_WRITE), (w)))
#define vm_stkmap(a, f)	  vm_xbrwmap((a), (f))

/* No other special actions are needed for PXCT; the test for running
** extended is always made using PC section (regardless of context).
**	However, on an extended KL Uhler claims stack references should
** always be made in the current context.  What isn't clear is whether
** a KLX is expected to ignore the stack-context AC bit (12), or if it
** does undefined things as a result.
**	For generality this code does implement stack reference mapping even
** on a KLX, which corresponds to the latter option.  As long as the
** monitor only uses valid AC bit combinations we'll be fine.
**
** HOWEVER - it appears from the DFKED diagnostic that bit 12 DOES
** operate in a real KL and it affects both the test for PC section AND the
** mapping used for the stack pointer.  Ugh!!!!
** Don't know yet whether it is used by a real monitor.
*/

insdef(i_push)
{
    register w10_t w;
    register vaddr_t sloc;

    w = ac_get(ac);

#if KLH10_EXTADR
    /* Determine if stack pointer is local or global */
    if (PC_ISEXT && op10m_skipge(w) && op10m_tlnn(w, VAF_SMSK)) {
	/* Global format */
	op10m_inc(w);				/* Increment entire word */
	va_gfrword(sloc, w);			/* Get global addr from wd */
	vm_stkwrite(sloc, vm_read(e));		/* Push c(E) on stack */
    } else
#endif
    {
	/* Local format */
	RHSET(w, (RHGET(w)+1)&H10MASK);		/* Increment RH only */
	va_lmake(sloc, PC_SECT, RHGET(w));	/* Get local address */
	vm_stkwrite(sloc, vm_read(e));		/* Push c(E) on stack */

	/* All mem refs won, safe to store AC and test for setting Trap 2 */
	LHSET(w, (LHGET(w)+1)&H10MASK);		/* Increment LH */
	if (LHGET(w) == 0)			/* If became 0, */
	    PCFTRAPSET(PCF_TR2);		/* set Trap 2! */
    }
    ac_set(ac, w);
    return PCINC_1;
}

insdef(i_pushj)
{
    register w10_t pcw, w;
    register vaddr_t sloc;


    w = ac_get(ac);
    PC_1WORD(pcw);		/* Make PC+1 word */

#if KLH10_EXTADR

    /* Determine if stack pointer is local or global */
    if (PC_ISEXT && op10m_skipge(w) && op10m_tlnn(w, VAF_SMSK)) {
	/* Global format */
	op10m_inc(w);				/* Increment entire word */
	va_gfrword(sloc, w);			/* Get global addr from wd */
	vm_stkwrite(sloc, pcw);			/* Push PC word on stack */
	PCFCLEAR(PCF_FPD|PCF_AFI|PCF_TR2|PCF_TR1);	/* Clear these flags */
    } else
#endif
    {
	/* Local format */
	RHSET(w, (RHGET(w)+1)&H10MASK);		/* Increment RH only */
	va_lmake(sloc, PC_SECT, RHGET(w));	/* Get local address */
	vm_stkwrite(sloc, pcw);			/* Push PC word on stack */

	/* All mem refs won, safe to store AC and test for setting Trap 2 */
	PCFCLEAR(PCF_FPD|PCF_AFI|PCF_TR2|PCF_TR1);	/* Clear these flags */
	LHSET(w, (LHGET(w)+1)&H10MASK);		/* Increment LH */
	if (LHGET(w) == 0)			/* If became 0, */
	    PCFTRAPSET(PCF_TR2);		/* set Trap 2! */
    }
    ac_set(ac, w);
    PC_JUMP(e);				/* Jump to new PC */
    return PCINC_0;
}


insdef(i_pop)
{
    register w10_t w;
    register vaddr_t sloc;

    w = ac_get(ac);

#if KLH10_EXTADR
    /* Determine if stack pointer is local or global */
    if (PC_ISEXT && op10m_skipge(w) && op10m_tlnn(w, VAF_SMSK)) {
	/* Global format */
	va_gfrword(sloc, w);			/* Get global addr from wd */
	vm_write(e, vm_stkread(sloc));		/* Pop stack to c(E) */
	op10m_dec(w);				/* Decrement entire word */

    } else
#endif
    {
	/* Local format */
	va_lmake(sloc, PC_SECT, RHGET(w));	/* Get local address */
	vm_write(e, vm_stkread(sloc));		/* Pop stack to c(E) */

	/* All mem refs won, safe to store AC and test for setting Trap 2 */
	RHSET(w, (RHGET(w)-1)&H10MASK);		/* Decrement RH only */
	if (LHGET(w) == 0) {			/* If decrement will wrap, */
	    PCFTRAPSET(PCF_TR2);		/* set Trap 2! */
	    LHSET(w, H10MASK);
	} else
	    LHSET(w, LHGET(w)-1);		/* Decr LH, no mask needed */
    }
    ac_set(ac, w);
    return PCINC_1;
}

insdef(i_popj)
{
    register w10_t pcw, w;
    register vaddr_t sloc;

    w = ac_get(ac);

#if KLH10_EXTADR
    /* Determine if stack pointer is local or global */
    if (PC_ISEXT && op10m_skipge(w) && op10m_tlnn(w, VAF_SMSK)) {
	/* Global format */
	va_gfrword(sloc, w);			/* Get global addr from wd */
	pcw = vm_stkread(sloc);			/* Pop PC word off stack */
	op10m_dec(w);				/* Decrement entire word */
    } else
#endif
    {
	/* Local format */
	va_lmake(sloc, PC_SECT, RHGET(w));	/* Get local address */
	pcw = vm_stkread(sloc);			/* Pop PC word off stack */

	/* All mem refs won, safe to store AC and test for setting Trap 2 */
	RHSET(w, (RHGET(w)-1)&H10MASK);		/* Decrement RH only */
	if (LHGET(w) == 0) {			/* If decrement will wrap, */
	    PCFTRAPSET(PCF_TR2);		/* set Trap 2! */
	    LHSET(w, H10MASK);
	} else
	    LHSET(w, LHGET(w)-1);		/* Decr LH, no mask needed */
    }

    /* Build correct new PC from PC word popped off */
    if (PC_ISEXT)
	va_lfrword(e, pcw);
    else va_lmake(e, PC_SECT, RHGET(pcw));

    ac_set(ac, w);
    PC_JUMP(e);				/* Now jump to restored PC */
    return PCINC_0;
}


insdef(i_adjsp)
{
    register w10_t w;
    register h10_t h;

    w = ac_get(ac);			/* Get stack pointer */
#if KLH10_EXTADR
    /* Determine if stack pointer is local or global */
    if (PC_ISEXT && op10m_skipge(w) && op10m_tlnn(w, VAF_SMSK)) {
	/* Global format */
	if (H10SIGN & va_insect(e)) {	/* Negative adjustment? */
	    register w10_t wadj;
	    LRHSET(wadj, H10MASK, va_insect(e));	/* Set up word */
	    op10m_add(w, wadj);			/* Add negative adj */
	} else
	    op10m_addi(w, va_insect(e));	/* Can just add immediately */
    } else
#endif
    {
	/* Local format */
	register h10_t adj = va_insect(e);
	RHSET(w, (RHGET(w)+adj)&H10MASK);	/* Add offset to RH */
	if ((h = LHGET(w)+adj) & H10SIGN) {	/* New count negative? */
	    if ((adj & H10SIGN) && (LHGET(w) & H10SIGN)==0)
		PCFTRAPSET(PCF_TR2);		/* Neg E made cnt pos -> neg */
	} else {				/* New count positive */
	    if ((adj & H10SIGN)==0 && (LHGET(w) & H10SIGN))
		PCFTRAPSET(PCF_TR2);		/* Pos E made cnt neg -> pos */
	}
	LHSET(w, (h & H10MASK));		/* Store new count */
    }
    ac_set(ac, w);
    return PCINC_1;
}

