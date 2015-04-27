/* KN10OPS.H - PDP-10 arithmetic operations for KLH10
*/
/* $Id: kn10ops.h,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: kn10ops.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*
**	(Header file for routines in KN10OPS.C, plus inline macros)
** KN10OPS is primarily intended for use by the KLH10 PDP-10 emulator, but
** can also be used by KCC (PDP-10 C compiler) or other programs which want
** to emulate PDP-10 arithmetic operations.  Note there are a number of
** artifacts defined here for KCC which aren't needed by the KLH10.
*/

#ifndef KN10OPS_INCLUDED
#define KN10OPS_INCLUDED 1

#ifdef RCSID
 RCSID(kn10ops_h,"$Id: kn10ops.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

#include "word10.h"	/* Define architecture of PDP-10 word */


#ifdef OP10_PCFSET	/* If we'll need flags, define them here? */
			/* This should be cleaned up later... */
/* PDP-10 processor flags (not all used by all models).
**	These defs are the left half bit values of actual PDP-10 PC flags.
*/
#define PCF_ARO	0400000	/* Arithmetic Overflow (or Prev Ctxt Public) */
#define PCF_CR0	0200000	/* Carry 0 - Carry out of bit 0 */
#define PCF_CR1	0100000	/* Carry 1 - Carry out of bit 1 */
#define PCF_FOV	040000	/* Floating Overflow */
#if 0	/* Not used by arithmetic ops */
#  define PCF_FPD	020000	/* First Part Done */
#  define PCF_USR	010000	/* User Mode */
#  define PCF_UIO	04000	/* User In-Out (or Prev Ctxt User) */
#  define PCF_PUB	02000	/* Public Mode */
#  define PCF_INH	01000	/* Addr Failure Inhibit */
#endif
#define PCF_TR2	0400	/* Trap 2 (PDL overflow) */
#define PCF_TR1	0200	/* Trap 1 (Arith overflow) */
#define PCF_FXU	0100	/* Floating Exponent Underflow */
#define PCF_DIV	040	/* No Divide */

#endif /* OP10_PCFSET */

/* External data constants from KN10OPS */
extern w10_t w10zero;		/* Word of all zeros (0) */
extern w10_t w10ones;		/* Word of all ones (-1) */
extern w10_t w10one;		/* Word of just one  (1) */
extern dw10_t dw10zero;		/* Double of all zeros (0.0) */

/* Declare functions which implement all PDP-10 instruction operations. */

#if !WORD10_USENAT	/* Only needed if not using native PDP-10 code */

/* External symbol redefs to avoid name clashes (per 6-char monocase limit) */
#if CENV_CPU_PDP10			/* If compiling on a PDP-10 */
# define op10movn	o1movn
# define op10movm	o1movm
# define op10lshc	o1dlsh
# define op10ashc	o1dash
# define op10rot	o1rot
# define op10rotc	o1drot
# define op10ffo	o1ffo
# define op10fad	o1fad
# define op10fsb	o1fsb
# define op10fmp	o1fmp
# define op10fdv	o1fdv
# define op10fadr	o1fadr
# define op10fsbr	o1fsbr
# define op10fmpr	o1fmpr
# define op10fdvr	o1fdvr
# define op10fadl	o1fadl
# define op10fsbl	o1fsbl
# define op10fmpl	o1fmpl
# define op10fdvl	o1fdvl
# define op10fsc	o1fsc
# define op10fix	o1fix
# define op10fixr	o1fixr
# define op10fltr	o1fltr
# define op10dsub	o1dsub
# define op10dmul	o1dmul
# define op10dfad	o1dfad
# define op10dfsb	o1dfsb
# define op10dfmp	o1dfmp
# define op10dfdv	o1dfdv
# define op10dinc	o1dinc
# define op10dflt	o1dflt
# define op10xmul	o1xmul
# define op10uidiv	o1udiv
# define op10dfn	o1dfn
# define op10ufa	o1ufa

# define op10gfad	o1gfad
# define op10gfsb	o1gfsb
# define op10gfmp	o1gfmp
# define op10gfdv	o1gfdv
# define op10gfsc	o1gfsc
# define op10gdble	o1gdbl
# define op10dgfltr	o1dgfl
# define op10gfltr	o1gflt

# define op10xfdv	o1xfdv
# define op10xfix	o1xfix
# define op10xdiv	o1xdiv
# define op10xidiv	o1xidi
# define op10xgfix	o1xgfi
# define op10xgdfix	o1xgdf
# define op10xgsngl	o1xgsn


#endif /* CENV_CPU_PDP10 */

/* External routines */

#if 0
extern w10_t op10stow(int32);
#endif

#if OP10_KCCOPS		/* C simulated ops */
extern int
	op10cmp(w10_t, w10_t),
	op10ucmp(w10_t, w10_t),
	op10dcmp(dw10_t, dw10_t),
	op10tst(w10_t),
	op10dtst(dw10_t);
extern w10_t
	op10uimul(w10_t, w10_t),
	op10uflt(w10_t),
	op10dfix(dw10_t),
	op10dsngl(dw10_t);
extern dw10_t
	op10dneg(dw10_t),
	op10uidiv(w10_t, w10_t),
	op10dflt(w10_t),
	op10udflt(w10_t);
#endif /* OP10_KCCOPS */

extern int32
	op10wtos(w10_t);

extern w10_t
	op10utow(uint32),
	op10add(w10_t, w10_t),
	op10sub(w10_t, w10_t),
	op10imul(w10_t, w10_t),
	op10and(w10_t, w10_t),
	op10ior(w10_t, w10_t),
	op10xor(w10_t, w10_t),
	op10setcm(w10_t),
	op10lsh(w10_t, h10_t),
	op10ash(w10_t, h10_t),
	op10rot(w10_t, h10_t),
	op10movn(w10_t),
	op10movm(w10_t),
	op10inc(w10_t),
	op10fad(w10_t, w10_t),
	op10fsb(w10_t, w10_t),
	op10fmp(w10_t, w10_t),
	op10fdv(w10_t, w10_t),
	op10fadr(w10_t, w10_t),
	op10fsbr(w10_t, w10_t),
	op10fmpr(w10_t, w10_t),
	op10fdvr(w10_t, w10_t),
	op10fsc(w10_t, h10_t),
	op10fltr(w10_t),
	op10fix(w10_t),
	op10fixr(w10_t),
	op10ufa(w10_t, w10_t);	/* KA10 op */

extern dw10_t
	op10mul(w10_t, w10_t),
	op10xmul(w10_t, w10_t),
	op10div(dw10_t, w10_t),
	op10idiv(w10_t, w10_t),
	op10lshc(dw10_t, h10_t),
	op10ashc(dw10_t, h10_t),
	op10rotc(dw10_t, h10_t),
	op10circ(dw10_t, h10_t),	/* ITS instr only */
	op10dadd(dw10_t, dw10_t),
	op10dsub(dw10_t, dw10_t),
	op10fadl(w10_t, w10_t),		/* KA10 */
	op10fsbl(w10_t, w10_t),		/* KA10 */
	op10fmpl(w10_t, w10_t),		/* KA10 */
	op10fdvl(dw10_t, w10_t),	/* KA10 */
	op10dfad(dw10_t, dw10_t),
	op10dfsb(dw10_t, dw10_t),
	op10dfmp(dw10_t, dw10_t),
	op10dfdv(dw10_t, dw10_t),
	op10gfad(dw10_t, dw10_t),	/* KL */
	op10gfsb(dw10_t, dw10_t),	/* KL */
	op10gfmp(dw10_t, dw10_t),	/* KL */
	op10gfdv(dw10_t, dw10_t),	/* KL */
	op10gfsc(dw10_t, h10_t),	/* KL */
	op10gdble(w10_t),	/* KL */
	op10dgfltr(dw10_t),	/* KL */
	op10gfltr(w10_t),	/* KL */
	op10dmovn(dw10_t),
	op10dfn(dw10_t),	/* KA10 */
	op10dinc(dw10_t),	/* C simop but used by optest */
	op10fdble(w10_t);	/* C simop but used internally */

extern qw10_t		/* Return quadwords */
	op10dmul(dw10_t, dw10_t),
	op10ddiv(qw10_t, dw10_t);

extern int		/* Operations that can abort */
	op10xfdv(w10_t *, w10_t, int),
	op10xfix(w10_t *, int),
	op10xdiv(dw10_t *, w10_t),
	op10xidiv(dw10_t *, w10_t, w10_t),
	op10xgfix(dw10_t *, int),
	op10xgdfix(dw10_t *, int),
	op10xgsngl(dw10_t *);

extern int		/* Miscellaneous */
	op10ffo(w10_t);

#endif /* !WORD10_USENAT */

/* Define op10m_* macros for optimizing in-line coding if possible.
**	All of the test macros such as op10m_signtst, skip*, cam*, trn*
**	must return a LOGICAL true or false depending on the condition
**	being tested for.  This ensures that their value can be assigned
**	to an integer boolean variable, without compromising their efficiency
**	when used in C conditional expressions.
** The other op10m_* macros modify their first arg (which must be an lvalue!)
**	IN PLACE; no value is returned and no flags are set.
**
** HOWEVER...
**	The few op10mf_* macros that exist *DO* set the flags!
**	Their actions are otherwise identical to their op10m_ counterparts.
*/

#if WORD10_USEHWD || WORD10_USEGCCSPARC || WORD10_USEHUN
	/* Common internal macros to hack halfwords, which are accessed
	** using LHVAL and RHVAL -- which must be lvalues!
	*/
#  define OP10H_MOVN(w)	(LHVAL(w) = (RHVAL(w) = (-RHVAL(w) & H10MASK)) \
			? (LHVAL(w) ^ H10MASK) : (-LHVAL(w) & H10MASK) )

