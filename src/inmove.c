/* INMOVE.C - Word move instruction routines
*/
/* $Id: inmove.c,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: inmove.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include "klh10.h"
#include "kn10def.h"	/* Machine defs */
#include "kn10ops.h"	/* PDP-10 ops */

#ifdef RCSID
 RCSID(inmove_c,"$Id: inmove.c,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* See CODING.TXT for guidelines to coding instruction routines. */


/* Full-word data stuff */

insdef(i_exch)		/* EXCH AC,E */
{
    register vmptr_t p = vm_modmap(e);	/* Check access, get pointer */
    register w10_t w;
    w = vm_pget(p);			/* Save mem in temporary loc */
    vm_pset(p, ac_get(ac));		/* Move c(AC) to E */
    ac_set(ac, w);			/* Move c(E) to AC */
    return PCINC_1;
}


/* MOVEx group - MOVE, MOVEI, MOVEM, MOVES */

insdef(i_move)		/* MOVE AC,E */
{
    ac_set(ac, vm_read(e));
    return PCINC_1;
}

insdef(i_movei)		/* MOVEI AC,E */
{
    ac_setlrh(ac, 0, va_insect(e));
    return PCINC_1;
}

insdef(i_movem)		/* MOVEM AC,E */
{
    vm_write(e, ac_get(ac));
    return PCINC_1;
}

insdef(i_moves)		/* MOVES AC,E */
{
    register vmptr_t p = vm_modmap(e);	/* Make write access ref */
    if (ac) ac_set(ac, vm_pget(p));
    return PCINC_1;
}


/* MOVSx group - MOVS, MOVSI, MOVSM, MOVSS */

insdef(i_movs)		/* MOVS AC,E */
{
    register w10_t w;
    w = vm_read(e);
    ac_setlrh(ac, RHGET(w), LHGET(w));
    return PCINC_1;
}

insdef(i_movsi)		/* MOVSI AC,E */
{
    ac_setlrh(ac, va_insect(e), 0);
    return PCINC_1;
}

insdef(i_movsm)		/* MOVSM AC,E */
{
    register w10_t w;
    LRHSET(w, ac_getrh(ac), ac_getlh(ac));
    vm_write(e, w);
    return PCINC_1;
}

insdef(i_movss)		/* MOVSS AC,E */
{
    register vmptr_t p = vm_modmap(e);	/* Make R/W access ref */
    register w10_t w;
    LRHSET(w, vm_pgetrh(p), vm_pgetlh(p));
    vm_pset(p, w);				/* Store back in mem */
    if (ac) ac_set(ac, w);
    return PCINC_1;
}


/* MOVNx group - MOVN, MOVNI, MOVNM, MOVNS */

insdef(i_movn)		/* MOVN AC,E */
{
    register w10_t w;
    w = vm_read(e);
    op10mf_movn(w);		/* Negate, setting flags if necessary */
    ac_set(ac, w);
    return PCINC_1;
}

insdef(i_movni)		/* MOVNI AC,E */
{
    register w10_t w;
    LRHSET(w, 0, va_insect(e));
    op10mf_movn(w);		/* Negate, setting flags if 0 */
    ac_set(ac, w);
    return PCINC_1;
}

insdef(i_movnm)		/* MOVNM AC,E */
{
    register vmptr_t p = vm_wrtmap(e);	/* Check mem ref first */
    register w10_t w;
    w = ac_get(ac);
    op10mf_movn(w);		/* Negate, setting flags if necessary */
    vm_pset(p, w);
    return PCINC_1;
}

insdef(i_movns)		/* MOVNS AC,E */
{
    register vmptr_t p = vm_modmap(e);	/* Check mem ref first */
    register w10_t w;
    w = vm_pget(p);
    op10mf_movn(w);		/* Negate, setting flags if necessary */
    vm_pset(p, w);
    if (ac) ac_set(ac, w);
    return PCINC_1;
}


/* MOVMx group - MOVM, MOVMI, MOVMM, MOVMS */

insdef(i_movm)		/* MOVM AC,E */
{
    register w10_t w;
    w = vm_read(e);
    op10mf_movm(w);		/* Make pos, setting flags if necessary */
    ac_set(ac, w);
    return PCINC_1;
}

#if 0
insdef(i_movmi)		/* MOVMI AC,E  is identical to MOVEI AC,E */
#endif

insdef(i_movmm)		/* MOVMM AC,E */
{
    register vmptr_t p = vm_wrtmap(e);	/* Check mem ref first */
    register w10_t w;
    w = ac_get(ac);
    op10mf_movm(w);		/* Make pos, setting flags if necessary */
    vm_pset(p, w);
    return PCINC_1;
}

insdef(i_movms)		/* MOVMS AC,E */
{
    register vmptr_t p = vm_modmap(e);	/* Check mem ref first */
    register w10_t w;
    w = vm_pget(p);
    op10mf_movm(w);		/* Make pos, setting flags if necessary */
    vm_pset(p, w);
    if (ac) ac_set(ac, w);
    return PCINC_1;
}

/* Double-word data move instructions.
**	DMOVE and DMOVEM are handled differently from the other instructions
**	that manipulate double words, for greater efficiency.
**
** There is a large fuzzy gray area surrounding the question of page fails
** for double-word operands.  In general the KLH10 tries not to do anything
** at all if a page fail would happen on either the first or second word,
** thus avoiding the issue of intermediate results.
**
** BUT!  Examination of the ucode indicates otherwise:
**	KL DMOVE/DMOVN: Atomic; both words are fetched before any ACs are set.
**		A pagefail on read of 2nd word leaves no ACs changed.
**	KL DMOVEM/DMOVNM: First gets both ACs, then stores.
**		1st (high) word is stored, then 2nd (low);
**		A pagefail on write of 2nd word leaves 1st already stored.
**
**	KS DMOVE/DMOVN: same as KL.
**	KS DMOVEM/DMOVNM: 2nd word is stored, then 1st!?
**
** The KL diag (DFKEA) agrees with the KL for DMOVE and DMOVEM (DMOVN/M is
** not tested).
**
** Given this discrepancy, the KLH10's approach isn't so bad.  But just to
** be ultra safe, I'll attempt to emulate the KS/KL lossage.
*/

/* DMOVE checks to see if the double can be copied directly into the
** ACs.  If not, intermediate storage must be used to allow for the
** possibility of a page fault between words, or AC/address wraparound, or
** AC overlap if mem ref is to ACs.
*/

insdef(i_dmove)		/* DMOVE AC,E */
{
    /* See if fast move OK, normally true.  Hope the testing doesn't end up
    ** using more time than the conservative case!
    ** NOTE this assumes C structure copies are done sequentially.  If this
    ** is not true, must test for e == ac+1 instead.
    */
    if (ac_issafedouble(ac)		/* ACs contiguous? */
      && vm_issafedouble(e)		/* Mem locs safe too? */
      && (va_insect(e) != ac_off(ac,-1)))	/* no AC overlap with Mem? */
	*ac_mapd(ac) = vm_pgetd(vm_xrwmap(e,VMF_READ|VMF_DWORD)); /* Yep! */
    else {
	register dw10_t d;	/* Conservative case, use intermed stg */
	vm_dread(e, d);		/* Fetch the double */
	ac_dset(ac, d);		/* Set in ACs */
    }
    return PCINC_1;
}

insdef(i_dmovem)	/* DMOVEM AC,E */
{
    /* See if fast move OK, normally true.  Hope the testing doesn't end up
    ** using more time than the conservative case!
    ** NOTE this assumes C structure copies are done sequentially.  If this
    ** is not true, must test for e == ac-1 instead.
    */
    if (ac_issafedouble(ac)		/* ACs contiguous? */
      && vm_issafedouble(e)		/* Mem locs safe too? */
      && (va_insect(e) != ac_off(ac,1)))	/* no AC overlap with Mem? */
	vm_psetd(vm_xrwmap(e,VMF_WRITE|VMF_DWORD), *ac_mapd(ac)); /* Yep! */
    else {
	register dw10_t d;	/* Conservative case, use intermed stg */
#if KLH10_CPU_KL
	ac_dget(ac, d);		/* Get from ACs */
	vm_write(e, d.w[0]);	/* Do first word */
	va_inc(e);
	vm_write(e, d.w[1]);	/* Do second word */

#elif KLH10_CPU_KS
	register vaddr_t va;
	ac_dget(ac, d);		/* Get from ACs */
	va = e;
	va_inc(va);
	vm_write(va, d.w[1]);	/* Do second word first! */
	vm_write(e, d.w[0]);	/* Do first word last! */

#else	/* What I'd really prefer to use - atomic double write */
	register vmptr_t p0, p1;
	p0 = vm_xrwmap(e, VMF_WRITE);	/* Check access for 1st word */
	va_inc(e);			/* Bump E by 1, may wrap funnily */
	p1 = vm_xrwmap(e, VMF_WRITE);	/* Check access for 2nd word */
	ac_dget(ac, d);			/* Get from ACs */
	vm_pset(p0, d.w[0]);		/* Set 1st from AC */
	vm_pset(p1, d.w[1]);		/* Set 2nd from AC+1 */
#endif
    }
    return PCINC_1;
}

/* DMOVN is a vanilla double-precision arithmetic instruction.
**	May set flags.
*/
insdef(i_dmovn)			/* D_MOVN AC,E */
{
    register dw10_t d;
    vm_dread(e, d);	/* Fetch into D */
    op10mf_dmovn(d);	/* Negate in place, with flags */
    ac_dset(ac, d);	/* Store in ACs */
    return PCINC_1;
}

/* DMOVNM requires special-casing due to differing order of operation
**	for the double-word memory store.
**	May set flags.  Note flags are set prior to possible page fail,
**	thus potentially losing just like the ucode.
*/
insdef(i_dmovnm)		/* DMOVNM AC,E */
{
    register dw10_t d;

    ac_dget(ac, d);			/* Fetch AC, AC+1 into D */
    op10mf_dmovn(d);			/* Negate in place, with flags! */
    if (vm_issafedouble(e))		/* Fast move OK? */
	vm_psetd(vm_xrwmap(e,VMF_WRITE|VMF_DWORD), d);	/* Yep! */
    else {
#if KLH10_CPU_KL
	vm_write(e, HIGET(d));		/* Do first word */
	va_inc(e);
	vm_write(e, LOGET(d));		/* Do second word */

#elif KLH10_CPU_KS
	register vaddr_t va;
	va = e;
	va_inc(va);
	vm_write(va, LOGET(d));		/* Do second word first! */
	vm_write(e,  HIGET(d));		/* Do first word last! */

#else	/* What I'd really prefer to use - atomic double write */
	register vmptr_t p0, p1;
	p0 = vm_xrwmap(e, VMF_WRITE);	/* Check access for 1st word */
	va_inc(e);			/* Bump E by 1, may wrap funnily */
	p1 = vm_xrwmap(e, VMF_WRITE);	/* Check access for 2nd word */
	vm_pset(p0, HIGET(d));		/* Set 1st (high) */
	vm_pset(p1, LOGET(d));		/* Set 2nd (low) */
#endif
    }
    return PCINC_1;
}

/* BLT.  (XBLT is handled by the EXTEND instr code) */

/* BLT - with optimization for case where dest is source+1.
**	Note test for using same mapping, since BLT can be used for
**	transfers between user and exec maps!
*/

/* Note special page map context references for BLT.
** These are so PXCT can work by twiddling the context pointers.
** For:
**	Computing E			- normal ea_calc() using XEA map
**	Store word to destination	- normal XRW map
**	Read word from source		- special XBRW map
*/
#define vm_srcread(a)     (vm_pget(vm_xbrwmap((a), VMF_READ)))

/* Additional PXCT note for KLX:
**	Uhler claims both source and dest must use the same (previous)
** context (thus the only valid AC values are 5 and 15 -- bits 10 and 12
** must always be set!)
**	It isn't clear whether a KLX is expected to ignore those bits
** and always use previous context, or if it simply does undefined things
** if those bits are off (requesting current context).
**	For simplicity this code does implement current context mapping
** even on a KLX, which corresponds to the latter option.  As long as
** the monitor only uses valid AC bit combinations this is okay.
*/

/* There are some ambiguities in the DEC PRM p.2-8 description of BLT.

* Do the KI/KA test for RH[AC] >= RH(E) just as the KS/KL do, or can
  the destination block wrap around?
	[Assume yes, no dest wraparound]
* What happens if AC *is* in the destination block?
	[Assume value indeterminate unless last word of xfer, then is
	that value.]
* What happens if AC is in the source block?
	[Assume value indeterminate, but if no pager/int, is original value]
	[WRONG!!!! Turns out that for KS/KL, the AC is updated immediately
	with the final src,,cnt value!  So any copy involving that AC as
	source will contain the "final" value.  Aborts due to pagefail
	or PI will re-clobber it with the correct intermediate value.
	Exhibited by this test: MOVE 1,[1,,1] ? BLT 1,1
	which leaves 2,,2 in AC1.]

* Exactly what is left in the AC for the KL case of RH[AC] > RH(E)?
  The PRM says as if the "reverse xfer" happened, but does this
  mean the last locs that wd have been referenced, or the next ones?
	[Assume latter - the "next" ones.]
* The footnote on PRM p.2-8 claims an extended KL will count from section
  0 up into 1.  Exactly what does this mean?  That a source block
  could consist of stuff from sect 0 mem, then the ACs (cuz 1,,0-17 is
  used to ref the ACs), then sect 1 mem?  What about the optimized
  case where dest == src+1, would this still use the original
  word value or would the BLT start reading from the sect 1 source?
	[Assume we don't need to emulate this lossage.  Yeech.]

* !! Note from Uhler document that source and dest are incremented in-section
  only, even if addresses are global, without altering local/global flag
  that pager uses.
*/

insdef(i_blt)
{
    register int32 cnt;		/* Note signed */
    register vaddr_t src, dst;
    register vmptr_t vp;

#if KLH10_EXTADR
    src = dst = e;			/* E sets default section & l/g flag */
    va_setinsect(src, ac_getlh(ac));	/* Set up source addr */
    va_setinsect(dst, ac_getrh(ac));	/* Set up dest addr */
#else
    va_lmake(src, 0, ac_getlh(ac));
    va_lmake(dst, 0, ac_getrh(ac));
#endif
    cnt = (va_insect(e) - va_insect(dst));	/* # words to move, less 1 */

    if (cnt <= 0) {
	/* Special case, either just 1 word or dest block would wrap
	** (So-called "reverse transfer" attempt).  Main reason for
	** handling this specially is to emulate KL rev-xfer weirdness.
	*/
	register vmptr_t rp;

	/* First ensure we can carry out the single xfer expected */
	if (!(rp = vm_xbrwmap(src, VMF_READ|VMF_NOTRAP))
	  || !(vp = vm_xrwmap(dst, VMF_WRITE|VMF_NOTRAP))) {
	    pag_fail();				/* Ugh, take page-fail trap */
	}

	/* OK, now if KL or KS, pre-clobber AC appropriately *before*
	** the copy, so that if AC is involved, behavior will emulate
	** the machines.  Sigh.
	*/
#if KLH10_CPU_KL
	/* KL always updates as if the full reverse xfer happened */
	ac_setlrh(ac, (va_insect(src)+cnt+1) & H10MASK,
		      (va_insect(dst)+cnt+1) & H10MASK);
#elif KLH10_CPU_KS
	/* KS only reflects what actually got xfered (according to PRM;
	** it may in fact behave like the KL for all I know!)
	*/
	ac_setlrh(ac, (va_insect(src)+1) & H10MASK,
		      (va_insect(dst)+1) & H10MASK);
#endif
	vm_pset(vp, vm_pget(rp));	/* Copy directly from source */

	return PCINC_1;
    }

#if KLH10_CPU_KS || KLH10_CPU_KL
    /* Before starting xfer, clobber AC to final value.  This emulates
    ** the peculiar way the KS/KL do things.  Note that due to this
    ** pre-clobberage, there is no need to store a final value at the
    ** end of a successful BLT.  Any aborts MUST TAKE CARE to store the proper
    ** intermediate value!!
    */
    ac_setlrh(ac, (va_insect(src)+cnt+1) & H10MASK,
		  (va_insect(dst)+cnt+1) & H10MASK);
#endif /* KS || KL */

    if ((va_insect(src)+1) == va_insect(dst)	/* Special case? */
      && (cpu.vmap.xrw == cpu.vmap.xbrw)) {	/* Must have same mapping! */

	/* Simple set-memory-to-value BLT */
	register w10_t w;

	/* Get first word and remember that.  Just in case it helps the
	** compiler optimize, use xrw map instead of xbrw since we only
	** come here if they're the same.
	*/
	if (!(vp = vm_xrwmap(src, VMF_READ|VMF_NOTRAP))) {
	    ac_setlrh(ac, va_insect(src),	/* Oops, save AC */
			  va_insect(dst));
	    pag_fail();			/* Take page-fail trap */
	}
	w = vm_pget(vp);		/* Get word, remember it */

	do {
	    CLOCKPOLL();		/* Keep clock going */
	    if (INSBRKTEST()) {		/* Watch for interrupt */
		ac_setlrh(ac, va_insect(src),	/* Oops, save AC */
			      va_insect(dst));
		apr_int();		/* Take interrupt */
	    }
	    if (!(vp = vm_xrwmap(dst, VMF_WRITE|VMF_NOTRAP))) {
		ac_setlrh(ac, va_insect(src),	/* Oops, save AC */
			      va_insect(dst));
		pag_fail();			/* Take page-fail trap */
	    }
	    vm_pset(vp, w);		/* Store word value */
	    va_linc(src);		/* Do LOCAL increment of addrs! */
	    va_linc(dst);
	} while (--cnt >= 0);
    }
    else do {

	/* Normal BLT transfer */
	register vmptr_t rp;
	CLOCKPOLL();			/* Keep clock going */
	if (INSBRKTEST()) {		/* Watch for interrupt */
	    ac_setlrh(ac, va_insect(src),	/* Oops, save AC */
			  va_insect(dst));
	    apr_int();			/* Take interrupt */
	}

	if (!(rp = vm_xbrwmap(src, VMF_READ|VMF_NOTRAP))
	  || !(vp = vm_xrwmap(dst, VMF_WRITE|VMF_NOTRAP))) {
	    ac_setlrh(ac, va_insect(src),	/* Oops, save AC */
			  va_insect(dst));
	    pag_fail();			/* Take page-fail trap */
	}
	vm_pset(vp, vm_pget(rp));	/* Copy directly from source */
	va_linc(src);			/* Add 1 to both addrs */
	va_linc(dst);			/* Again, LOCAL increment only */

    } while (--cnt >= 0);

#if 0	/* No longer needed, code done inline to avoid this check */
    /* Broke out of loop, see if page-failed or not */
    if (cnt >= 0) {			/* Use this as indicator */
	ac_setlrh(ac, va_insect(src),	/* Oops, save AC */
		      va_insect(dst));
	pag_fail();			/* Ugh, take page-fail trap */
    }
#endif

#if 0 /* KLH10_CPU_KS || KLH10_CPU_KL */
    /* This is no longer used.  Instead, to more accurately emulate the
    ** KS/KL, the final AC value is stored immediately at the start of
    ** the BLT code, so if nothing goes wrong, nothing needs to be done.
    */

    /* End of BLT, but KS/KL must leave AC pointing
    ** to what would be the next transfer... unless AC happens
    ** to be the last destination!
    **
    ** vp still points to last destination, so we can use that for testing,
    ** avoiding hassle of determining whether dst is an ac reference!
    */
    if (ac_map(ac) != vp)
	ac_setlrh(ac, va_insect(src), va_insect(dst));
#endif	/* KS || KL */

    return PCINC_1;
}

#if 0	/* Old version, saved in memory of simpler times */

insdef(i_blt)
{
    register w10_t w, val;
    register int optim;

    e &= H10MASK;
    w = ac_get(ac);		/* Get src,,dst */

    if ((LHGET(w)+1)&H10MASK == RHGET(w))	/* Special case? */
      && (cpu.vmap.xrw == cpu.vmap.xbrw)) {	/* Must be same mapping! */
	val = vm_srcread(LHGET(w));
	optim = TRUE;
    } else optim = FALSE;
    for (;;) {
	vm_write(RHGET(w), (optim ? val : vm_srcread(LHGET(w))));
	if (RHGET(w) >= e) {	/* Last word?  Note also stop if AC.rh > E */
	    break;		/* Win! */
	}
	LHSET(w, (LHGET(w)+1)&H10MASK);		/* Add 1 to both halves */
	RHSET(w, (RHGET(w)+1)&H10MASK);
	ac_set(ac, w);		/* Store back in AC in case of pagefail */

	CLOCKPOLL();			/* Keep clock going */
	if (INSBRKTEST()) apr_int();	/* Watch for interrupt */
    }

    /* End of BLT, but to emulate KS10 exactly must leave AC pointing
    ** to what would be the next transfer... unless AC is last destination.
    */
    if (ac != RHGET(w)) {
	LHSET(w, (LHGET(w)+1)&H10MASK);		/* Add 1 to both halves */
	RHSET(w, (RHGET(w)+1)&H10MASK);
	ac_set(ac, w);				/* Store back in AC */
    }
    return PCINC_1;
}
#endif	/* 0 - old version */

