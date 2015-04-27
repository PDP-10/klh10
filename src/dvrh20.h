/* DVRH20.H - RH20 Massbus Controller definitions (generic)
*/
/* $Id: dvrh20.h,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: dvrh20.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#ifndef RH20_INCLUDED
#define RH20_INCLUDED 1

#ifdef RCSID
 RCSID(dvrh20_h,"$Id: dvrh20.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

#ifndef RH20_NSUP
# define RH20_NSUP 8
#endif

extern struct device *dvrh20_create(FILE *, char *);

/* RH20 Massbus Data Channel definitions
*/

/* Channel Status/Logout area in the EPT
*/
#define RH20_EPT_CCL(n) (0+(n<<2))	/* Channel Command List */
#define RH20_EPT_CST(n) (1+(n<<2))	/* Channel Status & Pointer */
#define RH20_EPT_CCW(n) (2+(n<<2))	/* Current Channel Cmd Word */
#define RH20_EPT_CIV(n) (3+(n<<2))	/* Unused by hardware; software
					** uses for PI vectoring
					*/
/* Format of Channel Command Word
*/
#define CCW_OP	0700000	/* LH: Channel Command Opcode */
# define CCW_HLT	0000000	/* LH: HALT opcode */
# define CCW_JMP	0200000	/* LH: JUMP opcode */
# define CCW_XFR	0400000	/* LH: XFER opcode, plus next 2 bits: */
# define CCW_XHLT	0200000	/* LH:	Xfer cmd: "Halt after xfer" */
# define CCW_XREV	0100000	/* LH:	Xfer cmd: "Reverse xfer" */
#define CCW_CNT	 077760	/* LH: Word Count (positive) */
#define CCW_ADR     017	/* LH: High bits of 22-bit address */
			/* B18-35 Low bits of ditto */

/* Channel Status Word
*/
#define CSW_SET		0400000	/* B0 - Always set 1 */
#define CSW_MPAR	0200000	/* B1 - Mem Par Err during CCW fetch */
#define CSW_NOAPE	0100000	/* B2 - Set if NO mem addr par err */
#define CSW_NOWC0	 040000	/* B3 - CCW count NOT 0 when CSW set */
#define CSW_NXM		 020000	/* B4 - Chan referenced NXM */
#define CSW_LXFE	   0400	/* B9 - Last Transfer Error */
#define CSW_RH20E	   0200	/* B10 - RH20 tried to start not-ready chn */
#define CSW_LWCE	   0100	/* B11 - Long Word Count Error */
#define CSW_SWCE	    040	/* B12 - Short Word Count Error */
#define CSW_OVN		    020	/* B13 - Overrun */
#define CSW_HIADR	    017	/* B14-17 High bits of 22-bit CCW addr+1 */
				/* B18-35 Low bits of ditto */
#define CSW_CLRSET (CSW_SET|CSW_NOAPE)	/* Set CSW to this when clearing */


/* RH20 Device Number assignment
*/
#define DEVRH20(n) (0540+((n)<<2))


/* RH20 CONO bits.
*/
#define RH20CO_CRAE	04000	/* B24 - Clears RAE and associated PI req */
#define RH20CO_CMC	02000	/* B25 - Clear Massbus Controller.  Resets
				** RH20 and all drives to power-up state.
				** Will hang channel if xfer in progress,
				** unless RCLP also set.
				*/
#define RH20CO_TEC	01000	/* B26 - Clear all xfer error indicators */
#define RH20CO_MBE	 0400	/* B27 - MassBus Enable */
#define RH20CO_RCLP	 0200	/* B28 - Reset Cmd List Ptr (inits channel) */
#define RH20CO_DSCRF	 0100	/* B29 - Clears cmd file xfer if hung
				** (ie if previous data xfer caused xfer
				** error, CONI bits 18-23, 26).
				*/
#define RH20CO_MEAE	  040	/* B30 - Allow Massbus ATTN to generate PI */
#define RH20CO_ST	  020	/* B31 - Stop Transfer. (nothing cleared) */
#define RH20CO_CCMD	  010	/* B32 - Clear Cmd Done, clears CONI bit 32
				**	and associated PI request.
				*/
#define RH20CO_PIA	   07	/* B33-35 - PI channel assignment (0 = none) */


/* RH20 CONI bits.
*/
#define RH20CI_DPE	0400000	/* B18 - Data Parity Error */
#define RH20CI_DEE	0200000	/* B19 - Drive Exception (xfer error) */
#define RH20CI_LWCE	0100000	/* B20 - Long Word Count Error */
#define RH20CI_SWCE	 040000	/* B21 - Short Word Count Error */
#define RH20CI_CE	 020000	/* B22 - Channel Error */
#define RH20CI_DR	 010000	/* B23 - Drive Response Error (ie no-ex) */
#define RH20CI_RAE	  04000	/* B24 - External Register Address error */
#define RH20CI_CNR	  02000	/* B25 - Channel Ready */
#define RH20CI_DOE	  01000	/* B26 - Data Overrun */
#define RH20CI_MBE	   0400	/* B27 - MassBus Enable */
#define RH20CI_MBA	   0200	/* B28 - MassBus ATTN */
#define RH20CI_SCRF	   0100	/* B29 - Secondary regs loaded */
#define RH20CI_MBAE	    040	/* B30 - Enable PI on RH20CI_MBA */
#define RH20CI_PCRF	    020	/* B31 - Primary regs loaded, xfer in progrs */
#define RH20CI_CMD	    010	/* B32 - Command Done */
#define RH20CI_PIA	     07	/* B33-35 - PIA set by previous CONO */


