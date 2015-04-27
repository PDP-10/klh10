/* DVNI20.H - NIA-20 Network Interface defniitions
*/
/* $Id: dvni20.h,v 2.3 2001/11/10 21:28:59 klh Exp $
*/
/*  Copyright © 1994, 2001 Kenneth L. Harrenstien
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
 * $Log: dvni20.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#ifndef NI20_INCLUDED
#define NI20_INCLUDED 1

#ifdef RCSID
 RCSID(dvni20_h,"$Id: dvni20.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* Initialization & sole entry point for NI20 driver
*/
#include "kn10dev.h"
extern struct device *dvni20_create(FILE *f, char *s);

#ifndef NI20_NSUP
# define NI20_NSUP 1
#endif

#ifndef RH20_INCLUDED
# include "dvrh20.h"		/* Need generic RH20 defs for data channel */
#endif

/* The NI port is a special case of the IPA20-L Computer Interconnect (CI)
** port hardware, which itself is a special-case Massbus/RH20 device.
** By convention the NI is Massbus/RH20 port #5.
*/

/* NI20 Device Number assignment
*/

#define NI20_DEV DEVRH20(5)


/* NI20 CONI bits (LH)
**	Read-only by 10; set by port device.
*/
#define NI20CI_PPT	0400000	/* B0 - Port present */
#define NI20CI_DCC	0100000	/* B2 - Diag CSR change */
				/* (Set when 10 writes CSR; cleared when
				** port reads it).
				*/
#define NI20CI_CPE	  04000	/* B6 - CRAM parity error */
#define NI20CI_MBE	  02000	/* B7 - Mbus error */
#define NI20CI_IDL	   0100	/* B11 - Idle */
#define NI20CI_DCP	    040	/* B12 - Disable complete */
#define NI20CI_ECP	    020	/* B13 - Enable complete */
#define NI20CI_PID	     07	/* B15-17 - Port type ID (always 7) */

/* NI20 CONI/CONO bits (RH)
**	All may be written by a CONO.
*/
#define NI20CO_CPT	0400000	/* B18 - Clear port */
#define NI20CO_SEB	0200000	/* B19 - Diag Select Ebuf */
				/* B20 - Diag Gen Ebus PE */
#define NI20CO_LAR	 040000	/* B21 - Diag Select LAR/SQR */
#define NI20CO_SSC	 020000	/* B22 - Diag Single Cyc */
				/* B23 - */
#define NI20CI_EPE	  04000	/* B24 - EBUS PARITY ERROR */
#define NI20CI_FQE	  02000	/* B25 - FREE QUEUE ERROR */
				/* 	Set by port; CONO of bit clears it! */
#define NI20CI_DME	  01000	/* B26 - DATA MOVER ERROR */
#define NI20CO_CQA	   0400	/* B27 - COMMAND QUEUE AVAILABLE */
#define NI20CI_RQA	   0200	/* B28 - RESPONSE QUEUE AVAILABLE */
				/* 	Set by port; CONO of bit clears it! */
				/* B29 - */
#define NI20CO_DIS	    040	/* B30 - DISABLE */
#define NI20CO_ENA	    020	/* B31 - ENABLE */
#define NI20CO_MRN	    010	/* B32 - MICRO-PROCESSOR RUN */
#define NI20CO_PIA	     07	/* B33-35 - PI channel assignment (0 = none) */


/* NI20 DATAO/DATAI word format
**	What values are read or written depends on the CONI bit states.
**	The sign bit of a DATAO means "Load RAM address register"
*/

#define NI20DO_LRA	0400000	/* LH: B0 Load RAM address reg */
#define NI20DO_RAR	0377740	/* LH: B1-12 RAR (if B0 set) */
#define NI20DO_MSB	    020	/* LH: B13 Select LH of u-word (MSBits) */

