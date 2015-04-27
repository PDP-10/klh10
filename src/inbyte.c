/* INBYTE.C - Byte instruction routines
*/
/* $Id: inbyte.c,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: inbyte.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include "klh10.h"
#include "kn10def.h"
#include "kn10ops.h"

#ifdef RCSID
 RCSID(inbyte_c,"$Id: inbyte.c,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* See CODING.TXT for guidelines to coding instruction routines. */

/* Note special page map context references for byte instructions.
** These are so PXCT can work by twiddling the context pointers.
** For:	
**	computing E			- normal ea_calc() using XEA map
**	read/write of byte ptr at c(E)	- normal XRW map
**	computing E of byte pointed to	- special XBEA map
**	read/write of byte data		- special XBRW map
**
** Extended KL PXCT notes:
**	No preprocessing is needed for EA computation because the default
** section # is always derived from the section the BP was fetched from,
** even under PXCT.  However, postprocessing is needed, hence every BP
** EA calc must check to see whether PXCT is in effect or not.  There
** seems to be no way of easily avoiding this check.
*/

/* More Extended KL notes:

* The PRM is unclear on the topic of how a byte pointer
  should be incremented when the I (indirect) field is set.  X can be
  treated as an invariant (except in the KA10 where ++Y will overflow).
	[Assume appropriately-sized Y (either 18 or 30 bits) is used,
	 with overflow wrapping around within Y.  If indirect happened
	 to be set, tough shit.
	 Empirical testing on KL confirms this]

* What happens if any of the "reserved" (I,X) bits are set in a 2-word BP?
	[Ignored for now.]

* For IDPB and ILDB, which address is used for the byte reference -- one
  computed after incrementing the BP (thus equal to IPB+DPB), or one
  using the address derived from the original pointer (possibly incremented
  by 1)?
	[Assume former, which I think is better since it
	ensures reproducibility; also if increment produces a faulty pointer,
	will catch it quicker.  However, must take care in setting PCF_FPD.
	KS10 ucode appears to likewise use former method.
	Empirical testing on KL also confirms former.  Note in particular
	that any illegal indirection checks on the 2nd wd of a two-word BP
	are done AFTER the BP has been incremented!
	However, note KA10 caveat (footnote on PRM p.2-87) which implies KA
	uses the latter method!]

* The PRM claims that one-word global BPs are only legal in non-zero sections.
  More recent info (Uhler) indicates they are now legal in all sections, altho
  two-word BPs are still only valid in non-zero section contexts.
  SO: Can OWGBPs in section 0 use global addresses and access memory in other
  sections?
	[Assuming yes.]

* The PRM p.2-86 is not clear on whether two-word BPs always use the EFIW
  format for the second word.  The format illustrated is EFIW, but the
  text states "the second word can be local or global, direct or indirect".
  Uhler p.7 says "the second word is either an IFIW or an EFIW".
	[Assuming either IFIW or EFIW allowed]

* Whether the context is NZ-section (thus, whether TWGs are legal) is
  always determined by the section # that the first word of the BP was
  fetched from.  This is true even under PXCT!

*/

/* Full-word byte masks, indexed by # bits in right-justified byte */
w10_t wbytemask[64] = {
# define smask(n) (((uint18)1<<(n))-1)
# define WBMLO(n) W10XWD(0,smask(n))
# define WBMHI(n) W10XWD(smask((n)-18),H10MASK)
# define WBMFF(n) W10XWD(H10MASK,H10MASK)

	WBMLO(0),  WBMLO(1),  WBMLO(2),  WBMLO(3),  WBMLO(4),  WBMLO(5),
	WBMLO(6),  WBMLO(7),  WBMLO(8),  WBMLO(9),  WBMLO(10), WBMLO(11),
	WBMLO(12), WBMLO(13), WBMLO(14), WBMLO(15), WBMLO(16), WBMLO(17),

	WBMHI(18), WBMHI(19), WBMHI(20), WBMHI(21), WBMHI(22), WBMHI(23),
	WBMHI(24), WBMHI(25), WBMHI(26), WBMHI(27), WBMHI(28), WBMHI(29),
	WBMHI(30), WBMHI(31), WBMHI(32), WBMHI(33), WBMHI(34), WBMHI(35),

	WBMFF(36), WBMFF(37), WBMFF(38), WBMFF(39), WBMFF(40), WBMFF(41),
	WBMFF(42), WBMFF(43), WBMFF(44), WBMFF(45), WBMFF(46), WBMFF(47),
	WBMFF(48), WBMFF(49), WBMFF(50), WBMFF(51), WBMFF(52), WBMFF(53),
	WBMFF(54), WBMFF(55), WBMFF(56), WBMFF(57), WBMFF(58), WBMFF(59),
	WBMFF(60), WBMFF(61), WBMFF(62), WBMFF(63)
};

