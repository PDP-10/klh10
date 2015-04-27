/* KN10OPS.C - PDP-10 arithmetic operations
*/
/* $Id: kn10ops.c,v 2.3 2001/11/10 21:28:59 klh Exp $
*/
/*  Copyright © 1991, 1992, 1993, 2001 Kenneth L. Harrenstien
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
 * $Log: kn10ops.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*	Contains functions to provide 36-bit PDP-10 arithmetic operations
**	on a machine which does not support them.
**
** KN10OPS is primarily intended for use by the KLH10 PDP-10 emulator, but
** can also be used by KCC (PDP-10 C compiler) or other programs which want
** to emulate PDP-10 arithmetic operations.  Note there are a number of
** artifacts defined here for KCC which aren't needed by the KLH10.
**
** Supported PDP-10 machine ops are "op10*()" where * is:
**	AND/IOR/XOR/SETCM		Single word logical operations
**	MOVN/DMOVN			Negation
**	LSH/LSHC/ASH/ASHC/ROT/ROTC	Shifting
**	ADD/SUB/MUL/DIV/IDIV		Single precision fixed point
**	DADD/DSUB/DMUL/DDIV		Double precision fixed point
**	FAD/FSB/FMP/FDV			Single precision floating point
**	FADR/FSBR/FMPR/FDVR		Single precision floating point, Round
**	DFAD/DFSB/DFMP/DFDV		Double precision floating point
** plus:
**	NEG/DNEG	Negation, single & double fixed point
**	INC/DINC	Increment, single & double fixed point
**	CMP/DCMP	Comparison (CAM*) single & double
**	TST/DTST	Testing (SKIP*)
*/

/* By default this file assumes it is being compiled in its own right
** as a module for the KLH10 emulator program.
**
** However, it can also be included in another file to provide other
** programs with their own interface to PDP-10 operation functions.
** Examples of this are KCC (PDP-10 C Compiler) and OPTEST (a program
** to test these functions).
**	To do this, the macro OP10_INCLUDEFLAG must be defined prior to
**	inclusion.
**
** Note: KCC should only include this if being built on a non-10 platform
** as a cross-compiler to generate PDP-10 code.
*/

#ifndef OP10_INCLUDEFLAG
  /* Assume being compiled for KLH10 as its own module */
# include "klh10.h"	/* KLH10 Config params */
# include "kn10def.h"	/* Emulated machine defs */

#ifdef RCSID
 RCSID(kn10ops_c,"$Id: kn10ops.c,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

# if 0			/* FYI: OP10_PCFSET definition from klh10.h */
#  define OP10_PCFSET(f) (cpu.mr_pcflags |= (f), \
	(((f)&(PCF_TR1|PCF_TR2)) ? INSBRKSET() : 0))
# endif

# if 0 /* KLH10_DEBUG */ /* OPTEST.C much better for debugging now */
    int op10debug = 0;
#  define OP10_DBGPRINT(list) if (op10debug) printf list
#  include <stdio.h>
# endif

    /* Determine what values are OK for IDIVI. */
# if KLH10_CPU_KLX		/* KLX accepts +1 */
#  define OP10_IDIVI_OK(w) op10m_skipg(w)
# elif KLH10_CPU_KS		/* KS accepts both +1 and -1 */
#  define OP10_IDIVI_OK(w) op10m_skipn(w)
# endif

    /* Determine whether to include G format instructions */
# ifndef OP10_GFMT
#  define OP10_GFMT KLH10_CPU_KL
# endif

    /* Determine behavior of FDV */
# ifndef OP10_FDV_KL
#  define OP10_FDV_KL (KLH10_CPU_KL || KLH10_CPU_KS)
# endif

#endif

/* Includer-defined macros.  The following should be defined appropriately
** by either kn10def.h or the includer, if desired.  They will default
** to something reasonable if not defined.
**
** OP10_PCFSET(flgs) - Must be defined if the facilities here are to return
**	PC flag results; the kn10ops.h file also checks for this in order to
**	define various things.
**	If not defined, no flag operations are compiled.
** OP10_DBGPRINTF(list) - Defined if debug output capability is desired.
**	If not defined, no debug code is compiled.
** OP10_GFMT - Defined 1 to say that G format ops should be included.
**	If 0 or undefined, none are included.
** OP10_KCCOPS - Defined 1 to include emulation of KCC C ops.
**	If 0 or undefined, not included.
** OP10_IDIVI_OK(w)  - Defined to specify action on a failing IDIVI; test
**	is made on divisor and returns TRUE if IDIVI is OK (ie should not
**	set no-divide and trap flags).
**	If not defined, acts like KA/KI and always fails for cases of 1 and +1.
** OP10_FDV_KL - Defined 1 to specify KL+KS behavior on FDV.
**	If 0 or undefined, uses KA+KI behavior.
*/

#include "kn10ops.h"	/* Get defs and macro facilities */

/* Resolve any defaults for OP10_ feature request macros */

#ifndef OP10_PCFSET		/* If not bothering with flags, */
# define IFFLAGS 0
# define OP10_PCFSET(a)		/* make this be no-op */
#else				/* Ah, wants flags! */
# define IFFLAGS 1
#endif

/* Debug output.  Invoke with nested parens, eg DEBUGPRF(("format", args)); */
#ifndef OP10_DBGPRINT
# define DEBUGPRF(list)		/* No debugging, so make no-op */
#else
# define DEBUGPRF(list) OP10_DBGPRINT(list)
# define DBGV_W(w) (long)LHGET(w),(long)RHGET(w)
# define DBGV_D(d) DBGV_W((d).HI),DBGV_W((d).LO)
# define DBGV_Q(q) DBGV_D((q).D0),DBGV_D((q).D1)
#endif

#ifndef OP10_GFMT		/* Default is not to include G-fmt stuff */
# define OP10_GFMT 0
#endif
#ifndef OP10_KCCOPS		/* Default is not to include KCC stuff */
# define OP10_KCCOPS 0
#endif


/* Determine whether DIV() or LDIV() is available from C */
#if defined(__STDC__) && __STDC__
# include <stdlib.h>		/* Get div, ldiv */

# ifdef WORD10_ITX32	/* Try to define DIV32 operation and result */
#  if WORD10_ITX32 == WORD10_TX_INT
#   define OP10_DIV32(n,d) div(n,d)
#   define OP10_DIV32T	div_t
#  elif WORD10_ITX32 == WORD10_TX_LONG
#   define OP10_DIV32(n,d) ldiv(n,d)
#   define OP10_DIV32T	ldiv_t
#  endif
# endif

# ifdef WORD10_ITX64	/* Try to define DIV64 operation and result */
#  if WORD10_ITX64 == WORD10_TX_INT
#   define OP10_DIV64(n,d) div(n,d)
#   define OP10_DIV64T	div_t
#  elif WORD10_ITX64 == WORD10_TX_LONG
#   define OP10_DIV64(n,d) ldiv(n,d)
#   define OP10_DIV64T	ldiv_t
#  endif
# endif

#endif

/* Defs for compatibility with older versions of this code */
#define HBITS H10BITS		/* Number of bits in halfword */
#define HSIGN H10SIGN		/* Sign bit for halfword */
#define HMASK H10MASK		/* Halfword Mask */
#define OP10XWD(l,r) XWDINIT(l,r)
#define OP10DXWD(h0,h1,h2,h3) DXWDINIT(h0,h1,h2,h3)
#define OP10QXWD(h0,h1,h2,h3,l0,l1,l2,l3) QXWDINIT(h0,h1,h2,h3,l0,l1,l2,l3)
#define HI w[0]
#define LO w[1]
#define D0 d[0]
#define D1 d[1]

/* Exported data */
w10_t w10one = OP10XWD(0,1);
w10_t w10mask = OP10XWD(HMASK,HMASK);	/* Word of all ones */
w10_t w10zero = OP10XWD(0,0);		/* Word of all zeros */
dw10_t dw10zero = OP10DXWD(0,0,0,0);

/* Internal data */
static w10_t w10maxneg = OP10XWD(HSIGN,0);
static dw10_t dw10one = OP10DXWD(0,0,0,1);
static dw10_t dw10maxneg = OP10DXWD(HSIGN,0,HSIGN,0); /* FixPt */
static qw10_t qw10maxneg = OP10QXWD(HSIGN,0,HSIGN,0,	   /* FixPt */
				    HSIGN,0,HSIGN,0);


static w10_t w10loobits[37] = {
# define lmask(n) (((uint18)1<<(n))-1)
# define WBMLO(n) W10XWD(0,lmask(n))
# define WBMHI(n) W10XWD(lmask((n)-18),H10MASK)

	WBMLO(0),  WBMLO(1),  WBMLO(2),  WBMLO(3),  WBMLO(4),  WBMLO(5),
	WBMLO(6),  WBMLO(7),  WBMLO(8),  WBMLO(9),  WBMLO(10), WBMLO(11),
	WBMLO(12), WBMLO(13), WBMLO(14), WBMLO(15), WBMLO(16), WBMLO(17),

	WBMHI(18), WBMHI(19), WBMHI(20), WBMHI(21), WBMHI(22), WBMHI(23),
	WBMHI(24), WBMHI(25), WBMHI(26), WBMHI(27), WBMHI(28), WBMHI(29),
	WBMHI(30), WBMHI(31), WBMHI(32), WBMHI(33), WBMHI(34), WBMHI(35),

	W10XWD(H10MASK, H10MASK)	/* Entry 36 is all ones */
# undef WBMLO
# undef WBMHI
# undef lmask
};

#if 0	/* Not needed at moment */

static w10_t w10hiobits[37] = {
# define amask(b) ((((uint18)1<<(HBITS-(b)))-1) ^ HMASK)
# define WBMLO(n) W10XWD(amask(n), 0)
# define WBMHI(n) W10XWD(H10MASK,amask((n)-18))

	WBMLO(0),  WBMLO(1),  WBMLO(2),  WBMLO(3),  WBMLO(4),  WBMLO(5),
	WBMLO(6),  WBMLO(7),  WBMLO(8),  WBMLO(9),  WBMLO(10), WBMLO(11),
	WBMLO(12), WBMLO(13), WBMLO(14), WBMLO(15), WBMLO(16), WBMLO(17),

	WBMHI(18), WBMHI(19), WBMHI(20), WBMHI(21), WBMHI(22), WBMHI(23),
	WBMHI(24), WBMHI(25), WBMHI(26), WBMHI(27), WBMHI(28), WBMHI(29),
	WBMHI(30), WBMHI(31), WBMHI(32), WBMHI(33), WBMHI(34), WBMHI(35),

	W10XWD(H10MASK, H10MASK)	/* Entry 36 is all ones */
# undef WBMLO
# undef WBMHI
};
static w10_t w10hizbits[37] = {
# define smask(b) (((uint18)H10MASK)>>(b))
# define WBMLO(n) W10XWD(smask(n), H10MASK)
# define WBMHI(n) W10XWD(0,smask((n)-18))

	WBMLO(0),  WBMLO(1),  WBMLO(2),  WBMLO(3),  WBMLO(4),  WBMLO(5),
	WBMLO(6),  WBMLO(7),  WBMLO(8),  WBMLO(9),  WBMLO(10), WBMLO(11),
	WBMLO(12), WBMLO(13), WBMLO(14), WBMLO(15), WBMLO(16), WBMLO(17),

	WBMHI(18), WBMHI(19), WBMHI(20), WBMHI(21), WBMHI(22), WBMHI(23),
	WBMHI(24), WBMHI(25), WBMHI(26), WBMHI(27), WBMHI(28), WBMHI(29),
	WBMHI(30), WBMHI(31), WBMHI(32), WBMHI(33), WBMHI(34), WBMHI(35),

	W10XWD(0, 0)	/* Entry 36 is all zeros */
# undef WBMLO
# undef WBMHI
};

/* Halfword bit masks */

static uint18 h10hiobits[H10BITS+1] = {
	amask(0), amask(1), amask(2), amask(3), amask(4), amask(5),
	amask(6), amask(7), amask(8), amask(9), amask(10), amask(11),
	amask(12), amask(13), amask(14), amask(15), amask(16), amask(17),
	H10MASK		/* Entry 18 is all ones */
};

#endif /* 0 */

/* Internal routines
**	Routines called x_name() are provided in order to carry out
**	arithmetic operations without the side-effects (e.g. flag setting)
**	of an actual instruction.
*/
static int adffo(dw10_t);	/* Arith Double FFO */
static qw10_t x_qneg(qw10_t);	/* Negate quadword */
static qw10_t x_qash1(qw10_t);	/* pseudo-ASH quadword left by 1 */
static int x_div(dw10_t *, w10_t);	/* Aux for single division */
static int ddivstep(dw10_t *, w10_t, int, int);	/* " " "     "     */
static dw10_t op10dfinc(dw10_t);	/* Increment DF int */
static qw10_t x_dmul(dw10_t, dw10_t);	/* For unsigned-double-int mult */
static w10_t ffadr(w10_t, w10_t, int);	/* Aux for FAD/FSB/FADR/FSBR */
static w10_t ffmpr(w10_t, w10_t, int);	/* Aux for FMP/FMPR */
static w10_t sfnorm(int, w10_t, int);	/* Float & normalize single-prec int */
static dw10_t dfad(dw10_t, dw10_t, int);	/* Aux for DFAD/DFSB */
static int qdivstep(qw10_t *, dw10_t, int);	/* For double division */

#if IFFLAGS
static void x_ashflg(w10_t, int);
static w10_t x_ash(w10_t, int);
static dw10_t x_ashc(dw10_t, int);
static dw10_t x_dadd(dw10_t, dw10_t);
static dw10_t x_dsub(dw10_t, dw10_t);
#else
# define x_ash(w,h)  op10ash(w, (h10_t)(long)imm8op(h))
# define x_ashc(d,h) op10ashc(d, (h10_t)(long)imm8op(h))
# define x_dadd(d,e) op10dadd(d,e)
# define x_dsub(d,e) op10dsub(d,e)
#endif


/* Certain instructions use a signed 8-bit immediate operand,
**	specifically ASH/C, LSH/C, ROT/C, and FSC.
** The sign bit is bit 18 (high bit of RH), applied to the low 8 bits.
** Note that a negative-signed 0 becomes -256, not 0!
** Also, these macros explicitly return an "int" result.  This is
** important as they are used to convert h10_t values into ints.
*/
#define imm8op(h)  (((h)&HSIGN) ? ((int)(h)|~0377) : ((int)(h)&0377))
#define imm8neg(h) (-((int)(h)|~0377))	/* Negate when known negative */


/* Simple macros, mainly for backward compatibility.
**	wskipl(w) - TRUE if word is negative (sign bit set).
**	wskipn(w) - TRUE if any bits in word are set.
**	wmagskipn(w) - Similar but ignores sign bit, checks magnitude only.
**	dskipn(d) - TRUE if any bits in doubleword are set
**	dlomagskipn(d) - Similar but ignores LOW word sign bit; HI sign
**			is still checked.
*/
#define wskipl(w)	op10m_skipl(w)
#define wskipn(w)	op10m_skipn(w)
#define wmagskipn(w)	op10m_magstst(w)
#define dskipn(d)	(wskipn((d).HI) || wskipn((d).LO))
#define dlomagskipn(d)	(wskipn((d).HI) || wmagskipn((d).LO))

/* Simple auxiliaries, primarily for KCC */

/* Convert unsigned base integer to w10_t */
w10_t op10utow(uint32 u)
{
    register w10_t w;

    LRHSET(w, ((u >> H10BITS) & H10MASK), (u & H10MASK));
    return w;
}

/* Convert w10_t to signed base integer */
int32 op10wtos(register w10_t w)
{
    return ((((int32)LHGET(w))|(~H10MASK)) << HBITS) | RHGET(w);
}


#if 0 /* Not used, and incorrect anyway */

w10_t op10stow(s)	/* Convert signed base integer to w10_t */
int32 s;		/* Defaults to signed! */
{
    register w10_t w;
    RHSET(w, s & HMASK);
    LHSET(w, (s >> HBITS) & HMASK);
    return w;
}
#endif


/* JFFO-type routines.
**	A return value of 0 means bit 0 (sign).
**	A value of 36 (or 72) means no bit found.
*/
/* Find First One.  Note returns int, not word! */
int op10ffo(register w10_t w)
{
    register int i;
    register uint18 reg;		/* Need unsigned for shifting */

    if (reg = LHGET(w)) i = 17;		/* Find right halfword and set up */
    else if (reg = RHGET(w)) i = 17+18;
    else return 36;

    while (reg >>= 1) --i;
    return i;

#if 0	/* Old version of this code */
    register uint32 reg;		/* Need unsigned for shifting */
    if (reg = LHGET(w)) i = 0;		/* Find right halfword and set up */
    else if (reg = RHGET(w)) i = 18;
    else return 36;
    while (!((reg <<= 1) & (H10SIGN<<1))) ++i;	/* Shift until found bit */
    return i;
#endif
}

/* ADFFO - Arithmetic Double Find First One.
**	IGNORES the low word sign bit, regardless of its setting.
**	Returns # of bits between high end and first one-bit.
** A return value of:
**	0    - sign bit was set
**	>=36 - high word clear, bit is in low word magnitude
**	>=71 - No bits were set in high word or non-sign bits of low word.
*/
static int adffo(register dw10_t d)
{
    register int i;
    register uint18 reg;		/* Need unsigned for shifting */

    if (reg = LHGET(d.HI)) i = 17;	/* Find right halfword and set up */
    else if (reg = RHGET(d.HI)) i = 17+18;
    else if (reg = (LHGET(d.LO)&H10MAGS)) i = 17+36-1;
    else if (reg = RHGET(d.LO)) i = 17+36+18-1;
    else return 36+36-1;

    while (reg >>= 1) --i;
    return i;
}

/* Logical operations and ones-complement negation */

w10_t op10and(register w10_t a, register w10_t b)
{
    op10m_and(a,b);
    return a;
}

w10_t op10ior(register w10_t a, register w10_t b)
{
    op10m_ior(a,b);
    return a;
}

w10_t op10xor(register w10_t a, register w10_t b)
{
    op10m_xor(a,b);
    return a;
}

w10_t op10setcm(register w10_t w)
{
    op10m_setcm(w);
    return w;
}

/* Negation - Single and Double */

/* MOVM - for completeness.  Returns magnitude, sets flags if
**	trying to make the max negative # positive.
*/
w10_t op10movm(register w10_t w)
{
    return op10m_skipl(w) ? op10movn(w) : w;
}

/* MOVN - If arg 0, set CRY0+CRY1.  If SETZ, set TRP1+OV+CRY1.
*/
w10_t op10movn(register w10_t w)
{
#if IFFLAGS
    op10mf_movn(w);
#else
    op10m_movn(w);
#endif
    return w;
}

/* DMOVN - Note that bit 0 of low word is always cleared, to follow
**	double floating-point convention.  Use DNEG to follow
**	the double fixed-point convention instead.
**	As for MOVN, if arg 0, set CRY0+CRY1; if SETZ, set TRP1+OV+CRY1.
*/
dw10_t
op10dmovn(register dw10_t d)
{
#if IFFLAGS
    op10mf_dmovn(d);
#else
    op10m_dmovn(d);
#endif
    return d;
}

/* QNEG - artificial "instr" to negate a quad-length fixed-point
**	value.  Follows fixed-point convention of copying sign bit
**	to low-order words.
*/
static qw10_t x_qneg(register qw10_t q)
{
    op10m_movn(q.D1.LO);	/* Negate lowest word */
    if (wmagskipn(q.D1.LO)) {
	op10m_setcm(q.D1.HI);	/* Low word has stuff, no carry into high */
	op10m_setcm(q.D0.LO);
	op10m_setcm(q.D0.HI);
    } else {
	op10m_movn(q.D1.HI);	/* Low word clear, so carry into high word */
	if (wmagskipn(q.D1.HI)) {	/* If has stuff, */
	    op10m_setcm(q.D0.LO);	/* no carry into higher words */
	    op10m_setcm(q.D0.HI);
	} else
	    op10m_dmovn(q.D0);	/* Lower word clear, so carry up */
    }
    /* Now make sign bit uniform */
    if (wskipl(q.D0.HI)) {
	op10m_signset(q.D0.LO);
	op10m_signset(q.D1.HI);
	op10m_signset(q.D1.LO);
    } else {
	op10m_signclr(q.D0.LO);
	op10m_signclr(q.D1.HI);
	op10m_signclr(q.D1.LO);
    }
    return q;
}

/* Shift instructions (Logical and Arithmetic) */

#ifndef OP10_USEMAC
#  define OP10_USEMAC 1
#endif
#if OP10_USEMAC
#  define LSHIFTM(w,s) op10m_lshift(w,s)
#  define RSHIFTM(w,s) op10m_rshift(w,s)
#else
#  define LSHIFTM(w,s) ((w) = op10lsh(w,s))
#  define RSHIFTM(w,s) ((w) = op10lsh(w,-(s)))
#endif


/* Logical shift */

w10_t op10lsh(register w10_t w,
	      register h10_t h)
{
    if (h & HSIGN) {		/* Right shift */
	h = imm8neg(h);		/* Get magnitude (was neg, so > 0) */
#if OP10_USEMAC
	op10m_rshift(w, h);
#else
	if (h > HBITS) {
	    if (h > HBITS*2) w.rh = 0;
	    else w.rh = (w.lh >> (h-HBITS));
	    w.lh = 0;
	} else {
	    w.rh = (w.rh >> h) | ((w.lh << (HBITS-h)) & HMASK);
	    w.lh >>= h;
	}
#endif
    } else {		/* Left shift */
	if (!(h &= 0377)) ;		/* Do nothing if 0 */
	else
#if OP10_USEMAC
	  op10m_lshift(w, h);
#else
	  if (h > HBITS) {
	    if (h > HBITS*2) w.lh = 0;
	    else w.lh = (w.rh << (h-HBITS)) & HMASK;
	    w.rh = 0;
	} else {
	    w.lh = ((w.lh << h)&HMASK) | (w.rh >> (HBITS-h));
	    w.rh = (w.rh << h) & HMASK;
	}
#endif
    }
    return w;
}

/* Double Logical shift */

