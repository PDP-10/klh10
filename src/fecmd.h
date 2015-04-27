/* FECMD.H - FE command parser and CTY utilities
*/
/* $Id: fecmd.h,v 2.1 2002/03/21 09:46:52 klh Exp $
*/
/*  Copyright © 2002 Kenneth L. Harrenstien
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
 * $Log: fecmd.h,v $
 * Revision 2.1  2002/03/21 09:46:52  klh
 * Initial distribution checkin
 *
 */

#ifndef FECMD_INCLUDED
#define FECMD_INCLUDED 1

#ifdef RCSID
 RCSID(fecmd_h,"$Id: fecmd.h,v 2.1 2002/03/21 09:46:52 klh Exp $")
#endif

/* Flag arguments to fe_aprcont() */
#define FEAPRF_START   0x1	/* Start address provided (else continue) */
#define FEAPRF_VERBOSE 0x2	/* Verbose feedback to user */
#define FEAPRF_MODE    0x4	/* Mode provided (else no change) */

/* Exported functions */
extern void fe_cmdloop(void);	/* actually klh10.c for now */
extern void fe_ctyenable(int);
extern void fe_ctydisable(int);

extern void fe_iosig(int);
extern void fe_iosigtest(void);
extern void fe_ctysig(ossighandler_t *rtn);
extern void fe_ctyreset(void);
extern int  fe_ctyintest(void);
extern int  fe_ctyin(void);
extern int  fe_ctyout(int);
extern int  fe_ctysout(char *, int);

extern void fe_ctycmdmode(void);
extern void fe_ctyrunmode(void);
extern void fe_ctycmdrunmode(void);

extern int  fe_ctycmchar(void);
extern char *fe_ctycmline(char *, int);
extern void fe_ctycmforce(void);

extern void fe_cmpromptset(char *);
extern char *fe_cmprompt(int);

/* Exported data */
extern int feiosignulls;
extern int feiosiginps;
extern int feiosigtests;

#endif /* ifndef FECMD_INCLUDED */
