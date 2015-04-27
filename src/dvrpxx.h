/* DVRPXX.H - RP/RM Disk Drive definitions under RH20 for KL
*/
/* $Id: dvrpxx.h,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: dvrpxx.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */
/*
**	Portions of this file were derived from AI:SYSTEM;RH11 DEFS48
*/

#ifndef DVRPXX_INCLUDED
#define DVRPXX_INCLUDED 1

#ifdef RCSID
 RCSID(dvrpxx_h,"$Id: dvrpxx.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* Initialization & sole entry point for RPXX driver
*/
#include "kn10dev.h"
extern struct device *dvrp_create(FILE *f, char *s);


/* Historical disk physical parameter definitions.

					  Cyl/Unit
        Type    wd/sec  sec/trk trk/cyl T20	ITS	MaxSecs
        RP04/5  128     20      19      400	-	152,000
        RP06    128     20      19      800	812+3	309,700
        RP07    128     43      32      629	627+3	866,880
        RM03    128     30      5       820	820+3	123,450
        RM05    128     30      19      820	-	467,400
	RM80	128	30	14	-	556+3	234,780
Old RP:
	RP02	128	10	20	-	200+3	 40,600
	RP03	128	10	20	-	400+3	 80,600
Old RH10:
	RP04	128	20	19	-	406+5	156,180

Note that sectors of a disk formatted for the PDP-10 have 576 bytes/sector
instead of the (nowadays) more common 512 byte/sector format.  The actual
size of an emulated sector on a real disk will depend on the emulation format
selected.

For the basic case of 8 bytes per 36-bit word, one 128-word emulated
sector will occupy 1024 bytes of real disk space (2 real sectors).
Unless using a raw disk interface, an underlying OS using a 1K or larger
block size should be able to preserve atomicity and handle any bad blocks.

*/


/* CS1: (RH/DR) CTRL AND STATUS 1. (RH11:0, RH20:0) */

#  define	RH_XSC	(1<<15)	/* Special Condition */
#  define	RH_XTRE	(1<<14)	/* Transfer Error */
#  define	RH_XMCP	(1<<13)	/* Mass I/O Control Bus Parity Error */
#  define	RH_XDVA	(1<<11)	/* Drive Available */
#  define	RH_XPSE	(1<<10)	/* Port Select */
#  define	RH_XA17	(1<<9)	/* UB Address Extension Bit 17 */
#  define	RH_XA16	(1<<8)	/* UB Address Extension Bit 16 */
#  define	RH_XRDY	(1<<7)	/* Ready (for next command) */
#  define	RH_XIE	(1<<6)	/* Interrupt Enable */
#  define	RH_XCMD	077	/* Bits 1-5 specify commands. */
#  define	RH_XGO	(1<<0)	/* GO bit */


/* Commands with bit 0 (GO) included: */
/*	"2" Marks those that T20 knows about; "I" for ITS */

#define	RH_MNOP	01	/* I  No Operation */
#define	RH_MUNL	03	/* I2 Unload ("Standby"- the pack doesn't fly off). */
			/*	RP07: doesn't have this */
#define	RH_MSEK	05	/* I2 Seek to Cylinder */
#define	RH_MREC	07	/* I2 Recalibrate */
#define	RH_MCLR	011	/* I2 Drive clear (reset errors etc.) */
#define	RH_MRLS	013	/* I2 Drive release (dual port) */
#define	RH_MOFS	015	/*  2 Offset Heads Slightly */
				/* Just sets RH_SOFS on RP07 */
#define	RH_MCEN	017	/*  2 Return Heads To Centerline */
				/* Just clears RH_SOFS on RP07 */
#define	RH_MRDP	021	/*  2 Read-In Preset */
#define	RH_MACK	023	/* Acknowledge mounting of pack (reqrd before I/O) */
			/*	RP07: No-op */
	/* 25, 27 unused */
#define	RH_MSRC	031	/* I2 Search (for r.p.s.) */
	/* 33 - unused */
	/* 35 - RP07: Microdiagnostic Command */ 
	/* 37-47 unused */
#define	RH_MWCH	051	/* I  Write Check Data (?doesn't work) */
#define	RH_MWCF	053	/* I  Write Check Header & Data (?doesn't work) */
#define	RH_MWRT	061	/* I2 Write Data */
#define	RH_MWHD	063	/* I2 Write Header And Data (RP07: format track) */
#define	RH_MWTD	065	/* I  Write Track Descriptor (RP07 only) */
	/* 67 unused */
#define	RH_MRED	071	/* I2 Read Data */
#define	RH_MRHD	073	/* I2 Read Header and Data */
#define	RH_MRTD	075	/* I  Read Track Descriptor (RP07 only) */


/* WC:  (RH) WORD COUNT.     (RH11:1, RH20:-) */
/* BA:  (RH) UNIBUS ADDRESS. (RH11:2, RH20:-) */

/* ADR: (DR) DESIRED ADDRESS. (RH11:3, RH20:5) */
#  define	RH_ATRK	037400	/* Track */
#  define	RH_ASEC	000177	/* Sector */
/* These are the maximum fields; actual drives use smaller fields but
   all are in the same position, and virtually all monitor code manages them
   as ((Track<<8)+Sector).  The RP07 (for example) uses a 6-bit track
   and 7-bit sector, which in reality are 5 bits track and 6 bits sector
   as the high bit is only used to help detect overruns or bad addresses.
*/
#  define	RH_ATRKGET(a) ((((unsigned)(a))>>8) & (((unsigned)RH_ATRK)>>8))
#  define	RH_ASECGET(a) ((a)&RH_ASEC)
#  define	RH_ADRSET(t,s) ((((t)&0377)<<8) | ((s)&0377))

/* CS2: (RH) CTRL AND STATUS 2. (RH11:4, RH20:-) */

#  define	RH_YDLT	(1<<15)	/* Data Late */
#  define	RH_YWCE	(1<<14)	/* Write Check Error */
#  define	RH_YPE	(1<<13)	/* Parity Error */
#  define	RH_YNED	(1<<12)	/* Non-existant Drive */
#  define	RH_YNEM	(1<<11)	/* BA reg is NXM during DMA */
#  define	RH_YPGE	(1<<10)	/* Program Error */
#  define	RH_YMXF	(1<<9)	/* Missed Transfer */
#  define	RH_YMDP	(1<<8)	/* Mass Data Bus Parity Error */
#  define	RH_YOR	(1<<7)	/* Output Ready (for Silo buffer diag.) */
#  define	RH_YIR	(1<<6)	/* Input  Ready (for Silo buffer diag.) */
#  define	RH_YCLR	(1<<5)	/* Controller Clear */
#  define	RH_YPAT	(1<<4)	/* Parity Test */
#  define	RH_YBAI	(1<<3)	/* Unibus Address Increment Inhibit */
#  define	RH_YDSK	07	/* Bits 2-0 are the Unit Select. */

/* STS: (DR) DRIVE STATUS. (RH11:5, RH20:1) */

#  define	RH_SATN	(1<<15)	/* Attention Active */
#  define	RH_SERR	(1<<14)	/* Error */
#  define	RH_SPIP	(1<<13)	/* Positioning In Progress */
#  define	RH_SMOL	(1<<12)	/* Medium On-Line */
#  define	RH_SWRL	(1<<11)	/* Write Locked */
#  define	RH_SLBT	(1<<10)	/* Last Block (sector) Transferred */
#  define	RH_SPGM	(1<<9)	/* Programmable (dual port) */
#  define	RH_SDPR	(1<<8)	/* Drive Present */
#  define	RH_SDRY	(1<<7)	/* Drive Ready */
#  define	RH_SVV	(1<<6)	/* Volume Valid */
	/* All the above bits are used by T20.
	** MOL,DPR,RDY, and VV must all be present for the drive to be
	** considered "good".
	*/
#if 0	/* RP04-only bits in STS */
#  define	RH_SDE1	(1<<5)	/* Difference Equals 1 */
#  define	RH_SL64	(1<<4)	/* Difference Less Than 64 */
#  define	RH_SGRV	(1<<3)	/* Go Reverse */
#  define	RH_SDIG	(1<<2)	/* Drive To Inner Guard Band */
#  define	RH_SF20	(1<<1)	/* Drive Forward 20in/sec */
#  define	RH_SF5	(1<<0)	/* Drive Forward 5in/sec */
#endif /* RP04 */
#if 0	/* RP07-only bits in STS */
#  define	RH_SILV	(1<<2)	/* Interleaved Sectors */
#  define	RH_SEWN	(1<<1)	/* Early Warning */
#  define	RH_SOM	(1<<0)	/* Offset Mode */
#endif /* RP07 */

/* ER1: (DR) ERROR 1. (RH11:6, RH20:2) */

#  define	RH_1DCK	(1<<15)	/* Drive Data Check */
#  define	RH_1UNS	(1<<14)	/* Drive Unsafe */
#  define	RH_1OPI	(1<<13)	/* Operation Incomplete */
#  define	RH_1DTE	(1<<12)	/* Drive Timing Error */
#  define	RH_1WLK	(1<<11)	/* Write Lock Error */
#  define	RH_1IAE	(1<<10)	/* Invalid Address Error */
#  define	RH_1AOE	(1<<9)	/* Address Overflow Error */
#  define	RH_1CRC	(1<<8)	/* Header CRC Error */
#  define	RH_1HCE	(1<<7)	/* Header Compare Error */
#  define	RH_1ECH	(1<<6)	/* ECC Hard Error */
#  define	RH_1WCF	(1<<5)	/* Write Clock Fail */
#  define	RH_1FER	(1<<4)	/* Format Error */
#  define	RH_1PAR	(1<<3)	/* Control Bus Parity Error */
#  define	RH_1RMR	(1<<2)	/* Register Modification Refused */
#  define	RH_1ILR	(1<<1)	/* Illegal Register */
#  define	RH_1ILF	(1<<0)	/* Illegal Function */
/*	RH_1AOE is set if drive attempts to spiral-read past end of disk.
**	DCK,FER,CRC,HCE inspire T20 to attempt head offsetting.
*/

/* ATN: (DR*) ATTENTION SUMMARY. (RH11:7, RH20:4) */
	/* Each bit 7-0 corresponds to a drive asserting ATA. */
	/* Bit 1.1 is Drive #0, 1.2 is drive #1, etc */

/* LAH: (DR) LOOK AHEAD. (RH11:010, RH20: 7) */
				/* 2.2 - 1.7  Sector Count. */
				/* 1.6 - 1.5  Encoded Extension Field. */

/* BUF: (DR) DATA BUFFER. (RH11:011, RH20:-) */
/* MNT: (DR) MAINTENANCE. (RH11:012, RH20:3) */

/* TYP: (DR) DRIVE TYPE.  (RH11:013, RH20:6) */

# define RH_DTNBA (1<<15)	/* 2.7  Not block (sector) addressed */
# define RH_DTTAP (1<<14)	/* 2.6  Tape */
# define RH_DTMH  (1<<13)	/* 2.5  Moving Head (better be a 1!!) */
				/* 2.4  unused */
# define RH_DTRQ  (1<<11)	/* 2.3  Drive Request Required (dual port) */
				/* 2.2  unused */
# define RH_DTDYNGEOM (1<<9)	/* 2.1  Dynamic Geometry - KLH10 ONLY!! [*1] */
# define RH_DTTYP   0777	/* 1.9 - 1.1  Drive Type Number: */
# define RH_DTRP04	020	/* RP04 */
# define RH_DTRP05	021	/* RP05 */
# define RH_DTRP06	022	/* RP06 */
# define RH_DTRM03	024	/* RM03 */
# define RH_DTRM02	025	/* RM02 (slow RM03) */
# define RH_DTRM80	026	/* RM80 */
# define RH_DTRM05	027	/* RM05 */
# define RH_DTRP07	042	/* RP07 ("Fixed" - 041 is "Moving") */
			/* Types 020-042 inclusive are "RP04" types */
# define RH_DTDXB	061	/* DX20B/RP20 */
/* [*1] !!!NOTE!!!
   Bit 01000 is a special KLH10 addition that does not exist in any real
   hardware.  It is used to support larger drives without having
   to invent scores of new drive types.
   If set, it indicates that the drive is "dynamically sized"
   and certain drive registers can be read to obtain the actual desired disk
   geometry:
	ECC POS: re-used to hold Maximum Cylinder Value.  This is one less than
		the number of cylinders for the drive.
	ECC PAT: re-used to hold Maximum Track and Sector values, each of which
		are likewise one less than their respective numbers.
    For example, for a drive geometry of 32K cylinders, 32 tracks, 64 sectors,
		the values would be 32K-1, 31, and 63.
    Maximum possible values for each field are:
		Cylinder: 16 bits = 0177777
		Track:     8 bits = 0377  (but note most monitors assume 5 bits)
		Sector:    8 bits = 0377
 */

/* SER: (DR) SERIAL NUMBER. (RH11:014, RH20:010) */

/* OFS: (DR) OFFSET. (RH11:015, RH20:011) */
/* From ITS:
			; 2.9-2.8 Unused
			; 2.7  Sign Change (RP06 only)
			; 2.7  Command Modifier (RP07 only)
			;  Must be set before RH_MWHD, RH_MWTD or RH_MRTD
			; 2.6  Move Track Descriptor (RP07 only)
			;  0 = 128. bit track descriptor
			;  1 = 344. bit track descriptor
	; T20 uses?	; 2.4  Format Bit (1=16, 0=18)
	; T20 uses?	; 2.3  ECC Inhibit
			; 2.2  Header Compare Inhibit
			; 2.1  Skip Sector Inhibit (RM 16bit only)
			; 1.9  Unused
			; 
			; 1.8 - 1.1  Unused on RP07 
			;  RP07 doesn't support offsets
			; 
			; RP06 Offsets
			; 1.8 - 1.1  Offset Info
			;   +400  u"   00010000
			;   -400  u"   10010000
			;   +800  u"   00100000
			;   -800  u"   10100000
			;   +1200 u"   00110000
			;   -1200 u"   10110000
			;   Centerline 00000000
			;
			; RMxx Offsets
			; 1.1-1.7  Unused
			; 1.8  Offset Direction
			;   0 - Away from spindle
			;   1 - Towards spindle
*/

/* CYL: (DR) DESIRED CYLINDER. (RH11:016, RH20:012) */

#if 0 /* RP06-specific drive regs and defs */
 /* CCY: (DR) CURRENT CYL. (RH11:017, RH20:013) */
 /* ER2: (DR) ERROR 2.     (RH11:020, RH20:014) */
#  define	RH_2NHS	(1<<10)	/* No Head Selection */
#  define	RH_2WRU	(1<<8)	/* Write Ready Unsafe */
 /* ER3: (DR) ERROR 3.     (RH11:021, RH20:015) */
#  define	RH_3OFC	(1<<15)	/* Off Cylinder */
#  define	RH_3SKI	(1<<14)	/* SeekIncomplete (also sets UNS+ATA+PIP+RDY)*/
#  define	RH_3DCL	(1<<6)	/* DC power low (or perhaps AC?) */
#  define	RH_3ACL	(1<<5)	/* AC power low (or perhaps DC?) */
				/* (the documentation is confused about */
				/* which is which.) */
	/* T20 checks OFC,SKI */
#endif /* RP06P */

#if 0 /* RP07-specific drive regs and defs */
 /* CCY: (DR) CURRENT CYL. (RH11:017, RH20:013) */
 /* ER2: (DR) ERROR 2.     (RH11:020, RH20:014) */
#  define	RH_2PRG	(1<<15)	/* Program Error */
#  define	RH_2CRM	(1<<14)	/* Control ROM parity error */
#  define	RH_2H88	(1<<13)	/* 8080 in drive is hung */
/* DEC calls the following three bits READ/WRITE UNSAFE 1, 2 and 3. */
#  define	RH_2WU3	(1<<12)	/* Write current when no write in progress */
#  define	RH_2WU2	(1<<11)	/* More than one head selected */
#  define	RH_2WU1	(1<<10)	/* No write transitions during a write */
#  define	RH_2WOV	(1<<9)	/* Write Overrun */
#  define	RH_2WRU	(1<<8)	/* Write Ready Unsafe */
#  define	RH_2COD	0377	/* Error Code */
/* Error codes are:
	012 Seek operation too long.
	013 Guard band detected during seek operation.
	014 Seek operation overshoot.
	104 Guard band detection failure during recalibrate operation.
	105 Reference gap or guard band pattern detection failure during
	    recalibrate operation.
	106 Seek error during recalibrate operation.
	112 Heads have attempted to land on guard band during recalibrate
	    operation.
*/
 /* ER3: (DR) ERROR 3.     (RH11:021, RH20:015) */
#  define	RH_3BDS	(1<<15)	/* Bad Sector */
#  define	RH_3SKI	(1<<14)	/* Seek Incomplete (see error code in ER2) */
#  define	RH_3DSE	(1<<13)	/* Defect Skip Error */
#  define	RH_3WCF	(1<<12)	/* Write Current Failure */
#  define	RH_3LCF	(1<<11)	/* Logic Control Failure */
#  define	RH_3LBC	(1<<10)	/* Loss of Bit Clock */
#  define	RH_3LCE	(1<<9)	/* Loss of Cylinder Error */
#  define	RH_3X88	(1<<8)	/* 8080 in drive failed to respond to a command */
#  define	RH_3DCK	(1<<7)	/* Device Check */
#  define	RH_3WHD	(1<<6)	/* Index Unsafe (Bad RH_MWHD) */
#  define	RH_3DCL	(1<<5)	/* DC Low voltage */
#  define	RH_3SDF	(1<<4)	/* Serdes Data (data buffer timing) Failure */
#  define	RH_3PAR	(1<<3)	/* Data Parity Error during write operation */
#  define	RH_3SYB	(1<<2)	/* Sync Byte error */
#  define	RH_3SYC	(1<<1)	/* Sync Clock failure */
#  define	RH_3RTM	(1<<0)	/* Run timeout */
#endif /* RP07P */

/*  RM has no Current Cylinder Register, there is no Error 2 Register
    Some of the Error 3 bits differ from those for the RP07.
    Q: Should the two unused regs even be defined?  Should they cause
    IO page failure errors if referenced?
*/
#if 0 /* RM03|RM80 drive regs and defs */
 /* CCY: (DR) CURRENT CYL. (RH11:017, RH20:013) (not used) */
 /* ER2: (DR) ERROR 2.     (RH11:020, RH20:014) (not used) */
 /* ER3: (DR) ERROR 3.     (RH11:021, RH20:015) */
#  define	RH_3BSE	(1<<15)	/* Bad Sector (sector marked bad on disk) */
#  define	RH_3SKI	(1<<14)	/* SeekIncomplete (also sets UNS+ATA+PIP+RDY)*/
#  define	RH_3OPE	(1<<13)	/* Drive address plug was removed */
#  define	RH_3IVC	(1<<12)	/* Invalid Command (really drive not valid) */
#  define	RH_3LSC	(1<<11)	/* LSC Sucks */
#  define	RH_3LBC	(1<<10)	/* Loss of Bitcheck (hardware lossage) */
#  define	RH_3DVC	(1<<7)	/* Device Check (generic hardware lossage) */
#  define	RH_3SSE	(1<<5)	/* Skip Sector found (can't happen in 18bit) */
#  define	RH_3DPE	(1<<3)	/* Data Parity Error in controller */
#endif /* RM03P|RM80P */

/* POS: (DR) ECC POSITION. (RH11:022, Rh20:016) */
/* PAT: (DR) ECC PATTERN.  (RH11:023, Rh20:017) */

#endif /* ifndef DVRPXX_INCLUDED */