dw10_t op10lshc(register dw10_t d,
		register h10_t h)
{
    if (h & HSIGN) {	/* Right shift */
	h = imm8neg(h);			/* Get magnitude (was neg, so > 0) */
	if (h >= HBITS*2) {
	    if (h > HBITS*4) op10m_setz(d.LO);
	    else {
		d.LO = d.HI;		/* Set up for macro */
		h -= HBITS*2;		/* See how much farther to go */
		RSHIFTM(d.LO, h);	/* Right-shift remaining distance */
	    }
	    op10m_setz(d.HI);
	} else {
	    register w10_t tmp;
	    tmp = d.HI;			/* Save old value of high wd */
	    RSHIFTM(d.HI, h);		/* Right-shift high by amount */
	    RSHIFTM(d.LO, h);		/* ditto for low wd */
	    h = HBITS*2 - h;		/* Find left-shift for lost hi bits */
	    LSHIFTM(tmp, h);		/* Move into position for IOR */
	    op10m_ior(d.LO, tmp);	/* IOR the bits back in */
	}
    } else {
	if (!(h &= 0377)) ;		/* Pos, do nothing if 0 */
	else if (h >= HBITS*2) {
	    if (h >= HBITS*4) op10m_setz(d.HI);
	    else {
		d.HI = d.LO;		/* Set up for macro */
		h -= HBITS*2;
		LSHIFTM(d.HI, h);	/* Do positive left-shift of word */
	    }
	    op10m_setz(d.LO);
	} else {
	    register w10_t tmp;
	    tmp = d.LO;			/* Save old val of low wd */
	    LSHIFTM(d.HI, h);		/* Shift high word left in place */
	    LSHIFTM(d.LO, h);		/* Shift low word left in place */
	    h = HBITS*2 - h;		/* Find right-shift for lost hi bits */
	    RSHIFTM(tmp, h);		/* Move into position for IOR */
	    op10m_ior(d.HI, tmp);	/* IOR the bits back */
	}
    }

    return d;
}


/* ASH, ASHC - If any bits of significance (a 1 if positive, 0 if negative)
**	are shifted out, set overflow and Trap 1.
*/

/* Full-word null-bit masks, indexed by # bits for a shift
**	Entry 1 has high bit set, 2 has high 2 bits, etc.
*/

static w10_t ashwmask[37] = {
# define amask(b) ((((uint18)1<<(HBITS-(b)))-1) ^ HMASK)
# define WBMLO(n) W10XWD(amask(n), 0)
# define WBMHI(n) W10XWD(H10MASK,amask((n)-18))

	WBMLO(0),  WBMLO(1),  WBMLO(2),  WBMLO(3),  WBMLO(4),  WBMLO(5),
	WBMLO(6),  WBMLO(7),  WBMLO(8),  WBMLO(9),  WBMLO(10), WBMLO(11),
	WBMLO(12), WBMLO(13), WBMLO(14), WBMLO(15), WBMLO(16), WBMLO(17),

	WBMHI(18), WBMHI(19), WBMHI(20), WBMHI(21), WBMHI(22), WBMHI(23),
	WBMHI(24), WBMHI(25), WBMHI(26), WBMHI(27), WBMHI(28), WBMHI(29),
	WBMHI(30), WBMHI(31), WBMHI(32), WBMHI(33), WBMHI(34), WBMHI(35),

	W10XWD(H10MASK, H10MASK)	/* Entry 36 is all ones */
};

/* Macro to do right-shift ASH of negative number, when shift is
**	known to be 0 <= s <= 35
*/
#define NASH_RSHIFTM(w, s) \
	(RSHIFTM(w, s), op10m_ior(w, (ashwmask+1)[s]))

#if IFFLAGS
	/* Similar table, but for halfwords only */
static int32 ashmask[HBITS] = {
	amask(0), amask(1), amask(2), amask(3), amask(4), amask(5),
	amask(6), amask(7), amask(8), amask(9), amask(10), amask(11),
	amask(12), amask(13), amask(14), amask(15), amask(16), amask(17)
};
#endif /* IFFLAGS */
#undef amask


#if IFFLAGS
/* Auxiliary to set flags if shift will lose a bit */

static void
x_ashflg(register w10_t w,
	 register int shift)
{
    if (wskipl(w)) {		/* If negative, */
	if (shift > 35) {	/* see if shifting entire word */
	    /* Always fail here, cuz 0-bits coming in from the right will
	    ** trigger overflow when they leave bit 1 of a negative number.
	    */
	    OP10_PCFSET(PCF_ARO|PCF_TR1);
	    return;
	}
	op10m_setcm(w);		/* invert so looking for 1-bits */
    }
    if (shift >= 35) {
	if (!wskipn(w))
	    return;
    } else if (shift >= 17) {
	if (!LHGET(w) && !(RHGET(w) & ashmask[shift-17]))
	    return;
    } else if (!(LHGET(w) & ashmask[shift+1]))
	return;
    OP10_PCFSET(PCF_ARO|PCF_TR1);
}
#endif /* IFFLAGS */

/* Arithmetic shift */

w10_t op10ash(register w10_t w, register h10_t h)
{
    register int i = imm8op(h);

#if IFFLAGS
    if (i > 0)
	x_ashflg(w, i);	/* Shifting left, so check for overflow */
    return x_ash(w, i);
}

/* Arithmetic shift */

static w10_t x_ash(register w10_t w,
		   register int i)
{
#endif /* IFFLAGS */
    register int32 r;	/* Must be signed! */

    if (i >= 0) {		/* Left shift -- similar to logical */
	i &= 0377;
	r = wskipl(w);		/* Remember sign bit */
	LSHIFTM(w, i);		/* Do the shift */
	if (r) op10m_signset(w);	/* Set sign to same as before */
	else op10m_signclr(w);
	return w;
    }
    /* Do right shift */
    i = -i;			/* Get magnitude (was neg, so > 0) */
    if ((r = LHGET(w)) & HSIGN)
	r |= ~HMASK;		/* Extend sign bit of LH */
    if (i >= HBITS) {
	if (i > (HBITS*2-1)) i = HBITS*2-1;
	RHSET(w, ((r >> (i-HBITS)) & HMASK));
	LHSET(w, (wskipl(w) ? HMASK : 0));
    } else {
	RHSET(w, ((RHGET(w) >> i) | ((LHGET(w) << (HBITS-i)) & HMASK)));
	LHSET(w, ((r >> i) & HMASK));
    }
    return w;
}


/* Double Arithmetic shift */

dw10_t op10ashc(register dw10_t d,
		register h10_t h)
{
    register int i = imm8op(h);

#if IFFLAGS
    if (i > 0) {
	if (i > 35) {
	    /* If high word will be completely lost, special-case it */
	    i -= 35;			/* Adjust for remaining code */
	    if (wskipl(d.HI)) {
		op10m_signset(d.LO);	/* Must copy hi sign */
		op10m_setcm(d.HI);
	    } else
		op10m_signclr(d.LO);	/* Must copy hi sign */

	    /* Now test high and low words for losing non-sign bits */
	    if (wskipn(d.HI))		/* Losing from high? */
		OP10_PCFSET(PCF_ARO|PCF_TR1);
	    else
		x_ashflg(d.LO, i);	/* If not, see if losing from low */

	    /* Flag testing done, do the shift */
	    d.HI = x_ash(d.LO, i);	/* Shift, plop result into high */
	    op10m_tlz(d.LO, H10MAGS);	/* Clear low except for sign */
	    RHSET(d.LO, 0);
	    return d;			/* and that's it */
	}
	x_ashflg(d.HI, i);	/* Losing less than a word, just check high */
    }
    return x_ashc(d, i);	/* Do negative or within-high positive shift */
}

/* Internal ASHC.
**	NOTE!!!  Shift argument has type signed int (not halfword!)
**	and no modulo operation is applied to it.  If one is needed it
**	must be done to the argument prior to the call!
*/
static dw10_t
x_ashc(register dw10_t d,
       register int i)
{
#endif /* IFFLAGS */

    register int r;

    if (i >= 0) {		/*  Left shift -- similar to logical */
	if (i == 0) return d;	/* If no shift, no change to operand */
	r = wskipl(d.HI);		/* Remember sign bit */
	if (i >= (HBITS*2-1)) {
	    if (i >= (HBITS*4)-2) op10m_setz(d.HI);
	    else {
		i -= (HBITS*2-1);
		d.HI = d.LO;		/* Set up for macro */
		LSHIFTM(d.HI, i);	/* Do left-shift */
	    }
	    op10m_setz(d.LO);
	} else {
	    op10m_signclr(d.LO);	/* Ensure low sign is clear */
	    d.HI = op10ior(op10lsh(d.HI, i), op10lsh(d.LO, i-(HBITS*2-1)));
	    LSHIFTM(d.LO, i);		/* Shift left in place */
	}
	if (r) {			/* Restore original sign */
	    op10m_signset(d.HI);
	    op10m_signset(d.LO);
	} else {
	    op10m_signclr(d.HI);
	    op10m_signclr(d.LO);
	}
    } else {		/* Right shift -- bring in sign bit */
	r = wskipl(d.HI);		/* Remember sign bit */
	i = -i;				/* Get magnitude */
	if (i > (HBITS*2-1)) {
	    if (i > (HBITS*4-2)) d.LO = r ? w10mask : w10zero;
	    else d.LO = x_ash(d.HI, (HBITS*2-1)-i);
	    d.HI = r ? w10mask : w10zero;
	} else {
	    op10m_signclr(d.LO);	/* Ensure low sign is clear */
	    d.LO = op10ior(op10lsh(d.LO, -i), x_ash(d.HI, (HBITS*2-1)-i));
	    d.HI = x_ash(d.HI, -i);
	    if (r)
		op10m_signset(d.LO);	/* Set low sign if high is set */
	}
    }
    return d;
}

/* X_QASH1(q) - Special semi-arithmetic shift for quadword
**	Arith-shifts quadword left by 1, but does LSH in high word.
*/
static qw10_t x_qash1(register qw10_t q)
{
    LSHIFTM(q.D1.LO, 1);	/* Shift into sign bits */
    LSHIFTM(q.D1.HI, 1);
    LSHIFTM(q.D0.LO, 1);
    LSHIFTM(q.D0.HI, 1);
    if (wskipl(q.D1.LO)) {		/* Now carry anything necessary */
	op10m_signclr(q.D1.LO);
	op10m_iori(q.D1.HI, 1);
    }
    if (wskipl(q.D1.HI)) {
	op10m_signclr(q.D1.HI);
	op10m_iori(q.D0.LO, 1);
    }
    if (wskipl(q.D0.LO)) {
	op10m_signclr(q.D0.LO);
	op10m_iori(q.D0.HI, 1);
    }
    return q;
}


#define rothalf(hi, lo, x) /* Shift X bits of LO up into HI */ \
	((((hi) << (x)) | ((lo) >> (HBITS - (x)))) & HMASK)

/* Logical Rotate */

w10_t op10rot(register w10_t w, register h10_t h)
{
    register h10_t tmp;

    /* Canonicalize shift arg to positive value */
    if ((h = imm8op(h)) < 0)
	h = (HBITS*2) - ((-h) % (HBITS*2));	/* Right shift */
    else h %= (HBITS*2);			/* Left shift */

    if (h >= HBITS) {		/* Rotating more than one halfword? */
	tmp = LHGET(w);		/* Swap halves and reduce shifting */
	LHSET(w, RHGET(w));
	RHSET(w, tmp);
	h -= HBITS;
    }
    if (h > 0) {
	tmp = LHGET(w);			/* Save old LH */
	/* Shift bits up into LH from RH */
	LHSET(w, rothalf(LHGET(w), RHGET(w), h));
	/* Shift bits up into RH from temp */
	RHSET(w, rothalf(RHGET(w), tmp,  h));
    }
    return w;
}

/* Double Logical Rotate */

dw10_t op10rotc(register dw10_t d,
		register h10_t h)
{
    register w10_t wtmp;
    register h10_t tmp;

    /* Canonicalize shift arg to positive value */
    if ((h = imm8op(h)) < 0)
	h = (HBITS*4) - ((-h) % (HBITS*4));	/* Right shift */
    else h %= (HBITS*4);			/* Left shift */

    if (h >= HBITS*2) {		/* Rotating more than one word? */
	wtmp = d.HI;		/* Swap words and reduce shifting */
	d.HI = d.LO;
	d.LO = wtmp;
	h -= HBITS*2;
    }
    if (h >= HBITS) {		/* Rotating more than one halfword? */
	tmp = LHGET(d.HI);		/* Move halves and reduce shifting */
	LHSET(d.HI, RHGET(d.HI));
	RHSET(d.HI, LHGET(d.LO));
	LHSET(d.LO, RHGET(d.LO));
	RHSET(d.LO, tmp);
	h -= HBITS;
    }
    if (h > 0) {
	tmp = LHGET(d.HI);		/* Save high halfwd */
	/* Shift into LH from RH */
	LHSET(d.HI, rothalf(LHGET(d.HI), RHGET(d.HI), h));
	/* Shift into RH from lo LH */
	RHSET(d.HI, rothalf(RHGET(d.HI), LHGET(d.LO), h));
	/* Shift into lo LH fm lo RH */
	LHSET(d.LO, rothalf(LHGET(d.LO), RHGET(d.LO), h));
	/* Shift into lo RH fm temp */
	RHSET(d.LO, rothalf(RHGET(d.LO), tmp, h));
    }
    return d;
}

/* CIRC - Special ITS instruction!
**	Like ROTC except bits are shifted in and out of 2nd AC in the
**	opposite direction, thus CIRC 1,44 leaves in AC 2 the reverse of
**	what AC 1 contained.
** (Alan: CIRC behaves exactly like ROTC except you reverse AC+1 before and after the
	operation.  The behavior when count > 36. may well differ between
	KA/KI/KL/KS, but will be consistent with LSH, ROT, etc.)
*/
static h10_t hrev(h10_t);

/* Double Logical Circulate */

dw10_t op10circ(register dw10_t d,
		register h10_t h)
{
    register w10_t wtmp;
    register h10_t tmp;

    /* Canonicalize shift arg to positive value */
    if ((h = imm8op(h)) < 0)
	h = (HBITS*4) - ((-h) % (HBITS*4));	/* Right shift */
    else h %= (HBITS*4);			/* Left shift */

    /* Reduce to halfword case by circ'ing halfword chunks */
    if (h >= HBITS*3) {		/* Circ 54+n where (0 <= n < 18) */
	tmp = RHGET(d.HI);
	RHSET(d.HI, LHGET(d.HI));
	LHSET(d.HI, hrev((h10_t) LHGET(d.LO)));
	LHSET(d.LO, RHGET(d.LO));
	RHSET(d.LO, hrev(tmp));
	h -= HBITS*3;
    } else if (h >= HBITS*2) {	/* Circ 36+n where (0 <= n < 18) */
	wtmp = d.HI;
	LHSET(d.HI, hrev((h10_t) RHGET(d.LO)));
	RHSET(d.HI, hrev((h10_t) LHGET(d.LO)));
	LHSET(d.LO, hrev((h10_t) RHGET(wtmp)));
	RHSET(d.LO, hrev((h10_t) LHGET(wtmp)));
	h -= HBITS*2;
    } else if (h >= HBITS) {	/* Circ 18+n where (0 <= n < 18) */
	tmp = LHGET(d.HI);
	LHSET(d.HI, RHGET(d.HI));
	RHSET(d.HI, hrev((h10_t) RHGET(d.LO)));
	RHSET(d.LO, LHGET(d.LO));
	LHSET(d.LO, hrev(tmp));
	h -= HBITS;
    }

    if (h > 0) {		/* Circ n where (0 <= n < 18) */
	tmp = LHGET(d.HI);			/* Save high halfwd */
						/* Shift into      from */
	LHSET(d.HI, rothalf(LHGET(d.HI), RHGET(d.HI), h));
						/* hi LH      hi RH */
	RHSET(d.HI, rothalf(RHGET(d.HI), hrev((h10_t) RHGET(d.LO)), h));
						/* hi RH  rev lo RH */
	RHSET(d.LO, rothalf(LHGET(d.LO), RHGET(d.LO), 18-h));
						/* lo RH      lo LH */
	LHSET(d.LO, rothalf(hrev(tmp), LHGET(d.LO), 18-h));
						/* lo LH  rev hi LH */
    }
    return d;
}

/* HREV - reverse bits in halfword */
static h10_t hrev(register h10_t h)
{
    register h10_t r;
    register int i;

    if (!h) return 0;
    for (r = 0, i = 18; --i >= 0; ) {
	r = (r << 1) | (h & 01);	/* Transfer low bit to high */
	if (!(h >>= 1))			/* Check for early escape */
	    return r << i;
    }
    return r;
}
#undef rothalf

/* Fixed-point PC Flag facilities */

/*
	The algorithm used here to set the carry and overflow flags
is basically table-driven by the three sign bits of the two operands and
their result.
	Subtraction uses a different table; although one can get a correct
value simply by negating the second operand and adding it in, that procedure
does not set the flags properly.

	A + B = S	C0 C1	OVFL + TRAP1
	---------	-----	------------
[0]	0  0  0		-  -
[1]	0  0  1		-  X	AR0+TR1
[2]	0  1  0		X  X
[3]	0  1  1		-  -
[4]	1  0  0		X  X
[5]	1  0  1		-  -
[6]	1  1  0		X  -	AR0+TR1
[7]	1  1  1		X  X

	A - B = S	C0 C1	OVFL + TRAP1
	---------	-----	------------
[0]	0  0  0		X  X
[1]	0  0  1		-  -
[2]	0  1  0		-  -
[3]	0  1  1		-  X	AR0+TR1
[4]	1  0  0		X  -	AR0+TR1
[5]	1  0  1		X  X
[6]	1  1  0		X  X
[7]	1  1  1		-  -

*/


/* Define common macro for setting result flags of add operations.
** Requires that the original operands still be available.
*/
#define ADDFLAGS(sum, a, b) \
    if (wskipl(a)) {					\
	if (wskipl(b)) {			/* If both summands neg */\
	    if (wskipl(sum))			/* Always have a carry */\
		OP10_PCFSET(PCF_CR0+PCF_CR1);		\
	    else				/* Sum pos, overflowed */\
		OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_CR0);	\
	} else if (!wskipl(sum))		/* Mixed signs, so expect 1 */\
	    OP10_PCFSET(PCF_CR0+PCF_CR1);	/* Sum pos, carried out */\
    } else {						\
	if (!wskipl(b)) {			/* If both summands pos */\
	    if (wskipl(sum))			/* Sum shd be positive too */\
		OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_CR1);	\
	} else if (!wskipl(sum))		/* Mixed signs, so expect 1 */\
	    OP10_PCFSET(PCF_CR0+PCF_CR1);	/* Sum pos, carried out */\
    }

/* Define common macro for setting result flags of subtract operations.
** Requires that the original operands still be available.
*/
#define SUBFLAGS(sum, a, b) \
    if (!wskipl(a)) {				/* Check 0-3? */\
	if (!wskipl(b)) {			/* Check 0-1? */\
	    if (!wskipl(sum))			/* */\
		OP10_PCFSET(PCF_CR0+PCF_CR1);	/* [0] */\
	} else {				/* [1] is nop */\
	    if (wskipl(sum))			/* */\
		OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_CR1);	/* [3] */\
	}					/* [2] is nop */\
    } else {					/* Check 4-7 */\
	if (!wskipl(b)) {			/* Check 4-5 */\
	    if (!wskipl(sum))			/* [4] or [5] */\
		OP10_PCFSET(PCF_CR0+PCF_ARO+PCF_TR1);	/* [4] */\
	    else				\
		OP10_PCFSET(PCF_CR0+PCF_CR1);	/* [5] */\
	} else {				\
	    if (!wskipl(sum))			/* [6] or [7] */\
		OP10_PCFSET(PCF_CR0+PCF_CR1);	/* [6] */\
	}					/* [7] is nop */\
    }


/* Fixed-point Single-precision arithmetic */


w10_t op10add(register w10_t a, register w10_t b)
{
#if IFFLAGS
    register w10_t sum;

    sum = a;
    op10m_add(sum, b);		/* Add the words */
    ADDFLAGS(sum, a, b)		/* Invoke macro to set PC flags */
    return sum;
#else
    op10m_add(a,b);
    return a;
#endif
}

w10_t op10inc(register w10_t a)
{
    op10mf_inc(a);
    return a;
}

w10_t op10sub(register w10_t a, register w10_t b)
{
#if IFFLAGS
    register w10_t sum;

    sum = a;
    op10m_sub(sum, b);		/* Subtract the words */
    SUBFLAGS(sum, a, b)		/* Invoke macro to set PC flags */
    return sum;
#else
    op10m_sub(a,b);
    return a;
#endif
}

w10_t op10imul(register w10_t a, register w10_t b)
{
    register dw10_t d;
    register int sign;

    if (sign = wskipl(a))	/* Make args positive, remember signs */
	op10m_movn(a);
    if (wskipl(b)) {
	sign = !sign;
	op10m_movn(b);
#if IFFLAGS
	/* Check for screw case */
	if (wskipl(b) && wskipl(a)) {	/* If max neg * max neg, */
	    OP10_PCFSET(PCF_ARO+PCF_TR1);
	    return b;			/* Return low word (max neg) */
	}
#endif
    }
    d = op10xmul(a, b);		/* Do unsigned multiply, get double product */

    /* For IMUL, result must fit into one word, or overflow is set.
    ** Can check most efficiently here, while value is still positive,
    ** for a high-word of 0 - this will win 99.99% of the time.
    ** Just need to be careful of the 1 ? 0 case, which will fit if negated.
    */
#if IFFLAGS
    if (wskipn(d.HI)
      && (!sign || op10m_cain(d.HI, 1) || wskipn(d.LO)))
	OP10_PCFSET(PCF_ARO+PCF_TR1);
#endif

    if (sign)			/* Negate result if necessary */
	op10m_dneg(d);		/* Yup, negate in place, fixed-point style */
    return d.LO;		/* Return low-order result */
}

dw10_t op10mul(register w10_t a, register w10_t b)
{
    register dw10_t d;
    register int sign;

    if (sign = wskipl(a))
	op10m_movn(a);
    if (wskipl(b)) {
	sign = !sign;
	op10m_movn(b);
#if IFFLAGS
	if (wskipl(b) && wskipl(a)) {
	    OP10_PCFSET(PCF_ARO+PCF_TR1);
	    return dw10maxneg;		/* Return max double negative */
	}
#endif
    }
    d = op10xmul(a, b);		/* Do the multiply */
    if (sign)			/* Negate result if necessary */
	op10m_dneg(d);		/* Yup, negate in place, fixed-point style */
    return d;
}

#define DIGBTS 16
#define DIGMSK (((uint32)1<<DIGBTS)-1)
#define NDIGS(a) (((a)+DIGBTS-1)/DIGBTS)

