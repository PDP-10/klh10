/* DVCH11.H - PDP-11 Chaosnet Interface
*/
/* $Id: dvch11.h,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: dvch11.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */
/*
**	Portions of this file were derived from AI:SYSTEM;CH11 DEFS1
*/

#ifndef DVCH11_INCLUDED
#define DVCH11_INCLUDED 1

#ifdef RCSID
 RCSID(dvch11_h,"$Id: dvch11.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

extern struct device *dvch11_init(FILE *, char *);


/* CH11 addresses & assignments for KS10:
 CH11	Address		Vector	UBA#	BR-Level
  #1	0764140		0270	3	4 or 5 (dunno which)
 etc(?)	(some)		0230
*/

#define UB_CH11_BR  5
#define	UB_CH11_VEC 0270	/* CH11 Interrupt Vector */
#define	UB_CH11 0764140		/* CH11 Unibus Address (on UBA #3) */

/* CH11 Unibus Chaosnet Interface definitions */

#define UB_CHCSR 0764140	/* COMMAND STATUS REG */
#  define CH_BSY	01	/* 0 XMT BUSY (RO) */
#  define CH_LUP	02	/* 1 LOOP BACK (R/W) */
#  define CH_SPY	04	/* 2 RECIEVE MSGS FOR ANY DESTINATION (R/W) */
#  define CH_RCL	010	/* 3 CLEAR THE RECEIVER, IT CAN NOW GOBBLE ANOTHER MSG (WO) */
#  define CH_REN	020	/* 4 RCV INT ENB (R/W) */
#  define CH_TEN	040	/* 5 XMT INT ENB (R/W) */
#  define CH_TAB	0100	/* 6 TRANSMIT ABORTED BY ETHER CONFLICT (RO) */
#  define CH_TDN	0200	/* 7 TRANSMIT DONE. SET WHEN TRANSMITTER IS DONE */
#  define CH_TCL	0400	/* 8 CLEAR THE TRANSMITTER, MAKING IT READY (WO) */
#  define CH_LOS	017000	/* 9-12    LOST COUNT (RO) [# MSGS RCVED WITH RCV BFR FULL] */
/*		  ;        WHEN MSG IS WAITING IN BUFFER, THIS COUNTS
		  ;        THE MESSAGES THAT MATCHED OUR DESTINATION OR
		  ;        WERE BROADCAST, BUT COULDN'T BE RECIEVED.
		  ;	 WHEN RECEIVER IS RE-ENABLED (WRITE 1 INTO %CARDN)
		  ;	 THE COUNT IS THEN CLEARED.
		  ;	 WHEN A MESSAGE IS LOST, RECEIVER ZAPS ETHER
		  ;	 SO TRANSMITTER WILL ABORT (IF MESSAGE WAS DESTINED
		  ;        TO US.) */
#  define CH_RST	020000	/* 13 I/O RESET (WO) */
#  define CH_ERR	040000	/* 14 CRC ERROR (RO) */
#  define CH_RDN	0100000	/* 15  RCV DONE.   */

#define UB_CHMYN 0764142	/* MY # (READ ONLY) */
		  /* RETURNS THE [SOURCE] HOST# OF THIS INTERFACE. */

#define UB_CHWBF	0764142	/* WRITE BUFFER (WRITE ONLY) */
		/* FIRST WAIT FOR TDONE. (OR SET IT VIA CSR)
		  ;FIRST WORD IN RESETS TRANSMITTER AND CLEARS TDONE.
		  ;STORE INTO THIS REGISTER TO WRITE WORDS OF MESSAGE,
		  ;LAST WORD IN IS DESTINATION ADDRESS, THEN READ CAIXMT.
		  ;SOURCE ADDRESS AND CHECK WORD PUT IN BY HARDWARE. */

#define UB_CHRBF	0764144	/* READ BUFFER (READ ONLY) */
		/* THE FIRST WORD READ WILL BE FILLED TO THE LEFT
		  ;TO MAKE THE MESSAGE RECIEVED A MULTIPLE OF 16 BITS.
		  ;IF THE NUMBER OF DATA BITS IN THE MESSAGE WAS A
		  ;MULTIPLE OF 16, THIS WORD WILL BE THE FIRST WORD PUT
		  ;INTO THE BUFFER BY THE TRANSMITTING HOST.
		  ;THE LAST 3 WORDS READ ARE DESTINATION, SOURCE, CHECK. */

#define UB_CHRBC	0764146	/* RECEIVE BIT COUNTER  (READ ONLY) */
		/* WHEN A MESSAGE HAS BEEN RECEIVED THIS IS ONE LESS THAN
		  ;THE NUMBER OF BITS IN THE MESSAGE (16 X THE
		  ;NUMBER OF WORDS INCLUDING THE THREE OVERHEAD WORDS.)
		  ;AFTER THE LAST WORD (THE CRC WORD) HAS BEEN READ, IT IS 7777
		  ;BITS 10 AND 11 ARE THE HIGH ORDER BITS, AND IF THEY ARE ONE,
		  ;THEN THERE WAS A BUFFER OVERFLOW */

#define UB_CHXMT	0764152	/* READING THIS INITIATES TRANSMISSION (!!) */
		/* THE VALUE READ IS ONE'S OWN HOST#. */

/*
;REFERENCING ADDRESSES IN THE GROUP OF 8 WORDS NOT LISTED HERE, OR
;USING COMBINATIONS OF READ/WRITE NOT LISTED HERE, WILL TYPICALLY CAUSE
;STRANGE AND BIZARRE EFFECTS.
*/

#define UB_CH11END (UB_CHXMT+2)	/* First addr not used by CH11 regs */

#endif /* ifndef DVCH11_INCLUDED */