/* DATAI with
**    SEB=0, LAR=1	Reads Last Address Register (addr of last u-instr
**				fetched by port)
**				     <0> <1:13> <14> <15:35>
**				LAR:  1   LAR     0    -1
**
**    SEB=1		Reads EBUF word
**
**    SEB=0, LAR=0	Reads LH or RH of u-word addressed by RAR.
**				<0:5> <6:35>
**				 -1   <bits 0-29 or 30-59 of u-word>
*/
/* DATAO with
**    SEB=0		Writes RAR, or LH/RH of u-word addressed by RAR.
**			If B0=0: <0:5> <6:35>
**				  N/A   <bits 0-29 or 30-59 of u-word>
**			If B0=1: <0> <1:12> <13> <14:35>
**				  1   RAR   LH=1   N/A
**
**    SEB=1		?? Writes EBUF word?
**
*/


#define NI20_UADR 0377740	/* 7777B12 - Ucode address field in RAR/LAR */

#define NI20_UA_VER 0136	/* Ucode address of major & minor ver #s */
#define NI20_UA_EDT 0137	/* Ucode address of edit # */
#define NI20_UVF_MAJ	0170000	/* RH: 17B23 Major version # in UA_VER LH */
#define NI20_UVF_MIN	  01700	/* RH: 17B29 Minor version # in UA_VER LH */
#define NI20_UF_EDT	0177700	/* RH: 1777B29 Edit # in UA_EDT LH */

#define NI20_VERMAJ 01		/* Duplicate actual NIA20 ucode version */
#define NI20_VERMIN 0
#define NI20_EDITNO 0172	/* Edit # (171 is minimum acceptable to T20) */
				/* (167 is minimum acceptable to T10) */

	/* CRAM Parity Error PCs, to indicate error type */
#define NI20_CPE_INTERR 07750	/* Internal Error */
#define NI20_CPE_SLFTST 07751	/* Startup self-test failed */
#define NI20_CPE_CHNERR 07762	/* Channel Error */

	/* Size of internal tables */
#define NI20_NPTT 017	/* Max # of entries in Protocol Type Table */
#define NI20_NMTT 017	/* Max # of entries in Multicast Address Table */

/* NOTE: the NI port spec claims that 16 entries are used for both
** the PTT and MCAT.  However, both T10 and T20 allocate only 15 entries,
** and this is the size that the actual NI ucode returns for the RDNSA
** command.
**
**	!!!HOWEVER!!! The actual NI ucode *really does* have 16 entries,
**	and it *really does* read in 16 words for each table!
**	
** Groan.
** Who knows whether this caused any actual lossage for real PDP10s.
**
** The KLH10 will do the Right Thing and only read 15 entries, as advertised.
*/

/* Port Control Block (PCB) definitions
*/

/* Offsets into PCB */
enum {
	NI20_PB_CQI=0,	/*   0 COMMAND QUEUE INTERLOCK */
	NI20_PB_CQF,	/*   1 COMMAND QUEUE FLINK */
	NI20_PB_CQB,	/*   2 COMMAND QUEUE BLINK */
	NI20_PB_RS0,	/*   3 RESERVED FOR SOFTWARE */
	NI20_PB_RQI,	/*   4 RESPONSE QUEUE INTERLOCK */
	NI20_PB_RQF,	/*   5 RESPONSE QUEUE FLINK */
	NI20_PB_RQB,	/*   6 RESPONSE QUEUE BLINK */
	NI20_PB_RS1,	/*   7 RESERVED */
	NI20_PB_UQI,	/* 010 UNKNOWN PROTOCOL TYPE QUEUE INTERLOCK */
	NI20_PB_UQF,	/* 011 UNKNOWN PROTOCOL TYPE QUEUE FLINK */
	NI20_PB_UQB,	/* 012 UNKNOWN PROTOCOL TYPE QUEUE BLINK */
	NI20_PB_UQL,	/* 013 UNKNOWN PROTOCOL TYPE QUEUE LENGTH */
	NI20_PB_RS2,	/* 014 RESERVED */
	NI20_PB_PTT,	/* 015 PROTOCOL TYPE TABLE STARTING ADDRESS */
	NI20_PB_MTT,	/* 016 MULTICAST ADDRESS TABLE STARTING ADDRESS */
	NI20_PB_RS3,	/* 017 RESERVED */
	NI20_PB_ER0,	/* 020 KLNI ERROR LOGOUT 0 */
	NI20_PB_ER1,	/* 021 KLNI ERROR LOGOUT 1 */
	NI20_PB_LAD,	/* 022 ADDRESS OF CHANNEL LOGOUT WORD 1 */
	NI20_PB_CLO,	/* 023 CONTENTS OF CHANNEL LOGOUT WORD 1 */
	NI20_PB_PBA,	/* 024 PORT CONTROL BLOCK BASE ADDRESS */
	NI20_PB_PIA,	/* 025 PI LEVEL ASSIGNMENT */
	NI20_PB_IVA,	/* 026 INTERRUPT VECTOR ASSIGNMENT */
	NI20_PB_CCW,	/* 027 CHANNEL COMMAND WORD */
	NI20_PB_RCB	/* 030 POINTER TO READ COUNTERS BUFFER */
};