/* OP10XMUL - Special unsigned multiplication of 36-bit integers.
**	Returns a 71-bit product, with the low order sign bit clear.
*/
dw10_t op10xmul(register w10_t a, register w10_t b)
{
    register int ai, bi;
    register dw10_t d;
    uint32 av[NDIGS(36)], bv[NDIGS(36)], pv[2*NDIGS(36)];

    /* First get args into vectors of N-bit digit integers */
#define setvec(w,v) \
	v[0] = LHGET(w) >> (HBITS-(36-(2*DIGBTS))); \
	v[1] = ((LHGET(w) << (18-DIGBTS)) & DIGMSK) | (RHGET(w) >> DIGBTS); \
	v[2] = RHGET(w) & DIGMSK; \
	DEBUGPRF(("Array: %lo %lo %lo\n", (long)v[0], (long)v[1], (long)v[2]));
    setvec(a, av);
    setvec(b, bv);
/*    pv[0] = pv[1] = pv[2] = */ pv[3] = pv[4] = pv[5] = 0;

    /* Now multiply the vectors together.  Because we have 32-bit unsigned
    ** integers, we can hold the result of multiplying 2 16-bit integers.
    */
    for (ai = NDIGS(36); --ai >= 0;) {
	register uint32 hicarry = 0;
	for (bi = NDIGS(36); --bi >= 0;) {
#if 0
	    register uint32 prod, high;
	    prod = av[ai] * bv[bi];
	    high = prod >> DIGBTS;
	    prod &= DIGMSK;
	    DEBUGPRF(("%d*b%d: %lo,%lo + %lo + p%d => ",
				ai,bi,high,prod, hicarry, ai+bi+1));
	    hicarry += prod + pv[ai+bi+1];
	    pv[ai+bi+1] = hicarry & DIGMSK;
	    hicarry = (hicarry>>DIGBTS) + high;
	    DEBUGPRF(("p%d: %lo, carry %lo\n", ai+bi+1, pv[ai+bi+1], hicarry));
#else
	    hicarry += av[ai] * bv[bi] + pv[ai+bi+1];
	    pv[ai+bi+1] = hicarry & DIGMSK;
	    hicarry >>= DIGBTS;
#endif
	}
	pv[ai] = hicarry;
    }

    /* Now put together doubleword from 71-bit product vector.
    ** Have to leave low-order sign bit clear.
    */
    DEBUGPRF(("ArOut: %lo %lo %lo %lo %lo %lo\n",
	(long)pv[0], (long)pv[1], (long)pv[2],
	(long)pv[3], (long)pv[4], (long)pv[5]));

#if DIGBTS==16
    RHSET(d.LO, (pv[4] & ((1<<2)-1))<< (18-2) |  pv[5] );
    LHSET(d.LO, (pv[3] & ((1<<3)-1))<< (17-3) | (pv[4] >> 2) );
    RHSET(d.HI, (pv[2] & ((1<<5)-1))<< (18-5) | (pv[3] >> 3) );
    LHSET(d.HI, (pv[1] & ((1<<7)-1))<< (18-7) | (pv[2] >> 5) );
#elif DIGBTS==15
    RHSET(d.LO, (pv[4] & ((1<< 3)-1))<< (18-3)  |  pv[5] );
    LHSET(d.LO, (pv[3] & ((1<< 5)-1))<< (17-5)  | (pv[4] >> 3) );
    RHSET(d.HI, (pv[2] & ((1<< 8)-1))<< (18-8)  | (pv[3] >> 5) );
    LHSET(d.HI, (pv[1] & ((1<<11)-1))<< (18-11) | (pv[2] >> 8) );
#endif
    return d;
}

/* IDIV and DIV.  Note that if these routines are used for instruction
**	emulation, some provision must be made for aborting the instruction
**	if No Divide is set -- so that I/DIVM and I/DIVB won't clobber the
**	memory operand.
** X(I)DIV exists for this purpose.  Unlike the usual OP10x routines, its first
**	arg is a pointer (through which it fetches operand and stores value)
**	and it returns a zero int if the division failed.
** IDIV may need to do a special check for 1<<35 / -1 depending on how
**	accurately we want to emulate specific processors.
**	The KS10 result is 1<<35 with no error; others produce overflow.
**	On KA, KI, and some KLs (1<<35)/1 also overflows.  See proc ref man.
*/
dw10_t op10idiv(w10_t a, w10_t b)
{
    dw10_t d;
    d.LO = a;
    d.HI = wskipl(a) ? w10mask : w10zero;
    if (!x_div(&d, b)) {
	d.HI = a;
	op10m_setz(d.LO);
#ifdef OP10_IDIVI_OK
	if (!OP10_IDIVI_OK(b))
#endif
	    OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_DIV);
    }
    return d;
}

/* Note returns doubleword! High is quotient */
dw10_t op10div(
	       dw10_t d,	/* Dividend */
	       register w10_t w) /* Divisor */
{
    if (!x_div(&d, w))
	OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_DIV);
    return d;
}

int op10xidiv(dw10_t *ad,
	      w10_t a,
	      w10_t b)
{
    register int r;

    ad->LO = a;
    ad->HI = wskipl(a) ? w10mask : w10zero;
    if (r = x_div(ad, b))
	return r;	/* Success */

    /* Failure is only possible if A is 1<<35 and B is 1, 0, or -1.
    ** B=0 always causes No Divide.
    ** B=1  is OK on the KS and multi-section KLs.
    ** B=-1 is OK on the KS only.
    */
#ifdef OP10_IDIVI_OK
    if (OP10_IDIVI_OK(b)) {
	ad->HI = a;
	ad->LO = w10zero;
	return 1;
    }
#endif /* OP10_IDIVI_OK */
    OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_DIV);
    return 0;
}

int op10xdiv(dw10_t *ad,	/* Dividend */
	     register w10_t w)	/* Divisor */
{
    register int r;
    return (r = x_div(ad, w)) ? r :
#if IFFLAGS
			(OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_DIV), 0);
#else
			(0);
#endif
}

/* X_DIV - Internal single-precision division.
**
** NOTE: There is a discrepancy between the PRM and what an actual KLX does.
**	It appears that if the resulting quotient would be the largest
**	negative number (400000,,0) then certain operands are permitted
**	without causing a no-divide condition.
**
**	Specifically, args like [1 ? 0] / [-1] result in SETZ even
**	though this is explicitly prohibited by PRM p.2-14 which says
**	"If the high order word of the magnitude of the double length
**	number in AC,AC+1 is greater than or *EQUAL TO* the magnitude of
**	the operand specified by M, set Trap 1, ..."
**
**	This sort of makes sense as the result is arithmetically correct;
**	the max neg # is one less in magnitude than the max positive # so
**	this one case is allowed to escape.
**
** The crucial code is in ddivstep where it checks for the above condition.
** Unfortunately at that point it doesn't know anything about the ultimate
** sign of the result and so can't tell whether to special-case equality
** or not.  At least, it can't without the stupid extra argument passed
** as the "qsign" parameter.
**
** FDVR, which uses ddivstep also, doesn't seem to suffer from this
** inconsistency.  DDIV, which uses qdivstep but otherwise is conceptually
** identical, doesn't either -- that is, it follows the PRM.  Go figure.
*/
/* Note returns doubleword! High is quotient */
static int x_div(
		 dw10_t *ad,	/* Dividend */
	 register w10_t w)	/* Divisor */
{
    dw10_t d;
    int numsign, densign;

    d = *ad;			/* Don't touch original arg in case we fail */

    /* Make args positive.  Note that if either arg is the max neg number
    ** the result will be that same number.  Don't bother checking here
    ** as the initial dividend/divisor comparison test should catch them.
    */
    if (numsign = wskipl(d.HI))
	op10m_dmovn(d);		/* Make positive (unless 1<<35) */
    if (densign = wskipl(w))
	op10m_movn(w);		/* Get absolute value, ignore max neg */

    /* Check for zero divisor, otherwise initial test could pass if
    ** dividend high word was 1<<35.  Sigh.
    */
    if (!wskipn(w))
	return 0;		/* Fail, no divide */

    /* Quick method - see if can do division in native arithmetic */
    if (!wskipn(d.HI) && !(LHGET(d.LO)&(~(HMASK>>4)))
      && !(LHGET(w)&(~(HMASK>>4)))) {
	register uint32 num, den;
	num = op10wtos(d.LO);
	den = op10wtos(w);
	d.HI = op10utow(num/den);
	d.LO = op10utow(num%den);
	DEBUGPRF(("idivquick: %o/%o Q: %lo,%lo R: %lo,%lo\n",
		numsign, densign, DBGV_W(d.HI), DBGV_W(d.LO)));
    } else {
	/* Ugh, hack big numbers */
	/* Divide them, get double result */
	if (!ddivstep(&d, w, 35, numsign != densign))
	    return 0;			/* Fail, no divide */

	/* Fix up remainder */
	if (wskipl(d.LO))		/* If final remainder was negative */
	    op10m_add(d.LO, w);		/* add divisor back in */
	DEBUGPRF(("idivslow: %o/%o Q: %lo,%lo R: %lo,%lo\n",
		numsign, densign, DBGV_W(d.HI), DBGV_W(d.LO)));
    }

    /* Unsigned division done, now fix up results */
    if (numsign)		/* Remainder has same sign as dividend */
	op10m_movn(d.LO);	/* Store remainder */

    /* Now fix up quotient if dividend & divisor had different sign */
    if (numsign != densign)
	op10m_movn(d.HI);
    *ad = d;
    return 1;
}

static int
ddivstep(dw10_t *ad,
	 register w10_t w,
	 register int nmagbits,
	 int qsign)	/* TRUE if quotient will ultimately be negative */
{
    register dw10_t d;
    register w10_t q;
    register int qbit;

    d = *ad;

    DEBUGPRF(("ddivstep(%d) D: %lo,%lo,%lo,%lo W: %lo,%lo qsign %s\n",
		nmagbits, DBGV_D(d), DBGV_W(w), (qsign ? "TRUE" : "FALSE")));

    /* Apply initial test - high order wd must be less than abs val of 
    ** divisor, else overflow & no divide.
    */
    op10m_sub(d.HI, w);		/* Subtract divisor from high word */
    if (!wskipl(d.HI)) {	/* See if divisor was larger or equal */
	/* Ugh, enter Land of the Special Case.
	** According to the PRM this should always cause No Divide, but
	** on the KLX at least, this is okay provided the final quotient
	** has the maximum negative value (400000,,0).
	** This is the only reason for "qsign".  Sigh.
	** Note that for this case, the remainder will always be the low
	** word as-is (with sign cleared, so fixup done by x_div will
	** do the right thing).
	*/
	if (qsign && !wskipn(d.HI)) {
	    op10m_signclr(d.LO);	/* Flush low sign for test & rem */
	    /* Now one last check - can't have any more quotient bits,
	    ** so make sure rest of dividend is less than divisor, i.e. it
	    ** all goes into the remainder.
	    */
	    if (op10m_ucmpl(d.LO, w)) {
		LHSET(d.HI, H10SIGN);	/* Make quotient be max neg #! */
		*ad = d;
		return 1;		/* And return success! */
	    }
	}
	return 0;		/* Overflow, no divide */
    }

    /* First quotient bit (future sign bit) is always 0 at this point. */
    qbit = 0;			/* Set first quotient bit */
    op10m_setz(q);		/* Clear current & future signs */
    LSHIFTM(d.LO, 1);		/* Squeeze out low sign bit */

    while (--nmagbits >= 0) {		/* Loop N times for N magnitude bits */
	/* Do a LSHC d,1 to shift new bit into dividend */
	LSHIFTM(d.HI, 1);
	if (wskipl(d.LO))		/* If high bit set in low wd, */
	    op10m_iori(d.HI, 1);	/* shift into low bit of high */
	LSHIFTM(d.LO, 1);

	if (qbit) op10m_sub(d.HI, w);	/* Sub or add, per previous qbit */
	else op10m_add(d.HI, w);
	qbit = (wskipl(d.HI) ? 0 : 1);	/* Get new quotient bit */
	LSHIFTM(q, 1);			/* Shift quotient over, add bit */
	op10m_iori(q, qbit);
	DEBUGPRF(("ddivstep %d: D: %lo,%lo,%lo,%lo Q: %lo,%lo \n",
		nmagbits, DBGV_D(d), DBGV_W(q)));
#if 0
	/* Minor speed hack; works, but worth the overhead of zero-testing? */
	if (!wskipn(d.HI) && !wskipn(d.LO)) {
	    /* Special copout if hit zero early */
	    LSHIFTM(d.HI, nmagbits);
	    LSHIFTM(q, nmagbits);
	    break;
	}
#endif
    }

    ad->LO = d.HI;	/* Return quotient & remainder in right places */
    ad->HI = q;
    return 1;
}

/* Fixed-point Double-precision arithmetic */

dw10_t op10dadd(register dw10_t da, register dw10_t db)
{
#if IFFLAGS
    register w10_t sum;

    op10m_signclr(da.LO);	/* Must ignore signs of low words */
    op10m_signclr(db.LO);
    op10m_add(da.LO, db.LO);	/* Add low B to low A */

    /* The following is a near-duplicate of the code for op10add.
    ** It has to be replicated, instead of just calling op10add, in order to
    ** properly handle the carry from the low word addition without
    ** losing track of what the flags should be.
    */
    sum = da.HI;		/* Set up high sum */
    if (wskipl(da.LO))		/* If low sign set, carry to high sum! */
	op10m_inc(sum);
    op10m_add(sum, db.HI);	/* Add high words */

    ADDFLAGS(sum, da.HI, db.HI)	/* Check for and set PC flags */

    da.HI = sum;		/* OK, put sum back in place */
    if (wskipl(da.HI))		/* Copy sign bit into low word of result */
	op10m_signset(da.LO);
    else op10m_signclr(da.LO);
    return da;
}

static dw10_t
x_dadd(register dw10_t da, register dw10_t db)
{
#endif /* IFFLAGS */

    op10m_signclr(da.LO);	/* Must ignore signs of low words */
    op10m_signclr(db.LO);
    op10m_add(da.LO, db.LO);	/* Add low B to low A */
    if (wskipl(da.LO))		/* If sign set, carry to high */
	op10m_inc(da.HI);
    op10m_add(da.HI, db.HI);	/* Add high words */
    if (wskipl(da.HI))		/* Copy sign bit into low word of result */
	op10m_signset(da.LO);
    else op10m_signclr(da.LO);
    return da;
}

/* OP10DINC - Increment double-prec fixed-point. */

dw10_t op10dinc(register dw10_t d)
{
    op10m_inc(d.LO);
    if (!wmagskipn(d.LO)) {	/* Check for carry to high word */
	op10m_inc(d.HI);			/* Yep, bump high word */
	LHSET(d.LO, (LHGET(d.HI) & HSIGN));	/* Copy sign bit to low */
    }
    return d;
}

/* OP10DFINC - Increment double-prec floating-point (internal)
*/
static dw10_t op10dfinc(register dw10_t d)
{
    op10m_inc(d.LO);
    if (!wmagskipn(d.LO)) {	/* Check for carry to high word */
	op10m_signclr(d.LO);	/* Yep, clear low sign */
	op10m_inc(d.HI);	/* And, bump high word */
    }
    return d;
}


dw10_t op10dsub(register dw10_t da, register dw10_t db)
{
#if IFFLAGS
    register w10_t sum;

#if 1
    op10m_signclr(da.LO);	/* Must ignore signs of low words */
    op10m_signclr(db.LO);
    op10m_sub(da.LO, db.LO);	/* Sub low B from low A */
    sum = da.HI;		/* Set up high result word */
    if (wskipl(da.LO))		/* If low sign set, carry from high sum! */
	op10m_dec(sum);
    op10m_sub(sum, db.HI);	/* Subtract high words */

    SUBFLAGS(sum, da.HI, db.HI)	/* Check for and set PC flags */

    da.HI = sum;		/* OK, put sum back in place */
    if (wskipl(da.HI))		/* Copy sign bit into low word of result */
	op10m_signset(da.LO);
    else op10m_signclr(da.LO);
    return da;

#else
    op10m_movn(db.LO);		/* Negate low word */
    if (wmagskipn(db.LO))	/* See if stuff, ignore sign bit */
	op10m_setcm(db.HI);	/* Low word has stuff, no carry into high */
    else op10m_movn(db.HI);	/* Low word clear, so carry into high word */
    return op10dadd(da, db);	/* Now add da + (-db) */
#endif
}

static dw10_t
x_dsub(register dw10_t da, register dw10_t db)
{
#endif /* IFFLAGS */
    op10m_dmovn(db);		/* Negate 2nd operand in place */
    return x_dadd(da, db);	/* and add in */
}


qw10_t op10dmul(register dw10_t da, register dw10_t db)
{
    register int sign;

    if (sign = wskipl(da.HI))
	op10m_dneg(da);
    if (wskipl(db.HI)) {
	sign = !sign;
	op10m_dneg(db);
#if IFFLAGS
	if (wskipl(db.HI) && wskipl(da.HI)) {
	    OP10_PCFSET(PCF_ARO+PCF_TR1);
	    return qw10maxneg;		/* Return max neg quadword */
	}
#endif
    }
    return sign ? x_qneg(x_dmul(da, db)) : x_dmul(da, db);
}

/* Unsigned double-int multiply */

static qw10_t x_dmul(register dw10_t da, register dw10_t db)
{
    register qw10_t q;		/* Quad-length result */
    register int ai, bi;
    uint32 av[5], bv[5], pv[10];

    /* First get 71-bit args into vectors of 16-bit integers.
    ** The main screw is ignoring the sign bit of low-order word.
    */
#define dsetvec(dw,v) \
	v[0] =  LHGET(dw.HI) >> 11;		/* High 7 bits of LH */\
	v[1] = (LHGET(dw.HI) & ((1<<11)-1))<<5 | (RHGET(dw.HI) >> 13); \
	v[2] = (RHGET(dw.HI) & ((1<<13)-1))<<3 | ((LHGET(dw.LO) >> 14) & 07); \
	v[3] = (LHGET(dw.LO) & ((1<<14)-1))<<2 | (RHGET(dw.LO) >> 16); \
	v[4] =  RHGET(dw.LO) & (((h10_t)1<<16)-1); \
	DEBUGPRF(("Array: %lo %lo %lo %lo %lo\n", \
		(long)v[0],(long)v[1],(long)v[2],(long)v[3],(long)v[4]));

    dsetvec(da, av);
    dsetvec(db, bv);
    for (ai = 10; --ai >= 0; )		/* Clear product vector */
	pv[ai] = 0;

    /* Now multiply the vectors together.  Because we have 32-bit unsigned
    ** integers, we can hold the result of multiplying 2 16-bit integers.
    */
    for (ai = 5; --ai >= 0;) {
	register uint32 hicarry = 0;
	for (bi = 5; --bi >= 0;) {
#if 1
	    register uint32 prod, high;
	    prod = av[ai] * bv[bi];
	    high = prod >> DIGBTS;
	    prod &= DIGMSK;
	    DEBUGPRF(("%d*b%d: %lo,%lo + %lo + p%d => ",
		ai,bi, (long)high, (long)prod, (long)hicarry, ai+bi+1));
	    hicarry += prod + pv[ai+bi+1];
	    pv[ai+bi+1] = hicarry & DIGMSK;
	    hicarry = (hicarry>>DIGBTS) + high;
	    DEBUGPRF(("p%d: %lo, carry %lo\n",
			ai+bi+1, (long)pv[ai+bi+1], (long)hicarry));
#else
	    hicarry += av[ai] * bv[bi] + pv[ai+bi+1];
	    pv[ai+bi+1] = hicarry & DIGMSK;
	    hicarry >>= DIGBTS;
#endif
	}
	pv[ai] = hicarry;
    }

    /* Now put together quadword from low 140+1 bits of product vector.
    ** Screw, again, is skipping over sign bits of low-order words.
    */
    DEBUGPRF(("ArOut: %lo %lo %lo %lo %lo %lo %lo %lo %lo %lo\n",
		(long)pv[0], (long)pv[1], (long)pv[2], (long)pv[3],
		(long)pv[4], (long)pv[5], (long)pv[6], (long)pv[7],
		(long)pv[8], (long)pv[9]));

    LHSET(q.D0.HI, (pv[1] & ((1<<13)-1))<< (18-13) | (pv[2] >> (16-(18-13))) );
    RHSET(q.D0.HI, (pv[2] & ((1<<11)-1))<< (18-11) | (pv[3] >> (16-(18-11))) );
    LHSET(q.D0.LO, (pv[3] & ((1<< 9)-1))<< (18-10) | (pv[4] >> (16-(18-10))) );
    RHSET(q.D0.LO, (pv[4] & ((1<< 8)-1))<< (18- 8) | (pv[5] >> (16-(18- 8))) );
    LHSET(q.D1.HI, (pv[5] & ((1<< 6)-1))<< (18- 7) | (pv[6] >> (16-(18- 7))) );
    RHSET(q.D1.HI, (pv[6] & ((1<< 5)-1))<< (18- 5) | (pv[7] >> (16-(18- 5))) );
    LHSET(q.D1.LO, (pv[7] & ((1<< 3)-1))<< (18- 4) | (pv[8] >> (16-(18- 4))) );
    RHSET(q.D1.LO, (pv[8] & ((1<< 2)-1))<< (18- 2) |  pv[9] );

    /* Must duplicate sign bit in all low-order words. */
    if (wskipl(q.D0.HI)) {
	op10m_signset(q.D0.LO);
	op10m_signset(q.D1.HI);
	op10m_signset(q.D1.LO);
    }
    return q;
}

/* Note returns quadword! High double is quotient */

qw10_t op10ddiv(
		qw10_t qw,	/* Dividend */
	register dw10_t d)	/* Divisor */
{
    qw10_t origq;
    int numsign, densign;

    origq = qw;			/* Save original arg in case of error */

    /* Make args positive.  Note that if either arg is the max neg number
    ** the result will be that same number.  Don't bother checking here
    ** as the initial dividend/divisor comparison test should catch them.
    */
    if (numsign = wskipl(qw.D0.HI))
	qw = x_qneg(qw);
    if (densign = wskipl(d.HI))
	op10m_dmovn(d);

    /* Check for zero divisor */
    if (!dlomagskipn(d)) {
	OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_DIV);
	return origq;		/* Overflow, no divide */
    }

    /* See if can do simpler division */
    if (!dlomagskipn(qw.D0) && !wmagskipn(qw.D1.HI) && !wskipn(d.HI)) {
	op10m_signclr(qw.D1.HI);	/* Ensure sign is off in low words */
	op10m_signclr(d.LO);
	DEBUGPRF(("ddivquick: %o/%o N: %lo,%lo,%lo,%lo D: %lo,%lo\n",
		numsign, densign, DBGV_D(qw.D1), DBGV_W(d.LO)));

	if (!x_div(&qw.D1, d.LO)) {	/* Do single-prec division */
	    OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_DIV);
	    return origq;		/* Overflow, no divide */
	}
	qw.D0.LO = qw.D1.HI;		/* Quotient here, qw.D0.HI already 0 */
	op10m_setz(qw.D1.HI);		/* Remainder already in qw.D1.LO */
    } else {
	/* Ugh, must hack big numbers */
	if (!qdivstep(&qw, d, 70)) {		/* Divide, get 70 bits */
	    OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_DIV);
	    return origq;		/* Overflow, no divide */
	}
	/* Fix up remainder.  Always has same sign as dividend. */
	if (wskipl(qw.D1.HI))		/* If final result was negative */
	    qw.D1 = x_dadd(qw.D1, d);	/* add divisor back in */
    }
    DEBUGPRF(("ddiv: %o/%o Q: %lo,%lo,%lo,%lo R: %lo,%lo,%lo,%lo\n",
	numsign, densign, DBGV_D(qw.D0), DBGV_D(qw.D1)));

    /* Unsigned division done, now fix up results */
    if (numsign)		/* Remainder has same sign as dividend */
	op10m_dneg(qw.D1);	/* Negate in place, fixed-point style */

    /* Now fix up quotient if dividend & divisor had different sign */
    if (numsign != densign)
	op10m_dneg(qw.D0);	/* Negate in place, fixed-point style */

    return qw;
}

/* General Floating-Point arithmetic defs
*/

