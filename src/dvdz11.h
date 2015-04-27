/* DVDZ11.H - DZ11 Terminal Un-Multiplexor
*/
/* $Id: dvdz11.h,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: dvdz11.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */
/*
**	Portions of this file were derived from AI:SYSTEM;DZ11 10
*/

#ifndef DVDZ11_INCLUDED
#define DVDZ11_INCLUDED 1

#ifdef RCSID
 RCSID(dvdz11_h,"$Id: dvdz11.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

extern struct device *dvdz11_init(FILE *f, char *s);

/* DZ11 addresses & assignments for KS10:
 DZ11	Address		Vector	UBA#	BR-Level
  #1	0760010		0340	3	5
  #2	0760020		0350	3	5
  etc	... +10		...+10
*/

#define UB_DZ11_BR 5		/* BR level */
#define	UB_DZ11_VEC(n)  (0340+(8*n))	/* DZ11 Interrupt Vector */
#define UB_DZ11_RVEC(n) (DZ_VEC(n))	/*  Receive */
#define UB_DZ11_TVEC(n) (DZ_VEC(n)+4)	/*  Transmit */
#define	UB_DZ11(n)      (0760010+(8*n))	/* DZ11 Unibus Address */

/* DZ11 Unibus register addresses & bits.
**	These are particularly weird because of the 4 register addresses,
**	two of them have different meanings depending on whether one is
**	reading or writing.  BSIOx and BCIOx instructions on those are
**	inadvisable.
**		Offset	Read		Write		DEC Names
**		0	==		RCS		==	CSR
**		2	RDR:<flgs><CM>	RLP:<params>	RBUF	LPR
**		4	==		RTC:<DTR><TCR>	==	DTR+TCR
**		6	RMS:<CD><RI>	RTD:<BM><CM>	CAR+RNG	BRK+TBUF
**		     
Note: In T20's TTDZDV.MAC at DZHU2+n there is a BCIOB of LPR.  I don't
know how this can work without erroneously reading a char, unless the
unibus is a lot more sophisticated than I thought.  Since this is only
invoked when a line hangs up, and only affects 1 line out of the 8,
perhaps no one ever noticed.  Sigh.  --KLH
*/

/*
DZLNLN==3
DZNLN==1_DZLNLN			;Number of DZ terminal lines per board
DZLNM==DZNLN-1			;Line number mask given DZ number of TTY
*/

#define DZ_LM	03400		/* Line Number Mask */
#define DZ_LS	8		/* Line number shift */

#define UB_DZRCS(n) (UB_DZ11(n)+0)	/* Control & Status Register */
#  define DZ_CMN	010		/* Maintenance */
#  define DZ_CCL	020		/* Clear */
#  define DZ_CMS	040		/* Master Scan Enable */
#  define DZ_CRE	0100		/* Receiver Interrupt Enable */
#  define DZ_CRD	0200		/* Receiver Done */
#  define DZ_CSE	010000		/* Silo Alarm Enable */
#  define DZ_CSA	020000		/* Silo Alarm */
#  define DZ_CTE	040000		/* Transmitter Interrupt Enable */
#  define DZ_CTR	0100000		/* Transmitter Ready */

#define UB_DZRLP(n) (UB_DZ11(n)+02)	/* Line Parameter Register */
#  define DZ_LLM	07		/* Line number mask */
#  define DZ_LCL	010		/* Character Length position */
#  define DZ_LSC	040		/* Stop code bit */
#  define DZ_LPY	0100		/* Parity bit */
#  define DZ_LOP	0200		/* Odd parity */
#  define DZ_LSP	0400		/* Speed code position */
#  define DZ_LSS	8		/* Speed code shift */
#  define DZ_LRO	010000		/* Receiver on */

#define UB_DZRDR(n) (UB_DZ11(n)+02)	/* Read Data Register */
#  define DZ_DCM	0377		/* Character mask */
#  define DZ_DPE	010000		/* Parity Error */
#  define DZ_DFE	020000		/* Frame Error (break key) */
#  define DZ_DOR	040000		/* Overrun */
#  define DZ_DDV	0100000		/* Data valid */

#define UB_DZRTC(n) (UB_DZ11(n)+04)	/* Transmitter Control (low byte) */
					/* & Data Terminal flags (high) */

#define UB_DZRTD(n) (UB_DZ11(n)+06)	/* Transmitter Buffer & Break registers */
#  define DZ_TCM	0377		/* Character mask */
#  define DZ_TBM	0177400		/* Break mask */

#define UB_DZRMS(n) (UB_DZ11(n)+06)	/* Modem status */
#  define DZ_MRI	0377		/* Ring detect */
#  define DZ_MCD	0177400		/* Carrier detect */


#define UB_DZ11END(n) (UB_DZRMS(n)+2)	/* First addr not used by DZ11 regs */

#endif /* ifndef DVDZ11_INCLUDED */