/* Protocol Type Table (PTT) defs */

#define NI20_PT_FLD	0	/* Word offset for various fields */
# define NI20_PTF_ENA	0400000	/* B0 - Enable this protocol */
# define NI20_PTF_MBZ	0377774	/* B1-15 MBZ */
# define NI20_PTF_TYP	03777760 /* B16-31 LH+RH: Protocol Type */
# define NI20_PTF_FRE	01	/* B35 - Software? Entry "free" */
#define NI20_PT_FRQ	1	/* Wd offset for Free Queue header */
#define NI20_PT_VIR	2	/* Software (exec virt addr of FRQ) */
#define NI20_PT_LEN	3	/* Length of a PTT entry */

/* Multi-Cast Address Table (MCAT) defs */

#define NI20_MT_HAD	0	/* High 4 bytes of address */
#define NI20_MT_LAD	1	/* Low 2 bytes (in LH) */
# define NI20_MTF_ENA	01	/* B35 - Enable bit in LAD */
#define	 NI20_MT_LEN	2	/* Length of a MCAT entry */


/* Queue defs.
**	Note T20 waits up to 5 seconds for an interlock word to free up;
**	however, during that time it does nothing but spin.
**
**	If a queue is empty, the header links both point to the FLINK word.
**	If a queue entry is the first, its BLINK points to the header FLINK.
**	If a queue entry is the last, its FLINK points to the header FLINK.
**	Thus, link manipulation can take place by treating the list as
**	completely composed of queue entries, even though one of the
**	"entries" is actually part fo the queue header.
*/

	/* Queue Header - word offsets */
#define NI20_QH_IWD	0	/* Interlock Word */
#define NI20_QH_FLI	1	/* Forward Link */
#define NI20_QH_BLI	2	/* Backward Link */
#define NI20_QH_QES	3	/* Queue entry size (LEN in T20 code) */

	/* Queue Entry - word offsets */
#define NI20_QE_FLI	0	/* Forward Link */
#define NI20_QE_BLI	1	/* Backward Link */
#define NI20_QE_VIR	2	/* Software - Exec virt addr of entry */
#define NI20_QE_OPC	3	/* Operation Code */


/* Command Entry definitions */

#define NI20_CM_FLI	NI20_QE_FLI	/* Forward link */
#define NI20_CM_BLI	NI20_QE_BLI	/* Backward Link */
#define NI20_CM_VAD	NI20_QE_VIR	/* Software */
#define NI20_CM_CMD	NI20_QE_OPC	/* Command Operation Code etc */

	/* Error Status byte */
# define NI20_CMF_STSB	0776000 /* Status byte */
# define NI20_CMF_SRI	0200000	/* 0=Receive, 1=Send */
# define NI20_CMF_ERR	0174000	/* Error Type */
# define NI20_CMF_ERF	  02000	/* 1=Error status meaningful (else all MBZ) */

	/* Flags byte - for SEND DGM unless otherwise specified */
# define NI20_CMF_PAC	01000	/* Packing; 0=Indust-Compatible, 1=Reserved */
# define NI20_CMF_CRC	 0400	/* CRC appended (NB: unused by T10/T20!) */
# define NI20_CMF_PAD	 0200	/* Add len, pad if nec (T20: "unused"?) */
				/* 0100 reserved */
# define NI20_CMF_BSD	  040	/* Using Buffer Seg Descr format */
				/* 020 reserved */
				/* 010 reserved */
# define NI20_CMF_CLR	  010	/* Clear counters (Read-Counters cmd only) */
# define NI20_CMF_RSP	   04	/* Response requested (any cmd) */

	/* Opcode byte */