/* PDP-10 Single-precision hardware floating point has the format:
**	1 sign bit
**	8 exponent bits, excess-0200, ones-complement if negative
**	27 fraction bits, twos-complement if negative
**
** PDP-10 Double-precision hardware floating point has the format:
**	1 sign bit
**	8 exponent bits, excess-0200, ones-complement if negative
**	27 fraction bits, twos-complement if negative
** plus	1 ignored low-word sign bit (set 0 by all operations)
**	35 additional fraction bits (62 total)
*/


/* Single-prec Format Exponent macros (also applicable to D format)
**	These treat the exponent as a quantity from the LH of high word.
*/
#define SFEBITS 9
#define SFEMASK (((h10_t)1<<SFEBITS)-1)	/* Right-justified mask */
#define SFESIGN (H10SIGN >> (H10BITS-SFEBITS)) /* Right-justified sign */
#define SFEMAGS  (SFESIGN-1)			/* Right-justified mag bits */
#define SFELHF   (SFEMASK << (H10BITS-SFEBITS))	/* Exponent field in LH */
#define SFEGET(w) ((LHGET(w) >> (H10BITS-SFEBITS)) & SFEMASK)
#define SFEEXCESS (SFESIGN>>1)			/* The N in Excess-N */

/* Single-prec Format Fraction macros (also applicable to D format)
**	These treat the fraction as a quantity from the LH of high word.
*/
#define SFFBITS (H10BITS-SFEBITS)		/* LH fraction bits */
#define SFFMASK (((h10_t)1<<SFFBITS)-1)
#define SFFHIBIT (H10SIGN>>SFEBITS)		/* Top bit (not a sign) */
#define SFFMAGS (SFFMASK>>1)			/* Remaining mag bits */

/* Macro to canonicalize exponent and fraction.
**	exp - must be an "int" variable
**	w - first word of float/double/gdouble
*/
#define SFSETUP(exp, w) \
    exp = SFEGET(w);		/* Get sign and exponent */\
    if (exp & SFESIGN) {	/* Negative? */\
	exp = exp ^ SFEMASK;	/* If so, get ones-complement, and */\
	op10m_tlo(w, SFELHF);	/* propagate sign thru exp */\
    } else			/* Else mask sign+exp out of fract */\
	op10m_tlz(w, SFELHF)

#define SF_POSSETUP(sign, exp, w) \
    exp = SFEGET(w);		/* Get sign and exponent */\
    if (sign = (exp & SFESIGN)) {	/* Negative? */\
	exp = exp ^ SFEMASK;	/* If so, get ones-complement, and */\
	op10m_tlo(w, SFELHF);	/* propagate sign thru exp */\
	op10m_movn(w);		/* and make positive */\
    } else			/* Else mask sign+exp out of fract */\
	op10m_tlz(w, SFELHF)

/* After SF_POSSETUP, can test for normalization with:
**	op10m_tlnn(w, SFFHIBIT|(SFFHIBIT<<1))
** If SFFHIBIT<<1 is set, fraction was negative 400,,0.
** Else if SFFHIBIT is set, fraction was either positive
**	or a negative less than 400,,0.
** If neither bit is set, fraction is unnormalized.
**	Just remember zero is valid if exponent also zero.
*/

#define SF_DPOSSETUP(sign, exp, d) \
    exp = SFEGET((d).HI);		/* Get sign and exponent */\
    if (sign = (exp & SFESIGN)) {	/* Negative? */\
	exp = exp ^ SFEMASK;		/* If so, get ones-complement, and */\
	op10m_tlo((d).HI, SFELHF);	/* propagate sign thru exp */\
	op10m_dmovn(d);			/* and make positive */\
	if (!op10m_tlnn((d).HI, SFFMASK))	/* If carried out of fract */\
	    ++exp, LHSET((d).HI, SFFHIBIT);	/* adjust it */\
    } else				/* Else mask sign+exp out of fract */\
	op10m_tlz((d).HI, SFELHF)


#if OP10_GFMT

/* PDP-10 G Format double-precision floating point has the format:
**	1 sign bit
**	11 exponent bits, excess-02000, ones-complement if negative
**	24 fraction bits, twos-complement if negative
** plus	1 ignored low-word sign bit (set 0 by all operations)
**	35 additional fraction bits (59 total)
**
** Basically identical to Double (D-format) but with 3 bits taken from
** the precision and given to the exponent.
*/

/* G Format Exponent macros
**	These treat the exponent as a quantity from the LH of high word.
*/
#define GFEBITS 12
#define GFEMASK  (((h10_t)1<<GFEBITS)-1)	/* Right-justified mask */
#define GFESIGN  (H10SIGN >> (H10BITS-GFEBITS))	/* Right-justified sign */
#define GFEMAGS  (GFESIGN-1)			/* Right-justified mag bits */
#define GFELHF   (GFEMASK << (H10BITS-GFEBITS))	/* Exponent field in LH */
#define GFEGET(w) ((LHGET(w) >> (H10BITS-GFEBITS)) & GFEMASK)
#define GFEEXCESS (GFESIGN>>1)			/* The N in Excess-N */

/* G Format Fraction macros.
**	These treat the fraction as a quantity from the LH of high word.
*/
#define GFFBITS (H10BITS-GFEBITS)		/* LH fraction bits */
#define GFFMASK (((h10_t)1<<GFFBITS)-1)
#define GFFHIBIT (H10SIGN>>GFEBITS)		/* Top bit (not a sign) */
#define GFFMAGS (GFFMASK>>1)			/* Remaining mag bits */

/* Macro to canonicalize exponent and fraction.
**	exp - must be an "int" variable
**	w - first word of float/double/gdouble
*/
#define GFSETUP(exp, w) \
    exp = GFEGET(w);		/* Get sign and exponent */\
    if (exp & GFESIGN) {	/* Negative? */\
	exp = exp ^ GFEMASK;	/* If so, get ones-complement, and */\
	op10m_tlo(w, GFELHF);	/* propagate sign thru exp */\
    } else			/* Else mask sign+exp out of fract */\
	op10m_tlz(w, GFELHF)


/* Macro to insert exponent into high word of G-format double.
*/
#define GFEXPSET(w, exp) \
    if (wskipl(w))	/* If sign set, use ones-complement of exp */\
	LHSET(w, (LHGET(w) ^ ((h10_t)(exp & GFEMAGS) << (H10BITS-GFEBITS))));\
    else								\
	LHSET(w, (LHGET(w) | ((h10_t)(exp & GFEMAGS) << (H10BITS-GFEBITS))))

#define GF_DPOSSETUP(sign, exp, d) \
    exp = GFEGET((d).HI);		/* Get sign and exponent */\
    if (sign = (exp & GFESIGN)) {	/* Negative? */\
	exp = exp ^ GFEMASK;		/* If so, get ones-complement, and */\
	op10m_tlo((d).HI, GFELHF);	/* propagate sign thru exp */\
	op10m_dmovn(d);			/* and make positive */\
	if (!op10m_tlnn((d).HI, GFFMASK))	/* If carried out of fract */\
	    ++exp, LHSET((d).HI, GFFHIBIT);	/* adjust it */\
    } else				/* Else mask sign+exp out of fract */\
	op10m_tlz((d).HI, GFELHF)


#endif /* OP10_GFMT */

/* Floating-point Single-precision arithmetic */

/* Internal flags for ffadr(), selected so most common case (fadr)
** has flags of 0.
*/
#define FADF_NONORM	04	/* Set to skip normalization */
#define FADF_NEGB	02	/* Set to negate B before adding */
#define FADF_NORND	01	/* Set to skip rounding */

w10_t op10fadr(register w10_t a, register w10_t b)
{	return ffadr(a, b, 0);		/* No negate, Round */
}

w10_t op10fad(register w10_t a, register w10_t b)
{	return ffadr(a, b, FADF_NORND);	/* No negate, no round */
}

w10_t op10fsbr(register w10_t a, register w10_t b)
{	return ffadr(a, b, FADF_NEGB);	/* Negate, Round */
}

w10_t op10fsb(register w10_t a, register w10_t b)
{	return ffadr(a, b, FADF_NEGB|FADF_NORND);	/* Negate, no round */
}

/* Common floating add subroutine shared by above external routines. */

static w10_t ffadr(register w10_t a, register w10_t b, int flg)
{
    register dw10_t d;
    register int i, expa, expb;
    register uint18 rbit;		/* May be 1<<17 */

    SFSETUP(expa, a);		/* Set up exponent and fraction for A */
    SFSETUP(expb, b);		/* Ditto for B */

    if (flg & FADF_NEGB)	/* If actually subtracting B, */
	op10m_movn(b);		/* OK to negate fraction now */

    /* Now see which exponent is smaller; get larger in A, smaller in B.
    ** This needs to be done even if one of the args is completely zero, so
    ** that the other is set up in A for the normalization code.
    */
    if ((i = expa - expb) < 0) {
	w10_t tmp;			/* Swap args */
	int etmp;
	etmp = expb, tmp = b;
	expb = expa, b = a;
	expa = etmp, a = tmp;
	i = -i;
    }

    /* Now right-shift B by the number of bits in i, then add to A. */
    d.HI = x_ash(b, -i);		/* Shift B and put in hi word */
    op10m_add(d.HI, a);			/* then add A to it */
    d.LO = x_ash(b, 35-i);		/* Set 2nd word (low of B) */

    DEBUGPRF(("FADR preN: (%o) %lo,%lo,%lo,%lo\n", expa, DBGV_D(d)));

    /* D now has double-length sum.
    ** The high word sign of A is correct; that of low word may not be.
    ** Check high 10 bits to determine normalization step.
    */
    rbit = 0;
    switch (LHGET(d.HI) >> 8) {
	case 00004>>2:		/* Normalized positive fraction */
	    DEBUGPRF(("WINNING POSITIVE\n"));
	    rbit = (LHGET(d.LO) & (HSIGN>>1));
	    break;

	case 00010>>2:		/* Overflow of pos fraction */
	case 00014>>2:
	    DEBUGPRF(("OVERFLOW POSITIVE\n"));
	    rbit = RHGET(d.HI) & 01;
	    d.HI = x_ash(d.HI, -1);	/* Normalize by shifting right */
	    ++expa;			/* And adding one to exponent */
	    break;

	case 07760>>2:		/* Overflow of neg fraction, maybe zero mag */
	case 07764>>2:		/* Overflow of neg fraction, nonzero mag */
	    DEBUGPRF(("OVERFLOW NEGATIVE\n"));
	    d = x_ashc(d, -1);	/* First normalize into 0777 case, */
	    ++expa;		/* then fall thru below to check! */

	case 07770>>2:		/* Normalized negative fraction (probably) */
	    if (!(flg & FADF_NORND)) {
		rbit = LHGET(d.LO) & (HSIGN>>1);
		if (rbit && !op10m_txnn(d.LO, (HMASK>>2), HMASK))
		    rbit = 0;		/* No round if no other bits */
	    }
	    if (op10m_txnn(d.HI, 0777, HMASK) || rbit) {
		DEBUGPRF(("WINNING NEGATIVE\n"));
		break;
	    }
	    DEBUGPRF(("OVERFLOW NEG, ZEROMAG -2^28 case\n"));
	    ++expa;
	    op10m_tlo(d.HI, 0400);	/* Effect ASHC -1 */
	    break;

	/* Anything else is an unnormalized result and must be handled
	** using slow, fully general case.
	*/
	case 0:
	    if (!wskipn(d.HI) && !wmagskipn(d.LO)) {
		DEBUGPRF(("FADR: S: ZERO RESULT!\n"));
		return w10zero;
	    }
	default:
	    if (flg & FADF_NONORM)	/* May not want normalization */
		break;

	    /* Must mess with low word, sigh.  Ensure it has correct sign bit,
	    ** then find the first fraction bit in the 2-word sum.
	    ** Normalization shifts until B0 != B9 OR (B9==1 and B10-35 == 0).
	    ** The latter case must be checked for if negative.
	    */
	    if (wskipl(d.HI)) {
		op10m_signset(d.LO);
		if ((i = op10ffo(op10setcm(d.HI))) >= 36)
		    i = op10ffo(op10setcm(d.LO)) + 35;
	    } else {
		op10m_signclr(d.LO);
		if ((i = op10ffo(d.HI)) >= 36)
		    i = op10ffo(d.LO) + 35;
	    }
	    DEBUGPRF(("RENORMALIZATION: %d", i-9));
	    /* i now has position of first fraction bit; move it into place. */
	    d = x_ashc(d, (i -= 9));		/* Derive shift and do it */
	    expa -= i;				/* Adjust exponent */
	    rbit = LHGET(d.LO) & (HSIGN>>1);	/* Find roundup bit */
	    if (wskipl(d.HI)) {			/* Special checks for neg */
		DEBUGPRF((" checking neg"));
		if (!op10m_txnn(d.HI, 0777, HMASK)) {
		    /* If fraction is all zero, must be the ugly
		    ** special case of negative 2^28 (777000,,0)
		    */
		    DEBUGPRF((" - special -2^28 case"));
		    ++expa;
		    op10m_tlo(d.HI, 0400);	/* Effect ASHC -1 */
		    rbit = 0;
		} else if (rbit && !op10m_txnn(d.LO, (HMASK>>2), HMASK)) {
		    DEBUGPRF((" - no lower-round bits, rbit=0"));
		    rbit = 0;			/* No round if no other bits */
		}
	    }
	    DEBUGPRF(("\n"));
	    break;
    }

    /* At this point, fraction in d.HI is normalized, and rbit is set if a
    ** roundup increment must be done (which in rare cases can trigger
    ** a single-shift renormalization).
    */
    if (!(flg & FADF_NORND) && rbit) {
	DEBUGPRF(("ROUNDUP DONE"));
	op10m_inc(d.HI);		/* Add 1, then check for overflow */
	if (wskipl(d.HI)) {		/* Negatives are messy */
	    if (LHGET(d.HI) & 0400) {	/* If first fract bit is "wrong" */
		if ((LHGET(d.HI) & 0377) || RHGET(d.HI)) {	/* See if really OK */
		    --expa, d.HI = x_ash(d.HI, 1);	/* Nope, shift up */
		    DEBUGPRF((" - NEG RENORMALIZED!"));
		}
	    }
	} else if (!(LHGET(d.HI) & 0400)) {
	    ++expa, LHSET(d.HI, 0400);	/* Positive overflow, fix up */
	    DEBUGPRF((" - POS RENORMALIZED!"));
	}
	DEBUGPRF(("\n"));
    }
    DEBUGPRF(("FADR: S: (%o) %lo,%lo\n", expa, DBGV_W(d.HI)));

    /* Ta da!  Put exponent back in.  We can check now for overflow or
    ** underflow.
    */
    if (wskipl(d.HI))	/* If sign set, put ones-complement of exp */
	LHSET(d.HI, (LHGET(d.HI) ^ ((h10_t)(expa & 0377) << 9)));
    else
	LHSET(d.HI, (LHGET(d.HI) | ((h10_t)(expa & 0377) << 9)));

#if IFFLAGS
    if (expa < 0) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_FXU);
    else if (expa > SFEMAGS) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV);
#endif
    return d.HI;
}



w10_t op10fmp(register w10_t a, register w10_t b)
{	return ffmpr(a, b, 0);		/* Do without rounding */
}

w10_t op10fmpr(register w10_t a, register w10_t b)
{	return ffmpr(a, b, 1);		/* Do with rounding */
}

static w10_t ffmpr(register w10_t a, register w10_t b, int dornd)
{
    register dw10_t d;
    register int sign, exp, i, expb;

    /* Make operands positive and remember sign of result */
    SF_POSSETUP(sign, exp, a);		/* Get positive exp and fract */

    /* Repeat same steps for other arg */
    expb = SFEGET(b);			/* Get sign and exponent */
    if (expb & SFESIGN) {		/* Negative? */
	sign = !sign;			/* Set 0 if both args negative */
	expb = expb ^ SFEMASK;		/* If so, get ones-complement */
	op10m_tlo(b, SFELHF);		/* propagate sign thru exp */
	op10m_movn(b);			/* Then make positive */
    } else
	op10m_tlz(b, SFELHF);		/* Else mask sign+exp out of fract */

    /* Add exponents together */
    exp += expb - 0200;

    /* Now multiply the 27-bit magnitude numbers (maybe 28-bit if neg) */
    d = op10xmul(a, b);		/* Multiply the numbers, get double result */

    /* The high 17 bits of product should be empty (1 sign, 16 magnitude bits)
    ** If everything was normalized, the next bit (start of fraction) should
    ** be 1 and we just shift everything left 8 bits to get in place.
    ** If not, must look for first bit of fraction.
    */
    DEBUGPRF(("FMP preN: (%d.) %lo,%lo,%lo,%lo\n", exp, DBGV_D(d)));
    if (LHGET(d.HI) == 01) i = 8;
    else {
	/* High fraction bit not set... see if result was 0 */
	if (!wskipn(d.HI) && !wskipn(d.LO))
	    return d.HI;		/* Zero product, return 0.0 */
	i = adffo(d);			/* Find first fract bit in double */
	i -= SFEBITS;			/* Adjust for place shifting to */
    }

    /* OK, now normalize. */
    d = x_ashc(d, i);			/* Do ASHC bringing in zeros */
    exp -= i - 8;
    DEBUGPRF(("FMP pstN: (%d.) %lo,%lo,%lo,%lo\n", exp, DBGV_D(d)));

    /* Now do rounding.  See if bit lower than LSB is set; if so, add one
    ** to LSB.  This might possibly carry all the way to MSB, so be ready
    ** to re-normalize.
    */
    if (dornd) {
	if (op10m_tlnn(d.LO, (HSIGN>>1))) {	/* Roundup bit there? */
	    op10m_inc(d.HI);
	    if (op10m_tlnn(d.HI, SFELHF)) {	/* If carried out of fract */
		LHSET(d.HI, SFFHIBIT);	/* must be 01000, shift it back */
		++exp;			/* and adjust exponent */
	    }
	}
    }

    /* Fraction all done, just add exponent back now */
    op10m_tlo(d.HI, ((h10_t)(exp & SFEMAGS) << SFFBITS));
#if IFFLAGS
    if (exp < 0) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_FXU);
    else if (exp > SFEMAGS) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV);
#endif

    /* Convert back to negative result if necessary.  Note special hackery
    ** if not rounding, which merely gives ones-complement if any bits were
    ** set in low-order part.  This is needed to reproduce hardware, even
    ** though it results in a less precise answer!
    */
    if (sign) {
	if (dornd || !wskipn(d.LO))
	    op10m_movn(d.HI);
	else
	    op10m_setcm(d.HI);
    }
    return d.HI;
}


/* FDV - slightly different calling sequence needed because like
**	other divides, operation can be aborted without changing
**	any operands.
**
** OP10FDV{R} are provided for backward compatibility with optest prog.
*/

w10_t op10fdv(w10_t a, w10_t b)
{
    (void) op10xfdv(&a, b, 0);	/* Do without rounding */
    return a;			/* Return whatever result was */
}				/* (no change if no divide) */

w10_t op10fdvr(w10_t a, w10_t b)
{
    (void) op10xfdv(&a, b, 1);	/* Do with rounding */
    return a;			/* Return whatever result was */
}				/* (no change if no divide) */


int op10xfdv(w10_t *aw,
	     register w10_t b,
	     int dornd)
{
    register w10_t a = *aw;
    dw10_t d;
    register int sign, exp, i, expb;

    /* Make operands positive and remember sign of result */

    if (sign = wskipl(a)) {	/* Negative? */
	op10m_movn(a);		/* Then make positive */
	if (wskipl(a)) {		/* If numerator is max neg value, */
	    OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_DIV);
	    return 0;			/* PDP-10 always chokes, sigh */
	}
    }
    exp = SFEGET(a);			/* Get positive exponent */
    op10m_tlz(a, SFELHF);		/* Mask sign+exp out of fract */

    /* Repeat for other arg, but allow max neg number */
    expb = SFEGET(b);			/* Get sign and exponent */
    if (expb & SFESIGN) {		/* Negative? */
	sign = !sign;			/* Set 0 if both args negative */
	expb = expb ^ SFEMASK;		/* If so, get ones-complement */
	op10m_tlo(b, SFELHF);		/* propagate sign thru exp */
	op10m_movn(b);			/* Then make positive */
    } else
	op10m_tlz(b, SFELHF);		/* Else mask sign+exp out of fract */


    /* Subtract exponents.  The +1 compensates for pre-multiply of divisor.
    */
    exp += 0200 + 1 - expb;
    DEBUGPRF(("FDVR (%o) N: %lo,%lo D: %lo,%lo\n",
		exp, DBGV_W(a), DBGV_W(b)));

    /* Get two 27-bit (or 28 if SETZ) magnitude numbers and divide them.
    ** In order to ensure that the division can succeed, we pre-multiply the
    ** divisor by 2.  This is compensated for by adjusting the final exponent.
    ** This can fail and cause No Divide (etc) if operands aren't normalized.
    */
    b = x_ash(b, 1);		/* Make divisor be bigger */
    if (op10m_camge(a, b)) {
	DEBUGPRF(("FDVR: Fail, N: %lo,%lo >= D: %lo,%lo\n",
			DBGV_W(a), DBGV_W(b)));

	OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_DIV);
	return 0;		/* Fail, return original dividend */
    }

    d.HI = a;			/* Set up dividend */
    op10m_setz(d.LO);
    DEBUGPRF(("FDVR: N: %lo,%lo|%lo,%lo  D: %lo,%lo\n", DBGV_D(d), DBGV_W(b)));

    /* Used to be always 30; maybe revert if needed to duplicate
    ** unnormalized behavior??
    */
    i = (dornd ? 29 : 28);
    if (!ddivstep(&d, b, i, 0)) {	/* Divide them, get double result */
	OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_DIV);
	return 0;			/* Fail */
    }
    exp += 35-i;			/* Adjust for partial division */

    DEBUGPRF(("FDV preN: Q: %lo,%lo R: %lo,%lo  E: %o\n", DBGV_D(d), exp));

    if (!wskipn(d.HI)) {		/* Check for zero result */
	*aw = d.HI;
	return TRUE;
    }

    /* To compensate for possible unnormalized operands and roundup carry,
    ** we do a completely general normalization and round.  Note we've already
    ** tested for 0 so bits definitely exist.
    */
    i = op10ffo(d.HI) - 9;		/* Find first one-bit, derive shift */
    if (dornd && i < 0) {		/* If right-shifting, */
	/* Note max bits needed are 9, so "int" is big enough */
	register int bit = 1 << (-i-1);	/* Find last bit that will vanish */
	if (RHGET(d.HI) & bit) { 		/* If need to round up, */
	    static w10_t wrnd;
	    LRHSET(wrnd, 0, bit);
	    op10m_add(d.HI, wrnd);		/* do it */
	    DEBUGPRF(("ROUNDED UP!\n"));
	    if (i != (op10ffo(d.HI)-9)) {	/* See if carried */
		DEBUGPRF(("CARRIED!\n"));
		if (wskipl(d.HI)) {		/* Yes, into sign bit? */
		    DEBUGPRF(("CARRIED INTO SIGN!\n"));
		    d.HI = x_ash(d.HI, -1);	/* Yes, yecch!  Special */
		    op10m_signclr(d.HI);	/* handling to undo damage */
		    --exp;
		} else --i;		/* Carried but compensation trivial */
	    }
	}
    }
    if (i != 8)
	DEBUGPRF(("ABNORMALIZATION: %d\n", i));

    d.HI = x_ash(d.HI, i);		/* Shift the fraction into place */
    exp -= i + 8;			/* Adjust exponent for shift */

    DEBUGPRF(("FDV pstN: Q: %lo,%lo R: %lo,%lo  E: %o\n", DBGV_D(d), exp));

    /* Fraction all done, just add exponent back now */
    LHSET(d.HI, (LHGET(d.HI) | ((h10_t)(exp & SFEMAGS) << SFFBITS)));
