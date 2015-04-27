/* DVRH11.H - RH11 Controller definitions
*/
/* $Id: dvrh11.h,v 2.3 2001/11/10 21:28:59 klh Exp $
*/
/*  Copyright © 1992, 1993, 1997, 2001 Kenneth L. Harrenstien
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
 * $Log: dvrh11.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */
/*
**	Portions of this file were derived from AI:SYSTEM;RH11 DEFS48
*/

#ifndef RH11_INCLUDED
#define RH11_INCLUDED 1

#ifdef RCSID
 RCSID(dvrh11_h,"$Id: dvrh11.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

#ifndef RH11_NSUP
# define RH11_NSUP 3
#endif

extern struct device *dvrh11_create(FILE *f, char *s);	/* Initialization entry point */

/* RH11 Unibus register definitions:
*/

#if 0
/* Default RH11 addresses and interrupt vectors */
#define UBA1_RH_ADDR	0776700	/* UBA #1 RH11 registers start here */
#define	UBA1_RH_BR	6	/* Interrupts occur on level 6 (high pri) */
#define	UBA1_RH_VEC	0254	/* with this vector  */

#define UBA3_RH_ADDR	0772440	/* UBA #3 RH11 registers start here */
#define	UBA3_RH_BR	6	/* Interrupts occur on level 6 (high pri) */
#define	UBA3_RH_VEC	0224	/* with this vector */
#endif

/* RH11 register definitions.  Note that the ordering of these is different
** from that of the RH20.
**
** The CS1 register is shared between the RH11 and the currently selected
** drive.  All others are either completely in the RH11 or completely in
** each drive.  (this info per KSREF.MEM 12/78 p.3-6)
**
** RH11-only registers:
**	CS1 bits 15-13, 10-6 inclusive (0163700)
**	WC, BA, CS2, DB
** Each-Drive registers:
**	CS1 bits 12-11, 5-0 inclusive (0014077)
**	DA, DS, ER1, AS, LA, MR, DT, SN, OF, DC, CC, ER2, ER3, EC1, EC2
**	(Note AS is actually a summary of all drives)
*/

#define	RH11R_CS1	0	/* (RH/DR) CTRL AND STATUS 1. */
#define	RH11R_WC	1	/* (RH) WORD COUNT. */
#define	RH11R_BA	2	/* (RH) UNIBUS ADDRESS. */
#define	RH11R_ADR	3	/* (DR) DESIRED ADDRESS. */
#define	RH11R_CS2	4	/* (RH) CTRL AND STATUS 2. */
#define	RH11R_STS	5	/* (DR) DRIVE STATUS. */
#define	RH11R_ER1	6	/* (DR) ERROR 1. */
#define	RH11R_ATN	7	/* (DR*) ATTENTION SUMMARY. */
#define	RH11R_LAH	010	/* (DR) LOOK AHEAD. */
#define	RH11R_BUF	011	/* (DR) DATA BUFFER. */
#define	RH11R_MNT	012	/* (DR) MAINTENANCE. */
#define	RH11R_TYP	013	/* (DR) DRIVE TYPE. */
#define	RH11R_SER	014	/* (DR) SERIAL NUMBER. */
#define	RH11R_OFS	015	/* (DR) OFFSET. */
#define	RH11R_CYL	016	/* (DR) DESIRED CYLINDER. */
#define	RH11R_CCY	017	/* (DR) CURRENT CYLINDER (RM03/80 unused) */
#define	RH11R_ER2	020	/* (DR) ERROR 2          (RM03/80 unused) */
#define	RH11R_ER3	021	/* (DR) ERROR 3 */
#define	RH11R_POS	022	/* (DR) ECC POSITION. */
#define	RH11R_PAT	023	/* (DR) ECC PATTERN. */
#define RH11R_N		024	/* # of RH11 registers */


/* RH-internal register bits */

#define RH_CS1_RH11	0163700	/* RH11 bits in CS1 */
#define RH_CS1_DRIVE	 014077	/* DR bits in CS1: Bit12+RH_XDVA+RH_XCMD */

/* CS1: (RH/DR) CTRL AND STATUS 1 */
/*		Apparently only RH_XTRE and RH_XMCP cause errors */
/*		SC is only set if some ATTN bits are on */
#define RH_XSC	(1<<15)	/* [RH] RO  -I  Special Condition */
#define RH_XTRE	(1<<14)	/* [RH] R/W 2  Transfer Error (set it to clear?) */
#define RH_XMCP	(1<<13)	/* [RH] RO  2  Control Bus Parity Error */
		/*	(1<<12)	 * [dr] ??? */
#define RH_XDVA	(1<<11)	/* [dr] RO  -  Drive Available */
#define RH_XPSE	(1<<10)	/* [RH]  ?  -  Port Select */
#define RH_XA17	(1<<9)	/* [RH] R/W -  UB Address Extension Bit 17 */
#define RH_XA16	(1<<8)	/* [RH] R/W -  UB Address Extension Bit 16 */
#define RH_XRDY	(1<<7)	/* [RH] RO  2  Ready (for next command) */
#define RH_XIE	(1<<6)	/* [RH] R/W 2  Interrupt Enable */
#define RH_XCMD	077	/* [dr] R/W 2  Bits 1-5 specify commands. */
#define RH_XGO	(1<<0)	/* [dr] R/W 2  GO bit */

/* CS2: (RH) CTRL AND STATUS 2 */
/*			[i2] - Used by ITS,T20 */
#define RH_YDLT	(1<<15)	/* RO  2  Data Late == CSW_OVN Overrun */
#define RH_YWCE	(1<<14)	/* RO  -  Write Check Error */
#define RH_YPE	(1<<13)	/* RO  2  Parity Error == CSW_MPAR */
#define RH_YNED	(1<<12)	/* RO  2  Non-existant Drive */
#define RH_YNEM	(1<<11)	/* RO  2  BA is NXM during DMA == CSW_NXM */
#define RH_YPGE	(1<<10)	/* RO  2  Program Error   == CSW_RH20E RH error */
#define RH_YMXF	(1<<9)	/* RO  2  Missed Transfer == CSW_RH20E RH error */
#define RH_YMDP	(1<<8)	/* RO  2  Mass Data Bus Parity Error == CSW_MPAR */
#define RH_YOR	(1<<7)	/*  ?  -  Output Ready (for Silo buffer diag.) */
#define RH_YIR	(1<<6)	/*  ?  -  Input  Ready (for Silo buffer diag.) */
#define RH_YCLR	(1<<5)	/* R/W 2  Controller Clear */
#define RH_YPAT	(1<<4)	/*  ?  -  Parity Test */
#define RH_YBAI	(1<<3)	/*  ?  -  Unibus Address Increment Inhibit */
#define RH_YDSK	07	/* R/W 2  Unit Select */

#endif /* ifndef RH11_INCLUDED */