# define NI20_CMF_OPC	03770000 /* LH+RH: command opcode */

	/* "Time Domain Reflectometry" value.
	** For errors 00 and 01, B26-35 of the cmd word are set to
	** the time, in 100ns ticks, from the start of transmission until
	** the error event was detected by the hardware.
	*/
# define NI20_CMF_TDR	01777 /* TDR */


/* Command Opcodes */

#define NI20_OP_FLS	0	/* Flush commands -- must be illegal opcode */
#define NI20_OP_SND	1	/* Send Datagram */
#define NI20_OP_LDM	2	/* Load Multicast Address Table */
#define NI20_OP_LDP	3	/* Load Protocol Type Table */
#define NI20_OP_RCC	4	/* Read and Clear Counters */
#define NI20_OP_RCV	5	/* Datagram Received */
#define NI20_OP_WPL	6	/* Write PLI */
#define NI20_OP_RPL	7	/* Read PLI */
#define NI20_OP_RSI	8	/* Read Station Information */
#define NI20_OP_WSI	9	/* Write Station Information */

/* Error code definitions
**
**	Returned in NI20_CMF_ERR field of a response.  Note the field
** is only meaningful if the NI20_CMF_ERF bit (low bit) is set.
*/
#define NI20_ERR_EXC	0	/* Excessive collisions */
#define NI20_ERR_CCF	01	/* Carrier check failed */
#define NI20_ERR_CDF	02	/* Collision detect check failed */
#define NI20_ERR_SCI	03	/* Short circuit */
#define NI20_ERR_OCI	04	/* Open circuit */
#define NI20_ERR_FTL	05	/* Frame too long */
#define NI20_ERR_RFD	06	/* Remote failure to defer */
#define NI20_ERR_BCE	07	/* Block check error (CRC error) */
#define NI20_ERR_FER	010	/* Framing error */
#define NI20_ERR_DOV	011	/* Data overrun */
#define NI20_ERR_UPT	012	/* Unrecognized protocol type?!? */
#define NI20_ERR_FTS	013	/* Frame too short */

#define NI20_ERR_SCE	027	/* Spurious channel error */
#define NI20_ERR_CER	030	/* Channel error (WC <> 0) */
#define NI20_ERR_QLV	031	/* Queue length violation */
#define NI20_ERR_IPL	032	/* Illegal PLI function */
#define NI20_ERR_URC	033	/* Unrecognized command */
#define NI20_ERR_BLV	034	/* Buffer length violation */
#define NI20_ERR_RSV	035	/* Reserved */
#define NI20_ERR_TBP	036	/* Xmit buffer parity error */
#define NI20_ERR_INT	037	/* Internal error */


/* SNDDG (1= Send Datagram) command definitions */

#define NI20_CM_SNTXL NI20_CM_CMD+1	/* Word holding text length */
# define NI20_SNF_TXL	0177777		/* B20-35 Text length in bytes */
#define NI20_CM_SNPTY NI20_CM_CMD+2	/* Word holding protocol type */
# define NI20_SNF_PTY	03777740	/* B16-31 Protocol type */
#define NI20_CM_SNFRQ NI20_CM_CMD+3	/* Word holding FREEQ header addr */
		/* Note: KLNI.MEM is silent on the use of the FRQ word, and
		** the T20 PHYKNI code never references it!
		*/
#define NI20_CM_SNHAD NI20_CM_CMD+4	/* High order part of E/N address */
#define NI20_CM_SNLAD NI20_CM_CMD+5	/* Low order part */

#define NI20_CM_SNDTA NI20_CM_CMD+6	/* Non-BSD: 1st word of data */
#define NI20_CM_SNBBA NI20_CM_CMD+6	/* BSD: Physical BSD base address */
		/* Note: T20 always uses BSD format for sending */


/* BSD (Buffer-Seg-Descriptor) definitions */

#define NI20_BD_HDR	0	/* Packing mode & data seg addr */
# define NI20_BDF_PAC	04000	/* B6 - Packing mode 0=indust-compat, 1=rsvd */
				/* Note T20 def uses field B6-7 ?!? */