/* The following 4 won't work unless halfwords are stored in >=19 bits */
#  define OP10H_ADD(a,b) ( \
	(((RHVAL(a) += RHVAL(b)) & ~H10MASK)	\
		? ((RHVAL(a) &= H10MASK), ++LHVAL(a)) : 0), \
	      (LHVAL(a) = (LHVAL(a) + LHVAL(b)) & H10MASK) )
#  define OP10H_SUB(a,b) ( \
	(((RHVAL(a) -= RHVAL(b)) & ~H10MASK)	\
		? ((RHVAL(a) &= H10MASK), --LHVAL(a)) : 0), \
	      (LHVAL(a) = (LHVAL(a) - LHVAL(b)) & H10MASK) )
#  define OP10H_ADDI(w,r) ( \
	((RHVAL(w)=(RHVAL(w)+(r))) & ~H10MASK) \
		? ((RHVAL(w) &= H10MASK), LHVAL(w) = (LHVAL(w)+1)&H10MASK) \
		: 0 )
#  define OP10H_SUBI(w,r) ( \
	((RHVAL(w)=(RHVAL(w)-(r))) & ~H10MASK) \
		? ((RHVAL(w) &= H10MASK), LHVAL(w) = (LHVAL(w)-1)&H10MASK) \
		: 0 )
