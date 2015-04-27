/* DVDTE.H - DTE20 PDP-10/11 Interface definitions
*/
/* $Id: dvdte.h,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: dvdte.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/* References:
	[PRM] 6/82 PRM p.3-62
	[DTE] EK-DTE20-UD-004 "DTE20 Ten-Eleven Interface Unit Description"
		3rd edition Oct 1976
	[T20] APRSRV.MAC, PROLOG.MAC
*/

#ifndef DVDTE_INCLUDED
#define DVDTE_INCLUDED 1

#ifdef RCSID
 RCSID(dvdte_h,"$Id: dvdte.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* Only externally visible entry point for DTE driver
 */
#include "kn10dev.h"
extern struct device * dvdte_create(FILE *f, char *s);

/* Plus hackish entry for DVCTY use */
extern int dte_ctysin(int cnt);


#define DTE_NMAX 4		/* Max number of DTEs possible */
#define DTE_NSUP 1		/* Number supported for now */

/* DTE device #s */

#define DTE_DEV(n) (0200+((n)<<2))	/* 200, 204, 210, 214 */

/* DTE Control block offsets in EPT */

#define DTE_CB(n) (0140+(8*(n)))	/* EPT offset for DTE block N */

#define DTE_CBOBP 0	/* Output byte pointer (to 11) */
#define DTE_CBIBP 1	/* Input byte pointer (from 11) */
#define DTE_CBINT 2	/* Vector interrupt instruction */
	/*	  3	Unused */
#define DTE_CBEPW 4	/* Examine Protect Word    (size of 11-exam area) */
#define DTE_CBERW 5	/* Examine Relocation      (addr of 11-exam area) */
#define DTE_CBDPW 6	/* Deposit Protect Word    (size of 11-depo area) */
#define DTE_CBDRW 7	/* Deposit Relocation Word (addr of 11-depo area) */

/* NOTE: All refs via the BPs are to executive virtual memory, sec 0;
**	this is how the ucode is written.
*/


/* DTE20 DATAO defs [DTE p.1-25] */
				/* 0-22 MBZ, reserved by DEC */
#define DTE_TO10IB 010000	/* 23    To-10 "I" bit */
#define DTE_TO10BC  07777	/* 24-35 To-10 Byte Count (negative) */
	/*	If TO10IB is set, DTE interrupts both 10 and 11
	**	when receive xfer done, else just 10.
	*/

/* DTE20 CONI defs [DTE p.1-26] */

#define DTE_CIRM	0100000	/* 20 Restricted Mode */
#define DTE_CID11	040000	/* 21 Dead-11 (Unibus AC power low) */
#define DTE_CI11DB	020000	/* 22 TO11DB Doorbell request active */
				/* 23-25 zero */
#define DTE_CI10DB	01000	/* 26 TO10DB Doorbell request active */
#define DTE_CI11ER	0400	/* 27 Error during TO11 transfer */
				/* 28 - */
#define DTE_CI11DN	0100	/* 29 TO11 transfer done */
#define DTE_CI10DN	040	/* 30 TO10 transfer done */
#define DTE_CI10ER	020	/* 31 Error during TO10 transfer */
#define DTE_CIPI0	010	/* 32 DTE PI 0 enabled */
#define DTE_CIPI	07	/* 33-35 PI channel assignment */

/* DTE20 CONO defs [DTE p.1-27] */

#define DTE_CO11DB	020000	/* 22 TO11DB Doorbell request */
#define DTE_COCR11	010000	/* 23 Clear reload-11 button in DTE */
#define DTE_COSR11	04000	/* 24 Set reload-11 button in DTE */
				/* 25 MBZ */
#define DTE_CO10DB	01000	/* 26 Clear TO10DB Doorbell */
				/* 27-28 MBZ */
#define DTE_COCL11	0100	/* 29 Clear TO11DN and TO11ER */
#define DTE_COCL10	040	/* 30 Clear TO10DN and TO10ER */
#define DTE_COPIENB	020	/* 31 Load PI chan and PI0 enable bit */
#define DTE_COPI0	DTE_CIPI0
#define DTE_COPI	DTE_CIPI


/* DTE DATAO defs [DTE p.1-25] */
				/* 0-22 MBZ */
#define DTE_DO10IB	010000	/* 23 I-bit, interrupt 11 after xfer */
#define DTE_DO10BC	 07777	/* 24-35 Negative byte count for xfer */

/* EPT locations for DTE secondary protocol.
**
**	These appear to be special locations solely for use by the
**	Master DTE and no others, and only used during secondary ptcl.
**	Several are only used by KLDCP (Diagnostic Control Program) rather
**	than the normal RSX20F.
**
**	Because these defs come from T20 STG.MAC the names are
**	retained for consistency.
*/

#define DTEE_FLG 0444	/* OPERATION COMPLETE FLAG */
			/* Cleared by 10 prior to setting CMD (for some) */
			/* Set -1 by 11 after CMD handled (for some) */
#define DTEE_CKF 0445	/* CLOCK INTERRUPT FLAG */
			/* Set -1 by 11 each 60Hz tick, if clock enabled */
#define DTEE_CKI 0446	/* CLOCK INTERRUPT INSTRUCTION */
#define DTEE_T11 0447	/* TO 11 ARGUMENT */
#define DTEE_F11 0450	/* FROM 11 ARGUMENT */
#define DTEE_CMD 0451	/* COMMAND WORD */
			/* Set by 10, executed by 11 when 11DB rung */
#define DTEE_SEQ 0452	/* DTE20 OPERATION SEQUENCE NUMBER */
#define DTEE_OPR 0453	/* OPERATION IN PROGRESS FLAG */
#define DTEE_CHR 0454	/* LAST TYPED CHARACTER */
#define DTEE_TMD 0455	/* MONITOR TTY OUTPUT COMPLETE FLAG */
			/* Set 0 by 10 prior to DTECMD_MNO command */
			/* Set -1 by 11 when DTECMD_MNO is done (but note */
			/* RSX20F uses some bits to report options to 10) */
#define DTEE_MTI 0456	/* MONITOR TTY INPUT FLAG */
			/* Set -1 by 11 when F11 contains TTY input char */
			/* Set 0 by 10 when char read (allow more tty in) */
#define DTEE_SWR 0457	/* CONSOLE SWITCH REGISTER */


/* RSX20F option bits:

	Later versions of RSX20F set DTEE_TMD to a bitmask of options,
rather than simply -1.  TOPS-10 examines these bits to see whether certain
features are supported or not.  TOPS-20 doesn't use them at all.
The DTEPRM source defines the following four bits:

*/
#define DTE_20FOPT_GDT 01	/* 1B35	DF.GDT - Zero if have DTECMD_RTM */
#define DTE_20FOPT_CSH 02	/* 1B34	DF.CSH - Cache enabled by KLI */
				/*	1=on, 0=off */
#define DTE_20FOPT_8BA 04	/* 1B33	DF.8BA - Zero if can support */
				/*	8-bit enable/disable on DLS lines */
#define DTE_20FOPT_ODN 0100000	/* 1B20	DF.ODN - TTY output done */
		/* Symbol unused; apparently just to make sure
		** the word is non-zero when tty output function completes.
		*/

/* T20 Monitor "KLDTE" protocol for console TTY */

/* Note: if DTE_CBEPW is non-zero (at least for the master DTE)
**	then implicitly "primary protocol" is in effect.
**	I assume that if zero, then DTE cannot examine anything, and we're
**	using "secondary protocol" which is what DDT uses.
**
**	Secondary protocol is also known as "monitor mode" TTY I/O.
**	A command (.DTMMC) via DTECMD must also be sent to inform 11 of this.
**
**	Another command (.DTNMC) is used to leave monitor mode TTY I/O,
**	if primary protocol is restored by setting EPW non-zero.
*/

/* Command word values for DTEE_CMD above.
**	I've adopted the PROLOG definitions since those are the most common.
**	APRSRV has more complete defs which aren't really used.
**	DDT's hint that they came from somewhere else, but dunno where.
*/
#define DTECMDF_CMD (017<<8)	/* Command code in DTECMD */
#define DTECMDF_CHR 0377	/* TTY output byte in DTECMD */

/* RSX20F provides for a "secondary protocol" which is something very
** simple for boot/ddt/standalone exec programs.  Only four DTE commands
** are recognized in secondary protocol mode (10,11,12,13).  Actually,
** any command that isn't one of 11,12,13 is interpreted as 10 (output
** low byte to CTY)!
**
** It's unclear whether the other DTECMD values hinted at are
** actually used, or if they were old commands later obsoleted.  I can't
** find evidence for them in RSX20F.  They probably come from KLDCP.
*/
#define DTECMD_MNO  (010<<8)	/* Output char (low 8 bits) if monitor mode */
				/*(PROLOG:DTEMNO)(APRSRV:DTEMNO)(DDT:.DTMTO)*/
				/* (DTEPRM:DT.MTO) */
#define DTECMD_EMP  (011<<8)	/* Enter Monitor TTY Protocol (leave prim) */
				/* Or: Enter Secondary Protocol */
				/*(PROLOG:DTEEMP)(APRSRV:DTEMMN)(DDT:.DTMMC)*/
				/* (DTEPRM:DT.ESP) */
#define DTECMD_EPP  (012<<8)	/* Enter Primary Protocol (leave mon) */
				/* Or: Leave Secondary Protocol */
				/*(PROLOG:DTEEPP)(APRSRV:DTEMMF)(DDT:.DTNMC)*/
				/* (DTEPRM:DT.LSP) */
				/* Low bit (1B35) (DTEPRM:DT.RST) used to
				** indicate comm rgn was reset.
				*/
#define DTECMD_RTM  (013<<8)	/* Get date/time info */
				/* (APRSRV:DTERMM) */
				/* (DTEPRM:DT.GDT) */
				/* Used by T10 if DTE_20FOPT_GDT says OK. */
				/* T20 doesn't use. */


/* More info about DTECMD commands, from T20 APRSRV.
** These symbols are totally unused by the T20 monitor except for
** DTEMNO (duplicate of PROLOG's) and DTEMMN (used once in APRSRV).

	DTETTO==0B27		;TTY OUTPUT	[DIAMON: chars, 26]
	; 1 - PROGRAM CONTROL, NOT USED		[DIAMON: 406,407,414]
	DTECOF==2B27+0		;CLOCK OFF
	DTECON==2B27+1		;CLOCK ON
	; 3 - SWITCHES, NOT USED		[DIAMON]
	; 4 - TTY OUTPUT, SAME AS 0?
	; 5 - TTY INPUT, NOT USED
	DTEPTN==6B27+0		;PRINT NORMAL	[DIAMON: Clear DDT input mode]
	DTEPTF==6B27+1		;PRINT FORCED
	DTEDDI==7B27		;DDT INPUT	[DIAMON]
	DTEMNO==10B27		;MONITOR TTY OUTPUT
	DTEMMN==11B27		;MONITOR MODE ON
	DTEMMF==12B27		;MONITOR MODE OFF
	DTERMM==13B27		;READ MONITOR MODE
*/

/* KLDCP - (KL Diagnostics Control Program?) secondary ptcl commands
**
** The following DTE commands are apparently only used when KLDCP is running in
** the 11 instead of RSX20F. They are used by the diagnostics monitor code
** (DIAMON, D20MON), but no symbols are defined for them.
**
** Some have T20 APRSRV symbols, although nothing in the T20 monitor uses them.
*/

/* Summary of known KLDCP cmds used by DIAMON, SUBKL, SUBRTN:

KLDCP stuff:

TTY output:

0+char	char - Output char to TTY
0+^V	 026 - "Flush KLDCP output buffer"

Program control:

1+cmd	 401 - "Fatal program error in KL10" (then tries to jump to DDT)
1+cmd	 402 - "Error halt in KL10"
1+cmd	 403 - "End of program notification"
1+cmd	 404 - "End of pass notification"
1+cmd	 405 - "Get clock default word": (Stores result in CLKDFL)
		3B35 - Clock rate: 0=Full, 1=1/2, 2=1/4, 3=1/8
		3B32 - Clock source: 0=Normal, 1=Fast, 2=External, 3=Unused
		B0 - Cache 0 enabled
		B1,B2,B3 ditto for Cache 1,2,3.
1+cmd	 406 - "File Lookup Command" (returns value in F11)
1+cmd	 407 - "File Read Command" (returns word of ASCII in F11)
					(-1 if EOF)
1+cmd	 414 - "File Read 8-bit Command" (returns 8-bit byte in F11)
					(-1 if EOF)
Clock control: (FE clock ticks once per 16.67 ms (60HZ))

2+cmd	1000 - Turn clock off (disable).
2+cmd	1001 - Turn clock on (enable).
		When it goes off, sets $DTCLK (445) NZ and sends To-10 DB.
2+cmd	1002 - Enable clock with wait:
		$DTT11 (447) contains # ticks to wait,
		$DTCLK (445) set NZ when clock goes off (and sends DB to 10)
2+cmd	1003 - Read clock count since last enabled, returns in F11.


3	1400 - "Get switches"
5	2400 - Get TTY input char, wait for it (returns 0 if time out)
		Invoked in SUBKL; timeout appears to be defined in 11.
		10 thinks it defaults to 180 sec.
		Also appears to be expected to echo.
6	3000 - "Clear DDT input mode"
7	3400 - "DDT Input mode"

*/

#define DTECMD_DCP_CTYO (00<<8)		/* Output low 8 bits to CTY */
			/* If low byte == 026 (ctl-V) then acts as
			** "Flush KLDCP output buffer" */
			/* (APRSRV:DTETTO) */
#define DTECMD_DCP_PGM  (01<<8)		/* Program control; low byte subcmd */
#define DTECMD_DCP_CLK  (02<<8)		/* Clock control; low byte subcmd */
			/* (APRSRV:DTECOF/DTECON) */
#define DTECMD_DCP_RDSW (03<<8)		/* Read data switches into F11 word */
#define DTECMD_DCP_TIW  (05<<8)		/* TTY input, with wait */
#define DTECMD_DCP_CLDI (06<<8)		/* "Clear DDT input mode") */
			/* (APRSRV:DTEPTN/DTEPTF) */
#define DTECMD_DCP_DDTIN (07<<8)	/* "Use DDT input mode" */
			/* Used to read CTY input.  Sets F11 to CTY input char
			** AND ECHOES IT!  If no char, sets F11 to 0.
			*/


/* Clock sub-commands */
#define DTECMD_DCPCLK_OFF 0	/* Clock: Turn off (disable). */
#define DTECMD_DCPCLK_ON  1	/* Clock: Turn on (enable). */
				/*	When it fires, sets $DTCLK (445) NZ
				**	and sends To-10 DB. */
#define DTECMD_DCPCLK_WAIT 2	/* Clock: Turn on with wait */
				/*	$DTT11 (447) contains # ticks to wait
				**	before firing */
#define DTECMD_DCPCLK_READ 3	/* Clock: Read count */
				/*	Reads clock count since last enabled,
				**	returns in F11. */


/* DTE 10-11 Protocol Comm region definitions

Important orientation & terminology notes:

    COMBUF:	<header wds for proc #n, n-1, ...>
		<header wd for proc #1>
		<header wd for proc #0>
    COMBAS:
	10's (proc #0) own comreg:
	    10's fixed:	16 (COMDAT) wds		; 10's own stuff
	    DTE0 to-11:	 8 (COMRGN) wds		; For its comms TO this 11
	    DTE1 to-11:	 8 (COMRGN) wds		;	"
	    DTE2 to-11:	 8 (COMRGN) wds		;	"
	    DTE3 to-11:	 8 (COMRGN) wds		;	"

	11#1's own comreg:
	    #1's fixed:	16 (COMDAT) wds		; 11#1's fixed stuff
	    DTE0 to-11:	 8 (COMRGN) wds		; For its comms TO the 10
	11#2's own comreg:
	    #2's fixed:	16 (COMDAT) wds
	    DTE1 to-11:	 8 (COMRGN) wds
	11#3's own comreg:
	    #3's fixed:	16 (COMDAT) wds
	    DTE2 to-11:	 8 (COMRGN) wds
	11#4's own comreg:
	    #4's fixed:	16 (COMDAT) wds
	    DTE3 to-11:  8 (COMRGN) wds


	"Fixed" part of region is also called "owned" part.
	"Dynamic" part is also called "to" or "per processor" part.

	All relative addresses in the various com regions are relative to
	COMBAS!
*/

/* COMBUF header word - at offset 0 in examine reloc area.
**	This word allows the 11 to identify itself and locate COMBAS,
**	from which it can find its own comreg.
*/
#define DTE_CMP_CPUN	  03700	/* LH: Processor # {1,2,3,4} */
#define DTE_CMP_11CDOFF	0177777	/* RH: Offset from base to 11's comdat */

/* Compute offset from DTE's examine-reloc to combase */
#define DTE_CMBAS(w) (((LHGET(w)>>6)&037)+1)


/* "Own" COMDAT definitions - fixed part of owned comregion.
**	Size of 10's fixed area can actually vary from that for the 11s
**	but in practice it's always 16 words.
*/
#define DTE_CMOW_KAC 5	/* Wd offset: keepalive count */


/* "To" COMRGN definitions - dynamic part of owned comregion.
**	Again, this could vary but is always 8 words.
**	Most references are to this part of the comregions since that
**	is where stuff happens.
*/

#define DTE_CMTW_0 0	/* Wd offset: various static stuff */
# define DTE_CMT_PRO	0400000	/* LH: 1 IF THIS IS CONNECTION TO A -10 */
# define DTE_CMT_DTE	0200000	/* LH: 1 IF A DTE IS CONNECTING THESE TWO */
# define DTE_CMT_DTN	0140000	/* LH: <dte#> - DTE number 0-3 */
# define DTE_CMT_VRR	    076	/* LH: PROTOCOL IN USE BY THE TWO PROCESSORS */
				/*	Normally .VN20F == 0 */
# define DTE_CMT_SZ    01600000	/* LH+RH: Size of block in 8-wd units */
# define DTE_CMT_PNM	0177777	/* RH: Processor number (<dte#>+1) */

#define DTE_CMTW_PPT 1	/* Wd offset: Rel Addr of assoc'd proc's own comrgn */
			/* Note 10's fixed comrgn has reladdr 0 */

#define DTE_CMTW_STS 2	/* Wd offset: status bits */
# define DTE_CMT_PWF	0400000	/* LH: POWER FAIL INDICATOR */
# define DTE_CMT_L11	0200000	/* LH: LOAD-11 request indicator */
# define DTE_CMT_INI	0100000	/* LH: INI BIT FOR MCB PROTOCOL ONLY */
# define DTE_CMT_TST	 040000	/* LH: VALID EXAMINE BIT */
# define DTE_CMT_QP	    020	/* LH: 1 IF USING QUEUED PROTOCOL (.VN20F) */
# define DTE_CMT_FWD	     01	/* LH: SAYS TO DO FULL WORD TRANSFER */
# define DTE_CMT_IP	0400000	/* RH: INDIRECT POINTER IS SET UP */
# define DTE_CMT_TOT	0200000	/* RH: TOIT bit */
# define DTE_CMT_10IC	0177400	/* RH: TO10IC for queue transfers */
# define DTE_CMT_11IC	   0377	/* RH: TO11IC for queue transfers */

#define DTE_CMTW_CNT 3	/* Wd offset: queue counts */
# define DTE_CMT_QCT	0177777	/* RH: # of bytes in current direct transfer */
# define DTE_CMT_PCT ((0177777)<<16) /* LH+RH: # of bytes in entire msg */
		/* Cnt of all bytes in packet (incl all indirect data). */

#define DTE_CMTW_RLF 4	/* Wd offset: RELOAD PARAMETER FOR THIS -11 */
#define DTE_CMTW_KAK 5	/* Wd offset: MY COPY OF HIS CMKAC (keepalive cnt) */


/* Defs for RSX20F queued message protocol.
** Header is 10 8-bit bytes transferred in byte mode.
*/
#define DTE_QMH_SIZ 10		/* # bytes in message header */
#define DTE_QMH_INDIRF 0100000	/* High bit of function is "indirect" flag */


/* Function codes (to-11 unless otherwise noted) */

#define DTE_QFN_LCI	01	/* LINE COUNT IS */
#define DTE_QFN_ALS	02	/* CTY device alias */
#define DTE_QFN_HSD	03	/* HERE IS STRING DATA */
#define DTE_QFN_HLC	04	/* HERE ARE LINE CHARACTERS */
#define DTE_QFN_RDS	05	/* REQUEST DEVICE STATUS */
#define DTE_QFN_SDO	06	/* SPECIAL DEVICE OPERATION */
#define DTE_QFN_STS	07	/* HERE IS DEVICE STATUS */
#define DTE_QFN_ESD	010	/* ERROR ON DEVICE */
#define DTE_QFN_RTD	011	/* REQUEST TIME OF DAY */
#define DTE_QFN_HTD	012	/* HERE IS TIME OF DAY */
#define DTE_QFN_FDO	013	/* FLUSH OUTPUT (SENT TO 11 ONLY) */
#define DTE_QFN_STA	014	/* SEND TO ALL (SENT TO 11 ONLY) */
#define DTE_QFN_LDU	015	/* A LINE DIALED UP (FROM 11 ONLY) */
#define DTE_QFN_LHU	016	/* A LINE HUNG UP OR LOGGED OUT */
#define DTE_QFN_LBE	017	/* LINE BUFFER BECAME EMPTY */
#define DTE_QFN_XOF	020	/* XOF COMMAND TO THE FE */
#define DTE_QFN_XON	021	/* XON COMMAND TO THE FE */
#define DTE_QFN_SPD	022	/* SET TTY LINE SPEED */
#define DTE_QFN_HLA	023	/* HERE IS LINE ALLOCATION */
#define DTE_QFN_HRW	024	/* HERE IS -11 RELOAD WORD */
#define DTE_QFN_ACK	025	/* GO ACK ALL DEVICES AND UNITS */
#define DTE_QFN_TOL	026	/* TURN OFF/ON LINE (0 off, 1 on) */
#define DTE_QFN_EDR	027	/* ENABLE/DISABLE DATASETS */
#define DTE_QFN_LTR	030	/* LOAD TRANSLATION RAM */
#define DTE_QFN_LVF	031	/* LOAD VFU */
#define DTE_QFN_MSG	032	/* SUPPRESS SYSTEM MESSAGES TO TTY */
#define DTE_QFN_KLS	033	/* SEND KLINIK DATA TO THE -11 */
#define DTE_QFN_XEN	034	/* ENABLE XON (SENT TO 11 ONLY) */
#define DTE_QFN_BKW	035	/* BREAK-THROUGH WRITE */
#define DTE_QFN_DBN	036	/* DEBUG MODE ON */
#define DTE_QFN_DBF	037	/* DEBUG MODE OFF */
#define DTE_QFN_SNO	040	/* IGNORE NEW CODE FOR SERIAL NUMBERS */



/* FRONT END PSEUDO DEVICES */

#define DTE_QDV_CTY	01	/* CTY on the DL11-C */
#define DTE_QDV_DL1	02	/* DL11-C (or -E) on the master -11 */
#define DTE_QDV_DH1	03	/* DH11 lines 1-N */
#define DTE_QDV_DLS	04	/* Generic Data Line identifier */
				/* All lines represented by unit #s of this
				** "device", whether DL, DH, etc.
				*/
#define DTE_QDV_LPT	05	/* Line printer */
#define DTE_QDV_CDR	06	/* Card Reader */
#define DTE_QDV_CLK	07	/* Clock (not defined in T20) */
#define DTE_QDV_FE	010	/* Software Front End device */

/* KLH10 configurable parameter */
#define DTE_DLS_CTY	0	/* DLS Line # to use for CTY */


#endif /* ifndef DVDTE_INCLUDED */