#if IFFLAGS
    if (exp < 0) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_FXU);
    else if (exp > SFEMAGS) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV);
#endif

    /* Ugly hackery - see PRM 2-23 "Note" for FDV.
    ** For FDV on KL+KS a negative Q is represented by twos-complement only if
    ** remainder is 0, else ones-complement (similar to the way FMP works!).
    ** On KA+KI it is always twos-complement...
    */
    if (sign) {
#if OP10_FDV_KL		/* KL or KS */
	if (!dornd) {
	    /* Fix up remainder for testing */
	    if (wskipl(d.LO))		/* If negative, */
		op10m_add(d.LO, b);	/* add divisor back in */
	    if (wskipn(d.LO))
		op10m_setcm(d.HI);
	    else
		op10m_movn(d.HI);
	} else
#endif
	    op10m_movn(d.HI);
    }
    *aw = d.HI;
    return TRUE;
}

#if 0	/* Old version */

static w10_t ffdvr(a, b, dornd)
register w10_t a, b;
{
    dw10_t d;
    register int sign, exp, i, expb;

    /* Make operands positive and remember sign of result */
    d.HI = a;				/* Preserve A in case fail */
    if (sign = wskipl(d.HI)) {	/* Negative? */
	op10m_movn(d.HI);		/* Then make positive */
	if (wskipl(d.HI)) {		/* If numerator is max neg value, */
	    OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_DIV);
	    return a;			/* PDP-10 always chokes, sigh */
	}
    }
    exp = SFEGET(d.HI);			/* Get positive exponent */
    op10m_tlz(d.HI, SFELHF);		/* Mask sign+exp out of fract */

    /* Repeat for other arg, but allow max neg number */
    expb = SFEGET(b);			/* Get sign and exponent */
    if (expb & SFESIGN) {		/* Negative? */
	sign = !sign;			/* Set 0 if both args negative */
	expb = expb ^ SFEMASK;		/* If so, get ones-complement */
	op10m_tlo(b, SFELHF);		/* propagate sign thru exp */
	op10m_movn(b);			/* Then make positive */
    } else
	op10m_tlz(b, SFELHF);		/* Else mask sign+exp out of fract */


    /* Subtract exponents.  The +1 compensates for pre-multiply of divisor.
    */
    exp += 0200 + 1 - expb;
    DEBUGPRF(("FDVR (%o) N: %lo,%lo D: %lo,%lo\n",
		exp, DBGV_W(d.HI), DBGV_W(b)));

    /* Get two 27-bit (or 28 if SETZ) magnitude numbers and divide them.
    ** In order to ensure that the division can succeed, we pre-multiply the
    ** divisor by 2.  This is compensated for by adjusting the final exponent.
    ** This can fail and cause No Divide (etc) if operands aren't normalized.
    */
    b = x_ash(b, 1);		/* Make divisor be bigger */
    if (op10m_camge(d.HI, b)) {
	DEBUGPRF(("FDVR: Fail, N: %lo,%lo >= D: %lo,%lo\n",
			DBGV_W(d.HI), DBGV_W(b)));

	OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_DIV);
	return a;		/* Fail, return original dividend */
    }

    op10m_setz(d.LO);		/* Set up dividend */
    DEBUGPRF(("FDVR: N: %lo,%lo|%lo,%lo  D: %lo,%lo\n", DBGV_D(d), DBGV_W(b)));

    if (!ddivstep(&d, b, 30, 0)) {	/* Divide them, get double result */
	OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_DIV);
	return a;
    }
    exp += 35-30;			/* Adjust for partial division */

    DEBUGPRF(("FDVR: Q: %lo,%lo R: %lo,%lo  E: %o\n", DBGV_D(d), exp));

    if (!wskipn(d.HI))		/* Check for zero result */
	return d.HI;

    /* To compensate for possible unnormalized operands and roundup carry,
    ** we do a completely general normalization and round.  Note we've already
    ** tested for 0 so bits definitely exist.
    */
    i = op10ffo(d.HI) - 9;		/* Find first one-bit, derive shift */
    if (dornd && i < 0) {		/* If right-shifting, */
	/* Note max bits needed are 9, so "int" is big enough */
	register int bit = 1 << (-i-1);	/* Find last bit that will vanish */
	if (RHGET(d.HI) & bit) { 		/* If need to round up, */
	    static w10_t wrnd;
	    LRHSET(wrnd, 0, bit);
	    op10m_add(d.HI, wrnd);		/* do it */
	    DEBUGPRF(("ROUNDED UP!\n"));
	    if (i != (op10ffo(d.HI)-9)) {	/* See if carried */
		DEBUGPRF(("CARRIED!\n"));
		if (wskipl(d.HI)) {		/* Yes, into sign bit? */
		    DEBUGPRF(("CARRIED INTO SIGN!\n"));
		    d.HI = x_ash(d.HI, -1);	/* Yes, yecch!  Special */
		    op10m_signclr(d.HI);	/* handling to undo damage */
		    --exp;
		} else --i;		/* Carried but compensation trivial */
	    }
	}
    }
    if (i != 8)
	DEBUGPRF(("ABNORMALIZATION: %d\n", i));

    d.HI = x_ash(d.HI, i);		/* Shift the fraction into place */
    exp -= i + 8;			/* Adjust exponent for shift */

    /* Fraction all done, just add exponent back now */
    LHSET(d.HI, (LHGET(d.HI) | ((h10_t)(exp & SFEMAGS) << SFFBITS)));
#if IFFLAGS
    if (exp < 0) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_FXU);
    else if (exp > SFEMAGS) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV);
#endif

    if (sign)
	op10m_movn(d.HI);
    return d.HI;
}

#endif /* 0 - old version */

/* Single-precision Floating-point Conversions */

/* SFNORM - Returns normalized single-precision floating point, given
**	full-word integer fraction and exponent to use if fraction were
**	already in proper position (first significant bit at B9).
**	Sets flags if exponent overflows.
*/
static w10_t sfnorm(register int exp,
		    register w10_t w,	/* Contains signed integer fraction */
		    int rnd)		/* TRUE to round result */
{
    register int i, rbit;	/* rbit only needs 9 bits, so "int" safe */

    /* Normalizing a negative number has a lot of gross hair associated
    ** with it.  By far the simplest thing to do is convert all negative
    ** numbers to positive values and only negate back at the end of
    ** normalization.
    ** Skeptical?  See the old code here, included for posterity.  Good luck.
    */
#if 0 /* Old code */
#  define bmask(n) (((h10_t)1<<(n))-1)	/* Mask of N right-justified bits */
	/* Alternative def:
		define bmask(n) (01777777>>(n))
	  Then refs change from
		bmask(18-i)	=>	bmask(i+1)
		bmask(36-i)	=>	bmask(i-17)
	  Only used in sfnorm()...
	*/
    if (wskipl(w)) {		/* Find 1st significant bit */
	i = op10ffo(op10setcm(w));
	/* Messy - must see if any lower bits are set, and if not,
	** adjust position back to compensate for 2s-complement form.
	*/
	if (i < 17) {			/* Check LH and RH both? */
	    if (!RHGET(w) && !(LHGET(w) & bmask(18-i))) --i;
	} else if (i < 36) {		/* Check only RH */
	    if (!(RHGET(w) & bmask(36-i))) --i;
	} else i = 35;			/* w is -1 */

    } else i = op10ffo(w);
    DEBUGPRF(("SFNORM: %lo,%lo i=%d\n", DBGV_W(w), i));
    if (i >= 36)
	return w10zero;

    /* i now has position of first fraction bit; move it into place.
    ** For a negative number, the roundup bit is 1 only if at least
    ** one lower bit is set.
    */
    i -= 9;			/* Make relative to proper position, B9 */
    if (rnd && i < 0) {		/* If rounding OK and losing bits */
	rbit = 1 << ((-i)-1);	/* Get bitmask for last bit lost */
	if (wskipl(w))		/* If negative, must have at least one 1-bit */
	    rbit = RHGET(w) & (rbit-1);	/* in bits lost off right  */
	else rbit = RHGET(w) & rbit;
    } else rbit = 0;
    if (i)
	w = x_ash(w, i);	/* Derive shift and do it */
    exp -= i;			/* Adjust exponent */

    /* At this point, number in is normalized, and rbit is set if a
    ** roundup increment must be done (which in rare cases can trigger
    ** a single-shift renormalization).
    */
    if (rbit) {
	DEBUGPRF(("ROUNDUP DONE"));
	op10m_inc(w);			/* Add then check for overflow */
	if (wskipl(w)) {		/* Negatives are messy */
	    if (LHGET(w) & 0400) {	/* If first fract bit is "wrong" */
		if ((LHGET(w) & 0377) || RHGET(w)) {	/* See if really OK */
		    --exp, w = x_ash(w, 1);	/* Nope, shift up */
		    DEBUGPRF((" - NEG RENORMALIZED!"));
		}
	    }
	} else if (!(LHGET(w) & 0400)) {
	    ++exp, LHSET(w, 0400);		/* Positive overflow, fix up */
	    DEBUGPRF((" - POS RENORMALIZED!"));
	}
	DEBUGPRF(("\n"));
    }

    /* Ta da!  Put exponent back in.  We can check now for overflow or
    ** underflow.
    */
    if (wskipl(w))	/* If sign set, put ones-complement of exp */
	LHSET(w, (LHGET(w) ^ ((h10_t)(exp & 0377) << 9)));
    else
	LHSET(w, (LHGET(w) | ((h10_t)(exp & 0377) << 9)));

#else 	/* End old code, start new code */

    /* New regime.  Check for negative fraction and convert to use
    ** positive value instead.  The fraction is always assumed to be signed.
    **
    ** Note that the maximum negative number cannot be made positive,
    ** but this is compensated for by remembering the sign and using
    ** unsigned operations.
    */
    register int sign;

    if (sign = wskipl(w))
	op10m_movn(w);		/* Negate the fraction, max neg is OK. */
    i = op10ffo(w);

    DEBUGPRF(("SFNORM: %lo,%lo i=%d exp=%o\n", DBGV_W(w), i, exp));

    if (i >= 36)
	return w10zero;		/* No magnitude bits, so just return 0 */

    /* i now has position of first fraction bit; move it into place.
    */
    i -= 9;			/* Make relative to proper position, B9 */
    exp -= i;			/* Adjust exponent */
    if (rnd && i < 0)		/* If rounding OK and losing bits */
	rbit = RHGET(w)		/* Find value of last bit that will be lost */
		& (1 << ((-i)-1));
    else rbit = 0;

    if (i < 0)			/* Now do logical (unsigned) shift! */
	RSHIFTM(w, -i);
    else if (i > 0)
	LSHIFTM(w, i);

    DEBUGPRF(("SFNORMED: %lo,%lo i=%d exp=%o rbit=%o\n",
				DBGV_W(w), i, exp, rbit));

    /* At this point, number in w is normalized, and rbit is set if a
    ** roundup increment must be done.  In the rare case that this
    ** increment overflows, its value will be 1000,,0 and will trigger
    ** a single-shift renormalization.
    */
    if (rbit) {
	op10m_inc(w);			/* Add, then check for overflow */
	if (LHGET(w) > SFFMASK) {
	    ++exp;			/* Positive overflow, fix up */
	    LHSET(w, 0400);
	    DEBUGPRF(("ROUNDUP - RENORMED!\n"));
	} else
	    DEBUGPRF(("ROUNDUP\n"));
    }

    /* Fraction is normalized with exponent bits clear, can just OR
    ** exponent back in, and negate if needed to finish off result.
    */
    op10m_tlo(w, ((h10_t)(exp & SFEMAGS) << SFFBITS));
    if (sign)
	op10m_movn(w);

    /* Ta da! Check before returning for overflow or underflow.
    ** Note that the negation, if one, can never cause either since a
    ** positive number can always be negated.
    */
#endif /* New code */

#if IFFLAGS
    if (exp < 0) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_FXU);
    else if (exp > SFEMAGS) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV);
#endif

    return w;
}

w10_t
op10fltr(register w10_t w)
{
    return sfnorm(SFEEXCESS+27, w, 1);	/* Normalize and round */
}

/* FSC
**	Testing weirdness: when AC has a single-bit fraction (either
**	positive or complemented) and E is 400000, then *only* Floating
**	Underflow is set on a real DEC-2065.  The proc ref man claims
**	Overflow and Floating Overflow should always also be set, and
**	that's what this code does.
*/
w10_t
op10fsc(register w10_t w, register h10_t h)
{
    register int exp;

    SFSETUP(exp, w);		/* Set up exponent and fraction */
    exp += imm8op(h);		/* Scale per immediate arg */
    return sfnorm(exp, w, 0);	/* Put float together, don't round */
}

w10_t op10fix(w10_t w)
{
    op10xfix(&w, 0);		/* Fix, no round */
    return w;
}

w10_t op10fixr(w10_t w)
{
    op10xfix(&w, 1);		/* Fix, with round */
    return w;
}

/* Single Float->Fix conversion.
**	Note that rounding does NOT work the same way as for floats.
**	See PRM 2-28.
**	+(1.5) is rounded to +2, but -(1.5) is rounded to -1!
**
** If the first bit of the fraction is 1 for a positive number,
**	then 1 is added to the integral part (thus increasing its magnitude).
** If the first bit of the fraction is 1 for a negative number,
**	then 1 is added to the integral part (thus lowering its magnitude).
**
**	This produces asymmetric results due to twos-complement nature.
**	Apparently this was done for simplicity or to follow the "Algol
**	standard".
*/
int op10xfix(w10_t *aw, int dornd)
{
    register w10_t w = *aw;

#if 1
    register int i, sign;
    register uint18 rbit;

    SF_POSSETUP(sign, i, w);	/* Set up exponent, get positive fract */

    /* Apply exponent size test now */
    if (i > (SFEEXCESS+35)) {		/* If integer wd be too big, */
	OP10_PCFSET(PCF_ARO+PCF_TR1);	/* set flags */
	return 0;			/* and return arg unchanged */
    }
    
    /* OK to proceed!
    ** Fraction should now be a 27-bit positive integer.  It will be a 28-bit
    ** value of 1000,,0 only if given an unnormalized negative float with
    ** no one-bits in the fraction part.
    */
    i -= (SFEEXCESS+27);	/* Get # bits to shift fraction */
    if (i <= -28) {		/* If shifting it to oblivion */
	*aw = w10zero;		/* just return zero, skip rigmarole */
	return 1;
    }

    /* Determine whether to round.
    **	Note special test for negative values, needed in order to
    **	emulate peculiar way FIXR does rounding.
    */
    if (dornd && i < 0) {	/* If rounding OK and losing bits */
	if (i < -H10BITS) {	/* Find value of last bit that will be lost */
	    rbit = LHGET(w)	/* Bit comes from LH */
		    & (1 << ((-i)-(H10BITS+1)));
	} else {
	    rbit = RHGET(w)	/* Bit comes from RH */
		    & (((uint18)1) << ((-i)-1));
	}
	if (rbit && sign	/* Special test for negative .5 */
	  && !op10m_tdnn(w, w10loobits[(-i)-1]))
	    rbit = 0;		/* No round if so */
    } else rbit = 0;

    if (i < 0) {		/* Now do logical (unsigned) shift! */
	RSHIFTM(w, -i);
	if (rbit)		/* Need any rounding? */
	    op10m_inc(w);	/* Yep, trivial */
    } else if (i > 0)
	LSHIFTM(w, i);

    if (sign)			/* Now negate back if necessary */
	op10m_movn(w);

#else	/* New regime! */

    register int i;
    register uint18 rbit;

    SFSETUP(i, w);		/* Set up exponent, get signed fract */

    /* Apply exponent size test now */
    if (i > (SFEEXCESS+35)) {		/* If integer wd be too big, */
	OP10_PCFSET(PCF_ARO+PCF_TR1);	/* set flags */
	return 0;			/* and return arg unchanged */
    }
    
    /* OK to proceed!
    ** Fraction should now be a 27-bit positive or negative integer.
    ** It will be a 28-bit value of -1000,,0 only if given an unnormalized
    ** negative float with no one-bits in the fraction part.
    */
    i -= (SFEEXCESS+27);	/* Get # bits to shift fraction */
    if (i <= -28) {		/* If shifting it to oblivion */
	*aw = w10zero;		/* just return zero, skip rigmarole */
	return 1;
    }

    /* Determine whether to round.
    */
    if (dornd && i < 0) {	/* If rounding OK and losing bits */
	if (i < -H10BITS) {	/* Find value of last bit that will be lost */
	    rbit = LHGET(w)	/* Bit comes from LH */
		    & (1 << ((-i)-(H10BITS+1)));
	} else {
	    rbit = RHGET(w)	/* Bit comes from RH */
		    & (((uint18)1) << ((-i)-1));
	}
    } else rbit = 0;

    if (i < 0) {		/* Now shift! */
	if (wskipl(w))
	    NASH_RSHIFTM(w, -i); /* Negative, must do ASH */
	else
	    RSHIFTM(w, -i);	/* Positive, fast logical is OK */
	if (rbit)		/* Need any rounding? */
	    op10m_inc(w);	/* Yep, trivial */
    } else if (i > 0)
	LSHIFTM(w, i);		/* Left-shift can always be logical */
#endif

    *aw = w;			/* Return result */
    return 1;			/* Say succeeded */
}

/* Floating-point Double-precision arithmetic.  REAL moby hair! */


dw10_t op10dfad(dw10_t a, dw10_t b)
{
    return dfad(a, b, 0);
}

dw10_t op10dfsb(dw10_t a, dw10_t b)
{
    return dfad(a, b, 1);
}

/* Common DFAD/DFSB routine.

	The PRM says addition is done in a "triple-length" sum.  It's
unclear exactly how many magnitude bits this is.
	For KLH10_CPU_KLX at least, 3 words of 27+35+35 bits are not enough;
the following computation failed when using just one extra word for
the "lost" roundup bits, because the low bit of the 2nd operand was
being shifted out of the 3rd word.  Even though these operands are
not normalized, ideally the emulation should still produce the
same results.
	
2065: 	dfad   700000,0,0,0    776000,0,0,1  =>  677400,0,0,0
Emul:	dfad   700000,0,0,0    776000,0,0,1  =>  677377,777777,377777,777777

	So, for the time being this code uses 4 words, with the lowest
word used only to hold shifted-out bits for possible rounding computations.

*/