#  define OP10H_INC(w) ( \
	(++RHVAL(w) & ~H10MASK) \
		? ((RHVAL(w) = 0), LHVAL(w) = (LHVAL(w)+1)&H10MASK) \
		: 0 )
#  define OP10H_DEC(w) ( \
	RHVAL(w) ? --RHVAL(w) \
		: ((RHVAL(w) = H10MASK), LHVAL(w) = (LHVAL(w)-1)&H10MASK) )

#  define OP10H_LSHIFT(w,s) \
   ((s) < H10BITS \
    ? (  (LHVAL(w) = ((LHVAL(w) << (s))&H10MASK) | (RHVAL(w)>>(H10BITS-(s)))) \
	   , (RHVAL(w) = (RHVAL(w) << (s)) & H10MASK) )\
    : (  (((s) > W10BITS) ? (LHVAL(w) = 0) \
			  : (LHVAL(w) = (RHVAL(w)<<((s)-H10BITS)) & H10MASK) )\
	   , (RHVAL(w) = 0) ) )
#  define OP10H_RSHIFT(w,s) \
   ((s) < H10BITS \
    ? (  (RHVAL(w) = (RHVAL(w) >> (s)) | ((LHVAL(w)<<(H10BITS-(s)))&H10MASK) )\
	   , (LHVAL(w) >>= (s)) )\
    : (  (((s) > W10BITS) ? (RHVAL(w) = 0) \
			  : (RHVAL(w) = LHVAL(w)>>((s)-H10BITS)) )\
	   , (LHVAL(w) = 0) ) )

/* Flag setting for ops:
**		AOJ/AOS					SOJ/SOS
**   -1 => 0	cry0+cry1		   0 => -1	none
** +max => setz	cry1+ovfl+trap1		setz => +max	cry0+ovfl+trap1
**	else	none			    else	cry0+cry1
*/
#  define OP10HF_INC(w) ( \
	  (++RHVAL(w) & H10MASK) ? 0 \
	: ((RHVAL(w) = 0), ((++LHVAL(w) & H10MAGS) ? 0 \
	: ((LHVAL(w) &= H10MASK) ? OP10_PCFSET(PCF_CR1|PCF_ARO|PCF_TR1) \
	: OP10_PCFSET(PCF_CR0|PCF_CR1) \
	))) )
#  define OP10HF_DEC(w) ( \
    (RHVAL(w) || (LHVAL(w)&H10MAGS)) \
	? (OP10H_DEC(w), OP10_PCFSET(PCF_CR0|PCF_CR1)) \
	: ((RHVAL(w) = H10MASK), (((LHVAL(w) ^= H10MASK) & H10SIGN) ? 0 \
	: OP10_PCFSET(PCF_CR0|PCF_ARO|PCF_TR1) \
    )) )
#  define OP10HF_MOVN(w) ( \
    (RHVAL(w) = (-RHVAL(w) & H10MASK))	\
	? (LHVAL(w) ^= H10MASK)		\
	: ( ((LHVAL(w) = (-LHVAL(w) & H10MASK)) & H10MAGS) ? 0	\
	    : ((LHVAL(w)&H10SIGN)	\
		? (OP10_PCFSET(PCF_CR1|PCF_ARO|PCF_TR1), 0)	\
		: (OP10_PCFSET(PCF_CR0|PCF_CR1), 0) )		\
	  ) )

#endif	/* HWD || GCCSPARC || HUN */


#if WORD10_USEHWD

