/* INFIX.C - Fixed-Point arithmetic instruction routines
*/
/* $Id: infix.c,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: infix.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include "klh10.h"
#include "kn10def.h"
#include "kn10ops.h"

#ifdef RCSID
 RCSID(infix_c,"$Id: infix.c,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* See CODING.TXT for guidelines to coding instruction routines. */


/* Fixed-point arithmetic, single-word operands & results */

#define addinsdef(name, nameI, nameM, nameB)	\
	insdef(name)				\
	{	ac_set(ac, fixop(ac_get(ac), vm_read(e))); \
		return PCINC_1;		\
	}				\
	insdef(nameI)			\
	{	register w10_t w;	\
		immop();		\
		return PCINC_1;		\
	}				\
	insdef(nameM)			\
	{	register vmptr_t p = vm_modmap(e); \
		vm_pset(p, fixop(ac_get(ac), vm_pget(p))); \
		return PCINC_1;		\
	}				\
	insdef(nameB)			\
	{	register vmptr_t p = vm_modmap(e); \
		ac_set(ac, fixop(ac_get(ac), vm_pget(p))); \
		vm_pset(p, ac_get(ac));	\
		return PCINC_1;		\
	}

#define fixop(a,b) op10add((a),(b))
#define immop() w = ac_get(ac); op10mf_addi(w,va_insect(e)); ac_set(ac,w)
    addinsdef(i_add, i_addi, i_addm, i_addb)
#undef immop
#undef fixop

#define fixop(a,b) op10sub((a),(b))
#define immop() w = ac_get(ac); op10mf_subi(w,va_insect(e)); ac_set(ac,w)
    addinsdef(i_sub, i_subi, i_subm, i_subb)
#undef immop
#undef fixop

#define fixop(a,b) op10imul((a),(b))
#define immop() LRHSET(w, 0, va_insect(e)); ac_set(ac, op10imul(ac_get(ac), w))
    addinsdef(i_imul, i_imuli, i_imulm, i_imulb)
#undef immop
#undef fixop
#undef addinsdef


/* Handle single-word instructions that produce double results */

#define fixlongdef(name, nameI, nameM, nameB) \
	insdef(name)			\
	{	dw10_t d;		\
		fixop(d, ac, vm_read(e)); \
		ac_dset(ac, d);		\
		return PCINC_1;		\
	}				\
	insdef(nameI)			\
	{	dw10_t d;		\
		register w10_t w; LRHSET(w, 0, va_insect(e)); \
		fixop(d, ac, w);	\
		ac_dset(ac, d);		\
		return PCINC_1;		\
	}				\
	insdef(nameM)			\
	{	register vmptr_t p = vm_modmap(e); \
		dw10_t d;		\
		fixop(d, ac, vm_pget(p)); \
		vm_pset(p, HIGET(d));	\
		return PCINC_1;		\
	}				\
	insdef(nameB)			\
	{	register vmptr_t p = vm_modmap(e); \
		dw10_t d;		\
		fixop(d, ac, vm_pget(p)); \
		vm_pset(p, HIGET(d));	\
		ac_dset(ac, d);		\
		return PCINC_1;		\
	}

#define fixop(d,a,b) (d = op10mul(ac_get(a),(b)))
    fixlongdef(i_mul, i_muli, i_mulm, i_mulb)
#undef fixop

#define fixop(d,a,b) if (!op10xidiv(&d, ac_get(a),(b))) return PCINC_1
    fixlongdef(i_idiv, i_idivi, i_idivm, i_idivb)
#undef fixop

/* Note special hackery to furnish double-word arg to OP10XDIV */
#define fixop(d,a,b) ac_dget(a,d); if(!op10xdiv(&d,(b))) return PCINC_1
    fixlongdef(i_div, i_divi, i_divm, i_divb)
#undef fixop

#undef fixlongdef

/* Double-precision fixed-point arithmetic */

#define doublefix(name, fixop) \
	insdef(name) 			\
	{	register dw10_t da, de;	\
		ac_dget(ac, da);	\
		vm_dread(e, de);	\
		hackstmt(fixop)		\
		return PCINC_1;		\
	}
#define hackstmt(op)	da = op(da, de); ac_dset(ac, da);
    doublefix(i_dadd, op10dadd)
    doublefix(i_dsub, op10dsub)
#undef hackstmt

#define hackstmt(op) { qw10_t q; q = op(da, de);	\
			ac_dset(ac,           q.d[0]);	\
			ac_dset(ac_off(ac,2), q.d[1]); }
    doublefix(i_dmul, op10dmul)
#undef hackstmt

#define hackstmt(op) { qw10_t q; q.d[0] = da; ac_dget(ac_off(ac,2),q.d[1]);\
			q = op(q, de);			\
			ac_dset(ac,           q.d[0]);	\
			ac_dset(ac_off(ac,2), q.d[1]); }
    doublefix(i_ddiv, op10ddiv)
#undef hackstmt
