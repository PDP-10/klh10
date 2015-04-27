/* KLH10 Macro Miscellany
*/
/* $Id: kn10mac.h,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: kn10mac.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/* Miscellaneous macro stuff that's sometimes useful */

#ifndef KN10MAC_INCLUDED
#define KN10MAC_INCLUDED 1

#ifdef RCSID
 RCSID(kn10mac_h,"$Id: kn10mac.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* These field-hacking macros rely on twos-complement arithmetic
** properties such that given a value M, the expression (M & -M) will
** isolate the rightmost ones-bit of M, which will be a simple power
** of 2.  This still isn't log2, but if M is a constant then
** a decent compiler should turn multiply and divide of this power-of-2
** into logical shifts.
*/

/* FLD(uval,mask)
**       Make integer field value from right-justified unsigned value
**	(same as MACSYM's FLD(v,m) macro)
*/
#define FLD(uval,mask) (((uval)*((mask)&(-(mask))))&(mask))
 
/* FLDGET(ui,mask)
**	Get right-justified value from field in unsigned integer
*/
#define FLDGET(ui,mask) (((ui)&(mask))/((mask)&(-(mask))))

/* FLDPUT(ui,mask,uval)
**	Put right-justified unsigned value into field in integer
*/
#define FLDPUT(ui,mask,uval) (((ui)&(~(mask)))|FLD(uval,mask))


#endif /* ifndef  KN10MAC_INCLUDED */
