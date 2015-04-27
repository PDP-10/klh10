/* KN10CLK.C - KLH10 internal clock facilities
*/
/* $Id: kn10clk.c,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: kn10clk.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include "klh10.h"
#include "osdsup.h"
#include "kn10def.h"	/* This includes kn10clk.h */
#include "kn10clk.h"

#if KLH10_CLKTRG_OSINT
# include "osdsup.h"	/* For os_vtimer */
#endif

#ifdef RCSID
 RCSID(kn10clk_c,"$Id: kn10clk.c,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

#ifndef KLH10_CLK_MAXTIMERS		/* Max # of timer entries */
#  define KLH10_CLK_MAXTIMERS 32
#endif

/*

The KN10CLK code operates on "cticks" which are internal units of a
value that only has meaning within KN10CLK.  Time values outside the
code are in units of microseconds.

	CTICK - smallest time unit CLK knows about.  A CTICK is considered
to be either an ITICK or SUB-ITICK (see below).
Synch clock: one ctick is one "instruction cycle" (counter increment).
Asynch clock: one ctick is one host-OS timer interrupt.

	ITICK - Interval tick, the interval the 10 OS knows about.
Some multiple of CTICK.  If the 10 has no interval in effect, a default
is used in order to continue driving devices (eg 60HZ).

	SUB-ITICK - a CTICK which is smaller than ITICK.  If CTICK and ITICK
are the same interval, then SUB-ITICKs don't exist.


Synchronous clock:
	one ctick == one "instruction cycle" == 1 usec (for now)
	Always running, extremely accurate in virtual world, although
	uncontrollably random in real time.

Real-time clock:
	one ctick == one host-OS timer interval interrupt == N usec,
			where N is variable depending on the setting
			of the interval timer.
	Some minimum/maximum interval?
	Hair when changing interval??
	Simplification: set rate at startup (config),
		then assume either on or off.

Want to avoid frequent re-computations from # usec into # ticks.
Can either:
	(1) Bite bullet, recompute each time.
		Good: will always work.
		Bad: slower each time must recompute.
	(2) Provide time-2-tick facility as macro
		Bad: cannot change at runtime.
		Bad: DLLs screwed if interval changed.
	(3) Provide time-2-tick facility as routine, cache result.
		Bad: cached value wrong if interval changes.
	(4) Register cached values so CLK code can recompute if interval
		changes.
		Bad: more complex, esp for devices.

Facility rules: anything that may use CLK must create a timer and remember
its handle, unless timer handler knows when to kill it (eg for one-shot).

Allow changing or setting interval, given handle.  (Allow this from inside
	handler?  How to tell in handler?  Would need state indicator.
	Can do, but disallow for now.
	How to tell if active (must resched) or quiescent?  Same problem.
	OK if simply disallow changing while active.)
Allow setting either one-shots or periodics.  Clock code will recompute
	values (esp periodics) itself when they change.

Timer event handler can return:
	CLKEVH_RET_KILL - to de-register (good for one-shot).
			Can forget handle.
	CLKEVH_RET_QUIET - to go quiescent (don't resched even if periodic).
	CLKEVH_RET_REPEAT - to repeat, using current interval.
	CLKEVH_RET_NOP	- indicates handler has disposed of timer.

Or can explicitly destroy timer, given handle.  (allow this from inside
	handler? yes, if return NOP.)

CLK will take care of updating all tick values within timers, including
quiescent ones.

Different clock entry queue lists:

CTICK QUEUE: (core clock queue)
	Doubly-linked list of active clock entries, completely general.
	They are linked in the order they should time out, with each
	separated from the next by a relative ctick value.
	State CLKENT_ST_CTICK.

MTICK QUEUE: (multiple-ITICK queue)
	Doubly-linked list of "MTICK" entries, which have a tick resolution
	of ITICK rather than CTICK.
	State CLKENT_ST_MTICK.

ITICK LIST: (every-ITICK)
	Doubly-linked list of "ITICK" entries, all of which are executed
	every time an ITICK is fired.
	State CLKENT_ST_ITICK.

QUIET LIST:
	Doubly-linked list of "quiet" entries, which facility user wants
	to keep around to help cut down overhead.
	State CLKENT_ST_QUIET, IQUIET, or MQUIET depending on type of
	entry (ctick-queue, itick-list, or mtick-queue).

FREE LIST:
	Singly-linked list of free entries.
	State CLKENT_ST_FREE.


Need to decide:
	Should ACTIVE drive ITICK, or other way around?

    ACTIVE->ITICK:
		- General, allows full flexibility
		- Problem: how to determine current progress towards ITICK
			for code that eg reads interval counter?  Can't just
			do countval-counter cuz those vals are different
			depending on current head of ACTIVE queue.
		Solution: maintain and update countval each time ACTIVE
			goes off.
		- Problem: How often should asynch mode trigger OS int?
			Want to minimize this (ie encourage use of ITICK).
			But if exact time is important...?  Punt for now,
			have asynch use ITICK.
	Setup:
		Put clk_itimeout() as entry in active queue, with
			period of one interval.
			Synch: set CLOCK_POLLED to bump intf_clk/INSBRK.
			Asynch: Set os_vtimer to bump intf_clk/INSBRK
		intf_clk test in apr_check should call clk_timeout.


	BIG ISSUE with ACTIVE->ITICK is whether ACTIVE entries should be fired
	on the spot, or deferred until apr_check() is called to provide
	a synchronization point.
	- Probably OK to fire immediately for synch mode cuz can only
	happen at places where CLOCKPOLL is explicitly invoked.
	- Unclear what to do for asynch interrupts.  
	
    ITICK->ACTIVE:
		- Requires ctick == itick for synch model, not just asynch.
		- Probably slightly less overhead.
		- CPU code invokes clkq processing only if needed.
	Setup:
		Put clk_timeout() as entry in ITICK queue.
			Synch: set CLOCK_POLLED to bump intf_clk/INSBRK.
			Asynch: Set os_vtimer to bump intf_clk/INSBRK.
		intf_clk test in apr_check should call clk_itimeout.


To recap, four possible modes of operation:

SYNCH_ITICK:
	Driven by ITICK.  Interval set by 10, or defaults to 60Hz.
	CLOCKPOLL() bumps intf_clk, and INSBRK to abort and get to apr_check.
	Go to "sync:" below.

SYNCH_CTICK:
	Driven by ACTIVE.
	CLOCKPOLL() invokes clk_trigger().
	If immed callouts supported:
		- invokes clk_timeout for immed entries.
		- If any synch entries, tickles SYNCH (bumps intf_clk & INSBRK)
			(note this could be an entry on immed list)
	If no immed callouts:
		- tickles SYNCH (bumps intf_clk & INSBRK)
	Go to "sync:" below.

OSINT_ITICK:
	Same as below, with optimizations to ignore sub-itick checks.

OSINT_CTICK:
	Driven by ACTIVE. Interval set by 10, or defaults to 60Hz.
		Optional: use smaller interval, to approximate true CTICKs?
	OS signal invokes clk_osint() at given real-time interval.
	clk_osint() bumps intf_clk, and INSBRK to abort and get to apr_check.
		[Do NOT execute immediate callouts at interrupt level!]
  sync:	apr_check notices intf_clk and calls clk_synctimeout().
	clk_synctimeout() bumps ACTIVE counter, calls clk_timeout() if needed
		(takes care of things with shorter or longer period than ITICK)
	Then invokes clk_itimeout(), either via explicit check or via entry
		in ACTIVE list.
		If sub-iticks not supported, then always invokes clk_itimeout.

*/

/*
	Another way of looking at clock features, from viewpoint of facility
user:

CALLOUT POINT: (feature)

    IMMEDIATE   - Callouts invoked immediately (within instr or OSINT handler).
    SYNCHRONIZED - delayed until reach between-instr synch point.

RESOLUTION: (feature)

    ITICK - Restricted to ITICK resolution (1 or N iticks)
    SUB-ITICK - Can callout at smaller intervals than ITICK (can also imply
		more-precise longer intervals).

TRIGGER MECHANISM: (implementation)

    COUNT - from CLOCKPOLL() counting down some number of cycle ticks.
    OSINT - from clk_osint() invoked by OS interval timer signal.


Ideally all of these could be specified independently for each
particular callout, and in theory this is possible.  However, in
practice some restrictions make life simpler:

COUNT mechanism:
	Completely flexible.  CLOCKPOLL invokes clk_trigger which checks
	for immediate sub-itick callouts and invokes them.  One of these
	can be the SYNCH tickler, or clk_trigger can automatically tickle SYNCH
	if any synch callouts are ready.
	SYNCH point will invoke both itick and synch-itick queues.

OSINT mechanism:
	- No SUB-ITICKs.  Can only do sub-iticks if OSINT interval
	is smaller than ITICK, which is tough.  Punt this for now.
	- No IMMEDIATE callouts.  In a single-threaded environment
	these would be invoked from the signal handler, which can find
	things in weird states.  Would have to suppress signals during critical
	code sections; too much overhead.

For debugging purposes, it would be best if COUNT and CLOCK used the same
mechanism.  Thus, the initially supported features are:

	
	Callout: SYNCHRONIZED
	Resolution: ITICK
	Trigger: COUNT or OSINT
	

*/

/* Convert various units to and from clkval ticks.
**	One tick corresponds either to one instruction, or to
**	one OS interval interrupt, depending on compile-time configuration.
** Always returns at least one tick, even if arg is zero.
*/

#define clk_msec2clk(ms) clk_usec2clk((ms)*1000)

static clkval_t
clk_usec2clk(int32 usec)
{
    register clkval_t val;
#if KLH10_CLKTRG_COUNT
    /* cticks are instr cycles.  Speed is defined by # of cycles per
    ** virtual msec; to find # of cycles in N usec, we do
    **		<N usec> * <cycles per usec>
    ** which is
    **		<N usec> * (<ipms> / 1000)
    ** which is rearranged as below to avoid integer roundoff error.
    */
    val = (usec * cpu.clk.clk_ipms) / 1000;
#elif KLH10_CLKTRG_OSINT
    val = ((usec + cpu.clk.clk_htickusec) / cpu.clk.clk_tickusec);
#endif
    return val ? val : 1;
}

static int32
clk_clk2usec(clkval_t clk)
{
#if KLH10_CLKTRG_COUNT
    /* Inverse of computation for usec2clk */
    return  (clk * 1000) / cpu.clk.clk_ipms;
#elif KLH10_CLKTRG_OSINT
    return clk * cpu.clk.clk_tickusec;
#endif
}


static clkval_t
clk_usec2tick(int32 usec)
{
    register clkval_t val;
    val = ((usec + cpu.clk.clk_hitickusec) / cpu.clk.clk_itickusec);
    return val ? val : 1;
}

/* Auxiliary macros and declarations */

/* Remove from any doubly-linked queue */
#define clk_2ldelete(e) \
	if ((e)->cke_prev->cke_next = (e)->cke_next) \
	    (e)->cke_next->cke_prev = (e)->cke_prev

/* Remove from a clock queue, updating relative ticks */
#define clk_qdelete(e) \
	if ((e)->cke_prev->cke_next = (e)->cke_next)		\
	    ((e)->cke_next->cke_ticks += (e)->cke_ticks),	\
	    (e)->cke_next->cke_prev = (e)->cke_prev


/* Clock queue entries (timers)
*/
static struct clkent clkenttab[KLH10_CLK_MAXTIMERS];

static void clk_stinsert(struct clkent *ce);
static void clk_stdelete(struct clkent *ce);
static void clk_qinsert(struct clkent *ce, struct clkent **qh);
static void clk_osint(void);
static void clk_itusset(int32 usec);


void
clk_init(void)
{
    register int i = sizeof(clkenttab)/sizeof(clkenttab[0]);
    register struct clkent *ce;

    for (ce = clkenttab; --i >= 0; ++ce) {
	ce->cke_state = CLKENT_ST_FREE;
	ce->cke_next = i ? ce+1 : NULL;
    }

    cpu.clk.clk_free = clkenttab;
    cpu.clk.clk_ctickq = NULL;
    cpu.clk.clk_itickq = NULL;
    cpu.clk.clk_itickl = NULL;
    cpu.clk.clk_quiet = NULL;

#if KLH10_CLKTRG_OSINT
    /* Currently OSINT mode must always have a default itick interval;
    ** no provision for turning timer completely off if nothing needs it.
    ** Use 1/30 sec if the compile-time default is "let PDP-10 set it", since
    ** we don't really need more resolution internally and the 10 will
    ** almost certainly be setting it to 60 or 1000 after booting.
    **
    ** Someday try to make this dynamic by determining whether we're
    ** running on a fast or slow machine??
    */
    if (cpu.clk.clk_ithzfix = KLH10_CLK_ITHZFIX) {
	cpu.clk.clk_ithzcmreq = cpu.clk.clk_ithzfix;
	cpu.clk.clk_itusfix = CLK_USECS_PER_SEC / cpu.clk.clk_ithzfix;
    } else {
	/* PDP-10 will pick it later; use 1/30 til then */
	cpu.clk.clk_ithzcmreq = 30;
	cpu.clk.clk_itusfix = 0;
    }

    cpu.clk.clk_ithzosreq = 0;			/* No OS request yet */
    cpu.clk.clk_ithz = cpu.clk.clk_ithzcmreq;
    cpu.clk.clk_itickusec = CLK_USECS_PER_SEC / cpu.clk.clk_ithz;
    cpu.clk.clk_hitickusec = cpu.clk.clk_itickusec / 2;

    /* CTICK == ITICK */
    cpu.clk.clk_tickusec = cpu.clk.clk_itickusec;
    cpu.clk.clk_htickusec = cpu.clk.clk_hitickusec;
    cpu.clk.clk_icntval = 1;
    cpu.clk.clk_icnter = 0;

    /* Don't start the OS interval timer right now; clk_resume() will do
    ** that for us when the CPU is started.
    */

#elif KLH10_CLKTRG_COUNT

    cpu.clk.clk_ipms = 1000;		/* Default 1:1 instrs:usec */
    cpu.clk.clk_ipmsrq = cpu.clk.clk_ipms;

    /* Set up ctick stuff */
    cpu.clk.clk_counter = cpu.clk.clk_ocnt = CLKVAL_NEVER;

    cpu.clk.clk_itickusec = 0;		/* No interval defined yet */
    cpu.clk.clk_icntval = 0;
    cpu.clk.clk_icnter = 0;
#endif
}

/* CLK_SUSPEND and CLK_RESUME
**	These are for temporarily halting internal clock interrupts and
**	timeout processing.
**	Because these are currently only invoked when starting or stopping
**	the PDP-10, it's easiest to simply turn the OS timer on or off
**	completely.  Note this also avoids having a SIGALRM always awaiting
**	the CPU when execution is continued!
*/
void
clk_suspend(void)
{
#if KLH10_CLKTRG_OSINT
    os_vtimer((ossighandler_t *)clk_osint, (uint32)0);
#endif
}
void
clk_resume(void)
{
#if KLH10_CLKTRG_OSINT
    os_vtimer((ossighandler_t *)clk_osint, cpu.clk.clk_itickusec);
#endif
}

/* CLK_IDLE - Called to go into an OS-dependent process idle state until
**	some PDP-10 interrupt happens.
**	This is intended to be called as a result of some special instruction
**	inserted into the PDP-10 OS null job.
**
** If doing synchronous counting, attempt to translate the remaining count
**	into some amount of real time, and sleep on that.
** If doing virtual-time OS interrupts, must determine approx amount of real
**	time left, and sleep on that.
** If doing real-time OS interrupts, just pause.
** If can't do anything or figure out what to do, it's OK to just do nothing
**	and return.  Worst that will happen is the platform spins and wastes
**	some CPU.
*/
void
clk_idle(void)
{
#if KLH10_CLKTRG_COUNT
    uint32 usec;

    /* Figure usec to next synchronized interrupt */
    usec = clk_clk2usec(CLK_CTICKS_UNTIL_ITICK());

    if (usec <= 100)		/* Arbitrary cutoff for now */
	return;			/* Not enough time, don't bother */

    /* Sleep until:
    **	timeout of usec (all gone, restart interval)
    **	or some other interrupt (some left, adjust interval)
    */
	/* KLH: NEEDS IMPLEMENTATION HERE */

#elif KLH10_CLKTRG_OSINT

    os_v2rt_idle((ossighandler_t *)clk_osint);

#endif
}

/* CLK_OSINT - Interrupt routine invoked on each real-time interval interrupt
*/
static void
clk_osint(void)
{
    CLK_TRIGGER();	/* For now, macro that triggers synch call */
}


/* CLK_SYNCTIMEOUT - called from synchronization point to invoke clock timeout.
**	If no sub-iticks supported, always invokes clk_itimeout functionality.
*/

void
clk_synctimeout(void)
{
    register struct clkent *ce, *nce;

    /* If doing sub-itick resolution, this is where the sub-itick callouts
    ** should be done.
    */
#if KLH10_CLKRES_SUBITICK
# if KLH10_CLKTRG_COUNT
    /* Update cticks thus far by original # of cticks since last
    ** setting of counter.
    */
    cpu.clk.clk_icnter += cpu.clk.clk_ocnt;
# endif

    if (ce = cpu.clk.clk_ctickq) {
	ce->cke_ticks = 0;		/* Canonicalize sub-itick counter */

	do {
	    nce = ce->cke_next;
	    switch ((*(ce->cke_rtn))(ce->cke_arg)) {
	    default:
		/* panic?? */

	    case CLKEVH_RET_NOP:	/* Do nothing (handler hacked self) */
		break;

	    case CLKEVH_RET_KILL:	/* Kill, put on freelist */
		if (ce->cke_prev->cke_next = nce)	/* Take off queue */
		    nce->cke_prev = ce->cke_prev;
		ce->cke_state = CLKENT_ST_FREE;		/* Add to freelist */
		ce->cke_next = cpu.clk.clk_free;
		cpu.clk.clk_free = ce;
		break;

	    case CLKEVH_RET_QUIET:	/* Go quiescent */
		if (ce->cke_prev->cke_next = nce)	/* Take off queue */
		    nce->cke_prev = ce->cke_prev;
		ce->cke_state = CLKENT_ST_QUIET;	/* Add to quiet list */
		ce->cke_prev = (struct clkent *)&cpu.clk.clk_quiet;
		if (ce->cke_next = cpu.clk.clk_quiet)
		    ce->cke_next->cke_prev = ce;
		cpu.clk.clk_quiet = ce;
		break;

	    case CLKEVH_RET_REPEAT:	/* Put back on queue */
		if (nce) {		/* If other entries exist, */
		    if (ce->cke_prev->cke_next = nce)	/* Take off queue */
			nce->cke_prev = ce->cke_prev;
		    clk_stinsert(ce);	/* Put back */
		} else {
		    ce->cke_ticks = ce->cke_oticks;
		}
		break;

	    }
	} while ((ce = nce) && ce->cke_ticks <= 0);

	cpu.clk.clk_counter = cpu.clk.clk_ocnt
			= (ce ? ce->cke_ticks : CLKVAL_NEVER);
    } else {
	/* No queue entries, need to turn clock off??? */
	/* This actually shouldn't happen, but... */
	cpu.clk.clk_counter = cpu.clk.clk_ocnt = CLKVAL_NEVER;
    }

    /* If haven't reached an ITICK boundary yet, return now.
    ** Else drop thru to handle ITICK stuff
    */
#endif /* KLH10_CLKRES_SUBITICK */

#if KLH10_CLKRES_ITICK
# if KLH10_CLKTRG_COUNT
    /* Reset ctick counter to use a full tick */
    cpu.clk.clk_counter = cpu.clk.clk_ocnt = cpu.clk.clk_icntval;
    cpu.clk.clk_icnter = 0;
# endif

    /* Now check for special ITICK callouts */
    if (ce = cpu.clk.clk_itickl) {
	do {
	    /* ITICK callouts don't return values and are actually of type void
	    ** instead of int, hence funct pointer must be cast.
	    ** These callouts must be flushed
	    ** explicitly with clk_tmrkill() rather than by returning
	    ** CLKEVH_RET_KILL.
	    */
	    nce = ce->cke_next;
	    (*(void (*)(void *))(ce->cke_rtn)) (ce->cke_arg);
	} while (ce = nce);
    }
#endif /* KLH10_CLKRES_ITICK */

    /* Now check for callouts of ITICK resolution which aren't ITICKs */
    if ((ce = cpu.clk.clk_itickq)
      && --(ce->cke_ticks) <= 0) {
	do {
	    nce = ce->cke_next;
	    switch ((*(ce->cke_rtn))(ce->cke_arg)) {
	    default:
		/* panic?? */

	    case CLKEVH_RET_NOP:	/* Do nothing (handler hacked self) */
		break;

	    case CLKEVH_RET_KILL:	/* Kill, put on freelist */
		if (ce->cke_prev->cke_next = nce)	/* Take off queue */
		    nce->cke_prev = ce->cke_prev;
		ce->cke_state = CLKENT_ST_FREE;		/* Add to freelist */
		ce->cke_next = cpu.clk.clk_free;
		cpu.clk.clk_free = ce;
		break;

	    case CLKEVH_RET_QUIET:	/* Go quiescent */
		if (ce->cke_prev->cke_next = nce)	/* Take off queue */
		    nce->cke_prev = ce->cke_prev;
		ce->cke_state = CLKENT_ST_MQUIET;	/* Add to quiet list */
		ce->cke_prev = (struct clkent *)&cpu.clk.clk_quiet;
		if (ce->cke_next = cpu.clk.clk_quiet)
		    ce->cke_next->cke_prev = ce;
		cpu.clk.clk_quiet = ce;
		break;

	    case CLKEVH_RET_REPEAT:	/* Put back on queue */
		if (nce) {		/* If other entries exist, */
		    if (ce->cke_prev->cke_next = nce)	/* Take off queue */
			nce->cke_prev = ce->cke_prev;
		    clk_qinsert(ce, &cpu.clk.clk_itickq); /* Put back */
		} else {
		    ce->cke_ticks = ce->cke_oticks;
		}
		break;

	    }
	} while ((ce = nce) && ce->cke_ticks <= 0);
    }
}


#if 0	/* Now part of clk_synctimeout!! */

/* Invoke interval timeout, execute everything on ITICK list.
*/
void
clk_itimeout()
{
}
#endif /* 0 */



/* CLK_IMMTIMEOUT - invoked by OSINT or COUNT when clock-trigger event
**	happens, to check for and execute any *immediate* callouts.
**	Not yet implemented.
*/
void
clk_immtimeout(void)
{
}


/* Routines to get a timer (clock callout entry)
**	clk_tmrget(rtn, arg, usec)	- General-purpose timer (ITICK res)
**	clk_ctmrget(rtn, arg, usec)	- Sub-itick timer (CTICK res)
**	clk_itmrget(rtn, arg)		- Permanent ITICK-interval 
*/

struct clkent *
clk_tmrget(int (*rtn)(void *), void *arg, int32 usec)
{
    register struct clkent *ce;

    if (ce = cpu.clk.clk_free) {
	cpu.clk.clk_free = ce->cke_next;
	ce->cke_state = CLKENT_ST_MTICK;
	ce->cke_rtn = rtn;
	ce->cke_arg = arg;
	ce->cke_usec = usec;
	ce->cke_oticks = clk_usec2tick(usec);	/* Convert time to ITICKS */
	clk_qinsert(ce, &cpu.clk.clk_itickq);
    }
    return ce;
}

#if KLH10_CLKRES_SUBITICK

struct clkent *
clk_ctmrget(int (*rtn)(void *), void *arg, int32 usec)
{
    register struct clkent *ce;

    if (ce = cpu.clk.clk_free) {
	cpu.clk.clk_free = ce->cke_next;
	ce->cke_state = CLKENT_ST_CTICK;
	ce->cke_rtn = rtn;
	ce->cke_arg = arg;
	ce->cke_usec = usec;
	ce->cke_oticks = clk_usec2clk(usec);	/* Convert time to CTICKS */
	clk_stinsert(ce);		/* Insert */
    }
    return ce;
}
#endif

/* As above, but specifically intended to time out every interval tick, and
** returned value of callout routine is ignored.
*/
struct clkent *
clk_itmrget(void (*rtn)(void *), void *arg)
{
    register struct clkent *ce;

    if (ce = cpu.clk.clk_free) {		/* Get a free entry */
	cpu.clk.clk_free = ce->cke_next;
	ce->cke_rtn = (int (*)(void *)) rtn;	/* Set up its value */
	ce->cke_arg = arg;

	/* Now add to the itick list */
	ce->cke_state = CLKENT_ST_ITICK;
	ce->cke_prev = (struct clkent *)&cpu.clk.clk_itickl;
	if (ce->cke_next = cpu.clk.clk_itickl)
	    ce->cke_next->cke_prev = ce;
	cpu.clk.clk_itickl = ce;
    }
    return ce;
}


/* Set a timer interval, using quiescent timer */

void
clk_tmrset(register struct clkent *ce, int32 usec)
{
    if (ce) switch (ce->cke_state) {
    default:
	/* Shouldn't happen; panic? */
	return;

    case CLKENT_ST_IQUIET:	/* Cannot change interval of these */
    case CLKENT_ST_ITICK:
	return;

    case CLKENT_ST_MQUIET:
	ce->cke_usec = usec;
	ce->cke_oticks = clk_usec2tick(usec);	/* ITICK units */
	break;

    case CLKENT_ST_QUIET:
	ce->cke_usec = usec;
	ce->cke_oticks = clk_usec2clk(usec);	/* CTICK units */
	break;

    case CLKENT_ST_CTICK:
	clk_stdelete(ce);	/* Take off sub-itick queue */
	ce->cke_usec = usec;	/* Set new time values */
	ce->cke_oticks = clk_usec2clk(usec);	/* Convert to sub-iticks */
	clk_stinsert(ce);	/* Insert back into sub-itick queue */
	break;

    case CLKENT_ST_MTICK:
	clk_qdelete(ce);	/* Take off mtick queue list */
	ce->cke_usec = usec;	/* Set new time values */
	ce->cke_oticks = clk_usec2tick(usec);	/* Convert to iticks */
	clk_qinsert(ce, &cpu.clk.clk_itickq);	/* Insert back into queue */
	break;
    }
}

/* Make a timer quiescent.
*/
void
clk_tmrquiet(register struct clkent *ce)
{
    if (ce) switch (ce->cke_state) {
    default:
	/* Shouldn't happen; panic? */
	return;

    case CLKENT_ST_IQUIET:		/* Already quiet */
    case CLKENT_ST_MQUIET:
    case CLKENT_ST_QUIET:
	return;

    case CLKENT_ST_MTICK:		/* Active on ITICK queue */
	clk_qdelete(ce);		/* Take off its list */
	ce->cke_state = CLKENT_ST_MQUIET;
	break;

    case CLKENT_ST_CTICK:		/* Active on CTICK queue */
	clk_stdelete(ce);		/* Take off its list */
	ce->cke_state = CLKENT_ST_QUIET;
	break;

    case CLKENT_ST_ITICK:
	clk_2ldelete(ce);		/* Take off its list */
	ce->cke_state = CLKENT_ST_IQUIET;
	break;
    }

    /* Add to quiet list */
    ce->cke_prev = (struct clkent *)&cpu.clk.clk_quiet;
    if (ce->cke_next = cpu.clk.clk_quiet)
	ce->cke_next->cke_prev = ce;
    cpu.clk.clk_quiet = ce;
}


/* Activate a quiescent timer.
*/
void
clk_tmractiv(register struct clkent *ce)
{
    if (ce) switch (ce->cke_state) {
    default:
	/* Shouldn't happen; panic? */
	return;

    case CLKENT_ST_IQUIET:
	clk_2ldelete(ce);		/* Take off its list */
	ce->cke_state = CLKENT_ST_ITICK;

	/* Add onto ITICK list */
	ce->cke_prev = (struct clkent *)&cpu.clk.clk_itickl;
	if (ce->cke_next = cpu.clk.clk_itickl)
	    ce->cke_next->cke_prev = ce;
	cpu.clk.clk_itickl = ce;
	break;

    case CLKENT_ST_MQUIET:
	clk_2ldelete(ce);		/* Take off its list */
	ce->cke_state = CLKENT_ST_MTICK;
	clk_qinsert(ce, &cpu.clk.clk_itickq);	/* add onto ITICK queue */
	break;

    case CLKENT_ST_QUIET:
	clk_2ldelete(ce);		/* Take off its list */
	ce->cke_state = CLKENT_ST_CTICK;
	clk_qinsert(ce, &cpu.clk.clk_ctickq);	/* add onto CTICK queue */
	break;

    case CLKENT_ST_CTICK:		/* Already active */
    case CLKENT_ST_MTICK:
    case CLKENT_ST_ITICK:
	break;
    }
}


/* Delete timer given handle */

void
clk_tmrkill(register struct clkent *ce)
{

    if (!ce)
	return;
    switch (ce->cke_state) {
    case CLKENT_ST_FREE:
	return;

    case CLKENT_ST_CTICK:
	clk_stdelete(ce);
	break;

    case CLKENT_ST_MTICK:
	clk_qdelete(ce);		/* Take off clock queue */
	break;

    case CLKENT_ST_QUIET:
    case CLKENT_ST_IQUIET:
    case CLKENT_ST_MQUIET:
    case CLKENT_ST_ITICK:
	clk_2ldelete(ce);		/* Take off its list */
	break;
    }

    /* Put entry on freelist */
    ce->cke_state = CLKENT_ST_FREE;
    ce->cke_next = cpu.clk.clk_free;
    cpu.clk.clk_free = ce;
}


/* Routines to manage active clock queue */


/* Insert entry into appropriate place in active queue.
 * NOTE!  For speed this uses a type punning hack - it works
 * as long as cke_next is the FIRST element in a clkent.
 */
static void
clk_qinsert(register struct clkent *ce,
	    struct clkent **qh)
{
    register struct clkent *pce = (struct clkent *)qh;
    register struct clkent *nce;
    register clkval_t tim = ce->cke_oticks;

    for (nce = pce; nce = nce->cke_next; pce = nce) {
	if ((tim -= nce->cke_ticks) < 0) {
	    /* Win, must put entry ahead of this one */
	    tim += nce->cke_ticks;	/* Restore time */
	    nce->cke_ticks -= tim;	/* and adjust next entry */
	    break;
	}
    }

    /* Found place, put entry between PCE and NCE */
    ce->cke_ticks = tim;
    ce->cke_prev = pce;
    pce->cke_next = ce;
    if (ce->cke_next = nce)
	nce->cke_prev = ce;
}



/* Insert entry into sub-itick queue.
**	Needs special hackery to update separate sub-itick counter.
*/
static void
clk_stinsert(register struct clkent *ce)
{

    /* Canonicalize sub-tick counter state before trying to insert */
    if (cpu.clk.clk_ctickq) {
	cpu.clk.clk_ctickq->cke_ticks = cpu.clk.clk_counter;
	clk_qinsert(ce, &cpu.clk.clk_ctickq);
    } else {
	/* Optimize case where queue was empty */
	ce->cke_prev = (struct clkent *)&cpu.clk.clk_ctickq;
	ce->cke_next = NULL;
	cpu.clk.clk_ctickq = ce;
    }
    cpu.clk.clk_counter = cpu.clk.clk_ocnt	/* Update counter state */
		    = cpu.clk.clk_ctickq->cke_ticks;
}

/* Delete entry from sub-itick queue.
**	Needs special hackery to update separate sub-itick counter.
*/
static void
clk_stdelete(register struct clkent *ce)
{

    if (ce == cpu.clk.clk_ctickq) {
	cpu.clk.clk_ctickq->cke_ticks = cpu.clk.clk_counter;
	clk_qdelete(ce);
	if (cpu.clk.clk_ctickq)
	    cpu.clk.clk_counter = cpu.clk.clk_ocnt
		    = cpu.clk.clk_ctickq->cke_ticks;
    } else
	/* Not at head, can just do simple call */
	clk_qdelete(ce);		/* Take off clock queue */
}


#if 0	/* Not used yet (maybe never) */

/* Adjust all tick counters.
**	Something's changed in the way ticks are computed, so recompute
**	them all.
**	"usec" is the new number of usec per ctick.
*/
static void
clk_ctickset(int32 usec)
{
    register struct clkent *ce;
    register clkval_t clkval;
    register int32 cntusec;

    if (usec == cpu.clk.clk_tickusec)
	return;				/* No change, no op */

    /* Do active clock queue.  First entry needs
    ** special-casing since actual tick counter is in clk struct.
    */
    if (ce = cpu.clk.clk_ctickq) {

	/* Reverse-compute current countdown back to usec, so can then
	** convert to new value in ticks.
	*/
	cntusec = cpu.clk.clk_counter * cpu.clk.clk_tickusec;
	cpu.clk.clk_tickusec = usec;		/* OK to set new value now */
	cpu.clk.clk_htickusec = usec>>2;	/* Get half-tick for round */
	cpu.clk.clk_counter = clkval = clk_usec2clk(cntusec);
	cpu.clk.clk_ocnt = clkval;

	ce->cke_ticks = clkval;			/* Update 1st entry */
	ce->cke_oticks = clk_usec2clk(ce->cke_usec);

	/* For each remaining entry, convert ticks, then update
	** relative count.
	*/
	while (ce = ce->cke_next) {
	    ce->cke_oticks = clk_usec2clk(ce->cke_usec);
	    if ((ce->cke_ticks = ce->cke_oticks - clkval) < 0)
		ce->cke_ticks = 0;
	    clkval += ce->cke_ticks;
	}

    } else {
	cpu.clk.clk_tickusec = usec;		/* OK to set new value now */
	cpu.clk.clk_htickusec = usec>>2;	/* Get half-tick for round */
	cpu.clk.clk_counter = cpu.clk.clk_ocnt = CLKVAL_NEVER;
    }

    /* Finally do quiescent list. */
    for (ce = cpu.clk.clk_quiet; ce; ce = ce->cke_next) {
	switch (ce->cke_state) {
	case CLKENT_ST_QUIET:
	    ce->cke_oticks = clk_usec2clk(ce->cke_usec);
	    break;
	case CLKENT_ST_MQUIET:
	    ce->cke_oticks = clk_usec2tick(ce->cke_usec);
	    break;
	}
    }
}

#endif /* 0 */


/* CLK_ITHZSET - Set ITICK interval, using HZ instead of usec.
**	This function is currently invoked only by user command when
**	either the current or fixed interval in HZ is changed.
**	"hz" is the desired number of iticks per second.
**	Perhaps 0 could turn clock off (similar to clk_suspend but
**	in a different way).
*/
void
clk_ithzset(int hz)
{
    int32 usec;

    /* Sanity check, don't go too high or low */
    if (cpu.clk.clk_ithzfix) {		/* Force to this if set */
	hz = cpu.clk.clk_ithzfix;
    } else if (hz <= 0) {
	hz = 1;
    } else if (hz > 1000) {		/* 1 ms is max ever */
	hz = 1000;
    } 
    usec = (CLK_USECS_PER_SEC + (hz/2)) / hz;	/* Compute interval */

    /* Remember or clear fixed usec */
    cpu.clk.clk_itusfix = (cpu.clk.clk_ithzfix ? usec : 0);

    if (usec != cpu.clk.clk_itickusec)	/* If changing interval, */
	clk_itusset(usec);		/* Do it */
}

/* CLK_ITICKSET - Set ITICK interval.
**	This is the function called by any PDP-10 OS instructions.
**
**	"usec" is the new number of usec per itick.
*/
void
clk_itickset(int32 usec)
{
    cpu.clk.clk_ithzosreq = CLK_USECS_PER_SEC / usec;	/* Remember last req */

    if (!cpu.clk.clk_ithzfix			/* Change allowed? */
      && (usec != cpu.clk.clk_itickusec))	/* And new setting? */
	clk_itusset(usec);			/* Do it */
}

/* CLK_ITUSSET - Set ITICK interval in usec.
**
**	"usec" is the new number of usec per itick.
*/
static void
clk_itusset(int32 usec)
{
    register struct clkent *ce;
    register clkval_t clkval;

#if KLH10_CLKRES_ITICK
# if KLH10_CLKTRG_COUNT
    register int32 cntusec;

    /* Preserve current interval countdown by up-converting to usec */
    cntusec = clk_clk2usec(CLK_CTICKS_SINCE_ITICK());
# endif

    /* Set new itick interval */
    cpu.clk.clk_itickusec = usec;		/* Define new interval */
    cpu.clk.clk_hitickusec = usec >> 1;
    cpu.clk.clk_icntval = clk_usec2clk(usec);	/* Find in # cticks */
    cpu.clk.clk_ithz = cpu.clk.clk_ithzcmreq	/* Find in HZ */
		= (CLK_USECS_PER_SEC + (usec/2)) / usec;
#endif /* KLH10_CLKRES_ITICK */

    /* Update itick clock queue.
    ** For each entry, convert ticks, then update relative count.
    */
    clkval = 0;
    for (ce = cpu.clk.clk_itickq; ce; ce = ce->cke_next) {
	ce->cke_oticks = clk_usec2tick(ce->cke_usec);
	if ((ce->cke_ticks = ce->cke_oticks - clkval) < 0)
	    ce->cke_ticks = 0;
	clkval += ce->cke_ticks;
    }

    /* Update quiescent list - do all entries that might go back
    ** on itick queue later.
    */
    for (ce = cpu.clk.clk_quiet; ce; ce = ce->cke_next) {
	switch (ce->cke_state) {
	case CLKENT_ST_MQUIET:
	    ce->cke_oticks = clk_usec2tick(ce->cke_usec);
	    break;
	}
    }

#if KLH10_CLKRES_ITICK
# if KLH10_CLKTRG_OSINT
    /* Tell OS about new interval! */
    os_vtimer((ossighandler_t *)clk_osint, cpu.clk.clk_itickusec);

# elif KLH10_CLKTRG_COUNT
    /* Now restore state of interval countdown */
    cpu.clk.clk_icnter = clk_usec2clk(cntusec);	/* Find # cticks done so far */
    cpu.clk.clk_ocnt = cpu.clk.clk_icntval;	/* Assume started with itick */
    cpu.clk.clk_counter =
		cpu.clk.clk_ocnt - cpu.clk.clk_icnter;
# endif
#endif /* KLH10_CLKRES_ITICK */
}

#if KLH10_CLKTRG_COUNT

/* Set virtual clock speed - instrs per msec.
**	This defines the ratio of virtual usec to instruction cycles.
**	Only meaningful when using KLH10_CLKTRG_COUNT.
**	Default value of 1000 is a 1:1 ratio.
*/
void
clk_ipmsset(int32 ipms)
{
    register int32 cntusec;

    cpu.clk.clk_ipmsrq = ipms;
    if (ipms == cpu.clk.clk_ipms)
	return;				/* No change, no op */

    /* Preserve current interval countdown by up-converting to usec */
    cntusec = clk_clk2usec(CLK_CTICKS_SINCE_ITICK());

    /* Set new ctick definition */
    cpu.clk.clk_ipms = ipms;

    /* Set new itick interval, based on new ctick definition.
    ** Note time in terms of virtual usec does
    ** not change, just our definition of what a usec is.
    */
    cpu.clk.clk_icntval = clk_usec2clk(cpu.clk.clk_itickusec);


    /* Now restore state of interval countdown */
    cpu.clk.clk_icnter = clk_usec2clk(cntusec);	/* Find # cticks done so far */
    cpu.clk.clk_ocnt = cpu.clk.clk_icntval;	/* Assume started with itick */
    cpu.clk.clk_counter =
		cpu.clk.clk_ocnt - cpu.clk.clk_icnter;
}

#endif /* KLH10_CLKTRG_COUNT */