static dw10_t dfad(register dw10_t a,
		   register dw10_t b,
		   int negb)
{
    register int i, expa, expb;
    register uint18 rbit;		/* May be 1<<17 */
    register dw10_t rd;

    SFSETUP(expa, a.HI);		/* Set up exponent and fraction */
    SFSETUP(expb, b.HI);		/* Ditto for other arg */

    if (negb)				/* If actually subtracting B, */
	op10m_dmovn(b);			/* negate double fraction now */

    /* Now see which exponent is smaller; get larger in A, smaller in B.
    ** This needs to be done even if one of the args is completely zero, so
    ** that the other is set up in A for the normalization code.
    */
    if ((i = expa - expb) < 0) {
	dw10_t tmp;			/* Swap args */
	int etmp;
	etmp = expb, tmp = b;
	expb = expa, b = a;
	expa = etmp, a = tmp;
	i = -i;
    }
    DEBUGPRF(("DFAD A: (%o) %lo,%lo,%lo,%lo  B: (%o) %lo,%lo,%lo,%lo i=%d\n",
			expa, DBGV_D(a), expb, DBGV_D(b), i));

    /* Now right-shift B by the number of bits in i, then add to A.
    */
    rd = x_ashc(b, 70-i);	/* Get bottom 2 words of a 4-word ASHC */
    b = x_ashc(b, -i);		/* Shift B for adding */
    DEBUGPRF(("DFAD B: (%o) %lo,%lo,%lo,%lo  R: %lo,%lo,%lo,%lo\n",
			expa, DBGV_D(b), DBGV_D(rd)));

    a = x_dadd(a, b);		/* Add into A */

    DEBUGPRF(("  preN: (%o) %lo,%lo,%lo,%lo  R: %lo,%lo,%lo,%lo\n",
			expa, DBGV_D(a), DBGV_D(rd)));

    /* A now has sum, with the low-order 3rd word in RD's high word.
    ** The sign of A is correct; that of RD is irrelevant.
    ** Check high 10 bits to determine normalization step.
    */
    rbit = 0;
    switch (LHGET(a.HI) >> 8) {
	case 00004>>2:		/* Normalized positive fraction */
	    DEBUGPRF(("WINNING POSITIVE\n"));
	    rbit = (LHGET(rd.HI) & (HSIGN>>1));
	    break;

	case 00010>>2:		/* Overflow of pos fraction */
	case 00014>>2:
	    DEBUGPRF(("OVERFLOW POSITIVE\n"));
	    rbit = RHGET(a.LO) & 01;
	    a = x_ashc(a, -1);		/* Normalize by shifting right */
	    ++expa;			/* And adding one to exponent */
	    break;

	case 07760>>2:		/* Overflow of neg fraction, maybe zero mag */
	case 07764>>2:		/* Overflow of neg fraction, nonzero mag */
	    DEBUGPRF(("OVERFLOW NEGATIVE\n"));
	    rbit = RHGET(a.LO) & 01;
	    a = x_ashc(a, -1);	/* First normalize into 0777 case, */
	    rd = x_ashc(rd, -1);	/* Shift RD as well, */
	    if (rbit)		/* Copy bit lost from A */
		LHSET(rd.HI, (LHGET(rd.HI) | (HSIGN>>1)));
	    else
		LHSET(rd.HI, (LHGET(rd.HI) & ~(HSIGN>>1)));
	    ++expa;		/* then fall thru below to check! */

	case 07770>>2:		/* Normalized negative fraction (probably) */
	    rbit = LHGET(rd.HI) & (HSIGN>>1);
	    DEBUGPRF(("DFAD:NEG: (%o) %lo,%lo,%lo,%lo  %lo,%lo,%lo,%lo  r=%lo\n",
		expa, DBGV_D(a), DBGV_D(rd), (long)rbit ));

	    if (!(LHGET(rd.HI)&(HMASK>>2)) && !RHGET(rd.HI)
	      && !wmagskipn(rd.LO)) {
		rbit = 0;		/* No round if no other bits */
		DEBUGPRF(("NEG - RND DROPPED - "));
	    }
	    if ((LHGET(a.HI)&0777) || RHGET(a.HI) || rbit || wmagskipn(a.LO)) {
		DEBUGPRF(("WINNING NEGATIVE\n"));
		break;
	    }
	    DEBUGPRF(("OVERFLOW NEG (ZERO MAG)\n"));
	    rbit = 0;
	    LHSET(a.HI, 0777400);	/* Pretend right-shifted 1 bit */
	    ++expa;
	    break;

	/* Anything else is an unnormalized result and must be handled
	** using fully general case.
	*/
	case 0:
	    if (!wskipn(a.HI) && !wmagskipn(a.LO) && !wmagskipn(rd.HI)) {
		DEBUGPRF(("DFAD: S: ZERO RESULT!\n"));
		return dw10zero;
	    }
	default:
	    /* Must mess with RD, sigh.  Ensure it has correct sign bit,
	    ** then find the first fraction bit in the 3-word sum.
	    */
	    b.LO = rd.HI;
	    if (wskipl(a.HI)) {
		op10m_signset(b.LO);
		if ((i = op10ffo(op10setcm(a.HI))) >= 36) {
		    if ((i = op10ffo(op10setcm(a.LO))) >= 36) {
			i = op10ffo(op10setcm(b.LO)) + 70;
		    } else i += 35;
		}
	    } else {
		op10m_signclr(b.LO);		/* Clear sign bit */
		if ((i = op10ffo(a.HI)) >= 36) {
		    if ((i = op10ffo(a.LO)) >= 36) {
			i = op10ffo(b.LO) + 70;
		    } else i += 35;
		}
	    }
	    DEBUGPRF(("RENORMALIZATION: first %c-bit at %d = ASHC %d\n",
			(wskipl(a.HI) ? '0' : '1'), i, i-9));

	    /* i now has position of first fraction bit; move it into place. */
	    b.HI = a.LO;	/* Duplicate middle word for easy shift */
	    if ((i -= 9) < 35) {	/* Right shift or small left shift */
		a = x_ashc(a, i);
		b = x_ashc(b, i);
		if (i > 0) a.LO = b.HI;
	    } else if (i < 70) {	/* One-word or larger shift */
		a = x_ashc(b, i-35);
		op10m_setz(b.LO);
	    } else {			/* Two-word or larger shift */
		a.HI = x_ash(b.LO, i-70);
		op10m_setz(a.LO);
		op10m_setz(b.LO);
	    }
	    DEBUGPRF(("DFAD: S1:(%o) %lo,%lo,%lo,%lo  R: %lo,%lo\n",
			expa-i, DBGV_D(a),
			(long)LHGET(b.LO), (long)RHGET(b.LO)));

	    /* Now find roundup bit, and do fixups for negative results */
	    rbit = LHGET(b.LO) & (HSIGN>>1);	/* Find roundup bit */
	    if (wskipl(a.HI)) {			/* Special checks for neg */
		if (!(LHGET(b.LO)&(HMASK>>2)) && !RHGET(b.LO))
		    rbit = 0;			/* No round if no other bits */
		if (LHGET(a.HI) == 0777000 && !RHGET(a.HI) && !wmagskipn(a.LO)
		  && !rbit) {
		    ++expa, a = x_ashc(a, -1);	/* Make LH 0777400 */
		    DEBUGPRF(("    NEG FIXUP\n"));
		}
	    }
	    expa -= i;			/* Adjust exponent */
	    break;
    }

    /* At this point, number in A is normalized, and rbit is set if a
    ** roundup increment must be done (which in rare cases can trigger
    ** a single-shift renormalization).
    */
    if (rbit) {
	DEBUGPRF(("ROUNDUP DONE"));
	a = op10dfinc(a);		/* Add then check for overflow */
	if (wskipl(a.HI)) {		/* Negatives are messy */
	    if (LHGET(a.HI) & 0400) {	/* If first fract bit is "wrong" */
		if ((LHGET(a.HI) & 0377) || RHGET(a.HI)	/* See if really OK */
		  || wmagskipn(a.LO)) {
		    --expa, a = x_ashc(a, 1);		/* Nope, shift up */
		    DEBUGPRF((" - NEG RENORMALIZED!"));
		}
	    }
	} else if (!(LHGET(a.HI) & 0400)) {
	    ++expa, LHSET(a.HI, 0400);		/* Positive overflow, fix up */
	    DEBUGPRF((" - POS RENORMALIZED!"));
	}
	DEBUGPRF(("\n"));
    }
    DEBUGPRF(("DFAD: S: (%o) %lo,%lo,%lo,%lo\n", expa, DBGV_D(a)));

    /* Ta da!  Put exponent back in (it may have underflowed or overflowed
    ** but nothing we can do about it).
    */
    if (wskipl(a.HI))	/* If sign set, put ones-complement of exp */
	LHSET(a.HI, (LHGET(a.HI) ^ ((h10_t)(expa & SFEMAGS) << 9)));
    else
	LHSET(a.HI, (LHGET(a.HI) | ((h10_t)(expa & SFEMAGS) << 9)));
#if IFFLAGS
    if (expa < 0) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_FXU);
    else if (expa > SFEMAGS) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV);
#endif
    op10m_signclr(a.LO);	/* Double flt always zeros low sign */
    return a;
}


dw10_t op10dfmp(register dw10_t a, register dw10_t b)
{
    register qw10_t q;
    register int exp, expb;
    register int sign;
    int signb;

    /* Make operands positive and remember sign of result
    ** These macros guarantee a positive number of at most 62 magnitude bits,
    ** even for the max-neg-# case.
    */
    SF_DPOSSETUP(sign, exp, a);
    SF_DPOSSETUP(signb, expb, b);

    DEBUGPRF(("DFMP: A: (%o) %lo,%lo,%lo,%lo  B: (%o) %lo,%lo,%lo,%lo\n",
			exp, DBGV_D(a),	expb, DBGV_D(b)));


    sign ^= signb;		/* Set 0 if both were negative */
    exp += expb - SFEEXCESS;	/* Find resulting exponent */

    /* Have two 62-bit magnitude numbers, multiply them (124-bit product) */

    q = x_dmul(a, b);		/* Multiply the numbers, get quad result */

    DEBUGPRF(("DFMP Q: %lo,%lo,%lo,%lo|%lo,%lo,%lo,%lo exp=%o (%d.)\n",
			DBGV_Q(q), exp, exp));

    /* Since there are 124 bits of product with 35 magnitude bits in each
    ** word, the high word should contain 124-105 = 19 high mag bits.
    ** Thus the top 36-19=17 bits of product should be empty.
    ** If everything was normalized, the next bit (start of fraction, B17)
    ** should be 1 and we just shift everything left by 17-9=8 bits to get
    ** in place.
    ** If not, must normalize by looking for first bit of fraction.
    */
    if (!op10m_tlnn(q.D0.HI, 1<<(H10BITS-(1+17)))) {	/* Test B17 */
	/* No high fraction bits set... see if result was 0 */
	if (!RHGET(q.D0.HI) && !wskipn(q.D0.LO) && !dskipn(q.D1))
	    return q.D0;		/* Zero product, return 0.0 */
	do {
	    q = x_qash1(q);		/* ASH quad up one bit */
	    --exp;			/* Adjust exponent accordingly */
	} while (!op10m_tlnn(q.D0.HI, 1<<(H10BITS-(1+17))));
					/* Until get high fract bit */
    }

    /* Fraction in expected place, ASH 17-9=8 bits left to align for exp */
    q.D0 = x_ashc(q.D0, 8);		/* Do ASHC bringing in 8 zeros */
    op10m_iori(q.D0.LO,			/* Add 8 bits from 3rd wd */
		(LHGET(q.D1.HI) >> ((HBITS-1)-8)));

    DEBUGPRF(("DFMP2Q: %lo,%lo,%lo,%lo|%lo,%lo,%lo,%lo exp=%o (%d.)\n",
			DBGV_Q(q), exp, exp));

    /* Now do rounding.  See if bit lower than LSB is set (this is the next
    ** bit after the ones added from 3rd word).
    ** If so, add one to LSB.  This will overflow only if the fraction was
    ** all ones, resulting in a roundup to all zeros except the low exponent
    ** bit.  If so, re-normalize by simply resetting the LH to right value
    ** and adjusting the exponent.
    */
    if (LHGET(q.D1.HI) & (1 << ((HBITS-1)-(8+1))) ) {
	/* Special hack in attempt to emulate hardware.  Don't round if
	** result is going to be negative and there are no more low bits
	** set in the product (to the right of the round bit).
	*/
	if (sign && !op10m_tlnn(q.D1.HI, 0377)
	  && !RHGET(q.D1.HI) && !wskipn(q.D1.LO)) {
	    DEBUGPRF(("DFMP ROUNDUP NEGSKIPPED\n"));
	} else {
	    DEBUGPRF(("DFMP ROUNDUP\n"));
	    q.D0 = op10dfinc(q.D0);
	    if (op10m_tlnn(q.D0.HI, SFELHF)) {	/* If carried out of fract */
		LHSET(q.D0.HI, SFFHIBIT);	/* reset to proper value */
		++exp;			/* and adjust exponent */
		DEBUGPRF(("DFMP POS RENORM\n"));
	    }
	}
    }

    /* Fraction all done, just add exponent back now */
    LHSET(q.D0.HI, (LHGET(q.D0.HI) | ((h10_t)(exp & SFEMAGS) << SFFBITS)));
#if IFFLAGS
    if (exp < 0) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_FXU);
    else if (exp > SFEMAGS) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV);
#endif

    if (sign)
	op10m_dmovn(q.D0);
    return q.D0;
}

/* DFDV - note no-divide case simply returns 1st arg instead of
**	failing explicitly.
*/

dw10_t op10dfdv(register dw10_t a, register dw10_t b)
{
    qw10_t q;
    register int exp, i;
    register int expb, sign, signb;

#if 1	/* New code */
    /* Make operands positive and remember sign of result */
    /* The KLX appears to have a peculiarly rigid filter for the
    ** args of DFDV which the other DF ops don't share.
    ** If the 2nd arg (divisor) is 0, result is always no-divide.
    ** else, if the 1st arg is 400000,,0 0,,0
    **		then the result is a No-Divide (regardless of 2nd arg).
    ** else, if the 1st arg has no fraction bits set,
    **		then the result is always 0, even though this could be
    **		an unnormalized value.  This test is NOT applied
    **		to the divisor!!!
    */
    SF_DPOSSETUP(signb, expb, b);	/* Set up 2nd arg normally */
    if (!wskipn(b.HI) && !wmagskipn(b.LO)) {	/* Check for zero divisor */
	OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_DIV);
	return a;		/* Ugh, No Divide error.  Return original op */
    }

    /* Now set up A.  Note a different mechanism is used in order to emulate
    ** the way that the KL apparently chops off all exponent bits when
    ** extracting the fraction bits, thus turning an unnormalized negative
    ** number of (e.g.) 777000,,0 into 0,,0 instead of 1000,,0.
    **/
    q.D0 = a;			/* Set up A in Q (preserves orig value) */
    if (wskipl(q.D0.HI)) {
	op10m_dmovn(q.D0);
	if (wskipl(q.D0.HI)) {
	    OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_DIV);
	    return a;		/* Ugh, No Divide error.  Return original op */
	}
	sign = !signb;
    } else sign = signb;
    exp = SFEGET(q.D0.HI);	/* Get sign and exponent */
    op10m_tlz(q.D0.HI, SFELHF);	/* Chop out exponent bits */
    if (!wskipn(q.D0.HI) && !wmagskipn(q.D0.LO))
	return dw10zero;	/* No fraction bits, return 0 */

    DEBUGPRF(("DFDV: A: (%o) %lo,%lo,%lo,%lo  B: (%o) %lo,%lo,%lo,%lo\n",
	exp, DBGV_D(q.D0), expb, DBGV_D(b)));

    exp += SFEEXCESS - expb;	/* Do division on exponents */

    /* Now guaranteed to have two positive 62-bit magnitude numbers. */


#else /* end new, start old */

    /* Make operands positive and remember sign of result */
    q.D0 = a;
    if (sign = wskipl(a.HI)) {
	op10m_dmovn(q.D0);

	/* KLX always fails immediately with No Divide if dividend is
	** 400000,,0 ? 0,,0
	*/
	if (wskipl(a.HI)) {
	    OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_DIV);
	    return a;		/* Ugh, No Divide error.  Return original op */
	}
    }
    if (wskipl(b.HI)) {
	op10m_dmovn(b);
	sign = !sign;		/* Invert sense of sign flag */
    }

    /* Subtract exponents.
    */
    exp = 0200 + (LHGET(q.D0.HI) >> 9) - (LHGET(b.HI) >> 9);

    /* Get two 62-bit magnitude numbers and divide them.
    ** In order to ensure that the division can succeed, we pre-multiply the
    ** divisor by 2.  This is compensated for by adjusting the final exponent.
    ** This can fail with No Divide if the operands aren't normalized.
    */
    /* Mask out everything but fraction */
    LHSET(q.D0.HI, (LHGET(q.D0.HI) & 0777));
    LHSET(b.HI, (LHGET(b.HI) & 0777));
#endif	/* Old code */

    /* Divide them.
    ** Note that floating fractions, if normalized, will always have their most
    ** significant bit in the same place, which confuses the ordinary division
    ** test.
    ** So, in order to ensure that the division can succeed, we pre-multiply
    ** the divisor by 2 (to make it a 63-bit value).
    ** This is compensated for by adjusting the final exponent.
    ** This can fail with No Divide if the operands aren't normalized.
    **
    ** Note that the udcmpl test relies on both args being positive and the
    ** low sign bit being clear.
    */
    op10m_signclr(q.D0.LO);		/* Ensure valid for udcmpl test */
    op10m_signclr(b.LO);
    if (!op10m_udcmpl(q.D0, b)) {	/* If q.D0 >= b */
	DEBUGPRF(("DFDV Divisor pre-multiply!\n"));
	b = x_ashc(b, 1);		/* Make divisor be bigger */
	++exp;
	if (!op10m_udcmpl(q.D0, b)) {	/* If q.D0 >= b */
	    OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_DIV);
	    return a;		/* Ugh, No Divide error.  Return original op */
	}
    }
    q.D1 = dw10zero;		/* Set up dividend */
#if 0
    DEBUGPRF(("DFDV: N: %lo,%lo,%lo,%lo|%lo,%lo,%lo,%lo  D: %lo,%lo,%lo,%lo\n",
			DBGV_Q(q), DBGV_D(b)));
#endif

    if (!qdivstep(&q, b, 63)) {
	OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_DIV);
	return a;
    }
    exp += 70-63;		/* Compensate for dividing only 63 steps */

    DEBUGPRF(("DFDV: Q: %lo,%lo,%lo,%lo R: %lo,%lo,%lo,%lo  E: %o\n",
			DBGV_Q(q), exp));

    if (!dskipn(q.D0))		/* Check for zero result */
	return q.D0;

    /* To compensate for possible unnormalized operands and roundup carry,
    ** we do a completely general normalization and round.  Note we've already
    ** tested for 0 so bits definitely exist.
    */
    i = adffo(q.D0) - 9;		/* Find first one-bit, derive shift */
    if (i < 0) {			/* If right-shifting, */
	/* bit only needs 9 bits, so "int" is safe */
	register int bit = 1 << (-i-1);	/* Find last bit that will vanish */
	if (RHGET(q.D0.LO) & bit) { 		/* If need to round up, */
	    static dw10_t dwrnd;
	    LRHSET(dwrnd.HI, 0, 0);
	    LRHSET(dwrnd.LO, 0, bit);
	    q.D0 = x_dadd(q.D0, dwrnd);	/* do it */
	    DEBUGPRF(("ROUNDED UP!  Bit %o\n", bit));
	    if (i != (adffo(q.D0)-9)) {		/* See if carried */
		DEBUGPRF(("CARRIED!\n"));
		if (wskipl(q.D0.HI)) {	/* Yes, into sign bit? */
		    DEBUGPRF(("CARRIED INTO SIGN!\n"));
		    q.D0 = x_ashc(q.D0, -1);	/* Yes, yecch!  Special */
		    op10m_signclr(q.D0.HI);	/* handling to undo damage */
		    --exp;
		} else --i;		/* Carried but compensation trivial */
	    }
	}
    }
    DEBUGPRF(("%sNORMALIZATION: %d\n", ((i != -8) ? "AB" : ""), i));

    q.D0 = x_ashc(q.D0, i);		/* Shift the fraction into place */
    exp -= i + 8;			/* Adjust exponent for shift */

    /* Fraction all done, just add exponent back now */
    LHSET(q.D0.HI, (LHGET(q.D0.HI) | ((h10_t)(exp & 0377) << 9)));
#if IFFLAGS
    if (exp < 0) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_FXU);
    else if (exp > 0377) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV);
#endif

    if (sign)
	op10m_dmovn(q.D0);
    return q.D0;
}

static int qdivstep(qw10_t *aq,
		    register dw10_t d,
		    register int nmagbits)
{
    register qw10_t qw;
    dw10_t quot;
    register int qbit;

    /* Apply initial test - high order double must be less than abs val of 
    ** divisor, else overflow & no divide.
    */
    qw = *aq;			/* Get quadword */
    qw.D0 = x_dsub(qw.D0, d);
    qbit = (wskipl(qw.D0.HI) ? 0 : 1);	/* Get first quotient bit */
    if (qbit)
	return 0;		/* Overflow, no divide */

    DEBUGPRF(("qdivstep: %lo,%lo,%lo,%lo|%lo,%lo,%lo,%lo  %lo,%lo,%lo,%lo  nmags=%d, qbit=%o\n",
		DBGV_Q(qw), DBGV_D(d), nmagbits, qbit));


    /* First quotient bit (future sign bit) is always 0.
    ** Because quotient is always positive, its left-shift in the loop
    ** is simple enough to do inline.
    */
    op10m_setz(quot.HI);		/* Clear current & future signs */
    op10m_setz(quot.LO);		/* Clear low word in case small N */
    while (--nmagbits >= 0) {		/* Loop N times for N magnitude bits */
	qw = x_qash1(qw);		/* Shift new bit into dividend */
	qw.D0 = (qbit ? x_dsub(qw.D0, d) : x_dadd(qw.D0, d));
	qbit = (wskipl(qw.D0.HI) ? 0 : 1);	/* Get new quotient bit */

	/* Shift quotient left by 1 */
	LSHIFTM(quot.HI, 1);		/* Shift both words left 1 */
	LSHIFTM(quot.LO, 1);
	if (wskipl(quot.LO))		/* If low sign now set, */
	    op10m_iori(quot.HI, 1);	/* that becomes low bit of high wd */

	op10m_iori(quot.LO, qbit);	/* Add qbit to right end of quotient */

	DEBUGPRF(("qdivstep %d: D: %lo,%lo,%lo,%lo|%lo,%lo,%lo,%lo  Q: %lo,%lo,%lo,%lo \n",
		nmagbits, DBGV_Q(qw), DBGV_D(quot)));
    }
    op10m_signclr(quot.LO);	/* Ensure low sign clear, compensate for
				** fact weren't doing true ASH in loop.
				*/

    qw.D1 = qw.D0;	/* Return quotient & remainder in right places */
    qw.D0 = quot;
    *aq = qw;
    return 1;
}

#if OP10_GFMT

/* Floating-point G-format Double-precision arithmetic.  More moby hair!
*/

static dw10_t x_gfad(dw10_t, dw10_t, int);	/* Aux for GFAD/GFSB */
static dw10_t gfnorm(int, dw10_t, int);
static int aqfffrac(dw10_t, dw10_t);


dw10_t op10gfad(dw10_t a, dw10_t b)
{
    return x_gfad(a, b, 0);
}

dw10_t op10gfsb(dw10_t a, dw10_t b)
{
    op10m_dmovn(b);		/* This appears to work better?!?! */
    return x_gfad(a, b, 0);
}

/* Common GFAD/GFSB routine.

	You'd expect G format to behave more or less the same as D
format.  Well, welcome to reality.  The G format implementation is
different in subtle ways which mainly reveal themselves in the handling
of unnormalized operands.  This may be because it was a late addition
to the KL and must depend completely on ucode software rather than any
inherent hardware features (such as those that might exist for handling
F or D formats).

	In particular, it appears that if the exponent difference
between the operands is larger than 72, the KL simply normalizes the
largest operand and returns that, completely ignoring the other value.

	Also, seems to have same problem as DFAD/DFSB with regard to hanging
on to the lower bits for full normalization and rounding.
	From empirical testing it appears that 2 additional bits are
needed from a "4th word", as for DFAD (36*3 + 3 real bits, or
24+35+35+2 = 96 total magnitude bits).  Simplest to just use a full
extra 35-bit word; HOWEVER, its peculiar behavior in certain cases
suggests that lower bits beyond those 2 are not used in normalization,
yet are still used in rounding -- that is, if any "lost" bits are set, the
higher bits are immediately rounded prior to the normalization shift.
That's not quite right, but they definitely have SOME effect.

	Face it, the only way to emulate this miserable lossage
accurately would be to understand and do exactly what the KL ucode
does.  But is that really a good idea?  I mean, it just affects the low
bit, usually with unnormalized operands, and it would be much simpler
to just use better algorithms - which would return maximally accurate
results even with unnormalized operands, instead of spazzing in
mysterious ways.

*/

