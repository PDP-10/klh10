/* DVHOST.H - HOST native platform access defniitions
*/
/* $Id: dvhost.h,v 2.3 2001/11/10 21:28:59 klh Exp $
*/
/*  Copyright © 1994, 2001 Kenneth L. Harrenstien
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
 * $Log: dvhost.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#ifndef DVHOST_INCLUDED
#define DVHOST_INCLUDED 1

#ifdef RCSID
 RCSID(dvhost_h,"$Id: dvhost.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* Only externally visible entry point for HOST driver
 */
#include "kn10dev.h"
extern struct device * dvhost_create(FILE *f, char *s);

#define DVHOST_NSUP 1		/* Should be only 1! */

/* CONO bits
**	Preliminary hackery...
*/
#define DVHOST_CO_IDLE 01		/* "Op-code" to do idle hackery */

#define DVHOST_REG_N 1			/* If on a Unibus, only one register */

#endif /* ifndef DVHOST_INCLUDED */
