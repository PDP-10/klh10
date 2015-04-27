/* OPDATA.C - Holds data definitions and instruction opcode tables.
*/
/* $Id: opdata.c,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: opdata.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#define EXTDEF		/* Definitions, not declarations! */

#include <stddef.h>	/* For NULL */
#include <stdio.h>	/* For op_init error reporting */

#include "klh10.h"
#include "kn10def.h"
#include "opdefs.h"

#ifdef RCSID
 RCSID(opdata_c,"$Id: opdata.c,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* Initialize master table at compile time.
**	op_init() uses this data to fill out all other tables at runtime.
*/
struct opdef opclist[] = {
#  define idef(i, nam, en, rtn, fl)  {nam, i, fl, (opfp_t) rtn },
#  define iodef(i, nam, en, rtn, fl) {nam, i, fl, (opfp_t) rtn },
#  define ixdef(i, nam, en, rtn, fl) {nam, i, fl, (opfp_t) rtn },
#  include "opcods.h"
#  undef idef
#  undef iodef
#  undef ixdef
	{0, -1}			/* Ends with null name, -1 value. */
};

/* Made-up default defs not in master list */
static struct opdef opdef_muuo   =
		{ "MUUO",   0, (IF_1X1|IF_OPN), (opfp_t) i_muuo };
static struct opdef opdef_luuo   =
		{ "LUUO",   0, (IF_1X1|IF_OPN), (opfp_t) i_luuo };
static struct opdef opdef_ixdflt =
		{"X_UNDEF", 0, (IF_SPEC),       (opfp_t) ix_undef };
static struct opdef opdef_iodflt =
		{ "IO",     0, (IF_IO),         (opfp_t) NULL };
#if !KLH10_CPU_KS
static struct opdef opdef_diodflt =
		{"DIO",    0, (IF_IO),         (opfp_t) i_diodisp };
#endif /* !KS */


/* Old-IO instruction names, indexed by IOX_<name> */
char *opcionam[8] = {
	"BLKI", "DATAI", "BLKO",  "DATAO",
	"CONO", "CONI",	 "CONSZ", "CONSO"
};
int opcioflg[8] = {
	IF_IO|IF_SPEC|IF_MS|IF_M1,	/* BLKI   data -> @ ptr in c(E) */
	IF_IO|IF_MW|IF_M1,		/* DATAI  data -> c(E) */
	IF_IO|IF_SPEC|IF_MS|IF_M1,	/* BLKO   @ ptr in c(E) -> data */
	IF_IO|IF_MR|IF_M1,		/* DATAO  c(E) -> data */
	IF_IO|IF_ME,			/* CONO   E -> device */
	IF_IO|IF_MW|IF_M1,		/* CONI   device -> c(E) */
	IF_IO|IF_SKP|IF_ME,		/* CONSZ  !(device & E) */
	IF_IO|IF_SKP|IF_ME		/* CONSO   (device & E) */
};

char *opcdvnam[128] = {
	"APR",	/* 000 */
	"PI",	/* 004 */
	"PAG",	/* 010 */
	"CCA",	/* 014 */
	"TIM",	/* 020 */
	"MTR",	/* 024 */
    /*   030,034,040,044,050,054,060,064,070,074 */
	   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	"PTP",	/* 100 */
	"PTR",	/* 104 */
	0,	/* 110 */
	0,	/* 114 */
	"TTY", 	/* 120 */
	0,	/* 124 */
	"DIS" 	/* 130 */
};

/* OP_INIT - Initialize PDP10 instruction op data
**	The master table is built up at compile time from the definitions
**	in opdefs.h and opcods.h.  The actual tables needed for runtime
**	dispatch operation are too complicated for simple initialization, so
**	this is where they're set up at runtime.
*/
int
op_init(void)
{
    register struct opdef *op;
    register int i;
    int errs = 0;

    /* First set all dispatch tables to default values */

    /* Primary dispatch for normal ops */
    for (op = &opdef_muuo, i = 0; i < I_N; ++i) {	/* 0-777 = MUUO */
	cpu.opdisp[i] = op->oprtn;
	opcptr[i] = op;
    }
    /* Modify primary table so 001-037 become LUUOs */
    for (op = &opdef_luuo, i = 1; i <= 037; ++i) {
	cpu.opdisp[i] = op->oprtn;
	opcptr[i] = op;
    }
#if !KLH10_CPU_KS
    /* Modify primary table so 0700-0777 become dev-IO instructions */
    for (op = &opdef_diodflt, i = 0700; i <= 0777; ++i) {
	cpu.opdisp[i] = op->oprtn;
	opcptr[i] = op;
    }
#endif /* !KS */

    /* Secondary dispatch for EXTEND ops */
    for (op = &opdef_ixdflt, i = 0; i < IX_N; ++i) {	/* Extend ops */
	opcxrtn[i] = (opxfp_t) (op->oprtn);
	opcxptr[i] = op;
    }

    /* Secondary dispatch for IO ops */
    for (op = &opdef_iodflt, i = 0; i < IO_N; ++i) {	/* IO ops */
	opciortn[i] = (opiofp_t) (op->oprtn);
	opcioptr[i] = op;
    }

    /* Now set up values from compile-time data in master list */

    for(op = opclist, i = 0; (op->opstr || op->opval != -1); ++op, ++i) {
	if (0 <= op->opval && op->opval < I_N) {
	    cpu.opdisp[op->opval] = op->oprtn;		/* Normal op */
	    opcptr[op->opval] = op;
	} else if (IXOP(0) <= op->opval && op->opval <= IXOP(IX_N-1)) {
	    opcxrtn[IXIDX(op->opval)] = (opxfp_t) (op->oprtn);	/* EXTEND op */
	    opcxptr[IXIDX(op->opval)] = op;
	} else if (IOEXOP(0,0) <= op->opval
		&& op->opval <= IOEXOP(IOX_N-1,IODV_N-1)) {
	    opciortn[IOIDX(op->opval)] = (opiofp_t)(op->oprtn);	/* IO op */
	    opcioptr[IOIDX(op->opval)] = op;
	} else {
	    fprintf(stderr, "op_init: Illegal opcode! Def %d:(%#o, \"%s\")\n",
			i, op->opval, (op->opstr ? op->opstr : "<null>"));
	    ++errs;
	}
    }
    return (errs ? 0 : 1);
}
