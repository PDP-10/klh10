/* INFLT.C - Floating-Point arithmetic instruction routines
*/
/* $Id: inflt.c,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: inflt.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include "klh10.h"
#include "kn10def.h"
#include "kn10ops.h"

#ifdef RCSID
 RCSID(inflt_c,"$Id: inflt.c,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* See CODING.TXT for guidelines to coding instruction routines. */


/* Single-precision Floating point arithmetic */

#define singleflt(fltop, name, nameM, nameB) \
	insdef(name)			\
	{	ac_set(ac, fltop(ac_get(ac), vm_read(e)));	\
		return PCINC_1;	\
	}	\
	insdef(nameM)	\
	{	register vmptr_t p = vm_modmap(e);	\
		vm_pset(p, fltop(ac_get(ac), vm_pget(p)));	\
		return PCINC_1;	\
	}	\
	insdef(nameB)	\
	{	register vmptr_t p = vm_modmap(e);	\
		ac_set(ac, fltop(ac_get(ac), vm_pget(p)));	\
		vm_pset(p, ac_get(ac));		\
		return PCINC_1;	\
	}

#define singlefimm(fltop, nameI)	\
	insdef(nameI)			\
	{	register w10_t w;	\
		LRHSET(w, va_insect(e), 0);	\
		ac_set(ac, fltop(ac_get(ac), w));	\
		return PCINC_1;	\
	}

#define singleflong(longop, nameL)	\
	insdef(nameL)			\
	{	register dw10_t d;	\
		d = longop(ac_get(ac),vm_read(e));	\
		ac_dset(ac,d);		\
		return PCINC_1;		\
	}



    singleflt(op10fadr, i_fadr, i_fadrm, i_fadrb)
    singleflt(op10fsbr, i_fsbr, i_fsbrm, i_fsbrb)
    singleflt(op10fmpr, i_fmpr, i_fmprm, i_fmprb)
    singlefimm(op10fadr, i_fadri)
    singlefimm(op10fsbr, i_fsbri)
    singlefimm(op10fmpr, i_fmpri)

    singleflt(op10fad, i_fad, i_fadm, i_fadb)
    singleflt(op10fsb, i_fsb, i_fsbm, i_fsbb)
    singleflt(op10fmp, i_fmp, i_fmpm, i_fmpb)
    singleflong(op10fadl, i_fadl) /* "Immediate" mode for FAD is FADL */
    singleflong(op10fsbl, i_fsbl) /* "Immediate" mode for FSB is FSBL */
    singleflong(op10fmpl, i_fmpl) /* "Immediate" mode for FMP is FMPL */

#undef singleflt
#undef singlefimm
#undef singleflong


/* Variation for divide-type ops since divide may fail */

#define singlefdiv(name, nameM, nameB) \
	insdef(name)			\
	{	w10_t a;		\
		a = ac_get(ac);		\
		if (divop(&a, vm_read(e)))	\
		    ac_set(ac, a);	\
		return PCINC_1;	\
	}	\
	insdef(nameM)	\
	{	register vmptr_t p = vm_modmap(e);	\
		w10_t a;			\
		a = ac_get(ac);			\
		if (divop(&a, vm_pget(p)))	\
		    vm_pset(p, a);		\
		return PCINC_1;	\
	}	\
	insdef(nameB)	\
	{	register vmptr_t p = vm_modmap(e);	\
		w10_t a;			\
		a = ac_get(ac);			\
		if (divop(&a, vm_pget(p))) {	\
		    vm_pset(p, a);		\
		    ac_set(ac, a);		\
		}				\
		return PCINC_1;			\
	}

#define divop(a,b) op10xfdv(a,b,1)		/* Use rounding */
    singlefdiv(i_fdvr, i_fdvrm, i_fdvrb)
#undef divop

#define divop(a,b) op10xfdv(a,b,0)		/* Use no rounding */
    singlefdiv(i_fdv, i_fdvm, i_fdvb)
#undef divop

#undef singlefdiv


insdef(i_fdvri)		/* Special-case FDVRI */
{
    register w10_t w;
    w10_t a;
    a = ac_get(ac);
    LRHSET(w, va_insect(e), 0);
    if (op10xfdv(&a, w, 1))
	ac_set(ac, a);
    return PCINC_1;
}

insdef(i_fdvl)		/* Special-case FDVL (immediate mode for FDV) */
{
    register dw10_t d;
    ac_dget(ac, d);
    d = op10fdvl(d, vm_read(e));
    ac_dset(ac, d);
    return PCINC_1;
}

/* Double-precision Floating-point */

#define doubleflt(name, fltop) \
	insdef(name) \
	{	register dw10_t da, de;	\
		ac_dget(ac, da);	\
		vm_dread(e, de);	\
		da = fltop(da, de);	\
		ac_dset(ac, da);	\
		return PCINC_1;		\
	}
