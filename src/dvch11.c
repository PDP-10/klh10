/* DVCH11.C - Emulates CH11 Chaosnet interface for KS10
*/
/* $Id: dvch11.c,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: dvch11.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*	Just a dummy for now; writes do nothing and reads return 0.
*/

#include "klh10.h"

#if !KLH10_DEV_CH11 && CENV_SYS_DECOSF
	/* Stupid gubbish needed to prevent OSF/1 AXP compiler from
	** halting merely because compiled file is empty!
	*/
static int decosfcclossage;
#endif

#if KLH10_DEV_CH11	/* Moby conditional for entire file */

#include "kn10def.h"
#include "kn10dev.h"
#include "dvuba.h"
#include "dvch11.h"

#ifdef RCSID
 RCSID(dvch11_c,"$Id: dvch11.c,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

struct device dvch11;	/* Device structure for one CH11 */
			/* Can be static, but left external for debugging */


struct device *dvch11_init(FILE *f, char *s)
{
    iodv_setnull(&dvch11);	/* Set up as null device */

    dvch11.dv_addr = UB_CH11;		/* 1st valid Unibus address */
    dvch11.dv_aend = UB_CH11END;	/* 1st invalid Unibus address */
    dvch11.dv_brlev = UB_CH11_BR;
    dvch11.dv_brvec = UB_CH11_VEC;

    return &dvch11;
}

#endif /* KLH10_DEV_CH11 */