/* Simple word-value stuff */
#define op10m_signtst(w) (((w).lh&H10SIGN)!=0)	/* Test for sign bit */
#define op10m_magstst(w) ((w).rh || ((w).lh&H10MAGS))	/* Test for mag bits */
#define op10m_signclr(w) ((w).lh&=H10MAGS)	/* Clear sign (and up) */
#define op10m_signset(w) ((w).lh|=H10SIGN)	/* Set sign */

/* Unary ops */
#define op10m_skipn(w)	((w).lh || (w).rh)
#define op10m_setcm(w)	(((w).lh ^= H10MASK), ((w).rh ^= H10MASK))
#define op10m_setz(w)	((w).lh = 0, (w).rh = 0)
#define op10m_seto(w)	((w).lh = H10MASK, (w).rh = H10MASK)

/* Binary ops, no flags */
#define op10m_and(a,b)	((a).lh &= (b).lh, (a).rh &= (b).rh)
#define op10m_ior(a,b)	((a).lh |= (b).lh, (a).rh |= (b).rh)
#define op10m_xor(a,b)	((a).lh ^= (b).lh, (a).rh ^= (b).rh)
#define op10m_andcm(a,b) ((a).lh &= ~(b).lh, (a).rh &= ~(b).rh)
#define op10m_tdnn(a,b) (((a).lh & (b).lh) || ((a).rh & (b).rh))
#define op10m_camn(a,b)	((a).lh != (b).lh || (a).rh != (b).rh)
#define op10m_ucmpl(a,b) ((a).lh < (b).lh \
			|| ((a).lh==(b).lh && (a).rh < (b).rh))
#define op10m_caml(a,b)	((((a).lh^(b).lh)&H10SIGN) \
			? op10m_ucmpl(b,a) : op10m_ucmpl(a,b))
#define op10m_lshift(w,s) OP10H_LSHIFT(w,s)
#define op10m_rshift(w,s) OP10H_RSHIFT(w,s)
#define op10m_tlo(w,h) ((w).lh |= (h))
#define op10m_tlz(w,h) ((w).lh &= ~(h))
#define op10m_tlnn(w,h) (((w).lh & (h)) != 0)
#define op10m_tro(w,h) ((w).rh |= (h))
#define op10m_trz(w,h) ((w).rh &= ~(h))
#define op10m_trnn(w,h) (((w).rh & (h)) != 0)
#define op10m_txnn(w,l,r) (((w).lh & (l)) || ((w).rh & (r)))
#define op10m_cail(w,r)  ((w).lh ? op10m_signtst(w) : ((w).rh < (r)))
#define op10m_caile(w,r) ((w).lh ? op10m_signtst(w) : ((w).rh <= (r)))
#define op10m_cain(w,r)  ((w).rh != (r) || (w).lh)

/* Arithmetic ops, special flagless versions */
#define op10m_movn(w)	OP10H_MOVN(w)
#define op10m_add(a,b)	OP10H_ADD(a,b)
#define op10m_sub(a,b)	OP10H_SUB(a,b)
#define op10m_addi(w,r)	OP10H_ADDI(w,r)
#define op10m_subi(w,r)	OP10H_SUBI(w,r)
#define op10m_inc(w)	OP10H_INC(w)
#define op10m_dec(w)	OP10H_DEC(w)

/* Arithmetic ops, flag-setting versions! */
# ifdef OP10_PCFSET
#  define op10mf_inc(w)	OP10HF_INC(w)
#  define op10mf_dec(w)	OP10HF_DEC(w)
#  define op10mf_movn(w) OP10HF_MOVN(w)
# endif /* OP10_PCFSET */

/* endif WORD10_USEHWD */

#elif WORD10_USEINT

/* Simple word-value stuff */
#define op10m_signtst(w) ((XWDVAL(w)&W10SIGN)!=0)	/* Test for sign bit */
#define op10m_magstst(w) ((XWDVAL(w)&W10MAGS)!=0)	/* Test for mag bits */
#define op10m_signclr(w) (XWDVAL(w)&=W10MAGS)		/* Clear sign */
#define op10m_signset(w) (XWDVAL(w)|=W10SIGN)		/* Set sign */

/* Unary ops */
#define op10m_skipn(w)	(XWDVAL(w) != 0)
#define op10m_setcm(w)	(XWDVAL(w) ^= W10MASK)
#define op10m_setz(w)	(XWDVAL(w) = 0)
#define op10m_seto(w)	(XWDVAL(w) = W10MASK)

