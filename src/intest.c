/* INTEST.C - Logical and Arithmetic test instruction routines
*/
/* $Id: intest.c,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: intest.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include "klh10.h"
#include "kn10def.h"	/* Machine defs */
#include "kn10ops.h"	/* PDP-10 ops */

#ifdef RCSID
 RCSID(intest_c,"$Id: intest.c,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* See CODING.TXT for guidelines to coding instruction routines. */


/* Logical Testing */

#define testinsdef(name, nameE, nameA, nameN) \
	insdef(name)			\
	{	testop(;);		\
		return PCINC_1;		\
	}				\
	insdef(nameE)			\
	{	register pcinc_t ret;	\
		testop(skipop(==););	\
		return ret;		\
	}				\
	insdef(nameA)			\
	{	testop(;);		\
		return PCINC_2;		\
	}				\
	insdef(nameN)			\
	{	register pcinc_t ret;	\
		testop(skipop(!=););	\
		return ret;		\
	}

#define skipop(relop) \
	ret = ((ac_getlh(ac)&va_insect(e)) relop 0 ? PCINC_2 : PCINC_1)
#define testop(stmt) stmt
testinsdef(i_tln, i_tlne, i_tlna, i_tlnn)	/* TLN */
#undef testop

#define testop(stmt) stmt ac_setlh(ac, ac_getlh(ac) & ~va_insect(e))
testinsdef(i_tlz, i_tlze, i_tlza, i_tlzn)	/* TLZ */
#undef testop

#define testop(stmt) stmt ac_setlh(ac, ac_getlh(ac) | va_insect(e))
testinsdef(i_tlo, i_tloe, i_tloa, i_tlon)	/* TLO */
#undef testop

#define testop(stmt) stmt ac_setlh(ac, ac_getlh(ac) ^ va_insect(e))
testinsdef(i_tlc, i_tlce, i_tlca, i_tlcn)	/* TLC */
#undef testop
#undef skipop

#define skipop(relop) ret = ((ac_getrh(ac)&va_insect(e)) relop 0 ? 2 : 1)
#define testop(stmt) stmt
testinsdef(i_trn, i_trne, i_trna, i_trnn)	/* TRN */
#undef testop

#define testop(stmt) stmt ac_setrh(ac, ac_getrh(ac) & ~va_insect(e))
testinsdef(i_trz, i_trze, i_trza, i_trzn)	/* TRZ */
#undef testop

#define testop(stmt) stmt ac_setrh(ac, ac_getrh(ac) | va_insect(e))
testinsdef(i_tro, i_troe, i_troa, i_tron)	/* TRO */
#undef testop

#define testop(stmt) stmt ac_setrh(ac, ac_getrh(ac) ^ va_insect(e))
testinsdef(i_trc, i_trce, i_trca, i_trcn)	/* TRC */
#undef testop
#undef skipop

/* TDxx instructions need fullword data operations */

#define skipop(relop) ret = (op10m_tdnn(a, w) relop 0 ? 2 : 1)
#define testop(stmt) register w10_t a, w; \
		w = vm_read(e); a = ac_get(ac); stmt 
testinsdef(i_tdn, i_tdne, i_tdna, i_tdnn)	/* TDN */
#undef testop

#define testop(stmt) register w10_t a, w; \
		w = vm_read(e); a = ac_get(ac); stmt \
		op10m_andcm(a, w); ac_set(ac, a);
testinsdef(i_tdz, i_tdze, i_tdza, i_tdzn)	/* TDZ */
#undef testop

#define testop(stmt) register w10_t a, w; \
		w = vm_read(e); a = ac_get(ac); stmt \
		op10m_ior(a, w); ac_set(ac, a);
testinsdef(i_tdo, i_tdoe, i_tdoa, i_tdon)	/* TDO */
#undef testop

#define testop(stmt) register w10_t a, w; \
		w = vm_read(e); a = ac_get(ac); stmt \
		op10m_xor(a, w); ac_set(ac, a);
testinsdef(i_tdc, i_tdce, i_tdca, i_tdcn)	/* TDC */
#undef testop
/* Preserve skipop for TSxx instrs below */

#define testop(stmt) register w10_t a, w, ws; \
		ws = vm_read(e); LRHSET(w, RHGET(ws), LHGET(ws)); \
		a = ac_get(ac); stmt
testinsdef(i_tsn, i_tsne, i_tsna, i_tsnn)	/* TSN */
#undef testop

#define testop(stmt) register w10_t a, w, ws; \
		ws = vm_read(e); LRHSET(w, RHGET(ws), LHGET(ws)); \
		a = ac_get(ac); stmt \
		op10m_andcm(a, w); ac_set(ac, a);
testinsdef(i_tsz, i_tsze, i_tsza, i_tszn)	/* TSZ */
#undef testop

#define testop(stmt) register w10_t a, w, ws; \
		ws = vm_read(e); LRHSET(w, RHGET(ws), LHGET(ws)); \
		a = ac_get(ac); stmt \
		op10m_ior(a, w); ac_set(ac, a);
testinsdef(i_tso, i_tsoe, i_tsoa, i_tson)	/* TSO */
#undef testop

#define testop(stmt) register w10_t a, w, ws; \
		ws = vm_read(e); LRHSET(w, RHGET(ws), LHGET(ws)); \
		a = ac_get(ac); stmt \
		op10m_xor(a, w); ac_set(ac, a);
testinsdef(i_tsc, i_tsce, i_tsca, i_tscn)	/* TSC */
#undef testop
#undef skipop

#undef testinsdef

/* Arithmetic testing */

#define aobjdef(name, relop) \
	insdef(name)		\
	{	register w10_t w; w = ac_get(ac);	\
		LHSET(w, (LHGET(w)+1) & H10MASK);	\
		RHSET(w, (RHGET(w)+1) & H10MASK);	\
		ac_set(ac, w);	\
		if (op10m_skipl(w) relop 0) {	\
			PC_JUMP(e);		\
			return PCINC_0;		\
		} else return PCINC_1;		\
	}

aobjdef(i_aobjn, !=)		/* AOBJN */
aobjdef(i_aobjp, ==)		/* AOBJP */
#undef aobjdef

#define caiskip(name, relop, reltest) \
	insdef(name) { register w10_t a; \
		a = ac_get(ac); \
		return (reltest) ? PCINC_2 : PCINC_1; }

insdef(i_cai) { return PCINC_1; }	/* No-op, never skips */
caiskip(i_cail,  < , op10m_cail(a,va_insect(e)))
caiskip(i_caie,  ==, op10m_caie(a,va_insect(e)))
caiskip(i_caile, <=, op10m_caile(a,va_insect(e)))
insdef(i_caia) { return PCINC_2; }	/* Always skips */
caiskip(i_caige, >=, op10m_caige(a,va_insect(e)))
caiskip(i_cain,  !=, op10m_cain(a,va_insect(e)))
caiskip(i_caig,  > , op10m_caig(a,va_insect(e)))
#undef caiskip

#define camskip(name, relop, reltest)	\
	insdef(name)		\
	{	register w10_t a,b; \
		b = vm_read(e); a = ac_get(ac); \
		return (reltest) ? PCINC_2 : PCINC_1; \
	}

insdef(i_cam) { (void)vm_read(e); return PCINC_1; } /* No-op, never skips */
camskip(i_caml,  < , op10m_caml(a,b))
camskip(i_came,  ==, op10m_came(a,b))
camskip(i_camle, <=, op10m_camle(a,b))
insdef(i_cama) { (void)vm_read(e); return PCINC_2; }	/* Always skips */
camskip(i_camge, >=, op10m_camge(a,b))
camskip(i_camn,  !=, op10m_camn(a,b))
camskip(i_camg,  > , op10m_camg(a,b))
#undef camskip

	/* Common tests for next 6 groups (JUMP, SKIP, AOS/SOS, AOJ/SOJ) */
#define skiptestL	(op10m_skipl(w))
#define skiptestE	(op10m_skipe(w))
#define skiptestLE	(op10m_skiple(w))
#define skiptestGE	(op10m_skipge(w))
#define skiptestN	(op10m_skipn(w))
#define skiptestG	(op10m_skipg(w))

#define testjump(name, relop, ifjump)	\
	insdef(name)		\
	{	register w10_t w; w = ac_get(ac); \
		return ifjump ? (PC_JUMP(e), PCINC_0) : PCINC_1; \
	}

insdef(i_jump) { return PCINC_1; }		/* No-op, never jumps */
testjump(i_jumpl,  < , skiptestL)
testjump(i_jumpe,  ==, skiptestE)
testjump(i_jumple, <=, skiptestLE)
insdef(i_jumpa) { return (PC_JUMP(e), PCINC_0); }	/* Always jumps */
testjump(i_jumpge, >=, skiptestGE)
testjump(i_jumpn,  !=, skiptestN)
testjump(i_jumpg,  > , skiptestG)
#undef testjump


#define moveskip(name, ifskip)	\
	insdef(name)		\
	{	register w10_t w; w = vm_read(e);	\
		if (ac) ac_set(ac, w);		\
		return ifskip ? PCINC_2 : PCINC_1; \
	}
moveskip(i_skip, 0)		/* SKIP - Never skips */
moveskip(i_skipl, skiptestL)
moveskip(i_skipe, skiptestE)
moveskip(i_skiple,skiptestLE)
moveskip(i_skipa, 1)		/* SKIPA - Always skips */
moveskip(i_skipge,skiptestGE)
moveskip(i_skipn, skiptestN)
moveskip(i_skipg, skiptestG)
#undef moveskip

#define bumpskip(name, bumpop, ifskip)	\
	insdef(name)		\
	{	register vmptr_t p = vm_modmap(e);	\
		register w10_t w; w = vm_pget(p);	\
		bumpop;				\
		vm_pset(p, w);			\
		if (ac) ac_set(ac, w);		\
		return ifskip ? PCINC_2 : PCINC_1;		\
	}
bumpskip(i_aos,  op10mf_inc(w), 0)		/* AOS - never skips */
bumpskip(i_aosl, op10mf_inc(w), skiptestL)
bumpskip(i_aose, op10mf_inc(w), skiptestE)
bumpskip(i_aosle,op10mf_inc(w), skiptestLE)
bumpskip(i_aosa, op10mf_inc(w), 1)		/* AOSA - Always skips */
bumpskip(i_aosge,op10mf_inc(w), skiptestGE)
bumpskip(i_aosn, op10mf_inc(w), skiptestN)
bumpskip(i_aosg, op10mf_inc(w), skiptestG)

bumpskip(i_sos,  op10mf_dec(w), 0)		/* SOS - never skips */
bumpskip(i_sosl, op10mf_dec(w), skiptestL)
bumpskip(i_sose, op10mf_dec(w), skiptestE)
bumpskip(i_sosle,op10mf_dec(w), skiptestLE)
bumpskip(i_sosa, op10mf_dec(w), 1)		/* SOSA - Always skips */
bumpskip(i_sosge,op10mf_dec(w), skiptestGE)
bumpskip(i_sosn, op10mf_dec(w), skiptestN)
bumpskip(i_sosg, op10mf_dec(w), skiptestG)
#undef bumpskip


#define bumpjump(name, bumpop, ifskip)	\
	insdef(name)		\
	{	register acptr_t p = ac_map(ac);	\
		register w10_t w; w = ac_pget(p);	\
		bumpop; \
		ac_pset(p, w);			\
		return ifskip ? (PC_JUMP(e), PCINC_0) : PCINC_1;	\
	}
bumpjump(i_aoj,  op10mf_inc(w), 0)		/* AOJ - never jumps */
bumpjump(i_aojl, op10mf_inc(w), skiptestL)
bumpjump(i_aoje, op10mf_inc(w), skiptestE)
bumpjump(i_aojle,op10mf_inc(w), skiptestLE)
bumpjump(i_aoja, op10mf_inc(w), 1)		/* AOJA - Always jumps */
bumpjump(i_aojge,op10mf_inc(w), skiptestGE)
bumpjump(i_aojn, op10mf_inc(w), skiptestN)
bumpjump(i_aojg, op10mf_inc(w), skiptestG)

bumpjump(i_soj,  op10mf_dec(w), 0)		/* SOJ - never jumps */
bumpjump(i_sojl, op10mf_dec(w), skiptestL)
bumpjump(i_soje, op10mf_dec(w), skiptestE)
bumpjump(i_sojle,op10mf_dec(w), skiptestLE)
bumpjump(i_soja, op10mf_dec(w), 1)		/* SOJA - Always jumps */
bumpjump(i_sojge,op10mf_dec(w), skiptestGE)
bumpjump(i_sojn, op10mf_dec(w), skiptestN)
bumpjump(i_sojg, op10mf_dec(w), skiptestG)
#undef bumpjump

#undef skiptest

