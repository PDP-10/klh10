/* INEXTS.C - Extended (String) Instruction routines
*/
/* $Id: inexts.c,v 2.4 2002/05/21 10:06:55 klh Exp $
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
 * $Log: inexts.c,v $
 * Revision 2.4  2002/05/21 10:06:55  klh
 * Fixed XBLT to behave like real KL with respect to high 6 bits
 * of src/dst addresses.  New algorithm also fixes a pagefail bug.
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/* See CODING.TXT for guidelines to coding instruction routines. */

#include <stddef.h>
#include "klh10.h"
#include "kn10def.h"	/* Machine defs */
#include "kn10ops.h"	/* PDP-10 ops */

#ifdef RCSID
 RCSID(inexts_c,"$Id: inexts.c,v 2.4 2002/05/21 10:06:55 klh Exp $")
#endif

/* Exported functions (other than instrs) */

void inexts_init(void);		/* Initialize any necessary EXTEND stuff */

/* Local predeclarations */

#if KLH10_EXTADR
static int xea_fiwcalc(w10_t iw, unsigned int sect,
		       acptr_t acp, pment_t *map, vaddr_t *va, int f);
#endif

/* Notes:
**
** + The EXTEND string instructions are documented to be undefined when
**	used with PXCT (except for MOVSLJ), but appropriate mapping is
**	done anyway since it costs nothing and lets MOVSLJ work.  This may
**	change with extended addressing.
** + The G-Format extended instructions are handled in inflt.c.
*/

/* NOTES:
	The EXTEND instructions, as described by the PRM, contain a lot
of ambiguities as to exactly how they behave in response to unusual
conditions, especially with respect to the resulting AC contents.  Here
is a list of unclear points, with the assumptions made by KLH10 code.

* Are the AC bits at c(E0) checked?  PRM only says (p.1-25) "must be zero".
	[ANSWER: KS: AC bits MUST == 0 or it traps as a MUUO.
		 KL: AC bits ignored.
	]
* Is E1 always computed whether or not it is used?
	[Assume YES, based on PRM 1-25 top paragraph:
	"As with all instructons, before executing the second word the
	processor calculates an effective address for it; this is referred
	to as E1, ..."
	ANSWER: YES, if:
	    KS: opcode is 0-17 incl and AC=0 (else MUUO taken before E1 calc)
	    KL: opcode is 0-31 incl (else MUUO taken)
			 and not 20 (else E1 skipped)
	]
* Are fill bytes from c(E0+1), c(E0+2) always pre-fetched (liable to page
  fault before anything done), or only referenced when and if needed?
	[ANSWER:
	    KS/KL: pre-fetched for all MOVSx instructions.
	    KS/KL: pre-fetched for all CMPSx, but KS/KL differ if lengths equal
			(see comments at ix_cmps).
	    KS/KL: CVTBDx only fetches fill if needed.
	    KS: pre-fetched for EDIT (but float char only fetched as needed).
	    KL: both fill and float only fetched as needed.
	]
* What happens if the zero-specified high bits in string counts are
  non-zero?
	[ANSWER: traps as MUUO]

* Are the high 9 bits of string lengths used as flags for intermediate states,
  such as if a page fault interrupts between IBP and LDB?
	[ANSWER: No -- kept clear.]

* Are OWG byte pointers left alone if a page fault happens on the first ref,
  or have they already been converted (so that initial P is lost)?
	[ANSWER: already converted]

* Are byte pointers converted so that I and X are zero?  If not, are the
  X and I references made with every use of the BP?  What happens if
  I is set?
	[ANSWER: NO!  Apparently EA is recomputed on each BP ref.
	I and X are retained just as for normal ILDB/IDPB.
	Implementation here simulates this without actually doing the
	recomputation unless indirection makes it necessary.
	Of course, all that work is probably pointless because
	any user program that sets I won't work in general,
	because a new word is indirected through as soon as Y
	is incremented!]

* What happens if a 2-word BP has its 2nd word in IFIW format instead
  of EFIW -- is it converted to EFIW or left as IFIW?  If IFIW, are resulting
  pointer EAs re-computed with every reference to check for insection overflow?
  Argh!
	[ANSWER: no conversion is done; EFIW/IFIW distinction is maintained
	and addresses are incremented appropriately for the format, i.e.
	IFIW insection overflow just wraps.
	Furthermore, having both bit 0,1 set in 2nd word causes an illegal
	indirection page fault.]

* NOTE!  From empirical testing, an II page fault prior to any string use of BP
	results in BP having been incremented, but P is backed up so that
	a re-invocation will still refer to the same byte.
	e.g. initial BP: 004440,,0 ? 600001,,70
	      result BP: 444440,,0 ? 600001,,71

* Since OWGBPs are allowed in section 0 by the byte instructions, do the
  EXTEND string instructions convert them?  But if instr is restarted, TWGBPs
  will fail because they can't be used in section 0!  What's the plan??
	[ANSWER: OWGBPs not allowed in sect 0 by EXTEND instrs; they're
	interpreted as OWLBPs.]

* When a OWGBP is converted to a TWGBP, is the 2nd word an EFIW or IFIW?
	[ANSWER: EFIW with I+X= 0]

* What happens if BPs are not aligned?  (see also MOVSRJ question)
	[ANSWER: behave just as if using ILDB/IDPBs -- they stay unaligned
	until a word boundary is crossed, whereupon they are aligned.
	This applies to a MOVSRJ skip as well.]

* Do translation tables cross section boundaries?
	[Assume E1+offset follows same rules as normal E+offset, i.e.
	depends on whether E1 is local or global.
	WRONG!  On real KL, always crosses section boundaries, EVEN IF
	RUNNING IN ZERO SECTION, regardless of locality of E1.]

* Is there any limit on the size of a translation table?
	[ANSWER: No -- simply adds byte/2 to E1.  Can jump entire
	sections!]

* Does extended address computation for E1 depend on location of word at E0,
  or on PC?  What about computation of addresses for translation table entries?
	[Assume depends on location of word addressed by E0.
	Also assume E1+offset follows same rules as normal E+offset.]

--------------------
* MOVSO: does the offset apply to the fill byte?  Is the fill byte tested?
	[ANSWER: No to both.]
* MOVSO: is the oversize byte test applied with source or dest byte size?
	[ANSWER: Dest byte size]
* MOVSO: what is state of ACs if instr stops due to oversize byte?  Is source
  BP & count pointing to guilty byte or backed up?
	[Assume backed up, so a re-try will fail identically]

--------------------
* MOVST: If a translation function terminates, is the rest of the dest string
  filled or not?
	[ANSWER: No - not filled.]

--------------------
* MOVSRJ: Does this really always skip, even if some source bytes are left?
	[ANSWER: Yes - by definition no source bytes are ever left!]
* MOVSRJ: If source bytes are skipped, are any memory refs made (ie possible
  to page-fail) or is the source pointer merely bumped?
	[ANSWER: No mem refs - src pointer is simply bumped.  !!! BUT !!!
	On the KL, the src skip is always made as if the 1st wd was a
	OWLBP, regardless of actual format, thus this skip loses if BP
	is really a OWG, TWG, or TWL!!!  Ucode bug!]
* MOVSRJ: If source bytes skipped, is the BP bumped as if ILDB were done, or
  as if ADJBP (which preserves possible non-alignment)?
	[ANSWER: Bumped as if IBP, thus non-alignment is only preserved until
	 the first word increment.]

--------------------
* CMPSx: Are the bytes signed for comparison purposes?
	[Assuming unsigned]
* CMPSx: What about full-word 36-bit bytes?  (CAM uses signed compare)
	[Assuming unsigned for consistency]
* CMPSx: What happens to the length and pointer of a string that counts out
  and has its fill byte used?
	[Assuming length remains 0 and pointer is left pointing to last byte]
* CMPSx: Are the high bits of the fill-byte words used, or are they masked
  out before the comparison?
	[NO!  Formerly assumed masked out, but empirical testing shows this
	is not the case.  Fill word is used as a whole.]

--------------------
* CVTBDx: Must the N & M bits be clear initially?  Is their value ignored?
	[Assuming value ignored, since may be restarting]
* CVTBDx: Are the N and M bits always set to either 0 or 1, or are new
  settings just IOR'd in?
	[Assuming IOR'd, since may be restarting instruction]
* CVTBDx: Is the low-order sign bit ignored in the setting of N?
	[Assuming yes]
* CVTBDx: What happens for max negative integer (which cannot be readily
  negated into positive form)?
	[Assuming code does right thing & generates correct value]
* CVTBDx: Does a page-fault or interrupt really update all ACs by
  storing back new binary # and new pointer/length, or does it
  always either run to completion or restore original ACs?
	[Assuming former, updates all ACs with partial results]
* CVTBDx: Does CVTBDO check result byte for being oversize, like MOVSO?
	[Assuming not]

--------------------
* CVTBDT: What happens if translated digit has a 4-bit value greater
  than 9?  Is check made after translation or are the bits just masked?
	[Assume bits just masked, don't test for  9 < x <= 017]
* CVTBDT: What are source len and BP values if string is terminated, or
  aborted because of a bad digit?
	[Assume points to terminating or bad byte, with len indicating
	remainder of string]

--------------------
* CVTDBT: What happens if last source byte forces termination?  Does it
  skip because instruction ate all bytes, or not skip because a
  termination happened?
	[Assume no skip]

--------------------
* EDIT: What happens if an illegal command byte is seen?
	[Assume updates ACs, so a re-xct will immediately fail
	on that byte as well, and then MUUO-fails]
	[WRONG!  Diagnostic simulation simply treats unrecognized
	pattern bytes as NOPs.  Ucode agrees.  Sigh.]
*/


/* See opdefs.h for the definition of xinsdef(),
** which is used to define all extended instruction routines.
*/

insdef(i_extend)
{
    register w10_t xw;
    register unsigned int xop;

    xw = vm_read(e);		/* Get contents of E0 using normal XRW map */
    xop = iw_op(xw);		/* Find extended opcode */

    /* Check extended opcode (and perhaps AC) before doing indexed dispatch.
	Note that KL doesn't check AC field, whereas KS requires that
	it be zero!
     */
#if KLH10_CPU_KLX
    if (xop >= IX_N)
	return i_muuo(op, ac, e);		/* Bad op */

#elif KLH10_CPU_KS
# if (IX_N == 040 || IX_N == 020)	/* Faster check if power of 2 */
    if (LHGET(xw) & (((~(IX_N-1))<<9) | (AC_MASK<<5)))	/* Check op and AC fields */
# else
    if (xop >= IX_N || (LHGET(xw) & (AC_MASK<<5)))	/* Check op, AC field */
# endif
	return i_muuo(op, ac, e);		/* Bad op or AC */
#endif

    /* Calculate E1 using normal XEA map, which may page-fail!
    ** Following the normal instruction model,
    ** E1 would be calculated before looking at anything else (OP or AC).
    ** However...
    ** For both KS and KL this is always done after the initial OP+AC check,
    ** regardless of whether the instruction needs E1 or not, with
    ** one exception: on the KL, XBLT skips the E1 calc entirely!
    ** (this exception doesn't seem worth emulating)
    */
#if KLH10_EXTADR
    /* Default section for 2nd instr word EA calc is the section 
    ** that word was fetched from.
    */
    return (*opcxrtn[xop])(xop, ac, e, xea_calc(xw, va_sect(e)));
#else
    return (*opcxrtn[xop])(xop, ac, e, ea_calc(xw));
#endif
}

xinsdef(ix_undef)
{
    return i_muuo(I_EXTEND, ac, e0);
}


#if KLH10_SYS_T10 || KLH10_SYS_T20	/* DEC systems only */

/* All of the remaining code in this module is ignored for an ITS system,
** since the extended instructions were all tossed from the ITS ucode.
*/

/* AC flags for Binary<->Decimal conversions */

#define CVTF_L H10SIGN		/* User sets to control justification */
#define CVTF_S H10SIGN		/* Another name for same bit */
#define CVTF_N (H10SIGN>>1)	/* Instr sets 1 if non-zero */
#define CVTF_M (H10SIGN>>2)	/* Instr sets 1 if negative */
#define CVTF_ALL (CVTF_L|CVTF_N|CVTF_M)