/* RH20 DATAO/DATAI word format
**	If the LR bit of the word is clear, the prep reg is written.
**	Otherwise, the RS field selects the register to write, but
**	the RS, DRAES, and DS fields are always copied to the prep reg.
** Normally the read and write formats are the same, except for
**	a few bits in the word returned from external regs.
*/

/* Bits used for writes to prep reg and external regs */
#define RH20DO_RS	0770000	/* LH: Register Select */
#define RH20DO_LR	  04000	/* LH: Load Register (0 = prep reg) */
#define RH20DO_DRAES	   0400	/* LH: DRAE Suppress */
#define RH20DO_DS	     07	/* LH: Drive Select */
#define RH20DO_CBEP	0400000	/* RH: Cause even parity error (diag only) */

/* Additional external reg bits used for DATAI */
#define RH20DI_CBPE	  01000	/* LH: Control Bus Par Err during DATAI */
#define RH20DI_TRA	   0200	/* LH: Transfer Received */
#define RH20DI_CPA	0200000	/* RH: State of parity bit on massbus */
#define RH20DI_ERD	0177777	/* RH: External Register Data (16 bits) */

/* Bits used for internal BAR */
#define RH20DO_ADR	0177777	/* RH: Disk: BlkAdr, Tape: FrameCnt */

/* Bits used for internal TCR */
#define RH20DO_RCLP	  02000	/* LH: Reset Channel when xfer started */
#define RH20DO_SES	   0200	/* LH: Store Chan Status at end of xfer */
#define RH20DO_DTES	0200000	/* RH: Prevent drive errs from stopping xfer */
#define RH20DO_NBC	0177700	/* RH: Negative block count (sectors/recs) */
#define RH20DO_MFC	    077	/* RH: Drive command code to use for xfer */

/* Bits used for internal IVIR */
# define RH20DO_IVR	   0777	/* RH: EPT offset for PI function word */


/* RH20 register definitions:

	The RH20 has provision for 64 (0100) registers.  Only the
high 8 of these (070-077) are in the RH20 itself (internal); the others are
registers of the currently selected drive (external).
*/

/* RH20 Internal Registers.
**    Note:
**	There is also a "preparation register" that is set by all DATAOs
**	and used to set up the source for a DATAI, although the register
**	itself is not directly addressed.
*/
#define RH20_1ST	070	/* First internal register */

#define RH20_SBAR	070	/* R/W Secondary Block Address Reg */
#define RH20_STCR	071	/* R/W Secondary Transfer Control Reg */
#define RH20_PBAR	072	/* RO Primary Block Address Reg */
#define RH20_PTCR	073	/* RO Primary Transfer Control Reg */
#define RH20_IVIR	074	/* R/W Interrupt Vector Index Reg */
#define RH20_RR		075	/* RO Read Reg (Diagnostic only) */
#define RH20_WR		076	/* WO Write Reg (Diagnostic only) */
#define RH20_DCR	077	/* WO Diagnostic Control Reg (Diags only) */


/* RH20 External Registers.
**	These are not necessarily defined or used by the respective drives,
**	but do provide a standard framework.
**	Those commented as [I2] are external regs that T20 claims are
**	device independent.  [-2] are apparently unused by KL T20.
**
** RH11 regs not in drive: CS1 (partial), CS2, WC, BA, ATN?
** RH11 drive regs unknown mapping: BUF
*/
				/*	   RH11 RP07	*/
#define RHR_CSR		0	/* R/W      CS1 CS1 Control/command */
#define RHR_STS		01	/* RO  [I2] STS DS  Status */
#define RHR_ER1		02	/* R/W	    ER1 ER1 Error 1 */
#define RHR_MNT		03	/* R/W [-2] MNT	MR1 Maintenance */
#define RHR_ATTN	04	/* R/W [I2] ATN AS  Attention Summary */
#define RHR_BAFC	05	/* R/W [I2] ADR DA  Block Addr or Frame Cnt */
#define RHR_DT		06	/* RO  [I2] TYP	DT  Drive Type */
#define RHR_LAH		07	/* RO	    LAH	LA  Cur BlkAdr or R/W ChkChr */
#define RHR_SN		010	/* RO  [-2] SER	SN  [*] Serial Number */
#define RHR_OFTC	011	/* R/W	    OFS	OF  Offset or TapeControl */
#define RHR_DCY		012	/* R/W	    CYL	DC  Desired Cylinder Addr */
#define RHR_CCY		013	/* RO  [-2] CCY	CC  Current Cylinder Addr */
#define RHR_ER2		014	/* R/W	    ER2	ER2 [*] Error 2 */
#define RHR_ER3		015	/* R/W	    ER3	ER3 Error 3 */
#define RHR_EPOS	016	/* RO	    POS	EC2? ECC Position */
#define RHR_EPAT	017	/* RO	    PAT	EC2? ECC Pattern */

#define RHR_N		020	/* # of RH20 external registers */

/* [*] NOTE!!
**	There is a discrepancy in the RH20 doc, p. RH/2-20, External
** Register Summary.  Register 10 is shown as ER2 and reg 14 as SN,
** which disagrees with virtually everything else I've seen, which
** have reg 10=SN and reg 14=ER2 !!!  The following refs all agree
** on this latter interpretation:
**
**	T20 RP04 defs in PHYP4.MAC.
**	T10 RH20 defs in DEVPRM.MAC.
**	RP07 Tech Manual.
**	TM03 Tech Manual and T20 defs.
** 
** Hence, I've gone with the latter in my above definitions.
**
** NOTE!
**	Not clear what is actually returned in ATTN.  RH20 doc says all
** drives respond when RS=ATTN, so resulting value is somehow a summary
** of the ATTN bit for all drives.  If pattern is same as that for RH11
** then unit 0 is rightmost bit.  From T20 code this appears to be true.
*/

#endif /* ifndef RH20_INCLUDED */