/* Binary ops, no flags */
#define op10m_and(a,b)	(XWDVAL(a) &= XWDVAL(b))
#define op10m_ior(a,b)	(XWDVAL(a) |= XWDVAL(b))
#define op10m_xor(a,b)	(XWDVAL(a) ^= XWDVAL(b))
#define op10m_andcm(a,b) (XWDVAL(a) &= ~XWDVAL(b))
#define op10m_tdnn(a,b) ((XWDVAL(a) & XWDVAL(b)) != 0)
#define op10m_camn(a,b)	(XWDVAL(a) != XWDVAL(b))
#define op10m_ucmpl(a,b) (XWDVAL(a) < XWDVAL(b))
#define op10m_caml(a,b)	(((XWDVAL(a) ^ XWDVAL(b))&W10SIGN) \
			? op10m_ucmpl(b,a) : op10m_ucmpl(a,b))
#define op10m_lshift(w,s) (XWDVAL(w) = \
		((s) >= W10BITS) ? 0 : ((XWDVAL(w) << (s)) & W10MASK))
#define op10m_rshift(w,s) (XWDVAL(w) = \
		((s) >= W10BITS) ? 0 : ((XWDVAL(w) >> (s)) & W10MASK))
#define op10m_tlo(w,l) (XWDVAL(w) |= ((w10uint_t)(l)<<H10BITS))
#define op10m_tlz(w,l) (XWDVAL(w) &= ~((w10uint_t)(l)<<H10BITS))
#define op10m_tlnn(w,l) ((XWDVAL(w) & ((w10uint_t)(l)<<H10BITS)) != 0)
#define op10m_tro(w,h)  (XWDVAL(w) |= (h))
#define op10m_trz(w,h)  (XWDVAL(w) &= ~(h))
#define op10m_trnn(w,h) ((XWDVAL(w) & (h)) != 0)
#define op10m_txnn(w,l,r) ((XWDVAL(w) & XWDIMM(l,r)) != 0)
#define op10m_cail(w,r)	(op10m_signtst(w) \
			? (XWDVAL(w) > (r)) : (XWDVAL(w) < (r)))
#define op10m_caile(w,r) (op10m_signtst(w) \
			? (XWDVAL(w) >= (r)) : (XWDVAL(w) <= (r)))
#define op10m_cain(w,r)  (XWDVAL(w) != (r))

/* Arithmetic ops, special flagless versions */
#define op10m_movn(a)	(XWDVAL(a) = (-XWDVAL(a)) & W10MASK)
#define op10m_add(a,b)	(XWDVAL(a) = (XWDVAL(a)+XWDVAL(b)) & W10MASK)
#define op10m_sub(a,b)	(XWDVAL(a) = (XWDVAL(a)-XWDVAL(b)) & W10MASK)
#define op10m_addi(w,r)	(XWDVAL(w) = (XWDVAL(w)+(r)) & W10MASK)
#define op10m_subi(w,r)	(XWDVAL(w) = (XWDVAL(w)-(r)) & W10MASK)
#define op10m_inc(w)	(XWDVAL(w) = (XWDVAL(w)+1) & W10MASK)
#define op10m_dec(w)	(XWDVAL(w) = (XWDVAL(w)-1) & W10MASK)

/* Arithmetic ops, flag-setting versions! */
# ifdef OP10_PCFSET
#  define op10mf_inc(w) (op10m_inc(w), (op10m_magstst(w) ? 0 \
	: (op10m_signtst(w) ? OP10_PCFSET(PCF_CR1|PCF_ARO|PCF_TR1) \
			    : OP10_PCFSET(PCF_CR0|PCF_CR1) )))
#  define op10mf_dec(w) (op10m_magstst(w) \
	? (op10m_dec(w), OP10_PCFSET(PCF_CR0|PCF_CR1)) \
	: (op10m_setcm(w), (op10m_signtst(w) ? 0 \
		: OP10_PCFSET(PCF_CR0|PCF_ARO|PCF_TR1) )))
#  define op10mf_movn(w) (op10m_magstst(w) \
	? op10m_movn(w)			\
	: (op10m_signtst(w)		\
		? OP10_PCFSET(PCF_CR1|PCF_ARO|PCF_TR1)	\
		: OP10_PCFSET(PCF_CR0|PCF_CR1) ))
# endif /* OP10_PCFSET */

/* endif WORD10_USEINT */

#elif WORD10_USEGCCSPARC

/* Simple word-value stuff */
#define op10m_signtst(w) ((LHGET(w)&H10SIGN)!=0)	/* Test for sign bit */
#define op10m_magstst(w) (RHGET(w)||(LHGET(w)&H10MAGS))	/* Test for mag bits */
#define op10m_signclr(w) ((w) = w10_signclr(w))		/* Clear sign */
static inline w10_t w10_signclr(w10_t wi)
    {	register w10union_t w; w.i = wi;
	w.uwd.lh &= ~H10SIGN;
	return w.i;
    }
#define op10m_signset(w) op10m_tlo(w, H10SIGN)		/* Set sign */

/* Unary ops */
#define op10m_skipn(w)	(XWDVAL(w) != 0)
#define op10m_setcm(w)	(XWDVAL(w) ^= W10MASK)
#define op10m_setz(w)	(XWDVAL(w) = 0)
#define op10m_seto(w)	(XWDVAL(w) = W10MASK)

