/* DVTM03.H - RH11/TM03 Controller definitions
*/
/* $Id: dvtm03.h,v 2.3 2001/11/10 21:28:59 klh Exp $
*/
/*  Copyright © 1993, 2001 Kenneth L. Harrenstien
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
 * $Log: dvtm03.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */
/*
**	Portions of this file were derived from AI:SYSTEM;TM03S DEFS4
*/

#ifndef DVTM03_INCLUDED
#define DVTM03_INCLUDED 1

#ifdef RCSID
 RCSID(dvtm03_h,"$Id: dvtm03.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

#include "dvrh20.h"	/* Temp: get RHR ext reg defs */

/* Initialization & sole entry point for TM02/3 driver
*/
#include "kn10dev.h"
extern struct device *dvtm03_create(FILE *f, char *s);


/* NOTE:
	Of the RH20 external registers, the following are the ones
recognized (or not) by the TM02/TM03:
*/
#if 0
	RHR_CSR		0	/* R/W      CS1 Control/command */
	RHR_STS		01	/* RO  [I2] STS Status */
	RHR_ER1		02	/* R/W	    ER1 Error 1 */
	RHR_MNT		03	/* R/W [-2] MNT	Maintenance */
	RHR_ATTN	04	/* R/W [I2] ATN? Attention Summary */
	RHR_BAFC	05	/* R/W [I2] ADR Block Address or Frame Count */
	RHR_DT		06	/* RO  [I2] TYP	Drive Type */
	RHR_LAH		07	/* RO	    LAH	Current BlkAdr or R/W ChkChr */
 --*--	RHR_ER2		010	/* R/W	    ER2	Error 2 */
	RHR_OFTC	011	/* R/W	    OFS	Offset or TapeControl */
 --NO--	RHR_DCY		012	/* R/W	    CYL	Desired Cylinder Addr */
 --NO--	RHR_CCY		013	/* RO  [-2] CCY	Current Cylinder Addr */
 --NO--	RHR_SN		014	/* RO  [-2] SER	Serial Number */
 --NO--	RHR_ER3		015	/* R/W	    ER3	Error 3 */
 --NO--	RHR_EPOS	016	/* RO	    POS	ECC Position */
 --NO--	RHR_EPAT	017	/* RO	    PAT	ECC Pattern */
#endif /* 0 */
/*

--*-- = The RH20 doc (p.2-20) says the TM02 uses reg 014 for SN, and 010 is
	unused.
    HOWEVER, the TM03 doc says reg 010 is SN, and 014 is unused; this also
agrees with the TOPS-20 monitor code.  So RHR_ER2 is used in the TM02
emulation to refer to the transport serial number, rather than RHR_SN.

	(The doc agrees on all other regs.)
*/

#if 0	/* ITS AI RH11/TM03 defs */

/* RH11/TM03 Interrupt vector: */
#define UB_TM03_BR	6
#define	UB_TM03_VEC	0224	/*(224/4 = 45) Interrupts occur on level 6 */
				/* (high priority) on UBA #1. */
#ifndef UB_TM03
# define UB_TM03 0772440	/* Unibus address of first register  */
#endif
#endif /* 0 */

/* KLH: There is a source/binary inconsistency here.
**	TM03S.DEFS3 defines the base address as 772440 and that is what
**		the last AI ITS (1643) uses.
**		This is also what the DEC T20 code expects.
**		So that's what I'm using.
**	but TM03S.DEFS4 defines it as 772400 for some reason???
**
** Also, all of those files are wrong about claiming it is on UBA #1;
** as far as I can tell it is always on UBA #3 (DEC T20 confirms this).
*/

/* RH11/TM03 Unibus register addresses: */
/*	Note: CS1 bits are shared by controller (RH) & drive (DR) */


/* CS1: (RH/DR R/W) CTRL AND STATUS 1. (RH11:0, RH20:0) */

# define	TM_1SC	(1<<15)		/* RH: Special Condition */
# define	TM_1TE	(1<<14)		/* RH: Transfer Error */
# define	TM_1MP	(1<<13)		/* RH: Massbus Control Bus Parity Error */
# define	TM_1DA	(1<<11)		/* DR: Drive Available */
# define	TM_1A7	(1<<9)		/* RH: UB Address Extension Bit 17 */
# define	TM_1A6	(1<<8)		/* RH: UB Address Extension Bit 16 */
# define	TM_1RY	(1<<7)		/* RH: Ready */
# define	TM_1IE	(1<<6)		/* RH: Interrupt Enable */
# define	TM_1CM	077		/* DR: Bits 0-5 specify commands. */
# define	TM_1GO	01		/* DR: GO bit */

/* Commands with bit 0 (GO) included: */

# define	TM_NOP	01		/* No Operation */
# define	TM_UNL	03		/* Unload */
# define	TM_REW	07		/* Rewind */
# define	TM_CLR	011		/* Formatter clear (reset errs etc) */
# define	TM_RIP	021		/* Read-In Preset (not used by T20) */
# define	TM_ER3	025		/* Erase three inch gap */
# define	TM_WTM	027		/* Write Tape Mark */
# define	TM_SPF	031		/* Space Forward */
# define	TM_SPR	033		/* Space Reverse */
# define	TM_WCF	051		/* Write Check FOrward */
# define	TM_WCR	057		/* Write Check Reverse */
# define	TM_WRT	061		/* Write Forward */
# define	TM_RDF	071		/* Read Forward */
# define	TM_RDR	077		/* Read Data Reverse */

/* WC:  (RH R/W) WORD COUNT.      (RH11:1, RH20:-) */
/* BA:  (RH R/W) UNIBUS ADDRESS.  (RH11:2, RH20:-) */
/* FC:  (DR R/W) TAPE FRAME COUNT (RH11:3, RH20:5) */

/* CS2: (RH R/W) CTRL AND STATUS 2. (RH11:4, RH20:-) */

# define	TM_2DL	(1<<15)		/* Data Late */
# define	TM_2UP	(1<<13)		/* Unibus Parity Error */
# define	TM_2NF	(1<<12)		/* Non-existant Formatter */
# define	TM_2NM	(1<<11)		/* %TMBA is NXM during DMA */
# define	TM_2PE	(1<<10)		/* Program Error */
# define	TM_2MT	(1<<9)		/* Missed Transfer */
# define	TM_2MP	(1<<8)		/* Massbus Data Bus Parity Error */
# define	TM_2OR	(1<<7)		/* Output Ready (for Silo buffer diag.) */
# define	TM_2IR	(1<<6)		/* Input  Ready (for Silo buffer diag.) */
# define	TM_2CC	(1<<5)		/* Controller Clear */
# define	TM_2PT	(1<<4)		/* Parity Test */
# define	TM_2AI	(1<<3)		/* Unibus Address Increment Inhibit */
# define	TM_2FS	07		/* Formatter select */

/* FS: (DR RO) FORMATTER STATUS. (RH11:5, RH20:1) "STS" for RP */
				/* SS=SelectedSlave, S=AnySlave, M=TM03 fmtr */
# define	TM_SATA	(1<<15)	/* 100000 (M) Attention Active */
# define	TM_SERR	(1<<14)	/* 040000 (M) Error Summary */
# define	TM_SPIP	(1<<13)	/* 020000 (M/SS) Positioning in Progress */
# define	TM_SMOL	(1<<12)	/* 010000 (SS) Medium On-Line */
# define	TM_SWRL	(1<<11)	/*  04000 (SS) Write Locked */
# define	TM_SEOT	(1<<10)	/*  02000 (SS) End of Tape */
		/*	(1<<9)*//*	unused */
# define	TM_SDPR	(1<<8)	/*   0400 (M) Formatter Present (hardwired) */
# define	TM_SDRY	(1<<7)	/*   0200 (M) Drive/Formatter Ready */
# define	TM_SSSC	(1<<6)	/*   0100 (S) Any-Slave Status Change */
# define	TM_SPES	(1<<5)	/*    040 (SS) Phase Encoded (1600BPI) Mode */
# define	TM_SDWN	(1<<4)	/*    020 (SS) Settling Down */
# define	TM_SIDB	(1<<3)	/*    010 (M) PE Ident Burst Detected */
# define	TM_STM	(1<<2)	/*     04 (M) Tape Mark detected */
# define	TM_SBOT	(1<<1)	/*     02 (SS) Beginning of Tape */
# define	TM_SSLA	(1<<0)	/*     01 (SS) Slave Attention (came on-line
				**	manually while selected;
				**	cleared by init & drive clr)
				*/
   