/* Internal result codes for the various instructions */
/* NOTE: See EA_RES_PF, EA_RES_PI defs; must match RES_PF, RES_PI! */
enum xires {
	RES_OK=0,	/* 0 is general non-error status */
	RES_TRUNC=1,	/* Set 1 for efficient code (usu. returns PCINC_1) */
	RES_WON=2,	/* Ditto (usu. returns PCINC_2) */
	RES_PF,		/* Page Fail */
	RES_PI,		/* PI Interrupt */
	RES_MUUO };	/* MUUO trap */


/* Auxiliary power-of-10 table for CVT instructions */
#define DPOW_MAX 22
static dw10_t dpow10[DPOW_MAX];	/* Later maybe define at compile time */

void
inexts_init(void)		/* Initialize any necessary EXTEND stuff */
{
    register int i;
    register dw10_t d;
    h10_t savflgs;

    op10m_setz(d.w[0]);		/* Set double fix to 1 */
    XWDSET(d.w[1], 0, 1);
    dpow10[0] = d;		/* Store 1 as first table entry */
    XWDSET(d.w[1], 0, 10);	/* Then set it to 10 */

    /* Compute rest of power-of-10 table, ignoring any overflow traps */
    savflgs = cpu.mr_pcflags;
    for (i = 1; i < DPOW_MAX; ++i) {
	qw10_t q;
	q = op10dmul(dpow10[i-1], d);
	dpow10[i] = q.d[1];
    }
    cpu.mr_pcflags = savflgs;
}


/* Other auxiliaries */

/* Macro to fetch string length into a native 32-bit type for efficiency.
**	Does a mask test to see whether length word has any illegal bits set
**	and traps as a MUUO if so.
*/
#define AC_32GET(r, a, off, lmask) \
    {	register w10_t w;	\
	w = ac_get(ac_off(a, off));	\
	if (op10m_tlnn(w, lmask)) \
	    return i_muuo(I_EXTEND, (a), e0); \
	r = ((uint32)LHGET(w) << H10BITS) | RHGET(w); /* Convert to 32-bit value */ \
    }

#if KLH10_EXTADR
# define ILLEG_LHVABITS 0770000	/* Bits illegal in LH of 30-bit virt addr */
#else
# define ILLEG_LHVABITS 0777777
#endif

#if 0
static uint32
ac_32get(ac)
{
    register w10_t w;
    w = ac_get(ac);			/* Get c(AC) */
    return ((uint32)LHGET(w) << 18) | RHGET(w);	/* Convert to 32-bit value */
}
#endif /* 0 */

static void
ac_32set(int ac, uint32 val)
{
    register w10_t w;
    LRHSET(w, (val>>18)&H10MASK, val & H10MASK);	/* Put val into word */
    ac_set(ac, w);					/* Store in AC */
}

#if KLH10_CPU_KLX

/* XBLT
**	NOTE: Per [Uhler83], XBLT is allowed from section 0 on a KLX!
**	On the KS10 there is only section 0, so this always traps as a
**	MUUO on that machine.  Assume single-section KL similar.
**
** This is a straightforward implementation and there are several
** ways to optimize it:
**	- Separate loops for forward and reverse BLTs
**	- Test for 32-bit count and optimize in register
**	- Test for src -> src+1 and avoid re-reading value if mappings same.
**	- Test for within-mem-page BLTs and break into memcpy segments.
**
** Note: for PXCT, source uses XBEA mapping, dest uses XBRW.
**	These correspond to PXCT AC bits 11 and 12 respectively.
**
** Note: The PRM implies the high 6 bits must be zero, and the original
**	code here would cause a MUUO trap if they were set.  However, a
**	real KL appears to ignore these bits (and the ucode agrees); TOPS-20
**	CLISP turns out to depend on this!
**	Thus, this code now preserves the high 6 bits, although there is
**	no attempt to prevent overflow into them; this seems to be how
**	the KL ucode worked.
*/

xinsdef(ix_xblt)
{
    register vaddr_t src, dst;
    register vmptr_t svp, dvp;
    w10_t wcnt, wsrc, wdst, wdone;
    enum xires res = RES_WON;

    wsrc = ac_get(ac_off(ac,1));	/* Get source addr */
    va_gfrword(src, wsrc);		/* Make global addr from wd */
    wdst = ac_get(ac_off(ac,2));	/* Now get destination */
    va_gfrword(dst, wdst);		/* Likewise get global addr */

    wcnt = ac_get(ac);			/* Get possibly 36-bit cnt */
    if (op10m_skipge(wcnt)) {
	register uint32 cnt;
	uint32 origcnt;

	if (op10m_tlnn(wcnt, 0740000)) {
	    /* If any of high 4 bits are set, cannot represent in a 32-bit
	       count -- just use max possible count.  This works because
	       only 30 bits of address are possible, so a 32-bit count
	       would wrap all of virtual memory 4 times!  Will normally
	       get a page fail attempting this.
	    */
	    cnt = MASK32;
	} else {
	    cnt = W10_U32(wcnt);	/* Get 32 bits of count */
	    if (!cnt)			/* If count zero, */
		return PCINC_1;		/* return without any refs */
	}
	origcnt = cnt;

	/* Do normal forward transfer */
	for (;;) {
	    if ( !(svp = vm_xbeamap(src, VMF_READ|VMF_NOTRAP))
	      || !(dvp = vm_xbrwmap(dst, VMF_WRITE|VMF_NOTRAP))) {
		res = RES_PF;
		break;
	    }
	    vm_pset(dvp, vm_pget(svp));	/* Transfer the word */

	    va_ginc(src);		/* Bump addrs up */
	    va_ginc(dst);
	    if (--cnt == 0)		/* Bump count down, see if done */
		break;			/* stop loop! */

	    /* Done with one iteration, now check before doing next */
	    CLOCKPOLL();		/* More left, keep clock going */
	    if (INSBRKTEST()) {		/* Watch for PI interrupt */
		res = RES_PI;
		break;
	    }
	}
	cnt = origcnt - cnt;		/* Find # of words transferred */
	W10_U32SET(wdone, cnt);

    } else {
	/* Negative count means reverse xfer */
	register int32 cnt;		/* Signed! */
	int32 origcnt;

	if ((~W10_LH(wcnt)) & 0760000) {
	    /* If any of high 5 bits are clear, cannot represent in a 32-bit
	       count -- just use max possible count.  Same rationale as for
	       forward transfer.
	    */
	    cnt = -MASK31;
	} else {
	    cnt = W10_S32(wcnt);	/* Get 32 bits of signed count */
	}
	origcnt = cnt;

	/* Do reverse transfer */
	for (;;) {
	    va_gdec(src);		/* Bump addrs down */
	    va_gdec(dst);

	    if ( !(svp = vm_xbeamap(src, VMF_READ|VMF_NOTRAP))
	      || !(dvp = vm_xbrwmap(dst, VMF_WRITE|VMF_NOTRAP))) {
		res = RES_PF;
		break;
	    }
	    vm_pset(dvp, vm_pget(svp));	/* Transfer the word */

	    if (++cnt >= 0)		/* Bump count up; if gone, */
		break;			/* stop loop! */

	    /* Done with one iteration, now check before doing next */
	    CLOCKPOLL();		/* More left, keep clock going */
	    if (INSBRKTEST()) {		/* Watch for PI interrupt */
		res = RES_PI;
		break;
	    }
	}
	cnt = origcnt - cnt;		/* Find -<#> of words transferred */
	W10_S32SET(wdone, cnt);
    }

    /* Now update ACs.  wdone contains the # words transfered (positive if
     * forward, negative if reverse).
     */
    op10m_sub(wcnt, wdone);		/* Update word values */
    op10m_add(wsrc, wdone);
    op10m_add(wdst, wdone);

    ac_set(ac, wcnt);			/* Store back in ACs */
    ac_set(ac_off(ac, 1), wsrc);
    ac_set(ac_off(ac, 2), wdst);

    switch (res) {
    case RES_PF:	pag_fail();	/* Never returns */
    case RES_PI:	apr_int();	/* Never returns */
    default: break;
    }
    return PCINC_1;
}
#endif /* KLH10_CPU_KLX */

/* New EA calculation function needed to support string instructions
   Later move this to kn10pag.h, kn10cpu.c?
*/

enum eaarg { EA_ARG_IFIW=0, EA_ARG_EFIW=1, EA_ARG_UFIW=-1 };

/* NOTE!!!
	Code assumes EA_RES_PF == RES_PF (ditto RES_PI) for simplicity!!!
 */
enum eares { EA_RES_OK=0, EA_RES_PF=RES_PF, EA_RES_PI=RES_PI };

#if KLH10_EXTADR

/* XEA_FIWCALC - Extended Addressing IFIW/EFIW EA calc
**	Returns result code after depositing EA in location provided.
**
**	This is exactly the same algorithm as the standard (but faster)
**	XEA_XCALC function except that instead of aborting on any error
**	(page fail or PI), it returns with an error code.
**	Although slower, this functionality is needed to give the extended
**	string instructions a chance to clean up.
*/
static int
xea_fiwcalc(register w10_t iw,		/* IFIW/EFIW to evaluate */
	    register unsigned int sect,	/* Current section, if IFIW */
	    register acptr_t acp,	/* AC block mapping */
	    register pment_t *map,	/* Page table mapping */
	    register vaddr_t *va,	/* Result EA as virtual address */
	    int f)			/* arg flags */
{
    register vaddr_t e;		/* Address to return */

    if (f) {
	if (f > 0) goto xea_efiw;
	else goto xea_ufiw;
    }

    /* Handle IFIW - Instruction Format Indirect Word */
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
	if (!op10m_tlnn(iw, IW_I)) {		/* Indirection? */
	    *va = e;				/* Nope, done! */
	    return EA_RES_OK;
	}

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
	    register vmptr_t vp;

	    if (!(vp = vm_xmap(e,		/* Map E, may fault */
			VMF_READ|VMF_NOTRAP, acp, map))) {
		return EA_RES_PF;
	    }
	    iw = vm_pget(vp);		/* Get c(E) */

	    /* Note must test E via va_isext rather than using "sect"
	    ** because the new E built by indexing may have a different
	    ** section #.
	    */
	    if (!va_isext(e)) {
		sect = 0;		/* Not NZS, back to outer loop */
		break;
	    }
	    /* Handle indirect word in non-zero section */
  xea_ufiw:
	    if (op10m_skipl(iw)) {		/* Check bit 0 */
		if (!op10m_tlnn(iw, IW_EI)) {	/* Bits 0,1 == 10 or 11? */
		    sect = va_sect(e);
		    break;			/* = 10, back to local loop */
		}
		/* Generate page fail trap for bits 11 */
		/* This is failure code 24 (PRM 3-41) */
		pag_iiset(e, map);	/* E = loc of bad II word */
		return EA_RES_PF;
	    }

	    /* Extended Format Indirect Word */
  xea_efiw:
	    if (op10m_tlnn(iw, IW_EX)) {	/* Indexing? */
		register w10_t xw;
		xw = ac_xget(iw_ex(iw), acp);	/* Get c(X) then add Y */
		va_gmake30(e, (va_30frword(xw)+va_30frword(iw)) & VAF_30MSK);
	    } else {
		va_gmake30(e, va_30frword(iw));	/* No X, just Y */
	    }
	    if (!op10m_tlnn(iw, IW_EI)) {	/* Bits 0,1 == 00 or 01? */
		*va = e;			/* = 00, Done!  Global E */
		return EA_RES_OK;
	    }					/* = 01, get indirect wd */

	    /* Looping with another EFIW. */
	    CLOCKPOLL();
	    if (INSBRKTEST()) return EA_RES_PI;
	}

	/* Returning to IFIW loop */
	if (op10m_tlnn(iw, IW_I)) {	/* If another indirection */
	    CLOCKPOLL();		/* Check to avoid infinite @ */
	    if (INSBRKTEST()) return EA_RES_PI;
	}
    }
}

/* New version of this routine, to share above function */
vaddr_t
xea_fnefcalc(register w10_t iw,		/* EFIW to evaluate */
	     register acptr_t acp,	/* AC block mapping */
	     register pment_t *map)	/* Page table mapping */
{
    vaddr_t e;			/* Address to return */

    switch (xea_fiwcalc(iw, (unsigned)0, acp, map, &e, EA_ARG_EFIW)) {
	case EA_RES_OK: break;
	case EA_RES_PF: pag_fail();
	case EA_RES_PI: apr_int();
    }
    return e;
}

#else