static dw10_t x_gfad(register dw10_t a, register dw10_t b, int negb)
{
    register int i, expa, expb;
    register uint18 rbit;		/* May be 1<<17 */
    register dw10_t rd;

    GFSETUP(expa, a.HI);	/* Set up exp & fract for A */
    GFSETUP(expb, b.HI);	/* Set up exp & fract for B */

    if (negb)			/* If actually subtracting B, */
	op10m_dmovn(b);		/* negate fraction now */

    /* Now see which exponent is smaller; get larger in A, smaller in B.
    ** This needs to be done even if one of the args is completely zero, so
    ** that the other is set up in A for the normalization code.
    */
    if ((i = expa - expb) < 0) {
	dw10_t tmp;			/* Swap args */
	int etmp;
	etmp = expb, tmp = b;
	expb = expa, b = a;
	expa = etmp, a = tmp;
	i = -i;
    }

    /* Special KL G format crock. */
    if (i > 72) {
	DEBUGPRF(("GFAD i>72 NORM: i=%d (%o-%o) A: %lo,%lo,%lo,%lo\n",
			    i, expa, expb, DBGV_D(a)));
	return gfnorm(expa, a, 1);
    }

    /* Now right-shift B by the number of bits in i, then add to A. */
    rd = x_ashc(b, 70-i);	/* Get bottom 2 words of a 4-word ASHC */
    b = x_ashc(b, -i);		/* Shift B for adding */
    DEBUGPRF(("GFAD A: (%o) %lo,%lo,%lo,%lo  B: (%o) %lo,%lo,%lo,%lo i=%d\n",
		expa, DBGV_D(a), expb, DBGV_D(b), i)); 

    a = x_dadd(a, b);		/* Add into A */

    DEBUGPRF(("  preN: (%o) %lo,%lo,%lo,%lo  R: %lo,%lo,%lo,%lo\n",
		expa, DBGV_D(a), DBGV_D(rd))); 

    /* A now has sum, with the low-order 3rd word in RD's high word.
    ** The sign of A is correct; that of RD is irrelevant.
    ** Check high bits to determine normalization step, by looking
    ** at the bits of the exponent field PLUS the high fraction bit.
    */
    rbit = 0;
    switch (LHGET(a.HI) >> (H10BITS-(GFEBITS+1))) {
	case 000004>>2:		/* Normalized positive fraction */
	    DEBUGPRF(("WINNING POSITIVE\n"));
	    rbit = (LHGET(rd.HI) & (HSIGN>>1));
	    break;

	case 000010>>2:		/* Overflow of pos fraction */
	case 000014>>2:
	    DEBUGPRF(("OVERFLOW POSITIVE\n"));
	    rbit = RHGET(a.LO) & 01;
	    a = x_ashc(a, -1);		/* Normalize by shifting right */
	    ++expa;			/* And adding one to exponent */
	    break;

	case 077760>>2:		/* Overflow of neg fraction, maybe zero mag */
	case 077764>>2:		/* Overflow of neg fraction, nonzero mag */
	    DEBUGPRF(("OVERFLOW NEGATIVE\n"));
	    rbit = RHGET(a.LO) & 01;
	    a = x_ashc(a, -1);	/* First normalize into 077770 case, */
	    rd = x_ashc(rd, -1);	/* Shift RD as well, */
	    if (rbit)		/* Copy bit lost from A */
		LHSET(rd.HI, (LHGET(rd.HI) | (HSIGN>>1)));
	    else
		LHSET(rd.HI, (LHGET(rd.HI) & ~(HSIGN>>1)));
	    ++expa;		/* then fall thru below to check! */

	case 077770>>2:		/* Normalized negative fraction (probably) */
	    rbit = LHGET(rd.HI) & (HSIGN>>1);
	    DEBUGPRF(("GFAD:NEG: (%o) %lo,%lo,%lo,%lo  %lo,%lo,%lo,%lo  r=%lo\n",
		expa, DBGV_D(a), DBGV_D(rd), (long)rbit ));

	    if (rbit && !(LHGET(rd.HI)&(HMASK>>2)) && !RHGET(rd.HI)
	       && !wmagskipn(rd.LO)) {
		rbit = 0;		/* No round if no other bits */
		DEBUGPRF(("NEG - RND DROPPED - "));
	    }
	    if (op10m_tlnn(a.HI,GFFMASK) || RHGET(a.HI)
	      || rbit || wmagskipn(a.LO)) {
		DEBUGPRF(("WINNING NEGATIVE\n"));
		break;
	    }
	    DEBUGPRF(("OVERFLOW NEG (ZERO MAG)\n"));
	    rbit = 0;
	    LHSET(a.HI, 0777740);	/* Pretend right-shifted 1 bit */
	    ++expa;
	    break;

	/* Anything else is an unnormalized result and must be handled
	** using fully general case.
	*/
	case 0:
	    if (!wskipn(a.HI) && !wmagskipn(a.LO) && !wmagskipn(rd.HI)) {
		DEBUGPRF(("GFAD: S: ZERO RESULT!\n"));
		return dw10zero;
	    }
	default:
	    /* Must mess with RD, sigh.  Ensure it has correct sign bit,
	    ** then find the first fraction bit in the 3 (now 4) word sum.
	    */
	    if (wskipl(a.HI)) {
		op10m_signset(rd.HI);
		if ((i = op10ffo(op10setcm(a.HI))) >= 36) {
		    if ((i = op10ffo(op10setcm(a.LO))) >= 36) {
		/*	op10m_signset(rd.HI); */
			if ((i = op10ffo(op10setcm(rd.HI))) >= 36) {
			    op10m_signset(rd.LO);
			    i = op10ffo(op10setcm(rd.LO)) + 70 + 35;
			} else i += 70;
		    } else i += 35;
		}
	    } else {
		op10m_signclr(rd.HI);		/* Clear sign bit */
		if ((i = op10ffo(a.HI)) >= 36) {
		    if ((i = op10ffo(a.LO)) >= 36) {
		/*	op10m_signclr(rd.HI); */
			if ((i = op10ffo(rd.HI)) >= 36) {
			    op10m_signclr(rd.LO);
			    i = op10ffo(rd.LO) + 70 + 35;
			} else i += 70;
		    } else i += 35;
		}
	    }

	    /* i now has position of first fraction bit; move it into place. */
	    i -= GFEBITS;		/* Find actual shift to perform */
	    DEBUGPRF(("RENORMALIZATION: %d ", i));

	    /* G format hack, check for shift of 72.  This catches
	    ** the case of a negative number with all ones, as well as
	    ** limiting the positive all-zero case more severely than the
	    ** "case 0:" test above.
	    */
	    if (i > 72) {
		DEBUGPRF(("- i>72, RET 0\n"));
		return dw10zero;
	    }

#if 1	/* Test to see if just 2 extra bits are sufficient */

	    if (i > 35 && (LHGET(rd.LO) & 0300000)) {
		a = x_ashc(a, 2);
		op10m_tro(a.LO, (LHGET(rd.HI) >> (18-3)) & 03);
		rd = x_ashc(rd, 2);
		i -= 2;
		expa -= 2;

#if 1
	    /* This code implements "immediate rounding" such that if
	    ** normalization would use any bits from rd.LO (specifically
	    ** the low 33 bits), they are zapped (as above) but
	    ** the high bit is used for an immediate roundup.
	    **
	    ** Try using this only for negative numbers (gah)
	    */
		if (wskipl(a.HI) && (i > 35) && (LHGET(rd.LO)&(H10SIGN>>1))) {
		    DEBUGPRF((" - IMM ROUNDUP"));
		    op10m_inc(rd.HI);		/* Round up */
		    if (!wmagskipn(rd.HI))	/* Propagate */
			a = op10dfinc(a);
		    /* Ugh, must recompute i in case roundup changed it */
		    i = aqfffrac(a, rd) - GFEBITS;
		}
		op10m_setz(rd.LO);
#endif

	    }
#endif
#if 0	/* Test to see if extra 35 bits are sufficient.
	** Answer: Yes, but extra precision prevents emulating KL lossage.
	*/
	    if (i >= 35) {
		a = x_ashc(a, 35);
		a.LO = rd.HI;
		rd.HI = rd.LO;
		i -= 35;
		expa -= 35;
	    }
#endif

	    b.LO = rd.HI;	/* Set up for old code */
	    b.HI = a.LO;	/* Duplicate middle word for easy shift */
	    if (i < 35) {	/* Right shift or small left shift */
		a = x_ashc(a, i);
		b = x_ashc(b, i);
		if (i > 0) a.LO = b.HI;
	    } else if (i < 70) {	/* One-word or larger shift */
		a = x_ashc(b, i-35);
		op10m_setz(b.LO);
	    } else {			/* Two-word or larger shift */
		a.HI = x_ash(b.LO, i-70);
		op10m_setz(a.LO);
		op10m_setz(b.LO);
	    }

	    DEBUGPRF((" PostN S: %lo,%lo,%lo,%lo  R: %lo,%lo",
				DBGV_D(a), DBGV_W(b.LO)));

	    /* Now find roundup bit, and do fixups for negative results */
	    rbit = LHGET(b.LO) & (HSIGN>>1);	/* Find roundup bit */
	    if (wskipl(a.HI)) {			/* Special checks for neg */
		if (!(LHGET(b.LO)&(HMASK>>2)) && !RHGET(b.LO)) {
		    rbit = 0;			/* No round if no other bits */
		    DEBUGPRF((" - NEG RND DROPPED"));
		}
		if (LHGET(a.HI) == 0777700 && !RHGET(a.HI) && !wmagskipn(a.LO)
		  && !rbit) {
		    LHSET(a.HI, 0777740);	/* Effect an ASHC -1 */
		    ++expa;			/* Compensate for right-shft */
		    DEBUGPRF((" - NEG ZERO-MAG FIXUP"));
		}
	    }
	    DEBUGPRF(("\n"));
	    expa -= i;			/* Adjust exponent */
	    break;
    }

    /* At this point, number in A is normalized, and rbit is set if a
    ** roundup increment must be done (which in rare cases can trigger
    ** a single-shift renormalization).
    */
    if (rbit) {
	DEBUGPRF(("ROUNDUP DONE"));
	a = op10dfinc(a);		/* Add then check for overflow */
	if (wskipl(a.HI)) {		/* Negatives are messy */
	    if (op10m_tlnn(a.HI,GFFHIBIT)) {	/* If first fract bit is "wrong" */
		if (op10m_tlnn(a.HI, GFFMAGS)	/* See if really OK */
		  || RHGET(a.HI) || wmagskipn(a.LO)) {
		    --expa, a = x_ashc(a, 1);		/* Nope, shift up */
		    DEBUGPRF((" - NEG RENORMALIZED!"));
		}
	    }
	} else if (!op10m_tlnn(a.HI, GFFHIBIT)) {
	    ++expa, LHSET(a.HI, GFFHIBIT);	/* Positive overflow, fix up */
	    DEBUGPRF((" - POS RENORMALIZED!"));
	}
	DEBUGPRF(("\n"));
    }

    DEBUGPRF(("GFAD: S: (%o) %lo,%lo,%lo,%lo\n", expa, DBGV_D(a)));

    /* Ta da!  Put exponent back in (it may have underflowed or overflowed
    ** but nothing we can do about it).
    */
    if (wskipl(a.HI))	/* If sign set, put ones-complement of exp */
	 LHSET(a.HI, (LHGET(a.HI) ^ ((h10_t)(expa & GFEMAGS) << GFFBITS)));
    else LHSET(a.HI, (LHGET(a.HI) | ((h10_t)(expa & GFEMAGS) << GFFBITS)));
#if IFFLAGS
    if (expa < 0) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_FXU);
    else if (expa > GFEMAGS) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV);
#endif
    op10m_signclr(a.LO);	/* Double flt always zeros low sign */
    return a;
}

#if 1	/* Only needed for special KL-lossage-emulation experiment */

static int aqfffrac(register dw10_t a, register dw10_t rd)
{
    register int i;

    if (wskipl(a.HI)) {
	op10m_signset(rd.HI);
	if ((i = op10ffo(op10setcm(a.HI))) >= 36) {
	    if ((i = op10ffo(op10setcm(a.LO))) >= 36) {
	/*	op10m_signset(rd.HI); */
		if ((i = op10ffo(op10setcm(rd.HI))) >= 36) {
		    op10m_signset(rd.LO);
		    i = op10ffo(op10setcm(rd.LO)) + 70 + 35;
		} else i += 70;
	    } else i += 35;
	}
    } else {
	op10m_signclr(rd.HI);		/* Clear sign bit */
	if ((i = op10ffo(a.HI)) >= 36) {
	    if ((i = op10ffo(a.LO)) >= 36) {
	/*	op10m_signclr(rd.HI); */
		if ((i = op10ffo(rd.HI)) >= 36) {
		    op10m_signclr(rd.LO);
		    i = op10ffo(rd.LO) + 70 + 35;
		} else i += 70;
	    } else i += 35;
	}
    }
    return i;
}
#endif /* special hack */


/* GFMP on the real KL has some problems with failure to set the floating
    underflow flag in some situations.
    For example, this test:
VERIFY ERROR: "gfmp"
  Input: gfmp 40,0,0,2 777740,0,0,0 => 600037,777777,377777,777776  [440000]
  E Ver: gfmp 40,0,0,2 777740,0,0,0 => 600037,777777,377777,777776  [440100]

	The emulation result (floating underflow) is correct.
*/

dw10_t op10gfmp(register dw10_t a, register dw10_t b)
{
    register qw10_t q;
    register int exp, expb;
    register int sign;
    int signb;

    /* Make operands positive and remember sign of result */
    GF_DPOSSETUP(sign, exp, a);
    GF_DPOSSETUP(signb, expb, b);

    DEBUGPRF(("GFMP: A: (%o) %lo,%lo,%lo,%lo  B: (%o) %lo,%lo,%lo,%lo\n",
			exp, DBGV_D(a), expb, DBGV_D(b)));

    sign ^= signb;		/* Set 0 if both were negative */
    exp += expb - GFEEXCESS;	/* Add exponents to effect multiply */

    /* Now have two 59-bit magnitude positive numbers.  Multiply them
    ** to get 118 bits of product in 4 words.
    */
    q = x_dmul(a, b);		/* Multiply the numbers, get quad result */

    DEBUGPRF(("GFMP Q: %lo,%lo,%lo,%lo|%lo,%lo,%lo,%lo exp=%o (%d.)\n",
			DBGV_Q(q), exp, exp));

    /* Since there are 118 bits of product with 35 magnitude bits in each
    ** word, the high word should contain 118-105 = 13 high mag bits.
    ** Thus the top 36-13=23 bits of product should be empty, and if
    ** everything was normalized, the next bit (B23, start of fraction)
    ** should be 1 and we just shift everything left by 23-12=11 bits to get
    ** things into place.
    ** If not, must normalize by looking for first bit of fraction,
    ** which is a painfully slow process.
    */
    if (!op10m_trnn(q.D0.HI, 1<<(H10BITS-(1+23-H10BITS)))) {	/* Test B23 */
	/* No high fraction bits set... see if result was 0 */
	if (!RHGET(q.D0.HI) && !wskipn(q.D0.LO) && !dskipn(q.D1))
	    return q.D0;		/* Zero product, return 0.0 */
	do {
	    q = x_qash1(q);		/* ASH quad up one bit */
	    --exp;			/* Adjust exponent accordingly */
	} while (!op10m_trnn(q.D0.HI, 1<<(H10BITS-(1+23-H10BITS))));
					/* Until get high fract bit */
    }

    /* Fraction in expected place, ASH 23-12=11 bits left to align for exp */
    q.D0 = x_ashc(q.D0, 11);		/* Do ASHC bringing in 11 zeros */
    op10m_iori(q.D0.LO,			/* Add 11 bits from 3rd wd */
		(LHGET(q.D1.HI) >> ((HBITS-1)-11)));

    /* Now do rounding.  See if bit lower than LSB is set (this is the next
    ** bit after the ones added from 3rd word).
    ** If so, add one to LSB.  This will overflow only if the fraction was
    ** all ones, resulting in a roundup to all zeros except the low exponent
    ** bit.  If so, re-normalize by simply resetting the LH to right value
    ** and adjusting the exponent.
    */
    if (LHGET(q.D1.HI) & (1 << ((HBITS-1)-(11+1))) ) {
	q.D0 = op10dfinc(q.D0);
	if (op10m_tlnn(q.D0.HI, GFELHF)) {	/* If carried out of fract */
	    LHSET(q.D0.HI, GFFHIBIT);	/* reset to proper value */
	    ++exp;			/* and adjust exponent */
	}
    }

    /* Fraction all done, just add exponent back now */
    LHSET(q.D0.HI, (LHGET(q.D0.HI) | ((h10_t)(exp & GFEMAGS) << GFFBITS)));
#if IFFLAGS
    if (exp < 0) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_FXU);
    else if (exp > GFEMAGS) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV);
#endif

    if (sign)
	op10m_dmovn(q.D0);
    return q.D0;
}

/* GFDV - note no-divide case simply returns 1st arg instead of
**	failing explicitly.
*/

dw10_t op10gfdv(register dw10_t a, register dw10_t b)
{
    qw10_t q;
    register int exp;
    register int expb, sign, signb;
    dw10_t origa;

    /* Make operands positive and remember sign of result */
    /* The odd code here is similar to that for DFDV for the same reasons.
    */
    GF_DPOSSETUP(signb, expb, b);	/* Set up 2nd arg normally */
    if (!wskipn(b.HI) && !wmagskipn(b.LO)) {	/* Check for zero divisor */
	OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_DIV);
	return a;		/* Ugh, No Divide error.  Return original op */
    }

    /* Now set up A.  Note a different mechanism is used in order to emulate
    ** the way that the KL apparently chops off all exponent bits when
    ** extracting the fraction bits, thus turning an unnormalized negative
    ** number of (e.g.) 777000,,0 into 0,,0 instead of 1000,,0.
    **/
    origa = a;			/* Save original value of A */
    if (wskipl(a.HI)) {
	op10m_dmovn(a);
	if (wskipl(a.HI)) {
	    OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_DIV);
	    return origa;	/* Ugh, No Divide error.  Return original op */
	}
	sign = !signb;
    } else sign = signb;
    exp = GFEGET(a.HI);		/* Get sign and exponent */
    op10m_tlz(a.HI, GFELHF);	/* Chop out exponent bits */
    if (!wskipn(a.HI) && !wmagskipn(a.LO))
	return dw10zero;	/* No fraction bits, return 0 */


    DEBUGPRF(("GFDV: A: (%o) %lo,%lo,%lo,%lo  B: (%o) %lo,%lo,%lo,%lo\n",
			exp, DBGV_D(a), expb, DBGV_D(b)));

    exp += GFEEXCESS - expb;	/* Do division on exponents */

    /* For G-Format, 59-bit fracts */

    /* Now have two 59-bit magnitude numbers; calculate A/B.
    ** In order to ensure that the division can succeed, we pre-multiply the
    ** divisor by 2.  This is compensated for by adjusting the final exponent.
    ** This can fail with No Divide if the operands aren't normalized.
    **
    ** Note that the udcmpl test relies on both args being positive and the
    ** low sign bit being clear.
    */
    op10m_signclr(a.LO);		/* Ensure valid for udcmpl test */
    op10m_signclr(b.LO);
    if (!op10m_udcmpl(a, b)) {		/* If a >= b */
	DEBUGPRF(("GFDV Divisor pre-multiply!\n"));
	b = x_ashc(b, 1);		/* Make divisor be bigger */
	++exp;
	if (!op10m_udcmpl(a, b)) {	/* If a >= b */
	    OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_DIV);
	    return origa;	/* Ugh, No Divide error.  Return original op */
	}
    }
    q.D0 = a;
    q.D1 = dw10zero;		/* Set up dividend */

    if (!qdivstep(&q, b, 59+1)) {
	OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_DIV);
	return a;
    }

    /* Because the fractions are aligned, the result should be aligned as
    ** well.  However, because of the extra divide step needed to compute
    ** the rounding bit, the result is left-shifted 1 bit greater than
    ** it should be.  Adjust for this by decrementing the exponent.
    */
    --exp;

    DEBUGPRF(("GFDV: Q: %lo,%lo,%lo,%lo R: %lo,%lo,%lo,%lo  exp=%o (%d.)\n",
			DBGV_Q(q), exp, exp));

    if (!dskipn(q.D0))		/* Check for zero result */
	return q.D0;

    /* To compensate for possible unnormalized operands and roundup carry,
    ** we do a completely general normalization and round.  Note we've already
    ** tested for 0 so bits definitely exist.
    ** If an exponent under/overflow happens, GFNORM will catch it.
    */
    if (sign) {
	a = gfnorm(exp, q.D0, 1);	/* Normalize and round */
	op10m_dmovn(a);
	return a;
    }
    return gfnorm(exp, q.D0, 1);	/* Normalize and round */
}
#endif /* OP10_GFMT */

/* Floating-point Double-precision Conversions */

#if OP10_GFMT

/* GFNORM - Returns normalized G-format floating point, given
**	double-word integer fraction and exponent to use if fraction were
**	already in proper position (first significant bit at B12)
**	Sets flags if exponent overflows.
*/
static dw10_t gfnorm(register int exp,
		     register dw10_t d,	/* Contains signed integer fraction */
		     int rnd)		/* TRUE to round result */
{
    register int i, rbit;	/* rbit only needs 12 bits, so "int" safe */
    register int sign;

    /* This algorithm interprets the fraction as a signed double integer
    ** and converts it immediately to positive form, if negative.
    ** This is much easier than trying to do the right thing for negatives
    ** all through the code.  See the old code for sfnorm() to see what I mean.
    */

    /* Check for negative fraction and convert to use
    ** positive value instead.  Note that the maximum negative number
    ** cannot be made positive, but this is compensated for by
    ** adjusting its exponent and fraction.
    ** sfnorm() gets away with doing nothing because it can use a logical
    ** shift.  This won't work here because ASHC looks at the sign bit,
    ** hence the trickery here to ensure the sign is always 0.
    */
    if (sign = wskipl(d.HI)) {
	op10m_dmovn(d);		/* Negate the fraction. */
	if (wskipl(d.HI)) {		/* If still negative, */
	    ++exp;			/* adjust exponent and value */
	    LHSET(d.HI, H10SIGN>>1);	/* to get positive # */
	}
    }
    i = adffo(d);		/* Find first 1-bit in double, skip low sign */

    DEBUGPRF(("GFNORM: %lo,%lo,%lo,%lo  i=%d, exp=%o\n",
			DBGV_D(d), i, exp));
    if (i >= 71)
	return dw10zero;	/* No sig bits, force result to zero */

    /* i now has position of first fraction bit; move it into place.
    */
    i -= GFEBITS;		/* Make relative to proper position, B12 */
    exp -= i;			/* Adjust exponent */
    if (rnd && i < 0)		/* If rounding OK and losing bits */
	rbit = RHGET(d.LO)	/* Find value of last bit that will be lost */
		& (1 << ((-i)-1));
    else rbit = 0;

    if (i)
	d = x_ashc(d, i);	/* Now do double arithmetic shift! */

    DEBUGPRF(("GNORMED: %lo,%lo,%lo,%lo  exp=%d, rbit=%o\n",
			DBGV_D(d), exp, rbit));

    /* At this point, number in w is normalized, and rbit is set if a
    ** roundup increment must be done.  In the rare case that this
    ** increment overflows, its value will be 100,,0 and will trigger
    ** a single-shift renormalization.
    */
    if (rbit) {
	d = op10dfinc(d);		/* Add then check for overflow */
	/* (above call sets low sign, but it gets flushed later) */

	if (LHGET(d.HI) > GFFMASK) {
	    ++exp;			/* Positive overflow, fix up */
	    LHSET(d.HI, GFFHIBIT);
	    DEBUGPRF(("ROUNDUP - RENORMED!\n"));
	} else
	    DEBUGPRF(("ROUNDUP\n"));
    }

    /* Fraction is normalized with exponent bits clear, can just OR
    ** exponent back in, and negate if needed to finish off result.
    */
    op10m_tlo(d.HI, ((h10_t)(exp & GFEMAGS) << GFFBITS));
    if (sign)
	op10m_dmovn(d);		/* This forces low sign 0 as well */
    else
	op10m_signclr(d.LO);	/* Unsure, so force low sign 0 */

    /* Ta da! Check before returning for overflow or underflow.
    ** Note that the negation, if one, can never cause either since a
    ** positive number can always be negated.
    */

#if IFFLAGS
    if (exp < 0) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_FXU);
    else if (exp > GFEMAGS) OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV);
#endif

    return d;
}