/* Binary ops, no flags */
#define op10m_and(a,b)	(XWDVAL(a) &= XWDVAL(b))
#define op10m_ior(a,b)	(XWDVAL(a) |= XWDVAL(b))
#define op10m_xor(a,b)	(XWDVAL(a) ^= XWDVAL(b))
#define op10m_andcm(a,b) (XWDVAL(a) &= ~XWDVAL(b))
#define op10m_tdnn(a,b) ((XWDVAL(a) & XWDVAL(b)) != 0)
#define op10m_camn(a,b)	(XWDVAL(a) != XWDVAL(b))
#define op10m_ucmpl(a,b) (XWDVAL(a) < XWDVAL(b))
#define op10m_caml(a,b)	(((LHGET(a)^LHGET(b))&H10SIGN) \
			? op10m_ucmpl(b,a) : op10m_ucmpl(a,b))

#define op10m_lshift(w,s) ((w) = w10_lshift(w, s))
static inline w10_t w10_lshift(w10_t wi, h10_t s)
    {	register w10union_t w; w.i = wi;
	OP10H_LSHIFT(w, s);
	return w.i;
    }
#define op10m_rshift(w,s) ((w) = w10_rshift(w, s))
static inline w10_t w10_rshift(w10_t wi, h10_t s)
    {	register w10union_t w; w.i = wi;
	OP10H_RSHIFT(w, s);
	return w.i;
    }

#define op10m_tlo(w,l) ((w) = w10_tlo(w, l))
    static inline w10_t w10_tlo(w10_t wi, h10_t l)
    {	register w10union_t w; w.i = wi; w.uwd.lh |= l; return w.i; }
#define op10m_tlz(w,l) ((w) = w10_tlz(w, l))
    static inline w10_t w10_tlz(w10_t wi, h10_t l)
    {	register w10union_t w; w.i = wi; w.uwd.lh &= ~l; return w.i; }
#define op10m_tlnn(w,l) ((LHGET(w) & (l)) != 0)
#define op10m_tro(w,h)  (XWDVAL(w) |= (h))
#define op10m_trz(w,h)  (XWDVAL(w) &= ~(h))
#define op10m_trnn(w,h) ((XWDVAL(w) & (h)) != 0)
#define op10m_txnn(w,l,r) ((XWDVAL(w) & XWDIMM(l,r)) != 0)
#define op10m_cail(w,r)  (LHGET(w) ? op10m_signtst(w) : (RHGET(w) < (r)))
#define op10m_caile(w,r) (LHGET(w) ? op10m_signtst(w) : (RHGET(w) <= (r)))
#define op10m_cain(w,r)  (RHGET(w) != (r) || LHGET(w))

/* Arithmetic ops, special flagless versions */
#define op10m_movn(w)	((w) = w10_movn(w))
    static inline w10_t w10_movn(w10_t wi)
    {	register w10union_t w; w.i = wi; OP10H_MOVN(w); return w.i; }
#define op10m_add(a,b)	((a) = w10_add(a,b))
    static inline w10_t w10_add(w10_t ai, w10_t bi)
    {	register w10union_t a, b; a.i = ai; b.i = bi;
	OP10H_ADD(a,b);
	return a.i;
    }
#define op10m_sub(a,b)	((a) = w10_sub(a,b))
static inline w10_t w10_sub(w10_t ai, w10_t bi)
    {	register w10union_t a, b; a.i = ai; b.i = bi;
	OP10H_SUB(a,b);
	return a.i;
    }
#define op10m_addi(w,r)	((w) = w10_addi(w,r))
    static inline w10_t w10_addi(w10_t wi, h10_t r)
    {	register w10union_t w; w.i = wi; OP10H_ADDI(w,r); return w.i; }
#define op10m_subi(w,r)	((w) = w10_subi(w,r))
    static inline w10_t w10_subi(w10_t wi, h10_t r)
    {	register w10union_t w; w.i = wi; OP10H_SUBI(w,r); return w.i; }
#define op10m_inc(w)	((w) = w10_inc(w))
    static inline w10_t w10_inc(w10_t wi)
    {	register w10union_t w; w.i = wi; OP10H_INC(w); return w.i; }
#define op10m_dec(w)	((w) = w10_dec(w))
    static inline w10_t w10_dec(w10_t wi)
    {	register w10union_t w; w.i = wi; OP10H_DEC(w); return w.i; }

/* Arithmetic ops, flag-setting versions! */
# ifdef OP10_PCFSET
#  define op10mf_inc(w)	((w) = w10f_inc(w))
    static inline w10_t w10f_inc(w10_t wi)
	{ register w10union_t w; w.i = wi; OP10HF_INC(w); return w.i; }
#  define op10mf_dec(w)	((w) = w10f_dec(w))
    static inline w10_t w10f_dec(w10_t wi)
	{ register w10union_t w; w.i = wi; OP10HF_DEC(w); return w.i; }
