/* KLH10.H - KLH10 internal clock facility definitions
*/
/* $Id: kn10clk.h,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: kn10clk.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#ifndef KN10CLK_INCLUDED
#define KN10CLK_INCLUDED 1

#ifdef RCSID
 RCSID(kn10clk_h,"$Id: kn10clk.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* Determine desired clock features and implementation mechanism
** for (a) Triggering, (b) Resolution, and (c) Callout Point.
*/

/* Triggering mechanism (implementation), one of:
**	COUNT - Counter bumped by CLOCKPOLL once per executed instruction.
**	OSINT - host-OS interval timer interrupt.
*/
#ifndef KLH10_CLKTRG_OSINT
# define KLH10_CLKTRG_OSINT !IFCLOCKED		/* Use OS ints for iticks */
#endif
#define KLH10_CLKTRG_COUNT !KLH10_CLKTRG_OSINT	/* Use counter for iticks */

/* Clock resolution (feature), one of:
**	ITICK - Same as PDP-10 monitor's interval time.  Callouts restricted
**		to ITICK resolution.
**	SUBITICK - Some finer value; can callout at smaller intervals than
**		ITICK (can also imply more precise longer intervals).
**		Not supported yet.
*/
#define KLH10_CLKRES_ITICK 1			/* Resolution is 1 itick */
#define KLH10_CLKRES_SUBITICK !KLH10_CLKRES_ITICK	/* Sub-itick res */

/* Callout point (feature), one of:
**	SYNCH - callouts delayed until reach between-instr synch point.
**	IMMED - callouts invoked immediately (even within instr or OSINT
**			handler).  Not supported yet.
*/
#define KLH10_CLKXCT_SYNCH 1			/* Synchronized callouts */
#define KLH10_CLKXCT_IMMED !KLH10_CLKXCT_SYNCH	/* Can do immediate callouts */


/* Typedef for scalar variable holding # ticks */
typedef int32 clkval_t;

#define CLKVAL_NEVER ((clkval_t)((~0UL)>>1))	/* Max pos # */

enum clksta {		/* Clock queue entry states */
	CLKENT_ST_FREE,		/* On freelist */
	CLKENT_ST_CTICK,	/* on active CTICK queue (sub-ITICK) */
	CLKENT_ST_ITICK,	/* on active ITICK queue */
	CLKENT_ST_MTICK,	/* on active MTICK queue (multi-ITICK) */
	CLKENT_ST_QUIET,	/* on quiescent CTICK q (sub-ITICK) */
	CLKENT_ST_IQUIET,	/* on quiescent ITICK q */
	CLKENT_ST_MQUIET	/* on quiescent MTICK q (multi-ITICK) */
};

struct clkent {
	struct clkent *cke_next; /* MUST BE FIRST ELEMENT!  See clk_qinsert */
	struct clkent *cke_prev;
	int (*cke_rtn)(void *);	/* Callout function, NULL if entry free */
	void *cke_arg;		/* Argument to function */
	clkval_t cke_ticks;	/* # relative ticks til timed out */

	clkval_t cke_oticks;	/* Original # ticks */
	int32 cke_usec;		/* Original interval in usec */
	enum clksta cke_state;	/* State of entry */
};

typedef struct clkent *clktmr_t;	/* External handle for clock timer */

enum {				/* Return values for callout */
	CLKEVH_RET_KILL,	/* de-register, can forget handle */
	CLKEVH_RET_QUIET,	/* go quiescent, don't reschedule */
	CLKEVH_RET_REPEAT,	/* repeat, using current interval */
	CLKEVH_RET_NOP		/* Do nothing, handler has done something */
};

struct clkregs {
	struct clkent *clk_ctickq;	/* ctick-res queue (doubly-linked) */
	struct clkent *clk_itickq;	/* ITICK-res queue (doubly-linked) */
	struct clkent *clk_itickl;	/* ITICK list     (doubly-linked) */
	struct clkent *clk_quiet;	/* Quiescent list (doubly-linked) */
	struct clkent *clk_free;	/* Freelist (one-way) */

	clkval_t clk_counter;		/* Countdown: cticks of first entry */
	clkval_t clk_ocnt;		/* Original countdown value */
	clkval_t clk_icnter;		/* cticks thus far in interval */
	clkval_t clk_icntval;		/* cticks per itick interval */
	int32 clk_ipms;			/* # instrs per msec (CLKTRG only) */
	int32 clk_ipmsrq;		/* Requested val of ipms (sigh) */
	int32 clk_tickusec;		/* # usec per ctick */
	int32 clk_htickusec;		/* (tickusec/2) for rounding */

	int32 clk_itickusec;		/* # usec per itick interval */
	int32 clk_hitickusec;		/* (itickusec/2) for rounding */
	int32 clk_ithz;			/* Interval timer rate in HZ (#/sec) */
	int32 clk_ithzfix;		/* If set, fixes ITHZ value to use */
	int32 clk_itusfix;		/* Same in usec */
	int32 clk_ithzosreq;		/* Last OS-requested value in HZ */
	int32 clk_ithzcmreq;		/* Cmd-requested val of ITHZ (sigh) */
};
/* Note: The "prev" of first entry on doubly-linked list points back
** to appropriate member above.  Last entry's "next" is NULL.
*/