/* EA_FIWCALC - Non-extended version of above
*/
int
ea_fiwcalc(register w10_t iw,		/* IFIW to evaluate */
	   register acptr_t acp,	/* AC block mapping */
	   register pment_t *map,	/* Page table mapping */
	   register vaddr_t *va)	/* Result EA as virtual address */
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

	if (!(tmp & IW_I)) {			/* Indirection? */
	    *va = ea;				/* Nope, return now! */
	    return EA_RES_OK;
	}

	/* Indirection, do it */
	{
	    vmptr_t vp;
	    if (!(vp = vm_xmap(ea, VMF_READ|VMF_NOTRAP, acp, map)))
		return EA_RES_PF;		/* oops, page fault */
	    iw = vm_pget(vp);
	}
	if ((tmp = LHGET(iw)) & IW_I) {		/* If new word also indirect */
	    CLOCKPOLL();
	    if (INSBRKTEST()) return EA_RES_PI;	/* Stop infinite @ loops */
	}
    }
}
#endif /* !KLH10_EXTADR */

/* Byte pointer auxiliaries */

#define BPF_2WD 040	/* LH flag that indicates 2-wd BP if in NZ sect */

extern w10_t wbytemask[37];	/* From INBYTE.C */

/* Note that this structure is the same initially as the "canbp" struct
** used in inbyte.c.  Originally this was to allow using common code,
** but there seem to be too many differences between the byte instructions
** and the EXTEND string instructions.
*/
struct cleanbp {	/* Canonicalized BP */
	int p;		/* P - current position (bits from low end) */
	int s;		/* S - size (# bits in byte) */
	vaddr_t y;	/* Y - current word address */
	w10_t bmsk;	/* Byte mask of S bits */

	int newp;	/* New P when next word is needed */
	vmptr_t vp;	/* Set non-NULL if "wd" below is valid */
	w10_t wd;	/* Current Word being read or written */

	h10_t ycnt;	/* # times Y incremented */
	int ac;		/* First AC # BP comes from */
	int src;	/* TRUE if source, else dest */
	int fmt;	/* BP format */
# define BPF_OWL 0	/* 1-word local */
# define BPF_OWG 1	/* 1-word global */
# define BPF_TWL 2	/* 2-word local  (IFIW 2nd wd) */
# define BPF_TWG 3	/* 2-word global (EFIW 2nd wd) */

	int isindir;	/* TRUE if byte address uses indirection */
	enum xires err;	/* Set to a RES_ value if a xildb/xidpb fails */
};


/* Fetch a BP and set up in canonicalized form.
**	Note that when OWGBPs are supported, they must be
**	converted to 2-word TWGBP form and stored as that thereafter.
**	A flag is used to remember which format to store the BP back in.
**
**	Rule is peculiar but simple:
**		In 0-PC sect, no TWGBPs (or OWGBPs!!) are allowed; they are
**			all interpreted as OWLBPs.
**		In NZ PC sect, OWGBP always becomes TWGBP with EFIW 2nd wd.
**		TWGBPs and OWLBPs stay in original format.  In particular,
**			2nd word of a TWG may be (and remains) IFIW or EFIW,
**			and an illegal-indirection page fault is possible.
**
** Note: For support of PXCT of MOVSLJ, the BP EA calculation needs to know
** whether the BP is for a "source" or "destination".
** If the XEA map is different from the current map (meaning PXCT bit 9 set)
** then either XBEA or XBRW must be used (for source or dest BP respectively).
** Otherwise, just use current map, which itself requires unusual code.
*/

/* Extended PXCT notes:
	If no PXCT is in progress, default section is always PC section.
	Otherwise, for a PXCT things get rather hairy.
NOTE:
	PRM conflicts with Uhler on exact meanings of the PXCT AC bits.
	PRM claims the 10 bit is needed to force EA-calc in previous context
in addition to memory-ref;
	Uhler says either src (2) or dest (1) bit implies *both* EA-calc and
memory-ref for the respective direction.
	Since only known instance of PXCT'd MOVSLJ in T20 uses the 10 bit:
			LATSRV.MAC.4:	PXCT 11,[EXTEND	T1,[MOVSLJ
	either model will work correctly.

I'll go with Uhler for now, because it simplifies implementation and
makes sense.

*/
/* Actually, it now appears that PXCT doesn't work even with MOVSLJ!
** So skip any pretense of PXCT support until things clear up.
*/

#define BPSRC 1
#define BPDST 0

#if KLH10_EXTADR
	/* Crock for now; do correctly with shared include or rtn later */
extern struct owgbpe {
	int pands;		/* P&S value for this entry */
	int pnext;		/* Next P&S if incremented */
	int p;			/* P for this P&S */
	int s;			/* S for this P&S */
	int bpw;		/* # Bytes per word for this S */
	int btl;		/* # Bytes to left for this P&S */
} owgbptab[];
#endif

#if KLH10_EXTADR
# define XBPGET(bp,ac,off,src) if (!xbpget(bp,ac_off(ac,off),src)) \
			return i_muuo(I_EXTEND, (ac), e0)
#else
# define XBPGET(bp,ac,off,src) (void)xbpget(bp,ac_off(ac,off),src)
#endif

static enum xires xbpinit(struct cleanbp *bp, int ac, int src);

static int
xbpget(register struct cleanbp *bp,
       int ac,
       int src)		/* BPSRC if a source BP */
{
    switch (xbpinit(bp, ac, src)) {
	case RES_OK: return TRUE;
	case RES_PF: pag_fail();
	case RES_PI: apr_int();
	case RES_MUUO:
	default:
		return FALSE;
    }
}

static enum xires
xbpinit(register struct cleanbp *bp,
	int ac,
	int src)	/* BPSRC if a source BP */
{
    register w10_t w;
    register int err;

    bp->src = src;		/* Save args */
    bp->ac = ac;
    w = ac_get(ac);		/* Get the byte pointer */
    bp->p = LHGET(w) >> 12;	/* P in high 6 bits */

#if KLH10_EXTADR
    /* For extended addressing there are 3 cases:
    **	OWLBP - One-Word Local BP
    **	TWGBP - Two-Word Global BP (only valid in NZ PC sect)
    **	OWGBP - One-Word Global BP (only valid in NZ PC sect, immediately
    **				converted into a TWGBP)
    **		NOTE: this is contrary to the way byte instructions
    **		like DPB behave!  (They accept OWGBPs in section 0)
    */
    if (PC_ISEXT) {
	if (bp->p > 36) {		/* OWGBP? */
	    register struct owgbpe *tp;
	    if (bp->p >= 63)
		return RES_MUUO;	/* return i_muuo(op, ac, e); */
	    tp = &owgbptab[bp->p - 37];
	    bp->p = tp->p;
	    bp->s = tp->s;
	    va_gfrword(bp->y, w);	/* Get full 30-bit address */
	    bp->fmt = BPF_OWG;		/* Say originally an OWGBP */
	    bp->isindir = FALSE;

	} else if (op10m_tlnn(w, BPF_2WD)) {	/* TWGBP? */
	    /* It's a TWGBP!
	    ** Note the NZ-section test above is always made using PC section,
	    ** unlike byte instrs which always use the section the byte ptr
	    ** was fetched from.
	    ** This is actually consistent if you remember that the ACs are
	    ** considered to belong to PC section.
	    */
	    bp->s = (LHGET(w) >> 6) & 077;	/* S in next 6 */
	    w = ac_get(ac_off(ac,1));		/* Fetch 2nd word from AC+1 */

	    /* Interpret word as either an IFIW or EFIW.
	    ** Note this EA-calc is subject to hairy PXCT stuff!
	    ** Default section is PC section, unless affected by
	    ** PXCT in which case it's PCS.
	    ** No postprocessing is needed because both E and D bits are
	    ** implied if previous context applies to this pointer.
	    */
	    if (op10m_skipl(w)) {	/* IFIW? */
		if (op10m_tlnn(w, IW_EI)) { /* Bits 0,1 == 11? */
		    /* Generate page fail trap for bits 0,1 == 11 */
		    /* This is failure code 24 (PRM 3-41) */
		    /* BP came from ACs which are always current context */
		    vaddr_t e;		/* Set up args for pagefail */
		    va_lmake(e, 0, ac_off(ac,1));
		    pag_iiset(e, cpu.vmap.cur);
		    return RES_PF;
		}
		bp->fmt = BPF_TWL;	/* Say originally a TWLBP */
		bp->isindir = (op10m_tlnn(w, IW_I) != 0);
		if (cpu.mr_inpxct && (cpu.mr_inpxct & (src ? 02 : 01))) {
		    /* Use XBEA if source, XBRW if dest, plus PCS */
		    err = xea_fiwcalc(w, pag_pcsget(),
				(src ? cpu.acblk.xbea : cpu.acblk.xbrw),
				(src ?  cpu.vmap.xbea :  cpu.vmap.xbrw),
				&(bp->y), EA_ARG_IFIW);
		} else
		    err = xea_fiwcalc(w, PC_SECT,
			    cpu.acblk.xbea, cpu.vmap.xbea,
			    &(bp->y), EA_ARG_IFIW);

	    } else {			/* EFIW, no default section needed */
		bp->fmt = BPF_TWG;	/* Say originally a TWGBP */
		bp->isindir = (op10m_tlnn(w, IW_EI) != 0);
		if (cpu.mr_inpxct && (cpu.mr_inpxct & (src ? 02 : 01))) {
		    err = xea_fiwcalc(w, 0,
				(src ? cpu.acblk.xbea : cpu.acblk.xbrw),
				(src ?  cpu.vmap.xbea :  cpu.vmap.xbrw),
				&(bp->y), EA_ARG_EFIW);
		} else
		    err = xea_fiwcalc(w, 0,
				cpu.acblk.xbea, cpu.vmap.xbea,
				&(bp->y), EA_ARG_EFIW);
	    }
	    if (err)
		return (enum xires)err;
	}
	else
	    goto localbp;		/* Not a OWGBP or TWGBP, drop thru */

    } else {
	/* Plain vanilla one-word local BP.
	** As for TWGBPs, default section is PC section.
	*/
      localbp:
	bp->s = (LHGET(w) >> 6) & 077;	/* S in next 6 */
	bp->fmt = BPF_OWL;		/* Say originally an OWLBP */
	bp->isindir = (op10m_tlnn(w, IW_I) != 0);

	if (cpu.mr_inpxct && (cpu.mr_inpxct & (src ? 02 : 01))) {
	    /* Use XBEA if source, XBRW if dest, plus PCS */
	    err = xea_fiwcalc(w, pag_pcsget(),
			(src ? cpu.acblk.xbea : cpu.acblk.xbrw),
			(src ?  cpu.vmap.xbea :  cpu.vmap.xbrw),
			&(bp->y), EA_ARG_IFIW);
	} else {
	    /* Use XBEA mapping plus PC section */
	    err = xea_fiwcalc(w, PC_SECT,
			cpu.acblk.xbea, cpu.vmap.xbea,
			&(bp->y), EA_ARG_IFIW);
	}
	if (err)
	    return (enum xires)err;
    }

#else	/* end EXTADR */

    bp->s = (LHGET(w) >> 6) & 077;	/* S in next 6 */
    bp->isindir = (op10m_tlnn(w, IW_I) != 0);

    err = ((cpu.vmap.cur == cpu.vmap.xea)
	    ?        ea_fiwcalc(w, cpu.acblk.xea,  cpu.vmap.xea,  &(bp->y))
	    : (src ? ea_fiwcalc(w, cpu.acblk.xbea, cpu.vmap.xbea, &(bp->y))
		   : ea_fiwcalc(w, cpu.acblk.xbrw, cpu.vmap.xbrw, &(bp->y))));
    if (err)
	return (enum xires)err;
#endif /* !EXTADR */

    /* Have P, S, and Y.  Now finish setting up handy variables. */
    bp->newp = (W10BITS - bp->s) & 077;	/* New P when moving to next word */
    bp->bmsk =				/* Byte mask to use */
		wbytemask[bp->s <= W10BITS ? bp->s : W10BITS];
    bp->vp = NULL;
    bp->ycnt = 0;

    return RES_OK;
}