/* ERR: (DR RO) ERROR 1. (RH11:6, RH20:2) (ER1) */
				/* B=terminates in-progress data xfer, A=no */
# define	TM_ECDE	(1<<15)		/* (A) Correctable Data/CRC Error */
# define	TM_EUNS	(1<<14)		/* (B) Unsafe */
# define	TM_EOPI	(1<<13)		/* (B) Operation Incomplete */
# define	TM_EDTE	(1<<12)		/* (B) Controller Timing Error */
# define	TM_ENEF	(1<<11)		/* (B) Non Executable Function */
# define	TM_ECS	(1<<10)		/* (A) Correctable Skew/Illegal Tape Mark Error */
# define	TM_EFCE	(1<<9)		/* (A) Frame Count Error */
# define	TM_ENSG	(1<<8)		/* (A) Non-standard Gap */
# define	TM_EPEF	(1<<7)		/* (A) PE Format/LRC Error */
# define	TM_EINC	(1<<6)		/* (A) Incorrectable Data/Hard Error */
# define	TM_EMDP	(1<<5)		/* (A) Massbus Data Parity Error */
# define	TM_EFMT	(1<<4)		/* (B) Format Select Error */
# define	TM_EMCP	(1<<3)		/* (A) Massbus Control Parity Error */
# define	TM_ERMR	(1<<2)		/* (A) Register Modification Refused */
# define	TM_EILR	(1<<1)		/* (A) Illegal (non-ex) Register */
# define	TM_EILF	(1<<0)		/* (B) Illegal Function code */
#  define TM_EHD 044077		/* Hard errors - US,NEF,MD,FS,MC,RMR,ILR,ILF */