#  define op10mf_movn(w) ((w) = w10f_movn(w))
    static inline w10_t w10f_movn(w10_t wi)
	{ register w10union_t w; w.i = wi; OP10HF_MOVN(w); return w.i; }
# endif /* OP10_PCFSET */

/* endif WORD10_USEGCCSPARC */


#elif WORD10_USEHUN

/* Simple word-value stuff, same as USEINT */
#define op10m_signtst(w) ((XWDVAL(w)&W10SIGN)!=0)	/* Test for sign bit */
#define op10m_magstst(w) ((XWDVAL(w)&W10MAGS)!=0)	/* Test for mag bits */
#define op10m_signclr(w) (XWDVAL(w)&=W10MAGS)		/* Clear sign */
#define op10m_signset(w) (XWDVAL(w)|=W10SIGN)		/* Set sign */

/* Unary ops, same as USEINT */
#define op10m_skipn(w)	(XWDVAL(w) != 0)
#define op10m_setcm(w)	(XWDVAL(w) ^= W10MASK)
#define op10m_setz(w)	(XWDVAL(w) = 0)
#define op10m_seto(w)	(XWDVAL(w) = W10MASK)

/* Binary ops, no flags.  Most same as USEINT, others same as USEHWD. */
#define op10m_and(a,b)	(XWDVAL(a) &= XWDVAL(b))
#define op10m_ior(a,b)	(XWDVAL(a) |= XWDVAL(b))
#define op10m_xor(a,b)	(XWDVAL(a) ^= XWDVAL(b))
#define op10m_andcm(a,b) (XWDVAL(a) &= ~XWDVAL(b))
#define op10m_tdnn(a,b) ((XWDVAL(a) & XWDVAL(b)) != 0)
#define op10m_camn(a,b)	(XWDVAL(a) != XWDVAL(b))
#define op10m_ucmpl(a,b) (XWDVAL(a) < XWDVAL(b))
#define op10m_caml(a,b)	(((XWDVAL(a) ^ XWDVAL(b))&W10SIGN) \
			? op10m_ucmpl(b,a) : op10m_ucmpl(a,b))
#define op10m_lshift(w,s) OP10H_LSHIFT(w,s)
#define op10m_rshift(w,s) OP10H_RSHIFT(w,s)
#define op10m_tlo(w,l)    (LHVAL(w) |= (l))
#define op10m_tlz(w,l)	  (LHVAL(w) &= ~(l))
#define op10m_tlnn(w,l)   ((LHGET(w) & (l)) != 0)
#define op10m_tro(w,h)  (XWDVAL(w) |= (h))
#define op10m_trz(w,h)  (XWDVAL(w) &= ~(h))
#define op10m_trnn(w,h) ((XWDVAL(w) & (h)) != 0)
#define op10m_txnn(w,l,r) ((XWDVAL(w) & XWDIMM(l,r)) != 0)

#define op10m_cail(w,r)	(op10m_signtst(w) \
			? (XWDVAL(w) > (r)) : (XWDVAL(w) < (r)))
#define op10m_caile(w,r) (op10m_signtst(w) \
			? (XWDVAL(w) >= (r)) : (XWDVAL(w) <= (r)))
#define op10m_cain(w,r)   (XWDVAL(w) != (r))

/* Arithmetic ops, special flagless versions.  Same as USEHWD. */
#define op10m_movn(w)	OP10H_MOVN(w)
#define op10m_add(a,b)	OP10H_ADD(a,b)
#define op10m_sub(a,b)	OP10H_SUB(a,b)
#define op10m_addi(w,r)	OP10H_ADDI(w,r)
#define op10m_subi(w,r)	OP10H_SUBI(w,r)
#define op10m_inc(w)	OP10H_INC(w)
#define op10m_dec(w)	OP10H_DEC(w)

/* Arithmetic ops, flag-setting versions!  Same as USEHWD.*/
# ifdef OP10_PCFSET
#  define op10mf_inc(w)	OP10HF_INC(w)
#  define op10mf_dec(w)	OP10HF_DEC(w)
#  define op10mf_movn(w) OP10HF_MOVN(w)
# endif /* OP10_PCFSET */

#endif /* WORD10_USEHUN */


/* Derive remaining op10m macros in model-independent way */

#define op10m_iori(w,h)   op10m_tro(w,h)
#define op10m_andcmi(w,h) op10m_trz(w,h)