#if KLH10_EXTADR

#define BPF_2WD 040	/* LH flag that indicates 2-wd BP if in NZ sect */

/* Table of positions and sizes for OWGBPs
**	Ordered by P&S value from 037-077 inclusive.
*/
struct owgbpe {
	int pands;		/* P&S value for this entry */
	int pnext;		/* Next P&S if incremented */
	int p;			/* P for this P&S */
	int s;			/* S for this P&S */
	int bpw;		/* # Bytes per word for this S */
	int btl;		/* # Bytes to left for this P&S */
} owgbptab[] = {
	{37, 38, 36, 6, 6, 0},
	{38, 39, 30, 6, 6, 1},
	{39, 40, 24, 6, 6, 2},
	{40, 41, 18, 6, 6, 3},
	{41, 42, 12, 6, 6, 4},
	{42, 43,  6, 6, 6, 5},
	{43, 38,  0, 6, 6, 6},

	{44, 45, 36, 8, 4, 0},
	{45, 46, 28, 8, 4, 1},
	{46, 47, 20, 8, 4, 2},
	{47, 48, 12, 8, 4, 3},
	{48, 45,  4, 8, 4, 4},

	{49, 50, 36, 7, 5, 0},
	{50, 51, 29, 7, 5, 1},
	{51, 52, 22, 7, 5, 2},
	{52, 53, 15, 7, 5, 3},
	{53, 54,  8, 7, 5, 4},
	{54, 50,  1, 7, 5, 5},

	{55, 56, 36, 9, 4, 0},
	{56, 57, 27, 9, 4, 1},
	{57, 58, 18, 9, 4, 2},
	{58, 59,  9, 9, 4, 3},
	{59, 56,  0, 9, 4, 4},

	{60, 61, 36, 18, 2, 0},
	{61, 62, 18, 18, 2, 1},
	{62, 61,  0, 18, 2, 2}
};
#endif /* KLH10_EXTADR */

#ifndef KLH10_USE_CANBP
# define KLH10_USE_CANBP KLH10_EXTADR
#endif
#if KLH10_EXTADR && !KLH10_USE_CANBP
# error "Cannot turn off KLH10_USE_CANBP with extended addressing!"
#endif

#if KLH10_USE_CANBP

/* Struct used to canonicalize byte pointers */
struct canbp {
	int p;		/* P - current position (bits from low end) */
	int s;		/* S - size (# bits in byte) */
	vaddr_t y;	/* Y - current word address */
	w10_t bmsk;	/* Byte mask of S bits */
#if 0
	w10_t w;	/* Original BP (1st word) */
	int fmt;	/* BP format */
# define BPF_L1 0	/* 1-word local */
# define BPF_G1 1	/* 1-word global */
# define BPF_2  2	/* 2-word local/global */
			/* otherwise high 6 P&S bits */
#endif
};