# define NI20_BDF_SBA	077777777 /* B12-35 24-bit phys seg base addr */
#define NI20_BD_NXA	1	/* Next BSD address (24-bit) */
#define NI20_BD_SLN	2	/* Seg length */
# define NI20_BDF_SLN	0177777	/* B20-35 16-bit segment length in bytes */
#define NI20_BD_RES	3	/* Reserved for software */


/* LDMCAT (2= Load Multicast Address Table) Command definitions
**	(no additional queue data)
*/

/* LDPTT (3= Load Protocol Type Table) Command definitions
**	(no additional queue data)
*/

/* RCCNT (4= Read/Clear Counters) Command definitions
**	(no additional queue data)
** RCCNT response:
*/
enum ni20_rccntr {
	NI20_RC_BR=0,	/* BYTES RECEIVED */
	NI20_RC_BX,	/* BYTES TRANSMITTED */
	NI20_RC_FR,	/* FRAMES RECEIVED */
	NI20_RC_FX,	/* FRAMES TRANSMITTED */
	NI20_RC_MCB,	/* MULTICAST BYTES RECEIVED */
	NI20_RC_MCF,	/* MULTICAST FRAMES RECEIVED */
	NI20_RC_FXD,	/* FRAMES XMITTED, INITIALLY DEFERRED */
	NI20_RC_FXS,	/* FRAMES XMITTED, SINGLE COLLISION */
	NI20_RC_FXM,	/* FRAMES XMITTED, MULTIPLE COLLISIONS */
	NI20_RC_XF,	/* TRANSMIT FAILURES */
	NI20_RC_XFM,	/* TRANSMIT FAILURE BIT MASK */
# define NI20_RCF_LOC	04000	/* B24 - LOSS OF CARRIER */
# define NI20_RCF_XBP	02000	/* B25 - XMIT BUFFER PARITY ERROR */
# define NI20_RCF_RFD	01000	/* B26 - REMOTE FAILURE TO DEFER */
# define NI20_RCF_XFL	 0400	/* B27 - XMITTED FRAME TOO LONG */
# define NI20_RCF_OC	 0200	/* B28 - OPEN CIRCUIT */
# define NI20_RCF_SC	 0100	/* B29 - SHORT CIRCUIT */
# define NI20_RCF_CCF	  040	/* B30 - COLLISION DETECT CHECK FAILED */
# define NI20_RCF_EXC	  020	/* B31 - EXCESSIVE COLLISIONS */

	NI20_RC_CDF,	/* CARRIER DETECT CHECK FAILED */
	NI20_RC_RF,	/* RECEIVE FAILURES */
	NI20_RC_RFM,	/* RECEIVE FAILURE BIT MASK */
# define NI20_RCF_FLE	 0400	/* B27 - FREE LIST PARITY ERROR */
# define NI20_RCF_NFB	 0200	/* B28 - NO FREE BUFFERS */
# define NI20_RCF_FTL	 0100	/* B29 - FRAME TOO LONG */
# define NI20_RCF_FER	  040	/* B30 - FRAMING ERROR */
# define NI20_RCF_BCE	  020	/* B31 - BLOCK CHECK ERROR */