/* XBPUPDATE - Store BP back in AC.
**	Format to store is pre-determined by initial conversion.  See
**	XBPGET for the rules.
** NOTE: Care must be taken to preserve existing S, I, and X fields;
**	only P and Y can be updated.  For Y in particular this means
**	updating it with the # of word increments since the last update.
*/
static void
xbpupdate(register struct cleanbp *bp)
{
    register w10_t w;
    register int ac = bp->ac;

#if KLH10_EXTADR
    switch (bp->fmt) {
    case BPF_OWL:
#endif
	w = ac_get(ac);
	op10m_tlz(w, 0770000);			/* Clear for new P */
	op10m_tlo(w, ((h10_t)(bp->p) << 12));		/* Set new P */
	RHSET(w, (RHGET(w) + bp->ycnt)&H10MASK);	/* Set new Y */
	ac_set(ac, w);
#if KLH10_EXTADR
	break;	

    case BPF_TWL:
	w = ac_get(ac);
	op10m_tlz(w, 0770000);			/* Clear for new P */
	op10m_tlo(w, ((h10_t)(bp->p) << 12) | BPF_2WD);
	ac_set(ac, w);
	ac = ac_off(ac, 1);
	w = ac_get(ac);
	RHSET(w, (RHGET(w) + bp->ycnt)&H10MASK);	/* Set new Y */
	ac_set(ac, w);				/* Store 2nd wd */
	break;

    case BPF_OWG:			/* Store as simple TWG */
	XWDSET(w, (((h10_t)(bp->p) << 12) | (bp->s << 6) | BPF_2WD), 0);
	ac_set(ac, w);
	XWDSET(w, (h10_t)va_sect(bp->y), va_insect(bp->y));
	ac_set(ac_off(ac,1), w);		/* Store 2nd wd */
	break;

    case BPF_TWG:
	w = ac_get(ac);
	op10m_tlz(w, 0770000);			/* Clear for new P */
	op10m_tlo(w, ((h10_t)(bp->p) << 12) | BPF_2WD);
	ac_set(ac, w);
	ac = ac_off(ac, 1);
	w = ac_get(ac);
	{
	    uint32 y30;
	    y30 = (va_30frword(w) + bp->ycnt) & MASK30;	/* Get new 30-bit Y */
	    XWDSET(w, ((LHGET(w)&0770000)	/* Preserve high 6 bits */
			| (y30 >> 18)), y30 & H10MASK);
	}
	ac_set(ac, w);				/* Store 2nd wd */
	break;
    }
#endif /* KLH10_EXTADR */
    bp->ycnt = 0;		/* Must clear so further updates will work! */
}


/* XIBP - Increment clean pointer, return TRUE if word address bumped */
/* static int xibp(bp); */
#define xibp(bp) ( \
    (((bp)->p -= (bp)->s) < 0) \
	? ((bp)->p = (bp)->newp, (bp)->ycnt++, va_inc((bp)->y), 1) \
	: 0 )

/* XDECBP - Back up one, after already incremented.
**	This is a function rather than an in-line macro because it's
**	only called when backing out of an error, i.e. rarely.
**	This should never be called unless the BP has been incremented,
**	meaning that backing up should always be possible simply by
**	backing up P.
**	Unfortunately, nothing keeps the user from providing a bogus S.
**	Unclear what the real machine does, but here I'll try to recover.
*/
static void
xdecbp(register struct cleanbp *bp)
{
    if ((bp->p += bp->s) > W10BITS) {
#if 0
	panic("xdecbp: bad P!  P=%lo S=%lo", (long)bp->p, (long)bp->s);
#endif
	bp->p = bp->newp;		/* Attempt feeble recovery */
    }
}

/* XILDB - Fetch source byte.
**	Note: for support of PXCT'd MOVSLJ, the map used is XBEA which
** is interpreted by EXTEND as the "source" map.
**	ALSO NOTE: In order to conform to expectations of DFKCC diagnostic,
** the byte pointer is always incremented, whether or not a page failure
** happens.  If a page failure does happen, the backup simply adjusts
** P, and leaves Y alone; if Y was incremented, it is left incremented.
** This is guaranteed to still work since P will then point to start of
** word and the next increment will get us back to the page fail point.
**	ALSO NOTE: in order to accurately emulate the screw case where
** the BP is using an indirect address, we need to remember this fact
** and recompute the byte EA entirely when Y is incremented.  Ugh!
** Worst problem is that all normal EA computations
** do a pagfail longjmp, with no provision for a failure return!
** Thus we use a duplicate XEA calc that *does* do fail-return.
**	NOTE: also needs to provide for INSBRK exit!  Yuck!
*/

static enum xires
xildb(register struct cleanbp *bp, register w10_t *wp)
{
    register w10_t w;

    if ((bp->p -= bp->s) < 0) {		/* Find new P and Y */
	bp->p = bp->newp;		/* Next word, so fix up P */
	bp->vp = NULL;
	bp->ycnt++;
	if (bp->isindir) {		/* Using indirect EA? */
	    /* Ugh.  Write out BP, then re-compute byte EA. */
	    register enum xires res;
	    xbpupdate(bp);
	    if (res = xbpinit(bp, bp->ac, bp->src)) {
		xdecbp(bp);		/* Failed??  Back up P */
		return bp->err = res;
	    }
	}
	else
	    va_inc(bp->y);		/* Can just bump Y */
    }
    if (!bp->vp) {		/* Map new word addr if needed */
	if (!(bp->vp = vm_xbeamap(bp->y, VMF_READ|VMF_NOTRAP))) {
	    /* Map failure only backs up P, never Y! */
	    if ((bp->p += bp->s) > W10BITS)	/* Back up P */
		bp->p = bp->newp;		/* Catch any screwy P+S case */
	    return bp->err = RES_PF;		/* Report failure */
	}
	bp->wd = vm_pget(bp->vp);	/* Fetch new word */
    }

    w = bp->wd;
    op10m_rshift(w, bp->p);	/* Shift word to right-align byte */
    op10m_and(w, bp->bmsk);	/* Mask out byte */
    *wp = w;
    return RES_OK;
}


/* XIDPB - Deposit dest byte.
**	Note: for support of PXCT'd MOVSLJ, the map used is XBRW which
** is interpreted by EXTEND as the "destination" map.
**	Same behavior on page failure as XILDB above.
*/
static enum xires
xidpb(register struct cleanbp *bp, register w10_t *wp)
{
    register w10_t w, bmask;

    if ((bp->p -= bp->s) < 0) {		/* Find new P and Y */
	bp->p = bp->newp;		/* Next word, so fix up P */
	bp->vp = NULL;
	bp->ycnt++;
	if (bp->isindir) {		/* Using indirect EA? */
	    /* Ugh.  Write out BP, then re-compute byte EA. */
	    register enum xires res;
	    xbpupdate(bp);
	    if (res = xbpinit(bp, bp->ac, bp->src)) {
		xdecbp(bp);		/* Failed??  Back up P */
		return bp->err = res;
	    }
	}
	else
	    va_inc(bp->y);		/* Can just bump Y */
    }
    if (!bp->vp) {		/* Map new word addr if needed */
	if (!(bp->vp = vm_xbrwmap(bp->y, VMF_WRITE|VMF_NOTRAP))) {
	    /* Map failure only backs up P, never Y! */
	    if ((bp->p += bp->s) > W10BITS)	/* Back up P */
		bp->p = bp->newp;		/* Catch any screwy P+S case */
	    return bp->err = RES_PF;		/* Report failure */
	}
	bp->wd = vm_pget(bp->vp);	/* Fetch new word */
    }

    bmask = bp->bmsk;		/* Get mask for byte */
    w = *wp;			/* Fetch new byte */
    op10m_lshift(bmask, bp->p);	/* Shift out to align mask with byte pos */
    op10m_lshift(w, bp->p);	/* Shift new byte into position */
    op10m_andcm(bp->wd, bmask);	/* Clear bits from byte pos in word */
    op10m_and(w, bmask);	/* And remove excess bits from byte */
    op10m_ior(bp->wd, w);	/* Stuff byte into word! */

    /* Later try to defer storage until all done with word */
    vm_pset(bp->vp, bp->wd);	/* Store new word */
    return RES_OK;
}


/* This is used only for MOVSRJ.
**	The KL+KS ucode simply IBPs to effect the MOVSRJ skipping.
**	This does it more cleverly, but the real reason is so the EA can
**	be correctly recomputed if the BP was indirected through.
**	Any resulting page fault happens before the first actual ILDB ref
**	but that's okay.
** Note that no CLOCKPOLL/INSBRKTEST is needed because the loop here is
** limited to 35 iterations or less.
*/
static enum xires
xadjbp(register struct cleanbp *bp,
       register int32 n)	/* Positive # of bytes to adjust up by */
{
    register int32 yn;

    /* Make sure can't screw up due to bogus S (zero or >36) */
    if (bp->s <= 0)
	return RES_OK;

    while (--n >= 0) {
	if ((bp->p -= bp->s) < 0) {	/* Find new P until hit end */

	    /* Must increment Y by 1.  Take advantage of the forced word
	       alignment to add all remaining words in one swoop.
	    */
	    if (bp->s >= W10BITS) {	/* Guard against bogus S */
		yn = n+1;		/* Full-word bytes.  Note extra 1 */
		n = 0;
	    } else {
		yn = (n / (W10BITS/bp->s)) + 1;	/* Note extra 1 */
		n  = (n % (W10BITS/bp->s));
	    }
	    bp->ycnt += yn;
	    bp->p = bp->newp - (n * bp->s);

	    /* Now update Y.  If BP is indirect, must recompute the EA and
		possibly fail (which involves no backup).
	    */
	    if (bp->isindir) {
		xbpupdate(bp);
		return xbpinit(bp, bp->ac, bp->src);
	    } else
		va_add(bp->y, yn);
	    break;
	}
    }
    return RES_OK;
}

/* MOVSLJ - Move String Left Justified		(EXTEND [016 ])
**
** This is the only EXTEND string instruction legal for PXCTing (and only
** on an extended KL).  The
** necessary mappings are done by the byte-pointer auxiliary routines.
** See DEC PRM p.3-51.
*/
xinsdef(ix_movslj)
{
    register int32 len1, len2;	/* Note signed */
    register vaddr_t va;
    struct cleanbp s1, s2;
    w10_t wbyte;
    w10_t wfill;
    enum xires res = RES_TRUNC;

    AC_32GET(len1, ac,	0, 0777000);	/* Get source len */
    AC_32GET(len2, ac,	3, 0777000);	/* Get dest len */

    /* Now to satisfy diag expectations, always fetch fill byte, thus
    ** triggering a page fail at this point if necessary.
    */
    va = e0;
    va_inc(va);			/* Get E0+1 */
    wfill = vm_read(va);	/* Get c(E0+1) */

    if (!len2)				/* Quick check for null case */
	return len1 ? PCINC_1 : PCINC_2; /* Skip if no source bytes left */

    XBPGET(&s1, ac, 1, BPSRC);		/* Set up for read, may pagefault */
    XBPGET(&s2, ac, 4, BPDST);

    /* Default result depends on whether strings will count out together */
    res = (len1 == len2) ? RES_WON : RES_TRUNC;

    while (--len2 >= 0) {
	if (--len1 < 0) {			/* If source string is gone */
	    /* Get fill byte, start inner loop */
	    res = RES_WON;		/* Source string exhausted */
	    do {
		if (xidpb(&s2, &wfill)) {
		    /* Clean up and fail (normally page fail) */
		    res = s2.err;
		    ++len2;
		    break;
		}
	    } while (--len2 >= 0);
	    break;	/* Return, either won or page-failed */
	}

	/* Fetch & store byte */
	if (xildb(&s1, &wbyte)) {
	    /* Clean up and fail (normally page fail) */
	    res = s1.err;
	    ++len1, ++len2;	/* Back up */
	    break;
	}
	if (xidpb(&s2, &wbyte)) {	/* Store byte */
	    /* Clean up and page-fail */
	    res = s2.err;
	    xdecbp(&s1);		/* Back up source BP */
	    ++len1, ++len2;
	    break;
	}

	/* Done with one iteration, now check before doing next */
	if (len2 <= 0)
	    break;			/* Nope, all done */
	CLOCKPOLL();			/* More left, keep clock going */
	if (INSBRKTEST()) {		/* Watch for PI interrupt */
	    res = RES_PI;
	    break;
	}
    }

    /* Done, update ACs and return appropriate PC increment
    ** (Skips if source was exhausted)
    */
    ac_32set(ac, (len1 > 0 ? len1 : 0));
    xbpupdate(&s1);			/* Update BP in AC+1,2 */
    ac_32set(ac_off(ac,3), (len2 > 0 ? len2 : 0));
    xbpupdate(&s2);			/* Update BP in AC+4,5 */

    switch (res) {
    case RES_PF:	pag_fail();	/* Never returns */
    case RES_PI:	apr_int();	/* Never returns */
    case RES_WON:	return PCINC_2;	/* Skips if source exhausted */
    default:
    case RES_TRUNC:	return PCINC_1;
    }
}