/* CBPGET - Set up canonical BP structure.
**    BP - pointer to BP struct to use.
**    E  - EA of byte pointer word.
**    INC - 3-way action branch flag:
**		>0 - Increment, set FPD, and compute EA that BP points to,
**		=0 - No increment, just set up BP including EA computation.
**		<0 - Increment only, no FPD setting or EA computation.
**
** Returns TRUE if struct set up, otherwise caller should invoke i_muuo().
**
** Is there any way to make more of this routine be inline??
*/
static int
cbpget(register struct canbp *bp, register vaddr_t e, int inc)
{
    register w10_t w;
    register vmptr_t vp;
    register int p, s;
#if KLH10_EXTADR
    register vmptr_t vp2;
#endif

    /* Find loc of BP using appropriate access */
    vp = (inc ? vm_modmap(e) : vm_xrwmap(e, VMF_READ));	/* May pagefail */
    w = vm_pget(vp);			/* Get the byte pointer */

    p = LHGET(w) >> 12;			/* P in high 6 bits */

#if KLH10_EXTADR			/* Permit OWGBPs in any section */
    if (p > 36) {
	register struct owgbpe *tp;
	if (p >= 63)
	    return FALSE;	/* return i_muuo(op, ac, e); */
	tp = &owgbptab[p - 37];
	if (inc) {			/* Incrementing BP before use? */
	    if (tp->pands > tp->pnext)	/* If OWGBP going to next word, */
		op10m_inc(w);		/* bump the address */
	    tp = &owgbptab[tp->pnext - 37];	/* Find next P&S stuff */
	    op10m_tlz(w, 0770000);	/* Clear high 6 bits for new P&S */
	    op10m_tlo(w, (h10_t)(tp->pands) << 12);
	    vm_pset(vp, w);		/* Store new BP back */
	    if (inc > 0) {
		PCFSET(PCF_FPD);	/* Say first-part-done */
		va_gfrword(bp->y, w);	/* Get full address */
	    }
	} else
	    va_gfrword(bp->y, w);	/* Get full address */


	/* Later could perhaps make owgbpe same as canbp and return ptr? */
	bp->p = tp->p;
	bp->s = tp->s;
	return TRUE;
    }

    /* Not a OWGBP, see if it's a TWGBP.
    ** See if OK to check for one-word or not, by testing section part of
    ** the EA we used to fetch the BP.  This test is valid even under
    ** PXCT!
    */
    s = (LHGET(w) >> 6) & 077;		/* S in next 6 */
    if (va_isext(e)			/* See if section non-zero */
      && op10m_tlnn(w, BPF_2WD)) {	/* Yep, see if BP is 2-word kludge. */
	vaddr_t e0 = e;			/* Save E before increment */

	/* Handle two-word BP, yuck ptooey */
	va_inc(e);			/* Bump up to E+1 to get next word */
	if (inc) {
	    register h10_t hi6;

	    vp2 = vm_modmap(e);		/* Get E+1, may pagefail */
	    w = vm_pget(vp2);
	    if ((p -= s) < 0) {		/* Find new P */
		p = (W10BITS - s) & 077; /* Moving to next word */
		/* Tricky part -- bump Y of 2nd word appropriately */
		if (op10m_skipl(w)) {	/* See if IFIW local word */
		    RHSET(w, (RHGET(w)+1)&H10MASK);	/* Yup */
		} else {		/* Ugh, EFIW global */
		    hi6 = LHGET(w) & 0770000;
		    op10m_inc(w);	/* Add 1 to word */
		    op10m_tlz(w, 0770000);
		    op10m_tlo(w, hi6);	/* Put back in word */
		}
		vm_pset(vp2, w);	/* Now store back 2nd word */
	    }
	    /* Now store back new P */
	    hi6 = (vm_pgetlh(vp) & ~0770000) | ((h10_t)p << 12);
	    vm_psetlh(vp, hi6);
	    if (inc > 0)
		PCFSET(PCF_FPD);
	} else		/* inc == 0, No BP increment, just fetch 2nd word */
	    w = vm_read(e);

	/* Interpret word as either an IFIW or EFIW.
	** Note this EA-calc is subject to hairy PXCT stuff!
	** See comments for OWLBP case for more detail.
	*/
	if (inc >= 0) {
	    if (op10m_skipl(w)) {	/* IFIW? */
		if (op10m_tlnn(w, IW_EI)) { /* Bits 0,1 == 11? */
		    /* Generate page fail trap for bits 0,1 == 11 */
		    /* This is failure code 24 (PRM 3-41) */
		    pag_iifail(e, cpu.vmap.xrw); /* Unsure what args to give */
		}
		/* Local-format, need to decide what default sect is */
		if (cpu.mr_inpxct & 02)
		    va_xeabpcalc(bp->y, w, pag_pcsget());	/* Use PCS */
		else
		    va_xeabpcalc(bp->y, w, va_sect(e0));	/* Use E0's */
	    } else			/* EFIW, no default section needed */
		va_xeaefcalc(bp->y, w, cpu.acblk.xbea, cpu.vmap.xbea);

	    if (cpu.mr_inpxct		/* If in PXCT */
	      && (cpu.mr_inpxct & 01)	/* and byte data is prev ctxt */
	      && !(cpu.mr_inpxct & 02)	/* and byte E isn't */
	      && va_islocal(bp->y))	/* and E is local */
		va_lmake(bp->y,		/* then must use PCS for section #! */
			pag_pcsget(), va_insect(bp->y));
	}
	bp->p = p;
	bp->s = s;
	return TRUE;
    }
#else	/* end EXTADR */
    s = (LHGET(w) >> 6) & 077;		/* S in next 6 */
#endif	/* !EXTADR */

    /* Handle one-word local BP */
    if (inc) {
	if ((p -= s) < 0) {	/* Find new P */
	    p = (W10BITS - s) & 077;		/* Moving to next word */
	    RHSET(w, (RHGET(w)+1)&H10MASK);	/* Assume local-format */
	}
	/* Now store back new P */
	op10m_tlz(w, 0770000);
	op10m_tlo(w, ((h10_t)(p) << 12));
	vm_pset(vp, w);			/* Store updated BP */
	if (inc > 0)
	    PCFSET(PCF_FPD);
    }

    /* Find effective address of BP.
    ** The initial default section is always that from which the 1st BP
    ** word was fetched.
    **	*HOWEVER*, if a PXCT is in progress with bit 11 set (E2), this is
    ** replaced with the PCS (Prev Ctxt Sect) before proceeding with the
    ** EA calculation.
    **	A PXCT with bit 12 set (D2) can result in post-processing
    ** of the resulting EA, but this only happens if bit 11 wasn't set.
    */
    if (inc >= 0) {
#if KLH10_EXTADR
	if (cpu.mr_inpxct) {		/* If in PXCT, ugly hackery. */
	    if (cpu.mr_inpxct & 02)	/* Computing EA in prev ctxt? */
		va_xeabpcalc(bp->y, w, pag_pcsget());	/* Yes, use PCS+XBEA */
	    else {
		va_xeabpcalc(bp->y, w, va_sect(e));	/* No, normal (XBEA) */
		if (va_islocal(bp->y)		/* If result is local */
		  && (cpu.mr_inpxct & 01))	/* and byte E is prev ctxt */
		    va_lmake(bp->y,		/* then must use PCS! */
			pag_pcsget(), va_insect(bp->y));
	    }
	} else
	    va_xeabpcalc(bp->y, w, va_sect(e));	/* Set Y, use XBEA mapping */
#else
	bp->y = ea_bpcalc(w);
#endif
    }
    bp->p = p;
    bp->s = s;
    return TRUE;
}
#endif /* KLH10_USE_CANBP */

