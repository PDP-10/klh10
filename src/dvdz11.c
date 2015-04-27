/* DVDZ11.C - Emulates DZ11 terminal mpxr device for KS10
*/
/* $Id: dvdz11.c,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: dvdz11.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*	Just a dummy for now; writes do nothing and reads return 0.
*/

#include "klh10.h"

#if !KLH10_DEV_DZ11 && CENV_SYS_DECOSF
	/* Stupid gubbish needed to prevent OSF/1 AXP compiler from
	** halting merely because compiled file is empty!
	*/
static int decosfcclossage;
#endif

#if KLH10_DEV_DZ11	/* Moby conditional for entire file */

#include "kn10def.h"
#include "kn10dev.h"
#include "dvuba.h"
#include "dvdz11.h"

#ifdef RCSID
 RCSID(dvdz11_c,"$Id: dvdz11.c,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

#ifndef NDZ11S_MAX	/* Max # of DZ11s to support */
# define NDZ11S_MAX 4
#endif

struct dzdev {
    struct device dz_dv;
    int dz_flags;

    /* More stuff later if supporting this crock is ever deemed worthwhile.
       Personally I'd go with a DH11 for terminal support.
     */
};

	/* Flags for dz_dflags */
#define DVDZ11F_RPI 01		/* Receive side has PI request */
#define DVDZ11F_TPI 02		/* Transmit side has PI request */


static void dz11_init(struct device *d);
static dvureg_t dz11_pivec(register struct device *d);
/*
static dvureg_t dz11_read();
static void dz11_write();
*/

static int ndz11s = 0;			/* Bumped by one each config call! */
struct dzdev dvdz11[NDZ11S_MAX];	/* External for easier debugging */


struct device * dvdz11_init(FILE *f, char *s)
{
    register struct device *d;
    register int n;

    if ((n = ndz11s) >= NDZ11S_MAX-1) {
	fprintf(f, "Cannot add another DZ11, limit %d.\n", NDZ11S_MAX);
	return NULL;
    }
    ++ndz11s;
    d = (struct device *)&dvdz11[n];		/* Assign a device */
    iodv_setnull(d);		/* Set up as null device */

    d->dv_addr = UB_DZ11(n);		/* 1st valid Unibus address */
    d->dv_aend = UB_DZ11END(n);		/* 1st invalid Unibus address */
    d->dv_brlev = UB_DZ11_BR;		/* BR level */
    d->dv_brvec = UB_DZ11_VEC(n);	/* BR vector */
    d->dv_pivec = dz11_pivec;	/* PI: Get vector */
#if 0
    d->dv_read = dz11_read;	/* Read Unibus register */
    d->dv_write = dz11_write;	/* Write Unibus register */
#endif

    dz11_init(d);

    return d;
}

static dvureg_t dz11_pivec(register struct device *d)
{
    register int vec = 0;

    /* Give priority to input side (later can alternate) */
    if (d->dv_dflags & DVDZ11F_RPI) {
	d->dv_dflags &= ~DVDZ11F_RPI;
	vec = d->dv_brvec;		/* Receive vector same as base */
    } else if (d->dv_dflags & DVDZ11F_TPI) {
	d->dv_dflags &= ~DVDZ11F_TPI;
	vec = d->dv_brvec + 4;		/* Transmit vector is plus 4 */
    }

    if ((d->dv_dflags & (DVDZ11F_TPI | DVDZ11F_TPI)) == 0)
	(*d->dv_pifun)(d, 0);	/* Turn off interrupt request */

    return vec;			/* Return vector to use */
}

static void dz11_init(struct device *d)
{
}

#endif /* KLH10_DEV_DZ11 */

