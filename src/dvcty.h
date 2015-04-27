/* DVCTY.H - Support for Console TTY (FE TTY on some machines)
*/
/* $Id: dvcty.h,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: dvcty.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#ifndef DVCTY_INCLUDED
#define DVCTY_INCLUDED 1

#ifdef RCSID
 RCSID(dvcty_h,"$Id: dvcty.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* Exported functions */

extern void cty_init(void);
extern void cty_enable(void);
extern void cty_disable(void);
extern void cty_incheck(void);

#if KLH10_CPU_KS
extern void fe_intrup(void);	/* Move/rename this */
extern void cty_timeout(void);
#endif

/* Exported data */

extern int cty_debug;

#endif /* ifndef DVCTY_INCLUDED */