insdef(i_adjbp);	/* Forward decl for adjbp */

insdef(i_ibp)
{
    if (ac)
	return i_adjbp(op, ac, e);
  {
#if KLH10_USE_CANBP
    struct canbp cbp;

    if (!cbpget(&cbp, e, -1))	/* Set up BP info, increment only */
	return i_muuo(op, ac, e);
#else

    register vmptr_t vp = vm_modmap(e);	/* Get pointer to BP, normal mapping */
    register w10_t bp = vm_pget(vp);	/* Fetch it */
    register int p = LHGET(bp) >> 12;		/* P in high 6 bits */
    register int s = (LHGET(bp) >> 6) & 077;	/* S in next 6 */

#define IBPMACRO(vp,bp,p,s) \
  {			\
    if ((p -= s) < 0) {		/* Decrement P by S bits */\
	RHSET(bp, (RHGET(bp)+1)&H10MASK);	/* Add 1 to Y */\
	p = (36 - s) & 077; \
    } \
    LHSET(bp, (LHGET(bp) & 07777) | ((uint18)p << 12));	/* Put P back in */\
    vm_pset(vp, bp);		/* Store the BP back in memory */\
  }
    IBPMACRO(vp, bp, p, s)	/* Increment BP and store back */

#endif /* !KLH10_USE_CANBP */
  }
    return PCINC_1;
}