/* GFSC - G Format floating scale.
**	Note peculiarity wherein GFSC tests to see if the first word is zero.
**	It should actually test the MAGNITUDE of the FRACTION, meaning
**	that if the number is negative, the test is made for zero bits
**	rather than one bits.
**	However, the real machine appears to always check for zero bits
**	in the ENTIRE word, including the sign bit.  So a negative number
**	or a non-zero exponent never triggers this case, regardless of
**	whether its high word fraction is all ones or all zeroes.  Sigh.
**
** GFSC on a KL screws up on the flags when given an E of 400000 applied to
**	a fairly low exponent.  It sets Overflow and Floating Overflow,
**	but not Floating Underflow!
**	This code correctly sets Floating Underflow.
**	(Oddly enough, FSC also messes this case up, but in a different way!)
**
** A real machine also screws up peculiarly in another way:
** VERIFY ERROR: "gfsc"
**  Input: gfsc   777777,777777,777777,777777    100  =>  777200,0,20,0  [0]
**  E Ver: gfsc   777777,777777,777777,777777    100  =>  777140,0,0,0  [0]
**	Note the bogus "20" in the low LH!
**      Admittedly the argument is unnormalized, but this is still weird.
*/
dw10_t
op10gfsc(register dw10_t d,
	 register h10_t h)
{
    register int exp;

#if 1	/* See note above about this stupid test. */
    if (!wskipn(d.HI))
	return dw10zero;	/* High word, return all zeros */
#endif

    /* Canonicalize exponent and fraction */
    GFSETUP(exp, d.HI);		/* Set up exp and fraction */

    /* Do special check of 1st wd fractional part.
    ** Note that at this point the exponent has been replaced by copies of
    ** the sign bit, so the test is simplified.
    */
#if 0
    if (wskipl(d.HI)) {
	register dw10_t nd;
	nd = d;
	op10m_dmovn(nd);	/* Do a double negate, then test */
	if (!wskipn(nd.HI))
	    return dw10zero;
    } else
	if (!wskipn(d.HI))
	    return dw10zero;	/* High fraction zero, return all zeros */
#endif

    /* Scale up exponent.  Note immediate arg is taken as 11 bits, not
    ** 8 bits as for all other similar ops!  (fsc, lsh, etc)
    */
    exp += (((h)&HSIGN) ? ((int)(h)|~03777) : ((int)(h)&03777));

    return gfnorm(exp, d, 0);	/* Put G-float together, don't round */
}

/* GSNGL - G Format to Single-precision Format, with round.
**
** Note: According to PRM, AC is unchanged if the G exponent is out of
**	range to begin with.  However, this appears not to be always true, per
**	following counter-example:
** VERIFY ERROR: "gsngl"
**   Input: gsngl  157373,175573,76600,173751  =>  373731,755732  [440100]
**   E Ver: gsngl  157373,175573,76600,173751  =>  157373,175573  [440100]
**
**	Note result was clobbered.  Doesn't even have the excuse of
**	unnormalized arg for this one!
*/
/* G -> S, with round, TRUE if succeeded */
int
op10xgsngl(register dw10_t *ad)
{
    register int exp;
    register dw10_t d;

    d = *ad;			/* Fetch the double */

    /* Canonicalize exponent and fraction */
    GFSETUP(exp, d.HI);		/* Set up exp and fraction */

    /* Special check for zero, needed to avoid setting flags */
    if (!exp && !wskipn(d.HI)) {
	ad->w[0] = w10zero;
	return 1;
    }

    /* Convert to new exponent range */
    exp = (exp - GFEEXCESS) + SFEEXCESS;

    /* Now see if OK for single-prec */
    DEBUGPRF(("GSNGL: new exp=%o (%d.)\n", exp, exp));
    if (exp < 0) {
	OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV+PCF_FXU);
	return 0;
    } else if (exp > SFEMAGS) {
	OP10_PCFSET(PCF_ARO+PCF_TR1+PCF_FOV);
	return 0;
    }

    /* Now what?  PRM seems to imply no normalization is done, just
    ** rounding.  However, it seems best to carry out a fully general
    ** normalize and round.
    ** Need to find out what real machine does.
    */

    /* The following code uses cleverness to take advantage of
    ** existing single-prec renormalization & round.
    ** First we shift the fraction up by 10 bits, not 3, so as to take
    ** maximum advantage of any rounding from the low bits.  This is
    ** particularly necessary for negative numbers, otherwise it's possible
    ** for sfnorm's neg->pos algorithm to get a spurious roundup for an
    ** all-ones fraction.
    ** This is then fed to sfnorm(), with the new exponent adjusted down
    ** by 10-3 to compensate for the extra bit shifts; sfnorm() knows how to
    ** do a right-shift to put things back in place.
    **  sfnorm() will set the flags if rounding overflows, just as PRM says.
    */
    d = x_ashc(d, 3+7);		/* Shift double fraction 10 places left */

    ad->w[0] = sfnorm(exp-7, d.HI, 1);	/* Return result in 1st wd of dbl */
    return 1;
}

/* S -> G */

dw10_t
op10gdble(register w10_t w)
{
    register dw10_t d;
    register int exp;
    register int sign;

    SF_POSSETUP(sign, exp, w);	/* Set up sign, exponent and fraction */
    d.HI = w;			/* Set high word */
    op10m_setz(d.LO);		/* Clear low */

    /* See if fraction is already normalized, to save the time of
    ** a full renormalization call.
    */
    if (op10m_tlnn(d.HI, SFFHIBIT)) {	/* Normalized?  Most common case */
	d = x_ashc(d, -3);			/* Right-shift 3 bits */
	exp = (exp - SFEEXCESS) + GFEEXCESS;	/* Convert up to G exponent */
	LHSET(d.HI, (LHGET(d.HI) | ((h10_t)(exp) << (H10BITS-GFEBITS))));
    } else if (!wskipn(d.HI)) {		/* Zero?  Next most common */
	return d;
    } else {
	/* Either unnormalized operand, or fraction is negative 400,,0.
	** Invoke slow but fully general normalization.
	*/
	d = gfnorm(((exp - SFEEXCESS) + GFEEXCESS - 3), d, 0);
    }
    if (sign)			/* If necessary, convert back */
	op10m_dmovn(d);		/* Note low sign always left 0 */
    return d;
}

/* Dfix -> G, with round */

dw10_t
op10dgfltr(register dw10_t d)
{
    /* Double already in arg, just need to provide normalizer with
    ** right exponent.  Binary point is GFFBITS+H10BITS+35 away from
    ** normal position.
    ** Note also that gfnorm() correctly handles max-neg-# case!
    */
    return gfnorm(GFEEXCESS+GFFBITS+H10BITS+35, d, 1);
}

/* fix -> G, with round (not really) */

dw10_t
op10gfltr(register w10_t w)
{
    register dw10_t d;

    d.HI = w;			/* Set up double-word */
    op10m_setz(d.LO);

    /* Provide exponent & normalize it.
    ** Binary point of the single-prec fix is to right of 1st word.
    ** This is GFFBITS+H10BITS (ie all fraction bits in 1st word)
    ** away from desired position, so add those to exponent.
    */
    return gfnorm(GFEEXCESS+GFFBITS+H10BITS, d, 0);
}


/* The following 2 functions are placeholders for GFIX{R} and GDFIX{R},
** which on a real KL are actually simulated in the monitor.
** These functions HAVE NOT BEEN VERIFIED yet!
*/

/* G -> Dfix, TRUE if succeeded */

int
op10xgdfix(register dw10_t *ad, int rnd)
{
    register dw10_t d = *ad;
    register int i, rbit, sign;

    GF_DPOSSETUP(sign, i, d);	/* Set up exponent, get positive fract */

    /* Apply exponent size test now */
    if (i > (GFEEXCESS+70)) {		/* If integer wd be too big, */
	OP10_PCFSET(PCF_ARO+PCF_TR1);	/* set flags */
	return 0;			/* and return arg unchanged */
    }
    
    /* OK to proceed!
    ** Fraction is normally a 24+35= 59-bit positive integer.  It will be a
    ** 60-bit value of 100,,0 ? 0 only if the float was negative with no
    ** one-bits in the fraction part (which is an unnormalized value).
    */
    i -= (GFEEXCESS+59);	/* Get # bits to shift fraction */
    if (i <= -60) {		/* If shifting it to oblivion */
	*ad = dw10zero;		/* just return zero, skip rigmarole */
	return 1;
    }

    if (rnd && i < 0) {		/* If rounding OK and losing bits */
	/* Find value of last bit that will be lost */
	if (i <= -35) {		/* Bit in high word? */
	    if (i <= -(35+H10BITS))	/* Bit comes from High LH? */
		rbit = LHGET(d.HI)
		    & (1 << ((-i)-(35+H10BITS+1)));
	    else		/* Bit comes from High RH */
		rbit = RHGET(d.HI)
		    & (1 << ((-i)-(35+1)));
	} else {		/* Bit in low word (shift -35 < i < 0) */
	    if (i <= -H10BITS)	/* Bit comes from Low LH? */
		rbit = LHGET(d.LO)
		    & (1 << ((-i)-(H10BITS+1)));
	    else		/* Bit comes from Low RH */
		rbit = RHGET(d.LO)
		    & (1 << ((-i)-1));
	}
    } else rbit = 0;

    if (i)
	d = x_ashc(d, i);	/* Do the shift */

    if (rbit) {			/* Need any rounding? */
	op10m_inc(d.LO);	/* Yep, trivial */
	if (!wmagskipn(d.LO))	/* Check for carry to high */
	    op10m_inc(d.HI);	/* Will never overflow this one */
    }

    /* Since returning a double fix, make sure result conforms to fixed-point
    ** sign convention (low sign is copy of high's)
    */
    if (sign)
	op10m_dneg(d);		/* Negate (sets low sign) */
    else
	op10m_signclr(d.LO);	/* Else clear low sign */

    *ad = d;			/* Return result */
    return 1;			/* Say succeeded */
}


/* G -> fix, TRUE if succeeded */

int
op10xgfix(register dw10_t *ad, int rnd)
{
    /* See code for op10xgdfix() above (and op10xfix()) for comments.
    ** This is a concise version.
    */
    register dw10_t d = *ad;
    register int i, rbit, sign;

    GF_DPOSSETUP(sign, i, d);	/* Set up exponent, get positive fract */
    if (i > (GFEEXCESS+35)) {		/* If integer wd be too big, */
	OP10_PCFSET(PCF_ARO+PCF_TR1);	/* set flags */
	return 0;			/* and return arg unchanged */
    }
    i -= (GFEEXCESS+35);	/* Get # bits to shift fraction */
    if (i <= -36) {		/* If shifting it to oblivion */
	ad->HI = w10zero;	/* just return zero, skip rigmarole */
	return 1;
    }

    if (rnd && i < 0) {		/* If rounding OK and losing bits */
	/* Find value of last bit that will be lost */
	if (i <= -35) {		/* Bit in high word? */
	    if (i <= -(35+H10BITS))	/* Bit comes from High LH? */
		rbit = LHGET(d.HI)
		    & (1 << ((-i)-(35+H10BITS+1)));
	    else		/* Bit comes from High RH */
		rbit = RHGET(d.HI)
		    & (1 << ((-i)-(35+1)));
	} else {		/* Bit in low word (shift -35 < i < 0) */
	    if (i <= -H10BITS)	/* Bit comes from Low LH? */
		rbit = LHGET(d.LO)
		    & (1 << ((-i)-(H10BITS+1)));
	    else		/* Bit comes from Low RH */
		rbit = RHGET(d.LO)
		    & (1 << ((-i)-1));
	}
    } else rbit = 0;

    if (i)
	d = x_ashc(d, i);	/* Do the shift */

    if (rbit) {			/* Need any rounding? */
	op10m_inc(d.HI);	/* Yep, trivial */
	if (!wmagskipn(d.HI)) {	/* Check for carry out */
	    if (!sign) {	/* Ugh.  OK if result will be negative, */
		OP10_PCFSET(PCF_ARO+PCF_TR1);	/* else set flags */
		return 0;			/* and return arg unchanged */
	    }
	}
    }

    /* Since returning a double fix, make sure result conforms to fixed-point
    ** sign convention (low sign is copy of high's)
    */
    if (sign)
	op10m_movn(d.HI);	/* Negate (sets low sign) */

    ad->HI = d.HI;		/* Return result */
    return 1;			/* Say succeeded */
}

#endif /* OP10_GFMT */

/* Old obsolete operations for PDP-6, KA-10, KI-10.
*/

/* DFN - Double Floating Negate
**	No flags are set.  "Negating a correctly formatted floating point
**	number cannot cause overflow".
*/
dw10_t op10dfn(register dw10_t d)
{
    register h10_t h;

    op10m_setcm(d.HI);			/* Complement high order word */
    h = (LHGET(d.LO) ^ HMASK) & (HMASK>>9);	/* Ditto low but clear sign&exp */
    RHSET(d.LO, (-RHGET(d.LO)) & HMASK);	/* Negate low RH */
    if (!RHGET(d.LO))
	/* If RH clear, fix up LH, but avoid sign & exp bits!! */
	if (!(h = (h+1) & (HMASK>>9))) {	/* If need more, */
	    op10m_inc(d.HI);			/* increment hi wd */
	}
    /* Combine low-order LH back in, must leave high 9 bits alone.
    ** The high 9 bits of H are already clear.
    */
    LHSET(d.LO, ((LHGET(d.LO) & ~(HMASK>>9)) | h));
    return d;
}

/* UFA - Unnormalized Floating Add
**	Similar enough to FAD that can use the same code!  ffadr()
**	may normalize 1 bit if an overflow happens, which is the same
**	thing that UFA does.
*/
w10_t op10ufa(register w10_t a, register w10_t b)
{
    return ffadr(a, b, FADF_NONORM|FADF_NORND);	/* No norm, no round */
}


/* Long form single-precision (immediate mode of FAD, FSB, FMP, FDV) */

static dw10_t dtokad(register dw10_t d)	/* (double) -> (KA double) */
{
    register int exp;

    op10m_signclr(d.LO);		/* Sign shd be 0, but make sure */
    RSHIFTM(d.LO, 8);			/* Make room for exponent */
    if (wskipn(d.LO)) {
	exp = SFEGET(d.HI);		/* Get sign and exponent */
	if (exp & SFESIGN) {		/* Negative? */
	    exp = exp ^ SFEMASK;	/* If so, get ones-complement */
	}
	if (exp < 27)			/* If exponent too small, */
	    op10m_setz(d.LO);		/* just clear 2nd word */
	else				/* Ah, put 2nd exp into low wd */
	    LHSET(d.LO, (LHGET(d.LO) | ((h10_t)(exp-27) << SFFBITS)));
    }
    return d;
}

dw10_t op10fadl(register w10_t a, register w10_t b)
{
    return dtokad(dfad(op10fdble(a), op10fdble(b), 0));
}

dw10_t op10fsbl(register w10_t a, register w10_t b)
{
    return dtokad(dfad(op10fdble(a), op10fdble(b), 1));
}

/* FMPL - This implementation differs when the exponent overflows.
**	Hardware clears the low word; this code still attempts to set
**	the exponent.
*/
dw10_t op10fmpl(register w10_t a, register w10_t b)
{
    return dtokad(op10dfmp(op10fdble(a), op10fdble(b)));
}

/* FDVL - I'm not positive this emulates the hardware precisely for
**	abnormal cases.  In particular, no-divide probably fails.
*/
dw10_t op10fdvl(register dw10_t d,
		register w10_t w)
{
    /* Convert from KA software double to hardware double */
    LSHIFTM(d.LO, 8);
    op10m_signclr(d.LO);		/* Ensure sign flushed */
    return dtokad(op10dfdv(d, op10fdble(w)));
}

/* OP10FDBLE - Converts Float to Double Float.
**	Auxiliary for Long-form instructions above, also a KCC C simop
**	in its own right.
*/
/* (double)(float)w */
dw10_t op10fdble(register w10_t w)
{
    dw10_t d;
    d.HI = w;
    op10m_setz(d.LO);
    return d;
}

/* Floating-point conversions to and from native platform format.
*/
#if 0	/* Not needed currently - eventually for KCC only? */

#include <math.h>		/* For native assistance */

/* OP10_DPUTF - Native double float to single-word PDP-10 float
*/
w10_t op10_dputf(dbl)
double dbl;
{
    register w10_t w;
    register uint32 imant;
    int exp, neg = 0;

    if (dbl <= 0) {	/* Get magnitude for easier hacking */
	if (dbl == 0)
	    return w10zero;
	neg++;
	dbl = -dbl;
	if (dbl < 0)
	    return w10maxneg;
    }
    /* Split value into normalized exp and fraction, then
    ** floating-shift fraction so we have 27 binary digits when the double
    ** is converted into an integer.
    */
    imant = (int32)ldexp(frexp(dbl, &exp), 27);	/* Get 27-bit integ fraction */

    /* Form high half from exponent and high fraction.  It is perfectly
    ** possible for the exponent to overflow -- tough.
    */
    LHSET(w, (((h10_t)((exp+0200)&0377)<<9) | ((imant>>18)&0777)));
    RHSET(w, (imant & HMASK));
    if (neg)
	op10m_movn(w);
    return w;
}

/* OP10_FGETD - Single-word PDP-10 float to native double float
*/
double op10_fgetd(w)
register w10_t w;
{
    register uint32 imant;
    register int exp;
    register double dbl;
    int neg = 0;

    if (wskipl(w)) {
	neg++;
	op10m_movn(w);
    }
    imant = op10wtos(w) & (((uint32)1<<27)-1);	/* Get 27-bit fraction */
    exp = (LHGET(w) >> 9) & 0377;		/* Get 8-bit exponent */
    if (!exp && !imant) return 0.0;
    dbl = ldexp((double)imant, (exp-0200)-27);	/* Put it together */
    return neg ? -dbl : dbl;
}


/* OP10_DPUTD - Native double float to double-word PDP-10 double
*/
static dw10_t op10_dputd(dbl)
double dbl;
{
#if 0
#endif
}

/* OP10_DGETD - Double-word PDP-10 doublefloat to native double float
*/
static double op10_dgetd(d)
register dw10_t d;
{
#if 0
    register uint32 imant;
    register int exp;
    register double dbl;
    int neg = 0;

    if (wskipl(w)) {
	neg++;
	op10m_movn(w);
    }
    imant = op10wtos(w) & (((uint32)1<<27)-1);	/* Get 27-bit fraction */
    exp = (LHGET(w) >> 9) & 0377;		/* Get 8-bit exponent */
    if (!exp && !imant) return 0.0;
    dbl = ldexp((double)imant, (exp-0200)-27);	/* Put it together */
    return neg ? -dbl : dbl;
#endif
}
#endif /* 0 */

/* Simulated Ops for KCC C operations (not actual PDP-10 ops) */

#if OP10_KCCOPS

/* Comparisons and testing */

/* C simop: Gives result < = > 0 for A-B */

int op10cmp(register w10_t a, register w10_t b)
{
    return op10m_caml(a,b) ? -1 : op10m_camn(a,b);
}

/* C simop: Gives result < = > 0 for unsigned A-B */

int op10ucmp(register w10_t a, register w10_t b)
{
    return op10m_ucmpl(a,b) ? -1 : op10m_camn(a,b);
}

/* C simop: Gives result < = > 0 for W */

int op10tst(register w10_t w)
{
    return op10m_skipl(w) ? -1 : op10m_skipn(w);
}

/* C simop: Gives result < = > 0 for DW A-B */
**	Double compare is a bit weird since sign of low word must be ignored.
*/
int op10dcmp(register dw10_t da, register dw10_t db)
{
    register int i;
    if (i = op10cmp(da.HI, db.HI))
	return i;
    op10m_signclr(da.LO);	/* Clear sign bits of low words */
    op10m_signclr(db.LO);
    return op10cmp(da.LO, db.LO);
}

/* C simop: Gives result < = > 0 for doubleword */

int op10dtst(register dw10_t d)
{
    return op10m_signtst(d.HI) ? -1 : dskipn(d);
}

/* C simop: Double fix negate */

dw10_t op10dneg(register dw10_t d)
{
    op10m_dneg(d);
    return d;
}


/* C simop: Unsigned Multiply */

w10_t op10uimul(w10_t a, w10_t b)
{
    dw10_t d;
    d = op10xmul(a, b);
    if (RHGET(d.HI) & 01)	/* If low bit of high wd is set, */
	op10m_signset(d.LO);	/* set high bit of low wd */
    return d.LO;
}

/* C simop: Unsigned Divide */

dw10_t op10uidiv(register w10_t a, register w10_t b)
{
    dw10_t db;
    qw10_t q;

    if (!wskipl(a) && !wskipl(b))
	return op10idiv(a, b);

    /* Punt for now by doing double fix division */
    q.D0 = dw10zero;
    q.D1.HI = wskipl(a) ? w10one : w10zero;
    q.D1.LO = a;
    db.HI = wskipl(b) ? w10one : w10zero;
    db.LO = b;
    q = op10ddiv(q, db);
    db.HI = q.D0.LO;
    db.LO = q.D1.LO;
    return db;
}

/* C simop: (float)(unsigned)w */

w10_t op10uflt(register w10_t w)
{
    if (wskipl(w))
	return sfnorm(0200+27+1, op10lsh(w, -1), 1);
     return sfnorm(0200+27, w, 1);	/* Normalize and round */
}

/* C simop: (int)(double)d */

w10_t op10dfix(register dw10_t d)
{
    register int exp, sign;

    SF_DPOSSETUP(sign, exp, d);	/* Extract parts and set up positive fract */
    d = x_ashc(d, exp - 0233);
    if (sign)
	op10m_dmovn(d);
    return d.HI;
}

/* C simop: (double)(int)w */

dw10_t op10dflt(register w10_t w)
{
    register dw10_t d;
    d.HI = w;
    op10m_setz(d.LO);
    d = x_ashc(d, -8);		/* Make room for exponent */
    LHSET(d.HI, (LHGET(d.HI) ^ 0243000));	/* Set proper exponent */
    return dfad(d, dw10zero, 0);	/* Normalize and return */
}

/* C simop: (double)(unsigned)w */

dw10_t op10udflt(register w10_t w)
{
    register dw10_t d;
    d.HI = w;
    op10m_setz(d.LO);
    d = op10lshc(d, -9);	/* Make room for exponent */
    RSHIFTM(d.LO, 1);		/* Get out of way of sign bit */
    LHSET(d.HI, (LHGET(d.HI) ^ 0244000));	/* Set proper exponent */
    return dfad(d, dw10zero, 0);	/* Normalize and return */
}

/* C simop: (float)(double)d */

w10_t op10dsngl(register dw10_t d)
{
    int sign;
    if (sign = wskipl(d.HI))
	op10m_dmovn(d);
    if (LHGET(d.LO) & (HSIGN>>1)) {
	if (RHGET(d.HI) & 01) {
	    w10_t one;
	    LRHSET(one, (LHGET(d.HI) & 0777000), 01);
	    d.HI = ffadr(d.HI, one, FADF_NORND);	/* No rounding */
	} else
	    op10m_iori(d.HI, 1);
    }
    if (sign)
	op10m_movn(d.HI);
    return d.HI;
}

#endif /* OP10_KCCOPS */