/* BLTBU and BLTUB
**
**	New KS10 instructions, not documented in PRM; behavior here is based
** on description from Alan.  These are like BLT but transfer blocks of 8-bit
** bytes, packing or unpacking between "Byte format" and "Unibus format".
**
** "Byte format" is the normal PDP-10 format:
**	<byte0><byte1><byte2><byte3><4 unused bits>
**
** "Unibus format" is sort of like the screwy PDP-11 byte order:
**	<2-bits><byte1><byte0><2-bits><byte3><byte2>
**
** It isn't clear whether the <2-bits> fields are meaningful or whether they
** are mapped to and from the 4-bit field in Byte format.
** [Assume not mapped; extra bits are cleared in both directions]
**	
** [Alan sez:
** Of course in the case of a device that can put 18 bits of data on the
** Unibus, the extra bits show up where the diagram says "2 BITS".  (There is
** a bit in the UBA map that says whether the UBA should expect 18-bit data to
** be written to that page.  I have no idea what happens if you set it wrong,
** perhaps it just controls whether the extra bits are passed through or
** written as 0.)]
*/


#if KLH10_CPU_KS

/* BLTBU - Like BLT but transforms each word from Byte to Unibus format.
** BLTUB - Ditto, Unibus to Byte format.
**	For more explanation of the code, see BLT.
*/
#define defblt(name) \
	insdef(name) {    register w10_t w, val; \
	    register vaddr_t src, dst;		\
	    register uint18 stop = va_insect(e); \
	    w = ac_get(ac);			\
	    va_lmake(src, 0, LHGET(w));		\
	    va_lmake(dst, 0, RHGET(w));		\
	    for (;;) {				\
		val = vm_srcread(src);		\
		blttransform(val);		\
		vm_write(dst, val);		\
		if (RHGET(w) >= stop)		\
		    break;			\
		LHSET(w, (LHGET(w)+1)&H10MASK);	\
		RHSET(w, (RHGET(w)+1)&H10MASK);	\
		ac_set(ac, w);			\
		CLOCKPOLL();			\
		if (INSBRKTEST()) apr_int();	\
		va_inc(src);			\
		va_inc(dst);			\
	    }					\
	    if (ac != RHGET(w)) {		\
		LHSET(w, (LHGET(w)+1)&H10MASK);	\
		RHSET(w, (RHGET(w)+1)&H10MASK);	\
		ac_set(ac, w);			\
	    }					\
	    return PCINC_1;			\
	}
	