insdef(i_ldb)
{
# define LDBMACRO(p, s, y) \
  {			\
    register w10_t w;	\
    w = vm_pget(vm_xbrwmap(y, VMF_READ));	/* Fetch word (byte map) */\
    op10m_rshift(w, p);		/* Shift word to right-align byte */\
    op10m_and(w, wbytemask[s]);	/* Mask out byte */\
    ac_set(ac, w);		/* Store byte in AC */\
  }

#if KLH10_USE_CANBP
    struct canbp cbp;

    if (!cbpget(&cbp, e, 0))	/* Set up BP info, no increment */
	return i_muuo(op, ac, e);

    LDBMACRO(cbp.p, cbp.s, cbp.y)	/* Get byte into AC! */

#else
    register w10_t bp = vm_read(e);	/* Get BP, (normal map) */
    register vaddr_t y = ea_bpcalc(bp);	/* Find its E, (special map) */
    register int p = LHGET(bp) >> 12;		/* Get P field */
    register int s = (LHGET(bp) >> 6) & 077;	/* And S field */

    LDBMACRO(p, s, y)		/* Get byte into AC! */
#endif

    return PCINC_1;
}

insdef(i_ildb)
{
#if KLH10_USE_CANBP
    struct canbp cbp;

    /* Set up BP info, increment pointer unless FPD flag is set */
    if (!cbpget(&cbp, e, PCFTEST(PCF_FPD) ? 0 : 1))
	return i_muuo(op, ac, e);
    LDBMACRO(cbp.p, cbp.s, cbp.y)	/* Invoke shared LDB code */

#else

    register vmptr_t vp = vm_modmap(e);	/* Get pointer to BP, normal mapping */
    register w10_t bp = vm_pget(vp);	/* Fetch it */
    register int p = LHGET(bp) >> 12;		/* P in high 6 bits */
    register int s = (LHGET(bp) >> 6) & 077;	/* S in next 6 */

    if (!PCFTEST(PCF_FPD)) {	/* Unless already did it, */
	IBPMACRO(vp, bp, p, s)	/* Increment pointer */
    }
    PCFSET(PCF_FPD);		/* Say "First Part Done" in case pagefault */
    {
      register vaddr_t y = ea_bpcalc(bp);	/* Find BP's E (special map) */

      LDBMACRO(p, s, y)		/* Now do the LDB into AC */
    }
#endif

    PCFCLEAR(PCF_FPD);		/* Won, clear flag */
    return PCINC_1;
}


insdef(i_dpb)
{
#define DPBMACRO(p, s, y) \
  {		\
    register w10_t w;		\
    register w10_t bmask, byte;	\
    register vmptr_t bvp;	\
				\
    bvp = vm_xbrwmap(y, VMF_WRITE);	/* Use special byte data map */\
    byte = ac_get(ac);		/* Fetch source byte */\
    bmask = wbytemask[s];	/* Get & shift byte-sized mask */\
    op10m_lshift(bmask, p);	\
    op10m_lshift(byte, p);	/* Left-shift to position in word */\
    op10m_and(byte, bmask);	/* Mask off the source byte */\
    w = vm_pget(bvp);		/* Fetch dest word */\
    op10m_andcm(w, bmask);	/* And clear dest byte in word */\
    op10m_ior(w, byte);		/* Now can IOR byte into word */\
    vm_pset(bvp, w);		/* Store back in memory */\
  }

#if KLH10_USE_CANBP
    struct canbp cbp;

    if (!cbpget(&cbp, e, 0))	/* Set up BP info, no increment */
	return i_muuo(op, ac, e);

    DPBMACRO(cbp.p, cbp.s, cbp.y)	/* Do the DPB from AC */

#else

    register w10_t bp = vm_read(e);	/* Get byte pointer, normal map */
    register vaddr_t y = ea_bpcalc(bp); /* Calculate E that BP points to */
    register int p = LHGET(bp) >> 12;		/* Get P */
    register int s = (LHGET(bp) >> 6) & 077;	/* Get S */

    DPBMACRO(p, s, y)		/* Do the DPB from AC */

#endif
    return PCINC_1;
}