/* MOVSO - Move String Offset		(EXTEND [014 ])
*/
xinsdef(ix_movso)
{
    register int32 len1, len2;	/* Note signed */
    register vaddr_t va;
    struct cleanbp s1, s2;
    w10_t wbyte;
    w10_t wfill;
    register w10_t woff, wmask;
    enum xires res = RES_TRUNC;

    if (va_insect(e1) & H10SIGN)	/* See if high halfwd bit set */
	LRHSET(woff, H10MASK, va_insect(e1));	/* Yep, sign-extend it */
    else LRHSET(woff, 0, va_insect(e1));	/* Nope */

    AC_32GET(len1, ac,	0, 0777000);	/* Get source len */
    AC_32GET(len2, ac,	3, 0777000);	/* Get dest len */

    /* Now to satisfy diag expectations, always fetch fill byte, thus
    ** triggering a page fail at this point if necessary.
    */
    va = e0;
    va_inc(va);			/* Get E0+1 */
    wfill = vm_read(va);	/* Get c(E0+1) */

    if (!len2)			/* Quick check for null case */
	return len1 ? PCINC_1 : PCINC_2; /* Skip if no source bytes left */
    XBPGET(&s1, ac, 1, BPSRC);	/* Set up for read, may pagefault */
    XBPGET(&s2, ac, 4, BPDST);

    /* Final setup for offset - remember mask to use */
    wmask = s2.bmsk;		/* Get mask for byte */
    op10m_setcm(wmask);		/* Complement it to get test mask */

    /* Default result depends on whether strings count out together */
    res = (len1 == len2) ? RES_WON : RES_TRUNC;

    while (--len2 >= 0) {
	if (--len1 < 0) {			/* If source string is gone */

	    /* Note offset not applied to fill byte! */
	    res = RES_WON;		/* Source string exhausted */
	    do {
		if (xidpb(&s2, &wfill)) {
		    /* Clean up and page fail */
		    res = s2.err;
		    ++len2;
		    break;
		}
	    } while (--len2 >= 0);
	    break;	/* Return, either won or page-failed */
	}

	/* Fetch & store byte */
	if (xildb(&s1, &wbyte)) {
	    /* Clean up and page-fail */
	    res = s1.err;
	    ++len1, ++len2;	/* Back up */
	    break;
	}
	/* Now do special MOVSO stuff. */
	op10m_add(wbyte, woff);		/* Add offset */
	if (op10m_tdnn(wbyte, wmask)) {	/* Check for non-byte bits set */
	    /* Clean up and lose.  Source BP and len are left pointing
	    ** to the guilty byte (last one referenced).
	    */
	    ++len2;			/* Back up pre-decremented count */
	    res = RES_TRUNC;
	    break;
	}
	if (xidpb(&s2, &wbyte)) {	/* Store byte */
	    /* Clean up and page-fail */
	    res = s2.err;
	    xdecbp(&s1);		/* Back up source BP */
	    ++len1, ++len2;
	    break;
	}

	/* Done with one iteration, now check before doing next */
	if (len2 <= 0)			/* More to do? */
	    break;
	CLOCKPOLL();			/* More left, keep clock going */
	if (INSBRKTEST()) {		/* Watch for PI interrupt */
	    res = RES_PI;
	    break;
	}
    }

    /* Done, update ACs and return appropriate PC increment
    ** (Skips if source was exhausted)
    */
    ac_32set(ac, (len1 > 0 ? len1 : 0));
    xbpupdate(&s1);			/* Update BP in AC+1,2 */
    ac_32set(ac_off(ac,3), (len2 > 0 ? len2 : 0));
    xbpupdate(&s2);			/* Update BP in AC+4,5 */

    switch (res) {
    case RES_PF:	pag_fail();	/* Never returns */
    case RES_PI:	apr_int();	/* Never returns */
    case RES_WON:	return PCINC_2;	/* Skips if source exhausted */
    default:
    case RES_TRUNC:	return PCINC_1;
    }
}

/* MOVST - Move String Translated		(EXTEND [015 ])
**
** It appears that real machine always fetches the fill byte regardless
** of whether it is needed or not.  So I'm doing the same thing in order
** to accurately emulate the page fail testing done by the diagnostics.
** Sigh.  Similar problem for CMPSxx.
*/

xinsdef(ix_movst)
{
    register int32 len1, len2;	/* Note signed */
    register vmptr_t vp;
    register vaddr_t va;
    h10_t flags;
    struct cleanbp s1, s2;
    w10_t wbyte;
    w10_t wfill;
    enum xires res = RES_WON;

    /* First get lengths and check for illegal bits */
    AC_32GET(len1, ac, 0, 0077000);	/* Get source len */
    flags = ac_getlh(ac) & CVTF_ALL;	/* Find flags */

    AC_32GET(len2, ac, 3, 0777000);	/* Get dest len */

    /* Now to satisfy diag expectations, always fetch fill byte, thus
    ** triggering a page fail at this point if necessary.
    */
    va = e0;
    va_inc(va);			/* Get E0+1 */
    wfill = vm_read(va);	/* Get c(E0+1) */

    if (!len2)			/* Quick check for no-destination case */
	return len1 ? PCINC_1 : PCINC_2; /* Skip if no source bytes either */
    XBPGET(&s1, ac, 1, BPSRC);	/* Set up for read, may pagefault */
    XBPGET(&s2, ac, 4, BPDST);

    /* Start loop.  Note result defaults to RES_WON. */
    while (--len1 >= 0) {		/* Loop reading from source */
	register h10_t ent;
	if (xildb(&s1, &wbyte)) {	/* Fetch byte */
	    res = s1.err;	/* Clean up and page-fail */
	    ++len1;		/* Back up */
	    break;
	}
	/* Now do special MOVST stuff. */
	ent = RHGET(wbyte);
	va = e1;
	va_add(va, (ent>>1));	/* Get E1+(ent>>1) */
	if (!(vp = vm_xrwmap(va, VMF_READ|VMF_NOTRAP))) {
	    xdecbp(&s1);	/* Back up source bp */
	    ++len1;		/* Back up */
	    res = RES_PF;	/* Clean up and page-fail */
	    break;
	}
	/* Do translation (same table as CVTDBT) */
	ent = (ent & 01) ? vm_pgetrh(vp) : vm_pgetlh(vp);
	switch ((ent >> 15) & 07) {
		case 0:	break;
		case 1: res = RES_TRUNC;	break;
		case 2: flags &= ~CVTF_M;	break;
		case 3: flags |= CVTF_M;	break;
		case 4: flags |= CVTF_S|CVTF_N;	break;
		case 5: flags |= CVTF_N; res = RES_TRUNC; break;
		case 6: flags = CVTF_S|CVTF_N;		break;
		case 7: flags = CVTF_S|CVTF_N|CVTF_M;	break;
	}
	if (res != RES_WON)		/* If wants to stop now, */
	    break;			/* leave the loop! */

	if (flags & CVTF_S) {
	    LRHSET(wbyte, 0, ent & 007777);	/* Get translated byte */
	    if (xidpb(&s2, &wbyte)) {		/* Deposit it! */
		res = s2.err;			/* Clean up and page-fail */
		xdecbp(&s1);
		++len1;
/* KLH: Possible bug here, if pagefail at this point
   the flags have already been clobbered!  But flag state shouldn't
   affect the repeated translation, so this should be OK.
*/
		break;
	    }
	    if (--len2 <= 0) {		/* Byte stored, bump count! */
		if (len1 > 0)		/* Dest full, must stop. */
		    res = RES_TRUNC;	/* Say truncated, if any source left */
		break;
	    }
	}

	/* Done with one iteration, now check before doing next */
	if (len1 <= 0)			/* Stop now if none left */
	    break;
	CLOCKPOLL();			/* More left, keep clock going */
	if (INSBRKTEST()) {		/* Watch for PI interrupt */
	    res = RES_PI;
	    break;
	}
    }

    /* Now see if have to fill out rest of dest string.
    ** This only happens if result is still RES_WON.
    */
    if (res == RES_WON && len2 > 0) {
	for (;;) {
	    /* OK, set up for fill loop, using saved fill byte */
	    /* Note translation not applied to fill byte! */
	    if (xidpb(&s2, &wfill)) {
		res = s2.err;		    /* Clean up and page fail */
		break;
	    }
	    if (--len2 <= 0)
		break;
	    CLOCKPOLL();		/* More left, keep clock going */
	    if (INSBRKTEST()) {		/* Watch for PI interrupt */
		res = RES_PI;
		break;
	    }
 	}
    }

    /* Done, update ACs and return appropriate PC increment
    ** (Skips if source was exhausted)
    */
    if (len1 < 0)
	len1 = 0;			/* Clear so don't return -1! */
    ac_setlrh(ac, flags | (len1>>H10BITS), len1 & H10MASK);
    xbpupdate(&s1);			/* Update BP in AC+1,2 */
    ac_32set(ac_off(ac,3), (len2 > 0 ? len2 : 0));
    xbpupdate(&s2);			/* Update BP in AC+4,5 */

    switch (res) {
    case RES_PF:	pag_fail();	/* Never returns */
    case RES_PI:	apr_int();	/* Never returns */
    case RES_WON:	return PCINC_2;	/* Skips if source exhausted */
    default:
    case RES_TRUNC:	return PCINC_1;
    }
}

/* MOVSRJ - Move String Right Justified		(EXTEND [017 ])
**
** Although PRM doesn't say so explicitly, AC is always 0 unless the
** instr is interrupted, because the source string is always completely
** consumed one way or another.
*/
xinsdef(ix_movsrj)
{
    register int32 len1, len2;	/* Note signed */
    register vaddr_t va;
    struct cleanbp s1, s2;
    w10_t wbyte;
    w10_t wfill;
    enum xires res = RES_OK;

    AC_32GET(len1, ac, 0, 0777000);	/* Get source len */
    AC_32GET(len2, ac, 3, 0777000);	/* Get dest len */

    /* Now to satisfy diag expectations, always fetch fill byte, thus
    ** triggering a page fail at this point if necessary.
    */
    va = e0;
    va_inc(va);			/* Get E0+1 */
    wfill = vm_read(va);	/* Get c(E0+1), may pagefault */

    if (!len2)			/* Quick check for null dest case */
	return PCINC_2;		/* MOVSRJ always skips?? */
    XBPGET(&s1, ac, 1, BPSRC);	/* Set up for read, may pagefault */
    XBPGET(&s2, ac, 4, BPDST);

    /* Decide what to do */
    if (len1 < len2) {
	/* Use initial padding with fill.  Fill dest until same len as src. */
	for (;;) {
	    if (xidpb(&s2, &wfill)) {
		res = s2.err;
		break;
	    }
	    if (--len2 <= len1)		/* Update dest len, keep going */
		break;			/* unless dest now small enough */

	    CLOCKPOLL();		/* More to do, keep clock going */
	    if (INSBRKTEST()) {		/* Watch for PI interrupt */
		res = RES_PI;
		break;
	    }
	}
    } else if (len1 > len2) {
	/* Source too big, skip over source bytes before starting copy */
	res = xadjbp(&s1, len1-len2);	/* IBP it cleverly, RES_OK if won */
	len1 = len2;			/* Source len now same as dest */
    }

    if ((res == RES_OK)		/* Make sure still OK (not PF or PI) */
      && len2) {		/* and dest string still needs more stuff */
	/* At this point, len1 == len2 and at least one byte to copy */
	for (;;) {
	    if (xildb(&s1, &wbyte)) {	/* Fetch byte! */
		res = s1.err;		/* Page-fail.  No backup needed */
		break;
	    }
	    if (xidpb(&s2, &wbyte)) {	/* Store byte! */
		res = s2.err;
		xdecbp(&s1);		/* Page-fail, must back up source BP */
		break;
	    }

	    /* Done with one iteration, now check before doing next */
	    if (--len2 <= 0)
		break;			/* Nope, all done */
	    CLOCKPOLL();		/* More left, keep clock going */
	    if (INSBRKTEST()) {		/* Watch for PI interrupt */
		res = RES_PI;
		break;
	    }
	}
	len1 = len2;			/* Update len1 to match len2 */
    }

    /* Done, update ACs and return appropriate PC increment
    ** (MOVSRJ always skips unless interrupted)
    */
    ac_32set(ac, len1);
    xbpupdate(&s1);			/* Update BP in AC+1,2 */
    ac_32set(ac_off(ac,3), len2);
    xbpupdate(&s2);			/* Update BP in AC+4,5 */

    switch (res) {
    case RES_PF:	pag_fail();	/* Never returns */
    case RES_PI:	apr_int();	/* Never returns */
    default:
    case RES_OK:
    case RES_WON:	return PCINC_2;
    }
}