/* Macros for use by client code that help shield it from knowing
** grubby details about the clock model.
**
**	CLOCKPOLL() carries out a clock ctick update if synchronous.
**	CLK_CTICKS_SINCE_ITICK() # of cticks so far in the current interval.
**	CLK_CTICKS_UNTIL_ITICK() # of cticks left in current interval.
**	CLK_CTICKS_PER_ITICK	 # of cticks in an interval.
**
**	CLK_USEC_SINCE_ITICK()	# of usec so far in the current interval.
**	CLK_USEC_UNTIL_ITICK()	# of usec left in current interval.
**	CLK_USEC_PER_ITICK	# of usec in an interval.
*/
#if IFCLOCKED	/* Synchronous counter model (also assuming ctick==usec) */
# define CLOCKPOLL() if (--cpu.clk.clk_counter <= 0) CLK_TRIGGER()
# define CLK_CTICKS_SINCE_ITICK() (cpu.clk.clk_icnter + \
				(cpu.clk.clk_ocnt - cpu.clk.clk_counter))
# define CLK_CTICKS_UNTIL_ITICK() (cpu.clk.clk_counter)
# define CLK_CTICKS_PER_ITICK     (cpu.clk.clk_icntval)
# define CLK_USEC_SINCE_ITICK() CLK_CTICKS_SINCE_ITICK()
# define CLK_USEC_UNTIL_ITICK() CLK_CTICKS_UNTIL_ITICK()
# define CLK_USECS_PER_ITICK	CLK_CTICKS_PER_ITICK
#else		/* Real-time OSINT model */
# define CLOCKPOLL()
# define CLK_CTICKS_SINCE_ITICK() (0)
# define CLK_CTICKS_UNTIL_ITICK() (0)
# define CLK_CTICKS_PER_ITICK	  (1)
# define CLK_USEC_SINCE_ITICK()	(--ERROR--)
# define CLK_USEC_UNTIL_ITICK()	(--ERROR--)
# define CLK_USECS_PER_ITICK	(--ERROR--)
#endif

/* Common constants */

#define CLK_USECS_PER_SEC ((int32)1000000)
#define CLK_USECS_PER_MSEC ((int32)1000)

/* CLK_TRIGGER - Invoked when clock goes off, cause callouts now or later.
*/
#if !KLH10_CLKXCT_IMMED
# define CLK_TRIGGER() (INTF_SET(cpu.intf_clk), INSBRKSET())
#else
# define CLK_TRIGGER() clk_immtimeout()	/* Execute immediate callouts */
#endif

/* Externally visible routines */

extern void clk_init(void);	/* Initialize clock package */
extern void clk_suspend(void),	/* Suspend & resume clock */
	    clk_resume(void);
extern void clk_idle(void);	/* Idle CPU til next itick or I/O interrupt */
extern void clk_synctimeout(void); /* At synch point, invoke update/callouts */
extern void clk_immtimeout(void);  /* At immed trigger, "      "      "      */

/* CLK_ITICKSET - Set ITICK interval.
**	"usec" is the new number of usec per itick.
*/
extern void clk_itickset(int32 usec);	/* Set interval tick in usec */
extern void clk_ithzset(int hz);	/* Same but in HZ */

/* Set virtual clock speed - instrs per msec.
**	This defines the ratio of virtual usec to instruction cycles.
**	Only meaningful when using KLH10_CLKTRG_COUNT.
**	Default value of 1000 is a 1:1 ratio.
*/
extern void clk_ipmsset(int32 ipms);

/* Get a timer, with ITICK resolution.
**	At *very* roughly "usec" from now, it will invoke a callout
**	that executes "rtn(arg)".
**
*/
extern struct clkent *clk_tmrget(
			int (*rtn)(void *), void *arg, int32 usec);

/* Get a timer, with CTICK resolution.
**	Does not exist unless sub-iticks are supported.
*/
#if KLH10_CLKRES_SUBITICK
  extern struct clkent *clk_ctmrget(
			int (*rtn)(void *), void *arg, int32 usec);
#endif

/* Get a timer that fires every ITICK.
*/
extern struct clkent *clk_itmrget(void (*rtn)(void *), void *arg);

/* Change a timer's interval.
**	Does nothing for every-ITICK timers obtained from clk_itmrget().
*/
extern void clk_tmrset(struct clkent *ce, int32 usec);


/* Make a timer quiescent.
**	Interval and callout info is retained, but timer is removed
**	from clock queues and will never be fired until re-activated.
**	OK if already quiescent.
*/
extern void clk_tmrquiet(struct clkent *ce);

/* Make a timer active again.
**	Put back on appropriate clock queue just like a new timer (ie
**	its interval countdown is restarted from beginning).
**	No-op if timer already active (doesn't change position on queues).
*/
extern void clk_tmractiv(struct clkent *ce);

/* Delete timer.
**	Kills timer whether active or quiescent.
*/
extern void clk_tmrkill(struct clkent *ce);

#endif /* ifndef KN10CLK_INCLUDED */