/* ASN: (DR* R/W) ATTENTION SUMMARY. (RH11:7, RH20:4) (ATN) */
	/*	Each bit 7-0 corresponds to a formatter asserting ATA. */
	/* Or is that "slave", because this is a drive register??? */
	/* Most likely formatter, akin to RP drives */

/* CCR: (DR RO) CHECK CHARACTER REGISTER (RO) (RH11:010, RH20:7) (LAH) */
# define	TM_CDP	(1<<8)		/* Dead Track Parity/CRC Parity */
# define	TM_CEI	0177		/* Error Information */

/* BUF: (DR R/W) DATA BUFFER. (RH11:011, RH20:-) */
/* MNT: (DR R/W) MAINTENANCE. (RH11:012, RH20:3) */

/* TYP: (DR RO) DRIVE TYPE.  (RH11:013, RH20:6) */

# define	TM_DTNS (1<<15)	/* 2.7  Not Sector addressed (always on) */
# define	TM_DTTA (1<<14)	/* 2.6  Tape (always on) */
# define	TM_DTMH (1<<13)	/* 2.5  Moving Head (always off) */
# define	TM_DT7C (1<<12)	/* 2.4  7-track (always off) */
# define	TM_DTDC (1<<11)	/* 2.3  Dual controller (always off) */
# define	TM_DTSS (1<<10)	/* 2.2  Slave Selected (Slave Present) */
		/* (1<<9) */	/* 2.1  Unused */
# define	TM_DTDT	0777	/* 1.9 - 1.1  Selected Slave Type Number. */
# define TM_DT00 010		/*	10= No slave			*/
# define TM_DT16 011		/*	11= TE16   45 in/sec	*/
# define TM_DT45 012		/*	12= TU45   75 in/sec	*/
# define TM_DT77 014		/*	14= TU77  125 in/sec	*/

# define TM_DTTM03 050		/* Bit set if TM03, else TM02 */
# define TM_DTTM02 010		/*	lowest & highest TM03 types are 050-057
				**	   "   &    "    TM02   "    "  010-017
				*/

/* SER: (DR RO) SERIAL NUMBER. (RH11:014, RH20:010) */


/* TC:  (DR R/W) TAPE CONTROL REGISTER (RH11:015, RH20:011) (OFTC) */
				/* Selects & configures a slave transport */
# define	TM_TAC	(1<<15)	/* (RO) Acceleration (not up to speed) */
# define	TM_TFCS	(1<<14)	/* Frame Count Status */
# define	TM_TSA	(1<<13)	/* Slave Address (selected slave) Changed */
# define	TM_TEA	(1<<12)	/* Enable Abort on data transfer error */
				/*	(ie ERR bits 15,7,6,5) */
# define	TM_TDS	(07<<8)	/* Density Select Field */
# define	 TM_D02	00	/*   200 BPI (TM02 only) */
# define	 TM_D05	01	/*   556 BPI (TM02 only) */
# define	 TM_D08	02	/*   800 BPI NRZI (ITS thinks this is 3?) */
# define	 TM_D16	04	/*   1600 BPI PE */
# define	TM_TFS (017<<4)	/* Format Select */
# define	 TM_FCD	00	/*   PDP10 Core Dump */
# define	 TM_FAA	02	/*   ANSI-ASCII (TM02 only) */
# define	 TM_FIC	03	/*   Industry Compatible (32 bit mode) */
# define	TM_TEP	(1<<3)	/* Even Parity (for 800 NRZI only) */
# define	TM_TTS	07	/* Transport (Slave) Select */

#endif	/* ifndef DVTM03_INCLUDED */