/* CMPSxx - Compare Strings		(EXTEND [001-007 ])
**	Note 000 and 004, which otherwise would be CMPS and CMPSA,
** are respectively illegal and EDIT.
**
** In order to make the DFKCC diag happy it appears that we need to
** reference the appropriate fill word at E0+1 or E0+2 first, before
** comparing any bytes!
*/

/* Success bits for op */
#define CMPF_LT 01	/* Skip if S1 < S2 */
#define CMPF_EQ 02	/* Skip if S1 = S2 */
#define CMPF_GT 04	/* Skip if S1 > S2 */

static int cmpftab[] = {
	0,			/* [000 ] Illegal - wd be "CMPS", never skip */
	CMPF_LT,		/* [001 ] CMPSL */
	CMPF_EQ,		/* [002 ] CMPSE */
	CMPF_LT|CMPF_EQ,	/* [003 ] CMPSLE */
	-1,			/* [004 ] EDIT - wd be "CMPSA", always skip */
	CMPF_EQ|CMPF_GT,	/* [005 ] CMPSGE */
	CMPF_LT|CMPF_GT,	/* [006 ] CMPSN */
	CMPF_GT			/* [007 ] CMPSG */
};

/* Common code for CMPSx extended instructions.
** Skips if string comparison result matches any of the requested CMPF bits.
*/
xinsdef(ix_cmps)		/* Do all CMPSx instructions */
{
    register int32 len1, len2;	/* Note signed */
    register vaddr_t va;
    struct cleanbp s1, s2;
    w10_t w1, w2;
    register w10_t fill;
    enum xires res = RES_OK;
    register int cmpf;

    /* Set up string lengths. */
    AC_32GET(len1, ac, 0, 0777000);	/* Get source len */
    AC_32GET(len2, ac, 3, 0777000);	/* Get dest len */

    /* To accurately emulate the KS/KL with respect to page fails, we
    ** we need to fetch the appropriate fill byte first, and do it even
    ** if none is needed (string lengths equal).
    */
#if 0	/* More efficient, but won't trigger failures that diag expects */
    if (!len1 && !len2)			/* Quick check for null case */
	return (xop == (IX_CMPSE & 0777)) ? PCINC_2 : PCINC_1;
#endif

    /* It appears that when the lengths are equal, the fill byte fetched
    ** comes from E0+2 on the KS, E0+1 on the KL.  Whoopee.
    ** Again, this is only to keep diags happy.
    */
    va = e0;			/* Set up E0 */
#if KLH10_CPU_KL
    if (len1 <= len2)
#elif KLH10_CPU_KS
    if (len1 < len2)
#endif
	va_inc(va);		/* Get E0+1 (source fill) */
    else
	va_add(va, 2);		/* Get E0+2 (dest fill) */
    fill = vm_read(va);		/* Fetch fill byte, may pagefault */

    XBPGET(&s1, ac, 1, BPSRC);	/* Set up for read, may pagefault */
    XBPGET(&s2, ac, 4, BPDST);

    /* Loop until pagefail, interrupt, bytes not equal, or count out */
    cmpf = CMPF_EQ;			/* Default if count out is equal */
    for (;;) {
	if (--len1 >= 0) {		/* Get byte from string 1 */
	    if (xildb(&s1, &w1)) {
		res = s1.err;
		++len1;			/* Back up, assume BP backed up */
		break;
	    }
	} else {
	    if (len2 <= 0) break;
	    if (len1 == -1) {		/* If first time here, get fill byte */
		w1 = fill;
	    }
	}

	if (--len2 >= 0) {			/* Get byte from string 2 */
	    if (xildb(&s2, &w2)) {
		res = s2.err;
		++len2;				/* Back up */
		if (len1 >= 0) {
		    ++len1;
		    xdecbp(&s1);		/* Back up first BP */
		}
		break;
	    }
	} else {
	    /* No test for len1 cuz if we got here it's known to be OK */
	    if (len2 == -1) {		/* If first time here, get fill byte */
		w2 = fill;
	    }
	}
	
	/* Both bytes fetched, now test */
	if (op10m_camn(w1, w2)) {	/* Test for equality */
	    cmpf = (op10m_ucmpl(w1, w2)	/* Not equal!  Use unsigned compare */
			? CMPF_LT : CMPF_GT);
	    break;
	}

	/* Equal so far.  Set up to repeat loop... */
	CLOCKPOLL();			/* Keep clock going */
	if (INSBRKTEST()) {		/* Watch for PI interrupt */
	    res = RES_PI;
	    break;
	}
    }

    /* Return result after updating ACs */
    ac_32set(ac, (len1 > 0 ? len1 : 0));
    xbpupdate(&s1);			/* Update BP in AC+1 */
    ac_32set(ac_off(ac,3), (len2 > 0 ? len2 : 0));
    xbpupdate(&s2);			/* Update BP in AC+4 */

    switch (res) {
    case RES_PF:	pag_fail();		/* Never returns */
    case RES_PI:	apr_int();		/* Never returns */
    default:
	/* Skip if result bit found in op's success bits */
	return (cmpf & cmpftab[xop & 07]) ? PCINC_2 : PCINC_1;
    }
}

/* CVTBDO - Convert Binary to Decimal Offset		(EXTEND [012 ])
   CVTBDT - Convert Binary to Decimal Translated	(EXTEND [013 ])

NOTE:
	It has been discovered the hard way that CVTBDx does the
string length check before it attempts to load or reference the byte
pointers (in particular, during monitor startup to determine CPU type).
Hence the initialization of S2 must be delayed until after that point.

	Also, the fill byte fetch, unlike the MOVS and CMPS instructions,
is only done if needed -- not beforehand!

	Also, the real hardware uses the First-Part-Done flag to tell
itself that an interrupted CVTBDx has gubbish in the ACs that are not
meaningful to anything but the KL microcode.  The emulation here avoids
using FPD by always restoring the ACs to a sensible state.  For this reason,
diagnostics (ie DFKCC) that test interrupted results will always fail.

	Note that a successful CVTBDx is defined to clear the length AC,
even if no filling was done; thus the returned length cannot be used to
tell how many characters were deposited, and in fact its value during
an interrupt (on a real KL) is the # of digits left to do, not the # of
bytes left in destination buffer.  The code here emulates this for
simplicity.

*/
static int cvtb_digcnt(dw10_t *ad);

xinsdef(ix_cvtbd)	/* Do both CVTBDO and CVTBDT */
{
    register int32 len2;	/* Note signed */
    register vaddr_t va;
    h10_t flags;
    register int ndigs;
    struct cleanbp s2;
    w10_t wfill, woff;
    dw10_t d;
    int transf;
    enum xires res = RES_TRUNC;

    AC_32GET(len2, ac, 3, 0077000);	/* Get dest len (may fail) */
    flags = ac_getlh(ac_off(ac,3)) & CVTF_ALL;	/* Find flags */

    /* Check opcode to see if doing translate.  Must mask IX_ since enum
    ** has special high bit(s).
    */
    transf = (xop == (IX_CVTBDT & 0777));	/* See if doing translate */
    if (!transf) {			/* If not, set up offset */
	if (va_insect(e1) & H10SIGN)	/* See if high halfwd bit set */
	    LRHSET(woff, H10MASK, va_insect(e1)); /* Yep, sign-extend it */
	else LRHSET(woff, 0, va_insect(e1));	/* Nope */
    }

    ac_dget(ac, d);			/* Get binary double */

    /* Set N,M flags appropriately */
    if (op10m_signtst(d.w[0])) {
	flags |= CVTF_N | CVTF_M;	/* Non-zero and Minus */
	op10m_dmovn(d);			/* Make positive */
    } else {
	op10m_signclr(d.w[1]);		/* Positive, ensure low sign clear */
	if (op10m_skipn(d.w[0]) || op10m_skipn(d.w[1]))
	    flags |= CVTF_N;		/* Non-zero */
    }
    /* Put flags in now if KL -- easier than checking for aborts at
    ** XBPGET and fill-byte fetch below.
    */
#if KLH10_CPU_KL
    ac_setlrh(ac_off(ac,3), flags | (len2>>H10BITS), len2 & H10MASK);
#endif

    /* The low sign bit is guaranteed to be clear at this point,
    ** and the high sign bit will only be set if the entire number
    ** is the maximum negative value of -2^70.
    */

    /* At this point, determine how large number will be.
    ** Can either do a binary search on power-of-10 table, or
    ** actually build string and find out.
    ** Max is 22 digits; search would take max of 5 comparisons (2^5 == 32)
    ** whereas building string does divides by 10... ugh!
    ** Idea: faster to do own add/sub loop, using power-of-10 table.  A
    ** single regular DDIV does 70 dadd/dsubs!
    */
    if (op10m_signtst(d.w[0])) {	/* If still negative, */
	ndigs = 22;			/* is max # of 2^70!! */
    } else {
	ndigs = cvtb_digcnt(&d);
    }

    if (ndigs > len2) {
	/* Number requires more digits than dest string has, so fail */
	return PCINC_1;
    }

    /* OK to proceed, now set up destination BP.
    ** This cannot be done earlier since CVTBDx is used by certain
    ** processor-ID algorithms that leave garbage in the ACs and will be
    ** very surprised to get a page fail or MUUO instead!
    */
    XBPGET(&s2, ac, 4, BPDST);		/* Set up dest BP from ACs */

    /* If dest string is longer, and CVTF_L is set, pad out. */
    if ((len2 > ndigs) && (flags & CVTF_L)) {

	/* Get fill byte word from E0+1 - can page-fail!
	** No special cleanup needed as flags have already been set if KL.
	*/
	va = e0;
	va_inc(va);
	wfill = vm_read(va);

	res = RES_TRUNC;
	while (len2 > ndigs) {
	    if (xidpb(&s2, &wfill)) {	/* Store fill byte */
		res = s2.err;
		break;
	    }
	    --len2;
	    CLOCKPOLL();		/* Keep clock going */
	    if (INSBRKTEST()) {		/* Watch for PI interrupt */
		res = RES_PI;
		break;
	    }
	}
	if (res != RES_TRUNC) {
	    ac_setlrh(ac_off(ac,3), flags | (len2>>H10BITS), len2 & H10MASK);
	    xbpupdate(&s2);		/* Update BP in AC+4 */
	    switch (res) {
	    case RES_PF:	pag_fail();
	    case RES_PI:	apr_int();
	    default: break;
	    }
	}
    }

    /* At this point, only "ndigs" more bytes to deposit.
    ** OK to stop keeping track of len2, because returned count is
    ** always 0 if success, and if interrupted the real KL sets this
    ** to updated ndigs, not updated len2, so we do the same.
    ** ndigs is guaranteed to be nonzero (see cvtb_digcnt).
    */

    /* Build decimal string.  Starting with power-of-10 specified by
    ** ndigs, count how many times one has to subtract that before
    ** number is within range of lesser power, and that's the digit
    ** for that position.
    */
    for (;;) {
	register int dig;
	w10_t wbyte;
	dw10_t rbdw;			/* For rollback if pagefail */

	rbdw = d;			/* Save current number */
	if (ndigs == 1)
	    dig = RHGET(d.w[1]);	/* d contains last digit */
	else
	  for (dig = 0; !op10m_udcmpl(d, dpow10[ndigs-1]); ++dig) {
	    /* Carry out a DSUB.  Assumes low sign bit clear! */
	    op10m_sub(d.w[0], dpow10[ndigs-1].w[0]);	/* Sub high words */
	    op10m_sub(d.w[1], dpow10[ndigs-1].w[1]);	/* Sub low words */
	    if (op10m_signtst(d.w[1])) {	/* If low sign now set, */
		op10m_dec(d.w[0]);		/* carry to high word */
		op10m_signclr(d.w[1]);		/* and clear low sign */
	    }
	  }

	/* dig now contains digit 0-9 for this position */

	/* Offset or translate digit into a byte, then deposit that */
	if (!transf) {
	    wbyte = woff;
	    op10m_addi(wbyte, dig);
	} else {
	    /* Translate stuff */
	    register vmptr_t vp;
	    va = e1;
	    va_add(va, dig);		/* Get E1+dig */
	    if (!(vp = vm_xrwmap(va, VMF_READ|VMF_NOTRAP))) {
		res = RES_PF;		/* Page fail, can't fetch transl */
		d = rbdw;		/* Roll back to original # */
		break;
	    }
	    wbyte = vm_pget(vp);
	    if (ndigs == 1 && (flags & CVTF_M))	/* Special hack for last dig */
		RHSET(wbyte, LHGET(wbyte));	/* Use LH instead if M set */
	    LHSET(wbyte, 0);			/* Clear LH */
	}
	if (xidpb(&s2, &wbyte)) {
	    res = s2.err;		/* Page fail, can't deposit byte */
	    d = rbdw;			/* Roll back to original # */
	    break;
	}

	/* Success for this digit! */
	if (--ndigs <= 0) {		/* Was it last digit? */
	    op10m_setz(d.w[1]);		/* Clear low AC (high already clear) */
	    break;			/* And leave loop! */
	}

	CLOCKPOLL();			/* More digits, keep clock going */
	if (INSBRKTEST()) {		/* Watch for PI interrupt */
	    res = RES_PI;
	    break;
	}
    }


    ac_dset(ac, d);		/* Store back binary number */
    ac_setlrh(ac_off(ac,3), flags | (ndigs>>H10BITS), ndigs & H10MASK);
    xbpupdate(&s2);		/* Store BP back in AC+4,5 */
    switch (res) {
	case RES_PF:	pag_fail();
	case RES_PI:	apr_int();
	default: break;
    }

    return PCINC_2;
}