insdef(i_idpb)
{
#if KLH10_USE_CANBP
    struct canbp cbp;

    /* Set up BP info, increment pointer unless FPD flag is set */
    if (!cbpget(&cbp, e, PCFTEST(PCF_FPD) ? 0 : 1))
	return i_muuo(op, ac, e);
    DPBMACRO(cbp.p, cbp.s, cbp.y)	/* Invoke code shared with DPB */
#else

    register vmptr_t vp = vm_modmap(e);	/* Get pointer to BP, normal mapping */
    register w10_t bp = vm_pget(vp);	/* Fetch it */
    register int p = LHGET(bp) >> 12;		/* P in high 6 bits */
    register int s = (LHGET(bp) >> 6) & 077;	/* S in next 6 */

    if (!PCFTEST(PCF_FPD)) {	/* Unless already did it, */
	IBPMACRO(vp, bp, p, s)	/* Increment pointer */
    }
    PCFSET(PCF_FPD);		/* Say "First Part Done" in case pagefault */
    {
      register vaddr_t y = ea_bpcalc(bp);	/* Find BP's E (special map) */

      DPBMACRO(p, s, y)		/* Do the DPB from AC */
    }
#endif
    PCFCLEAR(PCF_FPD);		/* Won, clear flag */
    return PCINC_1;
}

/* IBP/ADJBP emulation.
** Following description taken from DEC hardware manual.  Integer
** divisions, of course.
** Let A = rem((36-P)/S)
** If S > 36-A	set no divide & exit
** If S = 0	set (E) -> (AC)
** If 0 < S <= 36-A:		NOTE: Dumb DEC doc claims < instead of <= !!!
**	L = (36-P)/S = # bytes to left of P
**	B = L + P/S  = # bytes to left + # bytes to right = # bytes/word
**	Find Q and R, Q*B + R = (AC) + L
**		where 1 <= R <= B	; that is, not neg or zero!
**	Then:
**		Y + Q	-> new Y	; must wraparound correctly.
**	36 - R*S - A	-> new P
**	Put new BP in AC.  Only P and Y fields changed, not S, I, X.
*/
/* NOTE:
**	There is a fair amount of code here to handle the special case
** where the count has a magnitude greater than 31 bits (which is the
** most we are portably guaranteed to have in native mode).
**	It will virtually never be executed, but is necessary.  It uses
** a few tricks to avoid making full-blown exceedingly slow calls to
** the idiv emulation.
**	Later a check should be made for the existence of a +36 bit type and
** that native type used.
*/

/* ADJBP is a pseudo-instruction variant of IBP, called if AC != 0 */