	NI20_RC_DUN,	/* DISCARDED UNKNOWN */
	NI20_RC_D01,	/* DISCARDED POSITION 1 */
	NI20_RC_D02,	/* DISCARDED POSITION 2 */
	NI20_RC_D03,	/* DISCARDED POSITION 3 */
	NI20_RC_D04,	/* DISCARDED POSITION 4 */
	NI20_RC_D05,	/* DISCARDED POSITION 5 */
	NI20_RC_D06,	/* DISCARDED POSITION 6 */
	NI20_RC_D07,	/* DISCARDED POSITION 7 */
	NI20_RC_D08,	/* DISCARDED POSITION 8 */
	NI20_RC_D09,	/* DISCARDED POSITION 9 */
	NI20_RC_D10,	/* DISCARDED POSITION 10 */
	NI20_RC_D11,	/* DISCARDED POSITION 11 */
	NI20_RC_D12,	/* DISCARDED POSITION 12 */
	NI20_RC_D13,	/* DISCARDED POSITION 13 */
	NI20_RC_D14,	/* DISCARDED POSITION 14 */
	NI20_RC_D15,	/* DISCARDED POSITION 15 */
	NI20_RC_D16,	/* DISCARDED POSITION 16 */
	NI20_RC_UFD,	/* UNRECOGNIZED FRAME DEST */
	NI20_RC_DOV,	/* DATA OVERRUN */
	NI20_RC_SBU,	/* SYSTEM BUFFER UNAVAILABLE */
	NI20_RC_UBU,	/* USER BUFFER UNAVAILABLE */
	NI20_RC_RS0,	/* PLI REG RD PAR ERROR,,PLI PARITY ERROR */
	NI20_RC_RS1,	/* MOVER PARITY ERROR,,CBUS PARITY ERROR */
	NI20_RC_RS2,	/* EBUS PARITY ERROR,,EBUS QUE PARITY ERROR */
	NI20_RC_RS3,	/* CHANNEL ERROR,,SPUR CHANNEL ERROR */
	NI20_RC_RS4,	/* SPUR XMIT ATTN ERROR,,CBUS REQ TIMOUT ERROR */
	NI20_RC_RS5,	/* EBUS REQ TIMEOUT ERROR,,CSR GRNT TIMEOUT ERROR */
	NI20_RC_RS6,	/* USED BUFF PARITY ERROR,,XMIT BUFF PARITY ERROR */
	NI20_RC_RS7,	/* RESERVED FOR UCODE */
	NI20_RC_RS8,	/* RESERVED FOR UCODE */
	NI20_RCLEN	/* # of counters */
};


/* DGRCV (5= Datagram Received) Command definitions
*/

#define NI20_CM_RDSIZ NI20_CM_CMD+1	/* Word holding text length */
# define NI20_RDF_SIZ	0177777		/* B20-35 Text len (+4 CRC) in bytes */
#define NI20_CM_RDDHA NI20_CM_CMD+2	/* High order destination addr */
#define NI20_CM_RDDLA NI20_CM_CMD+3	/* Low order part */
#define NI20_CM_RDSHA NI20_CM_CMD+4	/* High order source addr */
#define NI20_CM_RDSLA NI20_CM_CMD+5	/* Low order part */
#define NI20_CM_RDPTY NI20_CM_CMD+6	/* Word holding protocol type */
# define NI20_RDF_PTY	03777740	/* B16-31 Protocol type */
#define NI20_CM_RDPBA NI20_CM_CMD+7	/* Physical BSD base address */


/* WRTPLI (6= Write Port/Link Interface) Command definitions
*/

/* RDPLI (7= Read Port/Link Interface) Command definitions
*/

/* RDNSA (8= Read NI Station Address) Command definitions
**	No extra queue entry words for command, but response has:
*/
#define NI20_CM_RSHAD NI20_CM_CMD+1	/* High order station E/N addr */
#define NI20_CM_RSLAD NI20_CM_CMD+2	/* Low order */
#define NI20_CM_RSFLG NI20_CM_CMD+3	/* Misc flags */
# define NI20_RSF_FLGMSK 017	/* All flags */
# define NI20_RSF_CRC 010	/* Allow receipt of frames with CRC errs */
# define NI20_RSF_PMC  04	/* Receive all Multicast frames */
# define NI20_RSF_H40  02	/* Enable heartbeat detection by transceiver */
# define NI20_RSF_PRM  01	/* Promiscuous mode - receive all frames */
#define NI20_CM_RSVER NI20_CM_CMD+4	/* Version & table limits */
# define NI20_RSF_UCV  03770000	/* LH+RH: IPA20-L port Ucode version */
# define NI20_RSF_NMC     07700	/* # of MCAT entries allowed */
# define NI20_RSF_NPT       077	/* # of PTT entries allowed */


/* WRTNSA (9= Write NI Station Address) Command definitions
*/
#define NI20_CM_WSHAD NI20_CM_RSHAD	/* High order station E/N addr */
#define NI20_CM_WSLAD NI20_CM_RSLAD	/* Low order */
#define NI20_CM_WSFLG NI20_CM_RSFLG	/* Misc flags */
					/* See NI20_RSF_ defs */
#define NI20_CM_WSRTY NI20_CM_CMD+4	/* Retry count */
# define NI20_WSF_RTY  07777		/* Retry count (default 5) */
					/* T20 sets this to 16. */

#endif /* ifndef NI20_INCLUDED */