/* Count # decimal digits in double-word binary number, which is guaranteed
** to be positive with sign bits clear in both words.
*/

static int
cvtb_digcnt(dw10_t *ad)
{
    register int i;
    register dw10_t d;

    d = *ad;
    for (i = 1; i < DPOW_MAX; ++i)
	if (op10m_udcmpl(d, dpow10[i]))
	    break;

    /* i has the power of 10 that this number fits under, thus the # of digits
    ** needed to represent it is likewise i.  i will never be 0, even when
    ** the number is zero, because count starts at 1.
    */
    return i;
}

/* CVTDBO - Convert Decimal to Binary Offset		(EXTEND [010 ])
   CVTDBT - Convert Decimal to Binary Translated	(EXTEND [011 ])
*/

#define CVTF_S H10SIGN		/* User sets to indicate pre-existing # */

xinsdef(ix_cvtdb)		/* Do both CVTDBO and CVTDBT */
{
    register int32 len1;	/* Note signed */
    register int transf;	/* TRUE if CVTDBT, else CVTDBO */
    register vaddr_t va;
    h10_t flags;
    struct cleanbp s1;
    w10_t wbyte, woff;
    dw10_t d;
    unsigned int dig;
    enum xires res;

    AC_32GET(len1, ac, 0, 0077000);	/* Get src len (may MUUO-fail) */
    flags = ac_getlh(ac) & CVTF_ALL;	/* Find flags */
    XBPGET(&s1, ac, 1, BPSRC);		/* Set up source BP */

    /* Check opcode to see if doing translate.  Must mask IX_ since enum
    ** has special high bit(s).
    */
    transf = (xop == (IX_CVTDBT & 0777));
    if (!transf) {			/* If not, set up offset */
	if (va_insect(e1) & H10SIGN)	/* See if high halfwd bit set */
	    LRHSET(woff, H10MASK, va_insect(e1)); /* Yep, sign-extend it */
	else LRHSET(woff, 0, va_insect(e1));	/* Nope */
    }

    if (flags & CVTF_S)			/* Number already there? */
	ac_dget(ac_off(ac,3), d);	/* Get binary double */
    else {
	op10m_dsetz(d);		/* Nope, clear it */
	if (!transf)		/* If doing offset (CVTDBO), ensure */
	    flags |= CVTF_S;	/* that S is now set. */
    }

    /* Now loop over source string.  Note default result is RES_WON. */
    res = RES_WON;
    if (len1 > 0)
      for (;;) {		/* Loop check is near end */

	if (xildb(&s1, &wbyte)) {
	    res = s1.err;
	    break;
	}
	--len1;			/* Byte gobbled, so bump count */
	if (!transf) {		/* Do offset? */
	    op10m_add(wbyte, woff);	/* Add in offset */
	    if (LHGET(wbyte)) {
		res = RES_TRUNC;
		break;		/* Abort, digit is outside range */
	    }
	    dig = RHGET(wbyte);

	} else {		/* Do translation */
	    register vmptr_t vp;
	    register h10_t ent;
	    ent = RHGET(wbyte);
	    va = e1;
	    va_add(va, (ent>>1));	/* Get E1+(ent>>1) */
	    if (!(vp = vm_xrwmap(va, VMF_READ|VMF_NOTRAP))) {
		/* Page-fail trying to fetch translation table entry */
		xdecbp(&s1);	/* Back up source bp */
		++len1;
		res = RES_PF;
		break;
	    }
	    ent = (ent & 01) ? vm_pgetrh(vp) : vm_pgetlh(vp);
	    switch ((ent >> 15) & 07) {
		case 0:	break;
		case 1: res = RES_TRUNC;	break;
		case 2: flags &= ~CVTF_M;	break;
		case 3: flags |= CVTF_M;	break;
		case 4: flags |= CVTF_S|CVTF_N;	break;
		case 5: flags |= CVTF_N; res = RES_TRUNC; break;
		case 6: flags = CVTF_S|CVTF_N;		break;
		case 7: flags = CVTF_S|CVTF_N|CVTF_M;	break;
	    }
	    if (res != RES_WON)		/* If wants to stop now, */
		break;			/* leave the loop! */
	    dig = ent & 017;		/* Isolate digit in case S is set */
	}

	/* dig now contains the value 0-9.  Add into number so far. */
	if (!transf || (flags & CVTF_S)) {	/* Put digit in? */
	    dw10_t dtmp;

	    /* Verify that it's a 0-9 value! */  
	    if (dig > 9) {
		res = RES_TRUNC;
		break;		/* Abort, digit is outside range */
	    }

	    /* Multiply existing number by 10 and then add digit.
	    ** We do the multiply "by hand" here to avoid extremely slow
	    ** full-blown DMUL, by using fact multiplier is always 10.
	    ** This amounts to (x * 8) + (x * 2) which can be done with
	    ** two shifts and two adds.
	    */
	    dtmp = d;
	    op10m_ashcleft(dtmp, 1);	    /* Multiply by 2 */
	    op10m_ashcleft(d, 3);	    /* Multiply by 8 */

	    /* Add together to produce multiply by 10.  Code depends on
	    ** fact that above macros always clear sign bit of low word.
	    */
	    op10m_add(d.w[0], dtmp.w[0]);	/* Add high words */
	    op10m_add(d.w[1], dtmp.w[1]);	/* Add low words */
	    if (op10m_signtst(d.w[1])) {	/* If low sign now set, */
		op10m_inc(d.w[0]);		/* carry to high word */
		op10m_signclr(d.w[1]);		/* and clear low sign */
	    }
	    /* Now finally add in the digit! */
	    op10m_addi(d.w[1], dig);		/* Add into low word */
	    if (op10m_signtst(d.w[1])) {	/* If low sign now set, */
		op10m_inc(d.w[0]);		/* carry to high word */
		op10m_signclr(d.w[1]);		/* and clear low sign */
	    }
	}

	/* This byte done; if any more, do usual clock/PI check */
	if (len1 <= 0) {
	    res = RES_WON;		/* Source all gone, win! */
	    break;
	}
	CLOCKPOLL();			/* More left, keep clock going */
	if (INSBRKTEST()) {		/* Watch for PI interrupt */
	    res = RES_PI;
	    break;
	}
    }

    /* Left loop, see if it succeeded or if we aborted */
    if (res == RES_WON && (flags & CVTF_M))	/* If won, must negate? */
	op10m_dmovn(d);			/* Yup, do so */

    /* Now propagate high sign to low word.  This has to be done in a
    ** general way since if we started with a pre-loaded value
    ** the sign bits are unpredictable.
    */
    if (op10m_signtst(d.w[0]))
	op10m_signset(d.w[1]);		/* Set low sign */
    else
	op10m_signclr(d.w[1]);		/* Clear low sign */

    ac_setlrh(ac, flags | (len1>>H10BITS), len1 & H10MASK);
    xbpupdate(&s1);			/* Store byte ptr in AC+1,2 */
    ac_dset(ac_off(ac,3), d);		/* Store back binary number */
    
    switch (res) {
    case RES_PF:	pag_fail();	/* Never returns */
    case RES_PI:	apr_int();	/* Never returns */
    case RES_WON:	return PCINC_2;	/* Skips if ate source w/o probs */
    default:
    case RES_TRUNC:	return PCINC_1;
    }
}

/* EDIT - Edit String		(EXTEND [004 ])
**
** Extended KL note:
**	The pattern string and mark addresses must be handled differently
** depending on whether running extended or not.  Test for this is PC
** section; if 0, both mark & pattern are treated as 18-bit addrs, else
** as 30-bit addrs.
**	If using 18-bit addrs, having any high bits set <6:17> is
** undefined, but for the KLH10 I'll have them do a MUUO trap.
** OOPS.  Judging from the diagnostics, it appears that this is not
** what the machine does.  Instead, simply ignore any high bits not
** needed for the address (either 18-bit or 30-bit).  This includes
** the high 6 bits of the mark pointer, even though the PRM says they
** are MBZ.
*/
#define EC_MESSAG	0100
#define EC_SKPM		0500
#define EC_SKPN		0600
#define EC_SKPA		0700
#define EC0_STOP	000
#define EC0_SELECT	001
#define EC0_SIGST	002
#define EC0_FLDSEP	003
#define EC0_EXCHMD	004
#define EC0_NOP		005