doubleflt(i_dfad, op10dfad)
doubleflt(i_dfsb, op10dfsb)
doubleflt(i_dfmp, op10dfmp)
doubleflt(i_dfdv, op10dfdv)

/* G-Format floating-point (also double-word ops) */

#if KLH10_CPU_KL
doubleflt(i_gfad, op10gfad)
doubleflt(i_gfsb, op10gfsb)
doubleflt(i_gfmp, op10gfmp)
doubleflt(i_gfdv, op10gfdv)
#endif /* KL */

#undef doubleflt

/* Format Conversion (and UFA, DFN) */

insdef(i_fsc)
{
    ac_set(ac, op10fsc(ac_get(ac), (h10_t)va_insect(e)));
    return PCINC_1;
}

insdef(i_fix)
{
    w10_t w;
    w = vm_read(e);
    if (op10xfix(&w, 0))	/* Fix, no round, TRUE if succeeded */
	ac_set(ac, w);
    return PCINC_1;
}

insdef(i_fixr)
{
    w10_t w;
    w = vm_read(e);
    if (op10xfix(&w, 1))	/* Fix, with round, TRUE if succeeded */
	ac_set(ac, w);
    return PCINC_1;
}

insdef(i_fltr)
{
    ac_set(ac, op10fltr(vm_read(e)));
    return PCINC_1;
}


/* Obsolete KA10 instructions - DFN, UFA
**	Note FADL/FSBL/FMPL/FDVL are also obsolete KA ops.  Those were defined
**	earlier in the single-precision section.
*/

insdef(i_dfn)
{
    dw10_t d;
    register vmptr_t p = vm_modmap(e);	/* Set up to mung c(E) */
    d.w[0] = ac_get(ac);		/* Get hi word of double */
    d.w[1] = vm_pget(p);		/* Get lo word */
    d = op10dfn(d);			/* Do it! */
    ac_set(ac, d.w[0]);			/* Store results away */
    vm_pset(p, d.w[1]);
    return PCINC_1;
}

insdef(i_ufa)
{
    ac_set(ac_off(ac,1), op10ufa(ac_get(ac), vm_read(e)));
    return PCINC_1;
}

/* Double-Precision (G-Format) conversions */

#if KLH10_CPU_KL

xinsdef(ix_gfsc)		/* EXTEND [031 ] */
{
    register dw10_t d;

    ac_dget(ac, d);
    d = op10gfsc(d, (h10_t)va_insect(e1));
    ac_dset(ac, d);
    return PCINC_1;
}

xinsdef(ix_gsngl)	/* EXTEND [021 ] */
{
    dw10_t d;

    vm_dread(e1, d);
    if (op10xgsngl(&d))		/* G->F, with round, TRUE if succeeded */
	ac_set(ac, d.w[0]);
    return PCINC_1;
}

xinsdef(ix_gdble)	/* EXTEND [022 ] */
{
    register dw10_t d;

    d = op10gdble(vm_read(e1));	/* F->G */
    ac_dset(ac, d);
    return PCINC_1;
}


xinsdef(ix_dgfltr)	/* EXTEND [027 ] */
{
    register dw10_t d;

    vm_dread(e1, d);
    d = op10dgfltr(d);		/* Dfix -> G with round */
    ac_dset(ac, d);
    return PCINC_1;
}

xinsdef(ix_gfltr)	/* EXTEND [030 ] */
{
    register dw10_t d;

    d = op10gfltr(vm_read(e1));	/* fix -> G */
    ac_dset(ac, d);
    return PCINC_1;
}


#  if 0
/* These instructions are currently simulated in T10+T20 monitors.
** Later can provide the real thing in KN10OPS and activate this code.
*/
xinsdef(ix_gdfix)	/* EXTEND [023 ] */
{
    dw10_t d;
    vm_dread(e1, d);
    if (op10xgdfix(&d, 0))	/* G -> Dfix, no round, TRUE if succeeded */
	ac_dset(ac, d);
    return PCINC_1;
}
xinsdef(ix_gfix)	/* EXTEND [024 ] */
{
    dw10_t d;
    vm_dread(e1, d);
    if (op10xgfix(&d, 0))	/* G -> fix, no round, TRUE if succeeded */
	ac_set(ac, d.w[0]);
    return PCINC_1;
}
xinsdef(ix_gdfixr)	/* EXTEND [025 ] */
{
    dw10_t d;
    vm_dread(e1, d);
    if (op10xgdfix(&d, 1))	/* G -> Dfix, with round, TRUE if succeeded */
	ac_dset(ac, d);
    return PCINC_1;
}
xinsdef(ix_gfixr)	/* EXTEND [026 ] */
{
    dw10_t d;
    vm_dread(e1, d);
    if (op10xgfix(&d, 1))	/* G -> fix, with round, TRUE if succeeded */
	ac_set(ac, d.w[0]);
    return PCINC_1;
}
#  endif /* 0 */

#endif /* KLH10_CPU_KL */
