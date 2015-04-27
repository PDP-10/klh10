/* FELOAD.H - PDP-10 boot loader defs & routines
*/
/* $Id: feload.h,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: feload.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#ifndef FELOAD_INCLUDED
#define FELOAD_INCLUDED 1

#ifdef RCSID
 RCSID(feload_h,"$Id: feload.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

#include "klh10.h"
#include "word10.h"	/* For w10_t etc */
#include "kn10def.h"	/* For vaddr_t */
#include "wfio.h"	/* For WFILE */

enum loadtypes {
	LOADT_UNKNOWN,
	LOADT_SBLK,
	LOADT_PDUMP,
	LOADT_DECSAV,
	LOADT_DECEXE
};

struct loadinfo {
	/* Argument variables for loader */
	int ldi_type;		/* LOADT_xxx type (can be UNKNOWN) */
	int ldi_debug;		/* TRUE to print debug info during load */

	/* Remaining are all result variables from loading */
	char *ldi_typname;	/* String for LOADT_xxx type */
	int ldi_allerr;
	paddr_t ldi_loaddr, ldi_hiaddr;
	w10_t ldi_startwd;
	int ldi_ndata, ldi_nsyms;
	int ldi_cksumerr;
	int ldi_aibgot;		/* If non-zero, # wds in assembly info block */
	w10_t ldi_asminf[6];	/* MIDAS assembly info block: */
#define AIB_UNAME	0	/*	UNAME of person assembling */
#define AIB_TIME	1	/*	ITS disk fmt time of assembly */
#define AIB_DEV		2	/*	Device of source */
#define AIB_FN1		3	/*	FN1 of source */
#define AIB_FN2		4	/*	FN2 of source */
#define AIB_DIR		5	/*	SNAME of source */

	paddr_t ldi_evlen;	/* DEC: Entry vector length */
	paddr_t ldi_evloc;	/* DEC: Entry vector location */
};

/* Routines */
extern int fe_load(WFILE *wf, struct loadinfo *lp);
extern int fe_dump(WFILE *wf, struct loadinfo *lp);

#endif /* ifndef FELOAD_INCLUDED */
