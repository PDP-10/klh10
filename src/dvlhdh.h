/* DVLHDH.H - ACC LH-DH IMP Interface definitions
*/
/* $Id: dvlhdh.h,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: dvlhdh.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */
/*
**	Portions of this file were derived from AI:SYSTEM;LHDH DEFS5
*/

#ifndef DVLHDH_INCLUDED
#define DVLHDH_INCLUDED 1

#ifdef RCSID
 RCSID(dvlhdh_h,"$Id: dvlhdh.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/*
; The ACC IMP interface on the KS implements two separate Unibus IO
; devices, for input and output. Both of these devices transfer data in
; 32-bit mode only over the Unibus via DMA.  Because of the DMA data
; transfer the interrupt structure is trivial, and uses NETCHN only.
;
; Apparently the reset bits in the two CSRs are wired together, so
; resetting one side of the machine resets both. This action also drops
; the HOST READY line to the IMP.
;
; You must set %LHSE whenever the HOST READY line is high, or the IMP
; will be allowed to freely throw away data. This is true across IMP
; message boundaries, and even if no input request is active.
; 
*/

extern struct device *dvlhdh_create(FILE *f, char *s);


/* ACC LH-DH IMP Interface Bits. */

#if 0	/* Default BR, VEC, ADDR settings for AI ITS */
				/* On Unibus #3: */
#define	UB_LHDH_BR	6	/* Interrupts occur on level 6 (non-std) */
#define	UB_LHDH_IVEC	0250	/* Input side interrupt vector (non-std) */
#define	UB_LHDH_OVEC	0254	/* Output side assumed to be LH_IVEC+4 */
#define UB_LHDH      0767600	/* Base of LH/DH Unibus reg address space */
#endif

/* LHDH Registers - 16-bit word offsets from base address */
#define	LHR_ICS	0	/* Input Control and Status */
#define	LHR_IDB	1	/* Input Data Buffer */
#define	LHR_ICA	2	/* Input Current Word Address */
#define	LHR_IWC	3	/* Input Word Count */
#define	LHR_OCS 4	/* Output Control and Status */
#define	LHR_ODB	5	/* Output Data Buffer */
#define	LHR_OCA	6	/* Output Current Word Address */
#define	LHR_OWC	7	/* Output Word Count */
#define LHR_N	8	/* # of registers */


/* Bits in CSRs */

/* Bits common to input and output */
#define	LH_ERR	(1<<15)	/* Error present */
#define	LH_NXM	(1<<14)	/* Non Existant Memory on DMA */
#define	LH_MRE	(1<<9)	/* Master Ready Error (ready bounce during xfr) */
#define	LH_RDY	(1<<7)	/* Device Ready (modifying LHDH regs allowed) */
#define	LH_IE	(1<<6)	/* Interrupt Enable */
#define	LH_A17	(1<<5)	/* Address bit 17 for extended unibus xfrs */
#define	LH_A16	(1<<4)	/* Address bit 16 for extended unibus xfrs */
#define	LH_RST	(1<<1)	/* Interface Reset */
#define	LH_GO	01	/* GO - Start DMA Transfer */

/* Input side */
#define	LH_EOM	(1<<13)	/* End-of-Message received from IMP */
#define	LH_HR	(1<<11)	/* Host Ready (ACC's relay closed, debounced) */
#define	LH_INR	(1<<10)	/* IMP not ready */
#define	LH_IBF	(1<<8)	/* Input Buffer Full */
#define	LH_SE	(1<<3)	/* Store Enable (0 == flush data instead) */
#define	LH_HRC	(1<<2)	/* Host Ready Relay Control (1 to close relay) */

/* Output side */
#define	LH_WC0	(1<<13)	/* Output Word Count is zero */
#define	LH_OBE	(1<<8)	/* Output Buffer Empty */
#define	LH_BB	(1<<3)	/* Bus Back (loopback enable for testing) */
#define	LH_ELB	(1<<2)	/* Send EOM indication to IMP at end of xfr */
			/* (enable Last Bit Flag) */

#endif	/* ifndef DVLHDH_INCLUDED */
