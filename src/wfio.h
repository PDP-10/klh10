/* WFIO.H - 36-bit Word File I/O facilities
*/
/* $Id: wfio.h,v 2.4 2002/03/28 16:54:04 klh Exp $
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
 * $Log: wfio.h,v $
 * Revision 2.4  2002/03/28 16:54:04  klh
 * First pass at using LFS (Large File Support)
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#ifndef WFIO_INCLUDED
#define WFIO_INCLUDED 1

#ifdef RCSID
 RCSID(wfio_h,"$Id: wfio.h,v 2.4 2002/03/28 16:54:04 klh Exp $")
#endif

#include "cenv.h"
#include <stdio.h>	/* Needed for FILE definition */
#include <sys/types.h>	/* For off_t until it's in C99 header file */

#define WF_TYPENAMDEFS \
    wtdef(WFT_U36, "u36"), /* Unixified (Alan Bawden) */\
    wtdef(WFT_H36, "h36"), /* High-density (FTP Image) */\
    wtdef(WFT_C36, "c36"), /* Core-dump (std tape) */\
    wtdef(WFT_A36, "a36"), /* Ansi-Ascii (7-bit) */\
    wtdef(WFT_S36, "s36"), /* SIXBIT (7-track) */\
    wtdef(WFT_TNL, "tnl")  /* Text, Newline conversion (only 35 bits) */

enum wftypes {
#  define wtdef(i,n) i
	WF_TYPENAMDEFS
#  undef wtdef
};

#if CENV_SYSF_LFS > 0
typedef off_t wfoff_t;
#else
typedef long wfoff_t;
#endif

struct wfile {
	enum wftypes wftype;
	FILE *wff;
	char *wftypnam;	/* Name of type (returned by wf_typnam) */
	int wferrfmt;
	int wferrlen;
	wfoff_t wfloc;	/* Word offset from starting location in file */
	wfoff_t wfsiop;	/* Starting location in file (as a stdio pointer) */
	int wflastch;	/* May hold last char read (U36, H36, TNL only) */
#define WFC_NONE	(-1)	/* No value in lastch */
#define WFC_PREREAD	(-2)	/* Must pre-read to get value (H36 only) */
#define WFC_WVAL	(1<<7)	/* Min val saying last op was output (H36) */
#define WFC_CR		(015)	/* ASCII CR */
#define WFC_LF		(012)	/* ASCII LF */
	int wfdebug;
	unsigned char *wftp,	/* Only used if WF_DEBUG is true */
		wftbuf[5];
};
#define WFILE struct wfile	/* Analogous to stdio FILE */

extern void wf_init(WFILE *, int, FILE *);
extern int wf_type(char *);		/* Given typename, return type # */
extern int wf_rewind(WFILE *);
extern int wf_seek(WFILE *, wfoff_t);
extern int wf_flush(WFILE *);
extern int wf_get(WFILE *, w10_t *);
extern int wf_put(WFILE *, w10_t);

#define wf_typnam(wf) ((wf)->wftypnam)	/* Return name of WF type */

#endif /* ifndef WFIO_INCLUDED */