insdef(i_adjbp)
{
    register w10_t bp;
    register w10_t cw;		/* Count word */
    register int32 y, q;
    register int p, s, b, r;	/* 16-bit prec OK for these vars */
    int sign;

    bp = vm_read(e);		/* Fetch BP, normal mapping */
    p = LHGET(bp) >> 12;	/* P in high 6 bits */
#if KLH10_EXTADR
    if (p > 36) {
	register struct owgbpe *tp;	/* Handle OWGBP */

	if (p >= 63)
	    return i_muuo(op, ac, e);
	tp = &owgbptab[p - 37];
	b = tp->bpw;			/* Find # bytes per word */

	cw = ac_get(ac);		/* Get +/- # bytes from c(AC) */
	op10m_addi(cw, tp->btl);	/* Add positive # bytes on left */
	if (op10m_skipl(cw)) {
	    sign = -1;
	    op10m_movn(cw);
	} else sign = 0;
	if (op10m_tlnn(cw, 0760000)) {	/* If any high 5 bits are set */

	    /* Ugh, do full-blown word operations */
	    switch (b) {
		case 6:		/* Ugh. Size 6 is 6 bytes per word. */
		case 5:		/* Ugh. Size 7 is 5 bytes per word. */
		    /* Break up division into halfwords */
		    y = LHGET(cw) / b;
		    r = LHGET(cw) % b;
		    q = (((int32)r << 18) | RHGET(cw)) / b;
		    r = (((int32)r << 18) | RHGET(cw)) % b;
		    if (q & ~H10MASK)		/* Just call me paranoid */
			panic("i_adjbp: too-high quotient bits?! (%lo)\r\n",
				(long)q);

		    LRHSET(cw, y, q);
		    break;

		case 4:		/* Sizes 8 and 9 are easy; 4 bytes per word */
		    r = RHGET(cw) & 03;		/* Save remainder */
		    op10m_rshift(cw, 2);	/* Divide by 4 */
		    break;
		case 2:		/* Size 18 is easy too - also power of 2 */
		    r = RHGET(cw) & 01;		/* Save remainder */
		    op10m_rshift(cw, 1);	/* Divide by 2 */
		    break;
	    }
	    if (sign)
		op10m_movn(cw);	/* Negate Q for proper sign */
	    if (sign || !r) {	/* If R <= 0 (always true if count neg) */
		r = b - r;	/* then adjust to 0 < R, which means --Q too */
		op10m_dec(cw);
	    }
	} else {

	    /* Whew, can use native mode! */
	    y = ((int32)LHGET(cw) << 18) | RHGET(cw);
	    q = y / b;		/* Find # words to adjust */
	    r = y % b;		/* And # bytes left over */
	    if (sign)
		q = -q;		/* Negate Q for proper sign */
	    if (sign || !r) {	/* If R <= 0 (always true if count neg) */
		r = b - r;	/* then adjust to 0 < R, which means --Q too */
		--q;
	    }
	    LRHSET(cw, (q>>18)&H10MASK, (q & H10MASK));
	}
	op10m_add(bp, cw);		/* Add Q into BP global addr */
	op10m_tlz(bp, 0770000);		/* Clear bits for new P&S */

	/* Note cleverness here that derives offset from current TP by
	** subtracting "btl" to get to top of grouping, then adding "r" to
	** find correct P&S entry for the remainder.
	*/
	op10m_tlo(bp,			/* Insert new P&S value */
		((h10_t)tp[r - tp->btl].pands) << 12);
	ac_set(ac, bp);			/* Return new OWGBP in AC */
	return PCINC_1;
    }
#endif	/* KLH10_EXTADR */

    /* Not a OWGBP, see if 2-word global or 1-word local */
    s = (LHGET(bp) >> 6) & 077;	/* Get and test S */
    if (s == 0) {		/* If S == 0 just set c(E) -> c(AC) */
#if KLH10_EXTADR
	/* Special check to see if E+1 must be copied too */	
	if (va_isext(e)			/* See if section non-zero */
	  && op10m_tlnn(bp, BPF_2WD)) {	/* Yep, see if BP is two-word kludge */
	    va_inc(e);			/* Bump up to E+1 to get next word */
	    ac_set(ac_off(ac,1), vm_read(e));	/* Get it, may page-fail */
	}
#endif
    } else {
	register int l, a;	/* 16-bit prec OK for these vars */

	/* Now derive A = rem((36-P)/S) and test it */
	l = (36 - p) / s;	/* Find # bytes to left of P */
	a = (36 - p) % s;	/* Find rem (unbyted bits to left of P) */
	if (s > (36-a)) {	/* Check alignment */
	    PCFTRAPSET(PCF_TR1|PCF_ARO|PCF_DIV);	/* Ugh, barf. */
	    return PCINC_1;		/* Don't modify AC */
	}
	b = l + (p/s);		/* # bytes to left + # bytes to right */
				/* gives us # bytes per word */

	cw = ac_get(ac);	/* Get +/- # bytes from c(AC) */
	op10m_addi(cw, l);	/* Add positive # bytes on left */
	if (op10m_skipl(cw)) {
	    sign = -1;
	    op10m_movn(cw);
	} else sign = 0;
	if (op10m_tlnn(cw, 0760000)) {	/* If any high 5 bits are set */

	    /* Ugh, do full-blown word operations */
	    /* Break up division into halfwords */
	    y = LHGET(cw) / b;
	    r = LHGET(cw) % b;
	    q = (((int32)r << 18) | RHGET(cw)) / b;
	    r = (((int32)r << 18) | RHGET(cw)) % b;
	    if (q & ~H10MASK)		/* Just call me paranoid */
		panic("i_adjbp: too-high quotient bits?! (%lo)\r\n",
			(long)q);
#if KLH10_EXTADR	/* Only keep LH if might need for 2-wd BPs */
	    LRHSET(cw, y, q);
	    if (sign)
		op10m_movn(cw);	/* Negate Q for proper sign */
	    if (sign || !r) {	/* If R <= 0 (always true if count neg) */
		r = b - r;	/* then adjust to 0 < R, which means --Q too */
		op10m_dec(cw);
	    }
	    q = RHGET(cw);
#else
	    q = (y << 18) | q;	/* Make +30-bit native integer */
	    if (sign)
		q = -q;		/* Negate Q for proper sign */
	    if (sign || !r) {	/* If R <= 0 (always true if count neg) */
		r = b - r;	/* then adjust to 0 < R, which means --Q too */
		--q;
	    }
#endif
	} else {
	    /* Whew, can use native mode! */
	    y = ((int32)LHGET(cw) << 18) | RHGET(cw);
	    q = y / b;		/* Find # words to adjust */
	    r = y % b;		/* And # bytes left over */
	    if (sign)
		q = -q;		/* Negate Q for proper sign */
	    if (sign || !r) {	/* If R <= 0 (always true if count neg) */
		r = b - r;	/* then adjust to 0 < R, which means --Q too */
		--q;
	    }
#if KLH10_EXTADR	/* Only keep LH if might need for 2-wd BPs */
	    LRHSET(cw, (q>>18)&H10MASK, (q & H10MASK));
#endif
	}

	/* Q now contains at least 18 bits of the word count adjustment.
	** CW contains the entire 36 bits, if necessary.
	*/
	p = (36 - r*s) - a;	/* Have R, can now find new P */

#if KLH10_EXTADR
	if (va_isext(e)			/* See if section non-zero */
	  && op10m_tlnn(bp, BPF_2WD)) {	/* Yep, see if BP is two-word kludge */
	    register w10_t bp2;

	    /* Handle two-word BP, yuck ptooey */
	    va_inc(e);		/* Bump up to E+1 to get next word */
	    bp2 = vm_read(e);	/* Get it, may page-fail (sigh!) */

	    /* Tricky part -- bump Y of 2nd word appropriately */
	    if (op10m_skipl(bp2)) {	/* See if IFIW local word */
		RHSET(bp2, (RHGET(bp2) + q)&H10MASK);	/* Yup */
	    } else {			/* Ugh, EFIW global word */
		op10m_add(cw, bp2);	/* Add words together (RH can carry) */
		LRHSET(bp2,		/* Save high 6, use low 30 bits */
			(LHGET(bp2) & 0770000) | (LHGET(cw) & 07777),
			RHGET(cw));
	    }
	    ac_set(ac_off(ac,1), bp2);
	} else
#endif
	    RHSET(bp, (RHGET(bp) + q)&H10MASK);	/* New Y = Y + Q */
	LHSET(bp, (LHGET(bp) & 07777) | ((h10_t)p << 12));	/* New P */
    }

    /* Now store 1st (maybe only) word of BP to wrap up */
    ac_set(ac, bp);
    return PCINC_1;
}
