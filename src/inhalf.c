/* INHALF.C - Halfword instruction routines
*/
/* $Id: inhalf.c,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: inhalf.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include "klh10.h"
#include "kn10def.h"	/* Machine defs */
#include "kn10ops.h"	/* PDP-10 ops */

#ifdef RCSID
 RCSID(inhalf_c,"$Id: inhalf.c,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* See CODING.TXT for guidelines to coding instruction routines. */

/* Halfword data hackery */

#define halfinsdef(name, nameI, nameM, nameS) \
	insdef(name)				\
	{	register w10_t w;			\
		halfop(w, vm_read(e), ac_get(ac));	\
		ac_set(ac, w);			\
		return PCINC_1;			\
	}					\
	insdef(nameI)				\
	{	register w10_t v, w;	/* Need 2 to avoid self-clobber */\
		LRHSET(v, 0, va_insect(e));	\
		halfop(w, v, ac_get(ac));	\
		ac_set(ac, w);			\
		return PCINC_1;			\
	}					\
	insdef(nameM)				\
	{	register w10_t w;			\
		register vmptr_t p = vm_modmap(e);	\
		halfop(w, ac_get(ac), vm_pget(p));	\
		vm_pset(p, w);			\
		return PCINC_1;			\
	}					\
	insdef(nameS)				\
	{	register w10_t w;		\
		register vmptr_t p = vm_modmap(e);	\
		halfop(w, vm_pget(p), vm_pget(p));	\
		vm_pset(p, w);			\
		if (ac) ac_set(ac, w);		\
		return PCINC_1;			\
	}

		/* -------------------------- */

#define halfop(w,sh,dh) (LRHSET(w, LHGET(sh), RHGET(dh)))
halfinsdef(i_hll, i_hlli, i_hllm, i_hlls)		/* HLL */
#undef halfop

#define halfop(w,sh,dh) (LRHSET(w, LHGET(sh), 0))
halfinsdef(i_hllz, i_hllzi, i_hllzm, i_hllzs)		/* HLLZ */
#undef halfop

#define halfop(w,sh,dh) (LRHSET(w, LHGET(sh), H10ONES))
halfinsdef(i_hllo, i_hlloi, i_hllom, i_hllos)		/* HLLO */
#undef halfop

#if 1
# define halfop(w,sh,dh) w = (sh); if (op10m_signtst(w)) \
		op10m_tro(w, H10ONES); else op10m_trz(w, H10ONES)
#else	/* Old code - trips warnings with DU 4.0F compiler */
# define halfop(w,sh,dh) (LHSET(w, LHGET(sh)), \
		RHSET(w,((LHGET(w)&H10SIGN) ? H10ONES : 0)))
#endif
halfinsdef(i_hlle, i_hllei, i_hllem, i_hlles)		/* HLLE */
#undef halfop

		/* -------------------------- */

#define halfop(w,sh,dh) (LRHSET(w, LHGET(dh), LHGET(sh)))
halfinsdef(i_hlr, i_hlri, i_hlrm, i_hlrs)		/* HLR */
#undef halfop

#define halfop(w,sh,dh) (LRHSET(w, 0, LHGET(sh)))
halfinsdef(i_hlrz, i_hlrzi, i_hlrzm, i_hlrzs)		/* HLRZ */
#undef halfop

#define halfop(w,sh,dh) (LRHSET(w, H10ONES, LHGET(sh)))
halfinsdef(i_hlro, i_hlroi, i_hlrom, i_hlros)		/* HLRO */
#undef halfop

#if 1
# define halfop(w,sh,dh) LRHSET(w, 0, LHGET(sh)); \
		if (op10m_trnn(w, H10SIGN)) op10m_tlo(w, H10ONES)
#else /* Old code - trips warnings with DU 4.0F compiler */
# define halfop(w,sh,dh) (RHSET(w, LHGET(sh)), \
		LHSET(w,((RHGET(w)&H10SIGN) ? H10ONES : 0)))
#endif
halfinsdef(i_hlre, i_hlrei, i_hlrem, i_hlres)		/* HLRE */
#undef halfop

		/* -------------------------- */

#define halfop(w,sh,dh) (LRHSET(w, LHGET(dh), RHGET(sh)))
halfinsdef(i_hrr, i_hrri, i_hrrm, i_hrrs)		/* HRR */
#undef halfop

#define halfop(w,sh,dh) (LRHSET(w, 0, RHGET(sh)))
halfinsdef(i_hrrz, i_hrrzi, i_hrrzm, i_hrrzs)		/* HRRZ */
#undef halfop

#define halfop(w,sh,dh) (LRHSET(w, H10ONES, RHGET(sh)))
halfinsdef(i_hrro, i_hrroi, i_hrrom, i_hrros)		/* HRRO */
#undef halfop

#if 1
# define halfop(w,sh,dh) w = (sh); if (op10m_trnn(w, H10SIGN)) \
		op10m_tlo(w, H10ONES); else op10m_tlz(w, H10ONES)
#else /* Old code - trips warnings with DU 4.0F compiler */
# define halfop(w,sh,dh) (RHSET(w, RHGET(sh)), \
		LHSET(w,((RHGET(w)&H10SIGN) ? H10ONES : 0)))
#endif
halfinsdef(i_hrre, i_hrrei, i_hrrem, i_hrres)		/* HRRE */
#undef halfop

		/* -------------------------- */

#define halfop(w,sh,dh) (LRHSET(w, RHGET(sh), RHGET(dh)))
halfinsdef(i_hrl, i_hrli, i_hrlm, i_hrls)		/* HRL */
#undef halfop

#define halfop(w,sh,dh) (LRHSET(w, RHGET(sh), 0))
halfinsdef(i_hrlz, i_hrlzi, i_hrlzm, i_hrlzs)		/* HRLZ */
#undef halfop

#define halfop(w,sh,dh) (LRHSET(w, RHGET(sh), H10ONES))
halfinsdef(i_hrlo, i_hrloi, i_hrlom, i_hrlos)		/* HRLO */
#undef halfop

#if 1
# define halfop(w,sh,dh) LRHSET(w, RHGET(sh), 0); \
		if (op10m_signtst(w)) op10m_tro(w, H10ONES)
#else /* Old code - trips warnings with DU 4.0F compiler */
# define halfop(w,sh,dh) (LHSET(w, RHGET(sh)), \
		RHSET(w,((LHGET(w)&H10SIGN) ? H10ONES : 0)))
#endif
halfinsdef(i_hrle, i_hrlei, i_hrlem, i_hrles)		/* HRLE */
#undef halfop

#undef halfinsdef

