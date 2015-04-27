/* INBLSH.C - Boolean and Shift instruction routines
*/
/* $Id: inblsh.c,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: inblsh.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include "klh10.h"
#include "kn10def.h"	/* Machine defs */
#include "kn10ops.h"	/* PDP-10 ops */

#ifdef RCSID
 RCSID(inblsh_c,"$Id: inblsh.c,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* See CODING.TXT for guidelines to coding instruction routines. */


/* Boolean instructions */

#define boolinsdef(name, nameI, nameM, nameB)	\
	insdef(name)				\
	{	register w10_t ar, m;		\
		boolop(ac_get(ac), vm_read(e));	\
		ac_set(ac, ar);			\
		return PCINC_1;			\
	}					\
	insdef(nameI)				\
	{	register w10_t ar, m;		\
		boolop(ac_get(ac), (LRHSET(m,0,va_insect(e)), m)); \
		ac_set(ac, ar);			\
		return PCINC_1;			\
	}					\
	insdef(nameM)				\
	{	register vmptr_t p = vm_modmap(e); \
		register w10_t ar, m;		\
		boolop(ac_get(ac), vm_pget(p));	\
		vm_pset(p, ar);			\
		return PCINC_1;			\
	}					\
	insdef(nameB)				\
	{	register vmptr_t p = vm_modmap(e); \
		register w10_t ar, m;		\
		boolop(ac_get(ac), vm_pget(p));	\
		ac_set(ac, ar);			\
		vm_pset(p, ar);			\
		return PCINC_1;			\
	}

/* "boolop(a,b)" must use a and b to fetch args, and compute result into "ar",
** using scratch word "m" if needed.  If both are used, "ar" must be set first
** so that the immediate forms (which pre-set "m") can win.
*/

#define boolop(a,b) op10m_setz(ar)		/* SETZ ignores args */
boolinsdef(i_setz, i_setzi, i_setzm, i_setzb)
#undef boolop

#define boolop(a,b) op10m_seto(ar)		/* SETO ignores args */
boolinsdef(i_seto, i_setoi, i_setom, i_setob)
#undef boolop

#define boolop(a,b) (ar = (a))			/* SETA */
boolinsdef(i_seta, i_setai, i_setam, i_setab)
#undef boolop

#define boolop(a,b) (ar = (b))			/* SETM */
boolinsdef(i_setm, i_setmi, i_setmm, i_setmb)
#undef boolop

#define boolop(a,b) (ar = (a), op10m_setcm(ar))	/* SETCA */
boolinsdef(i_setca, i_setcai, i_setcam, i_setcab)
#undef boolop

#define boolop(a,b) (ar = (b), op10m_setcm(ar))	/* SETCM */
boolinsdef(i_setcm, i_setcmi, i_setcmm, i_setcmb)
#undef boolop

#define boolop(a,b) (ar = (a), m = (b), op10m_and(ar,m))	/* AND */
boolinsdef(i_and, i_andi, i_andm, i_andb)
#undef boolop

#define boolop(a,b) (ar = (b), m = (a), op10m_andcm(ar, m))	/* ANDCA */
boolinsdef(i_andca, i_andcai, i_andcam, i_andcab)
#undef boolop

#define boolop(a,b) (ar = (a), m = (b),	op10m_andcm(ar,m))	/* ANDCM */
boolinsdef(i_andcm, i_andcmi, i_andcmm, i_andcmb)
#undef boolop

#define boolop(a,b) (ar = (a), m = (b), \
	op10m_setcm(ar), op10m_setcm(m), op10m_and(ar,m))	/* ANDCB */
boolinsdef(i_andcb, i_andcbi, i_andcbm, i_andcbb)
#undef boolop

#define boolop(a,b) (ar = (a), m = (b), op10m_ior(ar,m))	/* IOR */
boolinsdef(i_ior, i_iori, i_iorm, i_iorb)
#undef boolop

#define boolop(a,b) (ar = (a), m = (b), \
		op10m_setcm(ar), op10m_ior(ar,m))	/* ORCA */
boolinsdef(i_orca, i_orcai, i_orcam, i_orcab)
#undef boolop

#define boolop(a,b) (ar = (a), m = (b), \
		op10m_setcm(m), op10m_ior(ar,m))	/* ORCM */
boolinsdef(i_orcm, i_orcmi, i_orcmm, i_orcmb)
#undef boolop

#define boolop(a,b) (ar = (a), m = (b), \
	op10m_setcm(ar), op10m_setcm(m), op10m_ior(ar,m))	/* ORCB */
boolinsdef(i_orcb, i_orcbi, i_orcbm, i_orcbb)
#undef boolop

#define boolop(a,b) (ar = (a), m = (b), op10m_xor(ar,m))	/* XOR */
boolinsdef(i_xor, i_xori, i_xorm, i_xorb)
#undef boolop

#define boolop(a,b) (ar = (a), m = (b), \
		op10m_xor(ar,m), op10m_setcm(ar))	/* EQV */
boolinsdef(i_eqv, i_eqvi, i_eqvm, i_eqvb)
#undef boolop

#undef boolinsdef

/* Shift and Rotate */

#define shiftdef(name, op) \
	insdef(name) { \
	    ac_set(ac, op(ac_get(ac), (h10_t)va_insect(e))); return PCINC_1; }

shiftdef(i_ash, op10ash)		/* May set flags */
shiftdef(i_lsh, op10lsh)
shiftdef(i_rot, op10rot)
#undef shiftdef

#define dshiftdef(name, op) \
	insdef(name)		\
	{	register dw10_t d; ac_dget(ac, d);	\
		d = op(d, (h10_t)va_insect(e));	\
		ac_dset(ac, d);	\
		return PCINC_1;	\
	}

dshiftdef(i_ashc, op10ashc)		/* May set flags */
dshiftdef(i_lshc, op10lshc)
dshiftdef(i_rotc, op10rotc)
dshiftdef(i_circ, op10circ)		/* Special ITS instruction */
#undef dshiftdef