xinsdef(ix_edit)
{
    register uint32 pp;		/* Pattern pointer */
    register vaddr_t ppva;	/* Pattern pointer virtual addr */
    register unsigned int ppbn;	/* Byte # within pattern word */
    register vaddr_t mark;	/* Mark pointer */
    register vmptr_t vp;
    register vaddr_t va;	/* Random temporary vaddr_t */
    register int inc;
    h10_t flags, ent;
    struct cleanbp s1, s2;
    w10_t pword, wbyte;
#if KLH10_EXTADR
    vaddr_t mark2;		/* Mark pointer plus 1, for TWGBPs */
#endif
    enum xires res;

    AC_32GET(pp, ac, 0, 0040000);	/* Get pattern addr (B3=0) */
    pp &= MASK30;		/* 30-bit mask */
#if KLH10_EXTADR
    if (PC_ISEXT)
	va_gmake30(ppva, pp);	/* Get vaddr_t from 30-bit value */
    else
#endif
    {
#if 0	/* This turns out not to be checked on the real machine */
	if (pp & (H10MASK<<H10BITS))	/* Not extended, high bits not OK */
	    return i_muuo(I_EXTEND, ac, e0);
#endif
	va_lmake(ppva, 0, pp & H10MASK);
    }
    ent = ac_getlh(ac);
    flags = ent & CVTF_ALL;	/* Find flags */
    ppbn = (ent >> 12) & 03;	/* Find byte # in pattern word */

  {
    register acptr_t acp;

    acp = ac_map(ac_off(ac,3));		/* Get mark pointer and check it */
#if 0	/* Not checked on real machine */
    if (op10m_tlnn(*acp, ILLEG_LHVABITS))
	return i_muuo(I_EXTEND, ac, e0);
#endif
#if KLH10_EXTADR
    if (PC_ISEXT) {
	va_gfrword(mark, *acp);
	mark2 = mark;
	va_ginc(mark2);
    } else
#endif
    {
#if 0	/* Not checked on real machine */
	if (LHGET(*acp))		/* Not extended, high bits not OK */
	    return i_muuo(I_EXTEND, ac, e0);
#endif
	va_lmake(mark, 0, RHGET(*acp));
#if KLH10_EXTADR
	mark2 = mark;
	va_linc(mark2);
#endif
    }
  }

    XBPGET(&s1, ac, 1, BPSRC);	/* Set up byte ptrs, may page-fail */
    XBPGET(&s2, ac, 4, BPDST);

    /* Start loop; leaves it only via goto!
    ** Note we check for PI only when fetching a new command word
    ** rather than after each command byte.  This will delay any PI
    ** by up to 3 edit commands but that should be okay.
    */
    pword = vm_read(ppva);	/* Set up pattern word, may page-fail */
    inc = 0;
    for (;;) {
	/* First increment pattern pointer and fetch command */
	ppbn += inc;
	if (ppbn > 3) {
	    pp += (ppbn >> 2);		/* Increment word ptr */
	    va_add(ppva, (ppbn>>2));
	    ppbn &= 03;			/* Reset byte # */

	    /* Now check for events before doing next */
	    CLOCKPOLL();		/* Keep clock going */
	    if (INSBRKTEST()) {		/* Watch for PI interrupt */
		res = RES_PI;
		goto cleanup;
	    }
	    /* Fetch next word of 4 commands */
	    if (!(vp = vm_xrwmap(ppva, VMF_READ|VMF_NOTRAP))) {
		goto pf_presrc;		/* Take pagefail exit */
	    }
	    pword = vm_pget(vp);	/* Get new pattern word */
	}
	inc = 1;			/* Reset to normal increment */
	switch (ppbn & 03) {
	case 0:	ent = LHGET(pword) >> 9;	break;
	case 1:	ent = LHGET(pword) & 0777;	break;
	case 2:	ent = RHGET(pword) >> 9;	break;
	case 3:	ent = RHGET(pword) & 0777;	break;
	}

	/* Now decode pattern command */
	switch (ent & 0700) {
	case EC_MESSAG:			/* Insert fill-type char */
	    va = e0;
	    if (flags & CVTF_S)
		va_add(va, (ent&077)+1);	/* Get E0+(ent&077)+1 */
	    else
		va_add(va, 1);		/* Get E0+1 */
	    vp = vm_xrwmap(va, VMF_READ|VMF_NOTRAP);
	    if (!vp)
		goto pf_presrc;		/* Take pagefail exit */
	    wbyte = vm_pget(vp);
	    if ((flags & CVTF_S) || op10m_skipn(wbyte)) {
		if (xidpb(&s2, &wbyte))
		    goto pf2_presrc;	/* No backup, take pagefail error */
	    }
	    continue;
	case EC_SKPM:			/* Skip if M set */
	    if (flags & CVTF_M)
		inc = (ent & 077) + 2;	/* Include PP+1 too */
	    continue;
	case EC_SKPN:			/* Skip if N set */
	    if (!(flags & CVTF_N))
		continue;		/* Nope */
	    /* N set, drop through to unconditional skip */
	case EC_SKPA:			/* Skip next n+1 commands */
		inc = (ent & 077) + 2;	/* Include PP+1 too */
	    continue;

	/* Not a recognized class, check entire byte as command */
	default: switch (ent) {
	    case EC0_SELECT:		/* Select Next source byte */
		break;			/* Drop out - complex subcommand */
	    case EC0_SIGST:
		if (flags & CVTF_S)	/* If S already set, */
		    continue;		/* treat as no-op */
		/* Do things in this sequence so if we page-fail nothing
		** has to be backed out of.
		*/
		va = e0;
		va_add(va, 2);	/* Get E0+2 */
		if (!(vp = vm_xrwmap(va, VMF_READ|VMF_NOTRAP)))
		    goto pf_presrc;
		wbyte = vm_pget(vp);	/* Get "float char" */
		if (!(vp = vm_xrwmap(mark, VMF_WRITE|VMF_NOTRAP)))
		    goto pf_presrc;

		xbpupdate(&s2);		/* Store dest BP back in AC+4 */
		vm_pset(vp, ac_get(ac_off(ac,4))); /* Also store at mark */
#if KLH10_EXTADR
		/* May need to set 2nd word of mark, if dest BP is 2-word */
		if (PC_ISEXT) {
		    if (vm_issafedouble(mark))
			vp++;
		    else if (!(vp = vm_xrwmap(mark2, VMF_WRITE|VMF_NOTRAP)))
			goto pf_presrc;
		    vm_pset(vp, ac_get(ac_off(ac,5)));	/* Store 2nd wd */
		}
#endif
		if (op10m_skipn(wbyte)) {	/* If float char non-zero, */
		    if (xidpb(&s2, &wbyte))	/* try storing it */
			goto pf2_presrc;	/* Fail, no backup */
		}
		flags |= CVTF_S;		/* OK to set flag now! */
		continue;
	    case EC0_FLDSEP:	/* Separate fields */
		flags &= ~(CVTF_S|CVTF_M|CVTF_N);
		continue;
	    case EC0_EXCHMD:	/* Exchange Mark and Dest BP */
		/* Find length of dest BP, and assume mark is same
		** For now (non-extended), always assume 1 word.
		** This code can be improved!
		*/
		xbpupdate(&s2);		/* Store dest BP back in AC+4 */
#if KLH10_EXTADR
		/* Gross hair to handle 2-word BPs */
		if (PC_ISEXT) {
		    vmptr_t vp2;
		    if (!(vp = vm_xrwmap(mark, VMF_WRITE|VMF_NOTRAP)))
			goto pf_presrc;
		    if (vm_issafedouble(mark))
			vp2 = vp+1;
		    else if (!(vp2 = vm_xrwmap(mark2, VMF_WRITE|VMF_NOTRAP)))
			goto pf_presrc;
		    wbyte = ac_get(ac_off(ac,5));	/* Save 2nd dst wd */
		    ac_set(ac_off(ac,5), vm_pget(vp2));	/* Set 2nd dst wd */
		    vm_pset(vp2, wbyte);		/* Set 2nd mark wd */
		} else	/* Do normal 1-word case */
#endif
		  {
		    if (!(vp = vm_xrwmap(mark, VMF_WRITE|VMF_NOTRAP)))
			goto pf_presrc;
		  }

		wbyte = ac_get(ac_off(ac,4));		/* Save 1st dst wd */
		ac_set(ac_off(ac,4), vm_pget(vp));	/* Set 1st dst wd */
		vm_pset(vp, wbyte);			/* Set 1st mark wd */

		/* Set up bp for internal use, fail if error result
		** (most likely MUUO for bogus OWGBP, if extended)
		*/
		if (res = xbpinit(&s2, ac_off(ac,4), BPDST)) {
		    /* Note: Not sure if this is correct behavior, since at
		    ** this point the AC has already been clobbered, and the
		    ** rest of the world is probably not in a very good
		    ** state either.  Perhaps try to restore from saved BP?
		    */
		    goto cleanup;
		}
		continue;

	    default:
		/* NOTE!  Unrecognized pattern bytes are simply treated
		** as NOPs.
		** Fall through to handle like NOP.
		*/
	    case EC0_NOP:
		continue;
	    case EC0_STOP:	/* Stop Edit */
		res = RES_WON;
		if (++ppbn > 3)		/* Increment PP one last time */
		    ppbn = 0, ++pp, va_inc(ppva);
		goto cleanup;
	    }
	}

	/* Drop through to handle SELECT command
	** Because the source string BP is incremented here, any pagefail
	** after a successful xildb must jump to pf_postsrc instead
	** of pf_presrc.
	*/
	if (xildb(&s1, &wbyte)) {	/* Get source byte */
	    res = s1.err;		/* oops */
	    goto cleanup;		/* no backup, not incremented yet */
	}

	/* Carry out translation-type subcommand */
	ent = RHGET(wbyte);
	va = e1;
	va_add(va, (ent>>1));	/* Get E1 + (ent>>1) */
	if (!(vp = vm_xrwmap(va, VMF_READ|VMF_NOTRAP))) {
	    goto pf_postsrc;	/* Page fail, back up source BP */
	}

	/* Do translation (similar to CVTDBT) */
	ent = (ent & 01) ? vm_pgetrh(vp) : vm_pgetlh(vp);
	switch ((ent >> 15) & 07) {
	case 2:
	    flags &= ~CVTF_M;
	    if (1) ;		/* Hack to skip over next stmt */
	    else {
	case 3:
		flags |= CVTF_M;
	    }
	case 0:
	    if (flags & CVTF_S)
		LRHSET(wbyte, 0, ent & 07777);	/* Set up translated byte */
	    else {				/* Get fill byte, check it */
		va = e0;
		va_inc(va);			/* Get E0+1 */
		if (!(vp = vm_xrwmap(va, VMF_READ|VMF_NOTRAP)))
		    goto pf_postsrc;
		wbyte = vm_pget(vp);
		if (!op10m_skipn(wbyte))	/* If fill byte zero, */
		    continue;			/* do nothing */
	    }
	    if (xidpb(&s2, &wbyte))	/* Try storing it */
		goto pf2_postsrc;	/* Ugh, back out and fail */
	    continue;

	case 5:
	    flags |= CVTF_N;
	    /* Drop thru to terminate edit */
	case 1:
	    res = RES_TRUNC;
	    if (++ppbn > 3)	/* Increment PP one last time */
		ppbn = 0, ++pp, va_inc(ppva);
	    goto cleanup;

	case 6:
	    flags &= ~CVTF_M;
	    if (1) ;		/* Hack to skip over next stmt */
	    else {
	case 7:
		flags |= CVTF_M;
	    }
	case 4:
	    flags |= CVTF_N;
	    if (!(flags & CVTF_S)) {
		/* These are exactly the same actions as SIGST!! */
		/* Do things in this sequence so if we page-fail nothing
		** (other than source BP) has to be backed out of.
		*/
		va = e0;
		va_add(va, 2);		/* Get E0+2 */
		if (!(vp = vm_xrwmap(va, VMF_READ|VMF_NOTRAP)))
		    goto pf_postsrc;
		wbyte = vm_pget(vp);	/* Get "float char" */
		if (!(vp = vm_xrwmap(mark, VMF_WRITE|VMF_NOTRAP)))
		    goto pf_postsrc;

		xbpupdate(&s2);		/* Store dest BP back in AC+4 */
		vm_pset(vp, ac_get(ac_off(ac,4))); /* Also store at mark */
#if KLH10_EXTADR
		/* May need to set 2nd word of mark, if dest BP is 2-word */
		if (PC_ISEXT) {
		    if (vm_issafedouble(mark))
			vp++;
		    else if (!(vp = vm_xrwmap(mark2, VMF_WRITE|VMF_NOTRAP)))
			goto pf_postsrc;
		    vm_pset(vp, ac_get(ac_off(ac,5)));	/* Store 2nd wd */
		}
#endif
		if (op10m_skipn(wbyte)) {	/* If float char non-zero, */
		    if (xidpb(&s2, &wbyte))	/* try storing it */
			goto pf2_postsrc;	/* No backup needed. */
		}
		flags |= CVTF_S;		/* OK to set flag now! */
	    }
	    LRHSET(wbyte, 0, ent & 07777);	/* Set up translated byte */
	    if (xidpb(&s2, &wbyte))		/* Try storing it */
		goto pf2_postsrc;
	    continue;
	}
	/* Should never come here */
    }

    /* Common return points for pagefails.  Control never drops or breaks
    ** out of main loop except with gotos.
    */
pf2_postsrc:
    xdecbp(&s1);	/* Back up source bp */
pf2_presrc:
    res = s2.err;	/* Get specific error result from S2 ref */
    goto cleanup;

pf_postsrc:
    xdecbp(&s1);	/* Back up source bp */
pf_presrc:		/* No backup needed */
    res = RES_PF;

cleanup:
    /* Done, update ACs and return appropriate PC increment
    */
    ac_setlrh(ac, flags | ((uint18)ppbn << 12) | (pp>>H10BITS), pp & H10MASK);
    xbpupdate(&s1);	/* Set AC+1, AC+2 */
    /* AC+3: Mark address is never changed, so needn't store it back */
    xbpupdate(&s2);	/* Set AC+4, AC+5 */

    switch (res) {
    case RES_MUUO:	return i_muuo(I_EXTEND, ac, e0);
    case RES_PF:	pag_fail();	/* Never returns */
    case RES_PI:	apr_int();	/* Never returns */
    case RES_WON:	return PCINC_2;	/* Skips if source exhausted */
    default:
    case RES_TRUNC:	return PCINC_1;
    }
}

#endif	/* T10 || T20 */
