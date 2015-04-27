/* DVUBA.H - Definitions for emulating KS10 Unibus Adapters
*/
/* $Id: dvuba.h,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: dvuba.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */
/*
**	Portions of this file were derived from AI:KSHACK;KSDEFS 193
*/

#ifndef DVUBA_INCLUDED
#define DVUBA_INCLUDED 1

#ifdef RCSID
 RCSID(dvuba_h,"$Id: dvuba.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* Define types to use for holding new-IO unibus addresses and register
** values.
**	Unibus addresses are 18 bits.
**	Unibus device registers are only 16 bits.
**		Since an "int" is guaranteed to always have at least 16
**		bits, and is normally the most computationally efficient
**		type, that's what we'll use here.
** All bets are off for the peculiar non-unibus devices such as the
**	memory status register (36-bit) and "unibus adapter" (32-bit).
**
*/
#ifndef WORD10_INT18		/* Ensure uint18 is defined */
# include "word10.h"
#endif
typedef uint18       dvuadr_t;	/* Device: Unibus Address */
typedef unsigned int dvureg_t;	/* Device: Unibus Register value */


#ifndef KLH10_DEVUBA_MAX
# define KLH10_DEVUBA_MAX 10	/* Max # of devs on each unibus */
#endif

/* In the ITS system source,
**	Unibus #1 has mnemonic UBAQ (for QSK => DSK).
**	       #3 has mnemonic UBAI
*/

/* Unibus Paging RAM - one per Unibus, 0763000-763077 inclusive */

#define UB_UBAPAG 0763000	/* UBA Paging RAM (One per Unibus) */
#define UBA_UBALEN	64	/* Length of UBA Paging RAM */
#define UB_UBAEND (UB_UBAPAG+UBA_UBALEN) /*  First non-valid addr */

/*			When read: */
#define UBA_PPAR 0020000	/* 4.5 RAM parity bit */
#define UBA_PRPW 0010000	/* 4.4 Force read-pause-write */
#define UBA_P16B 0004000	/* 4.3 Disable upper two bits on Unibus transfers */
#define UBA_PFST 0002000	/* 4.2 Fast mode enable */
#define UBA_PVAL 0001000	/* 4.1 Entry is valid */
#define UBA_PPVL 0000400	/* 3.9 Parity is valid */
/*
$UPPAG==:121200,,	; 3.2 - 2.2 ITS page number
			; 2.1 ITS half page
			; 3.2 - 2.1 DEC page number
*/
/*			When written: */
#define UBA_QRPW 0400000	/* 2.9 Force read-pause-write */
#define UBA_Q16B 0200000	/* 2.8 Disable 18-bit UB xfer (hi 2 bits) */
#define UBA_QFST 0100000	/* 2.7 Enable Fast (36-bit) xfer mode */
#define UBA_QVAL 0040000	/* 2.6 Entry is valid */
#define UBA_QPAG 0003777	/* 2.2 - 1.2 ITS page number */
				/* 1.1 ITS half page */
				/* 2.2 - 1.1 DEC page number */

/* Unibus Status Register - one per Unibus */

#define UB_UBASTA 0763100	/* UBA Status Register (One per Unibus) */
/*				[R=Read, W=Write, C=Cleared by writing a 1,
				 *=Cleared by any write]
*/
#define UBA_BTIM 0400000	/* 2.9 Unibus timeout [R/C] */
#define UBA_BBAD 0200000	/* 2.8 Bad mem data (on NPR transfer) [R/C] */
/*			   (Master will timeout instead if UBA_BDXF set) */
#define UBA_BPAR 0100000	/* 2.7 KS10 bus parity error [R/C] */
#define UBA_BNXD 0040000	/* 2.6 CPU addressed non-ex device [R/C] */
#define UBA_BHIG 0004000	/* 2.3 Intrpt req on BR7 or BR6 (high) [R] */
#define UBA_BLOW 0002000	/* 2.2 Intrpt req on BR5 or BR4 (low) [R] */
#define UBA_BPWR 0001000	/* 2.1 Power low [R|*] */
#define UBA_BDXF 0000200	/* 1.8 Disable xfr on uncorrectable data[R/W]*/
#define UBA_BINI 0000100	/* 1.7 Issue Unibus init [W] */
#define UBA_BPIH 0000070	/* 1.6-1.4 PI lev for BR7 or BR6 (hi) [R/W] */
#define UBA_BPIL 0000007	/* 1.3-1.1 PI lev for BR5 or BR4 (low) [R/W] */

#define UB_UBAMNT 0763101	/* UBA Maintenance (One per Unibus) */
/*				  1.2 Spare maintenance bit (?)
				  1.1 Change NPR address (?)
*/

/* Other Unibus devices */

/* KS10 Memory Status Register, controller 0 */
#define UB_KSECCS 0100000	/* Memory Status Register, ctlr 0 */


/* Define internal structure to emulate the above Unibii
*/
extern struct ubctl {
	h10_t ubpmap[UBA_UBALEN];	/* Contents of UBA paging RAM */
	h10_t ubsta;		/* Unibus Status Register */
	int ubn;		/* Unibus # (1 or 3) */
	int ubhilev;		/* "High" PI level bit assignment */
	int ublolev;		/* "Low"  "    "    "     "      */
	int *ubpireqs;		/* Address of place to put UB PI reqs */
	int ubndevs;		/* # devices on bus */
	struct device *ubdevs[KLH10_DEVUBA_MAX+1];	/* Devs on bus */
	dvuadr_t ubdvadrs[KLH10_DEVUBA_MAX*2];		/* Beg and end addrs */
} dvub1, dvub3;

extern void dvub_init(void);
int dvub_add(FILE *, struct device *, int);

extern uint32				/* All reads return 32 bits */
	ub_read (w10_t),		/* (ioaddr) */
	ub_bread(w10_t),		/* (ioaddr) */
	uba_read(struct ubctl *, dvuadr_t);	/* (ub, uaddr) */

extern void				/* All writes take 18 bits */
	ub_write(w10_t, h10_t),		/* (ioaddr, val) */
	ub_bwrite(w10_t, h10_t),	/* (ioaddr, val) */
	uba_write(struct ubctl *, dvuadr_t, h10_t);	/* (ub, uaddr, val) */

extern paddr_t ub1_pivget(int);		/* For PI system */
extern paddr_t ub3_pivget(int);

#endif /* ifndef DVUBA_INCLUDED */