/* Byte fmt to Unibus fmt, clears extra bits */
#define blttransform(v) \
	RHSET(v, ((RHGET(v)<<4)&(0377<<8))	\
			| ((LHGET(v)&03)<<6) | ((RHGET(v)>>12)&077) );	\
	LHSET(v, ((LHGET(v)>>10)&0377) | ((LHGET(v)<<6)&(0377<<8)) )
defblt(i_bltbu)
#undef blttransform


/* Unibus fmt to Byte fmt, clears extra bits */
#define blttransform(v) \
	LHSET(v, ((LHGET(v)&0377)<<10)		\
			| ((LHGET(v)&(0377<<8))>>6)	\
			| ((RHGET(v)>>6)&03) );		\
	RHSET(v, ((RHGET(v)>>4)&(0377<<4))	\
			| ((RHGET(v)&&077)<<12) )
defblt(i_bltub)
#undef blttransform

#endif /* KS */

#if KLH10_EXTADR

/* XMOVEI and XHLLI.
**	These two instructions are not as straightforward as they
** might seem.  One might expect them to simply:
**	(1) use whatever the section number of the EA is, or
**	(2) always set LH = 1 if reference is to an AC.
**
** In fact, they do neither.
**	The use of LH=1 to represent a global AC address happens ONLY if:
**	(a) EA is local, (b) section # is non-zero, and (c) in-section
**	address is 0-17 inclusive.
**
** In particular, a local reference to 0,,AC (even if made while PC_ISEXT)
**	will be stored as 0,,AC and not 1,,AC.
** Likewise, a global reference to 0,,AC is stored as 0,,AC even though
**	it *DOES* reference the ACs, just like 1,,AC!
**
** The following table sums it up:
**
**	LOCAL  0,,AC  => 0,,AC
**	LOCAL NZ,,AC  => 1,,AC
**	LOCAL  *,,NAC => *,,NAC
**	GLOBAL *,,*   => *,,*
**
** This behavior is coded into the va_iscvtacref() macro.
**
** Finally, this same conversion is also done for the EA stored as a result
** of a LUUO, MUUO, or pagefail trap.
** (what about PC?)
*/

/* XMOVEI - Actually an extended SETMI.
**	Special boolean variant when using extended addressing
*/
insdef(i_xmovei)
{
    if (va_iscvtacref(e))
	ac_setlrh(ac, 1, va_insect(e));	/* Global AC reference */
    else
	ac_setlrh(ac, va_sect(e), va_insect(e));
    return 1;
}


/* XHLLI - Actually an extended HLLI.
**	Special halfword variant when using extended addressing
*/
insdef(i_xhlli)
{
    if (va_iscvtacref(e))
	ac_setlh(ac, 1);	/* Global AC reference */
    else
	ac_setlh(ac, va_sect(e));
    return 1;
}

#endif	/* KLH10_EXTADR */