#define op10m_caie(w,r)  !op10m_cain(w,r)	/* (w == i) */
#define op10m_caig(w,r)  !op10m_caile(w,r)	/* (w >  i) */
#define op10m_caige(w,r) !op10m_cail(w,r)	/* (w >= i) */
#ifndef op10m_skipl
#  define op10m_skipl(w) op10m_signtst(w)	/* (w < 0) */
#endif
#ifndef op10m_skipg				/* (w > 0) */
#  define op10m_skipg(w) (!op10m_skipl(w) && op10m_skipn(w))
#endif
#define op10m_skipe(w)	!op10m_skipn(w)		/* (w == 0) */
#define op10m_skiple(w)	!op10m_skipg(w)		/* (w <= 0) */
#define op10m_skipge(w)	!op10m_skipl(w)		/* (w >= 0) */
#define op10m_camg(a,b)	op10m_caml(b,a)		/* (a  > b) */
#define op10m_came(a,b)	!op10m_camn(a,b)	/* (a == b) */
#define op10m_camle(a,b) !op10m_camg(a,b)	/* (a <= b) */
#define op10m_camge(a,b) !op10m_caml(a,b)	/* (a >= b) */

/* Model-independent op10mf_ flag-setting macros */

#ifdef OP10_PCFSET
# define op10mf_movm(w) (op10m_signtst(w) ? op10mf_movn(w) : 0)
# define op10mf_addi(w,r) \
    (op10m_signtst(w)	\
	? (op10m_addi(w,r), (op10m_signtst(w)		\
		? 0 : OP10_PCFSET(PCF_CR0|PCF_CR1)) )	\
	: (op10m_addi(w,r), (op10m_signtst(w)		\
		? OP10_PCFSET(PCF_CR1|PCF_ARO|PCF_TR1) : 0) ))
# define op10mf_subi(w,r) \
    (op10m_signtst(w)	\
	? (op10m_subi(w,r), (op10m_signtst(w)		\
		? OP10_PCFSET(PCF_CR0|PCF_CR1)		\
		: OP10_PCFSET(PCF_CR0|PCF_ARO|PCF_TR1))) \
	: (op10m_subi(w,r), (op10m_signtst(w)		\
		? 0 : OP10_PCFSET(PCF_CR0|PCF_CR1)) ))

# define op10mf_dmovn(d) ( \
    op10m_movn((d).w[1]),	/* Negate low word */\
    op10m_signclr((d).w[1]),	/* Clear its sign bit */\
    (op10m_skipn((d).w[1])	/* Anything left? */\
	? op10m_setcm((d).w[0])	/* Yep, just complement high */\
	: op10mf_movn((d).w[0])	/* Low clear, carry into high, with flags */\
    ))

/* Actually, the op10mf_inc and op10mf_dec macros could be model-independent
** as well (see the defs for USEINT) but the halfword versions are
** slightly more efficient.
*/
#endif /* OP10_PCFSET */

/* Model-independent double-word macros */

#define op10m_dsetz(d) (op10m_setz((d).w[0]), op10m_setz((d).w[1]))

#define op10m_dmovn(d) ( \
    op10m_movn((d).w[1]),	/* Negate low word */\
    op10m_signclr((d).w[1]),	/* Clear its sign bit */\
    (op10m_skipn((d).w[1])	/* Anything left? */\
	? op10m_setcm((d).w[0])	/* Yep, no carry, just complement high */\
	: op10m_movn((d).w[0])	/* Low clear, carry into high, no flags */\
    ))

/* Special double-word fixed-point negation, which sets low sign to high's. */
#define op10m_dneg(d) ( \
    op10m_dmovn(d),		/* Negate double, always clears low sign */\
    (op10m_signtst((d).w[0])	/* Is high sign set? */\
	? (op10m_signset((d).w[1]), 0)	/* Yep, set low sign */\
	: 0			/* Bogosity to keep C type-checking happy */\
    ))

/* Special unsigned add of word to double-word.  Leaves low sign clear. */
#define op10m_udaddw(da,b) (\
    op10m_signclr((da).w[1]),	/* Must ignore signs of low words */\
    op10m_signclr(b),	\
    op10m_add((da).w[1], (b)),	/* Add low B to low A */\
    (op10m_skipl((da).w[1]) ?	/* If sign set, clear and carry to high */\
	(op10m_signclr((da).w[1]), op10m_inc((da).w[0]), 0)	\
	: 0))

/* Special unsigned compare of double-words */
#define op10m_udcmpl(da,db) \
	(op10m_ucmpl((da).w[0], (db).w[0])		\
	    || (op10m_came((da).w[0], (db).w[0])	\
		 && op10m_ucmpl((da).w[1], (db).w[1]) ) )


/* Special restricted left-shift of double-word fix value.
**	Semi-arithmetic shift, ignores low-word sign
**	but shifts into high-word sign.
**	Restricted to shift of 1-17 bits.  Always clears low-word sign!
*/
#define op10m_ashcleft(d,s) ( \
    op10m_lshift((d).w[0], s),	\
    op10m_iori((d).w[0], \
	((LHGET((d).w[1])>>((H10BITS-1)-(s))) & (((h10_t)1<<(s))-1))), \
    op10m_lshift((d).w[1], s), \
    op10m_signclr((d).w[1])  )


#endif /* ifndef KN10OPS_INCLUDED */
