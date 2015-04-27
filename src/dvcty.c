/* DVCTY.C - Support for Console TTY (FE TTY on some machines)
*/
/* $Id: dvcty.c,v 2.4 2002/03/21 09:47:52 klh Exp $
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
 * $Log: dvcty.c,v $
 * Revision 2.4  2002/03/21 09:47:52  klh
 * Mods for CMDRUN (concurrent mode)
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include <stdio.h>

#include "klh10.h"
#include "kn10def.h"
#include "kn10ops.h"
#include "fecmd.h"
#include "dvcty.h"	/* Exported functions, etc */

#ifdef RCSID
 RCSID(dvcty_c,"$Id: dvcty.c,v 2.4 2002/03/21 09:47:52 klh Exp $")
#endif

/* Imported functions */
extern int dte_ctysin(int);

/* Local functions & data */
#if KLH10_CPU_KS
static int cty_sin(int cnt);
#endif

static struct clkent *ctytmr;	/* For re-check of CTY input */


/* CTY_CLKTMO - for use when doing without OS I/O interrupts and
**	need to explicitly poll the CTY to see if input is available.
**	ALSO used when 10 isn't accepting input fast enough, and we need
**	to re-check later to see if it can accept more.
**	This timeout routine is called at a synchronized (between-instr) point.
**
** Note: the call to cty_incheck is a bit risky as that routine may
** hack the timer (generally not a good idea while still in its callout!)
** but in this case it's safe as all it does is attempt to activate it,
** which is a no-op in this context (already active).
*/
static int
cty_clktmo(void *arg)	/* arg unused */
{
    if (cpu.fe.fe_debug)
	fprintf(stderr, "[cty_clktmo]");

#if KLH10_CTYIO_INT
    fe_iosigtest();		/* Do a check this way */
    return CLKEVH_RET_QUIET;	/* Go quiescent until re-activated */
#else
    cty_incheck();
    return CLKEVH_RET_REPEAT;	/* No signals, always stay alive */
#endif
}

void
cty_init(void)
{
    /* Set up timeout call once per 1/30 sec -- should be enough. */
    ctytmr = clk_tmrget(cty_clktmo, (void *)NULL, CLK_USECS_PER_SEC/30);

#if KLH10_CTYIO_INT
    clk_tmrquiet(ctytmr);	/* Disable timer for now */
    INTF_INIT(cpu.intf_ctyio);
    fe_ctysig(fe_iosig);	/* Set up TTY input signal handler */
    fe_iosigtest();		/* Trigger initial call */
#endif
}


/* CTY_INCHECK - Invoked either directly by timeout or indirectly by host-OS
**	signal.
**	Checks to see if TTY input is available, and if so does what's
**	necessary to get it from host-OS and feed it to either the FE
**	or the 10.
*/
void
cty_incheck(void)
{
    register int inpend;

    if (cpu.fe.fe_debug)
	fprintf(stderr, "[cty_incheck: isigs %d nullsigs %d inp %d]\n",
		feiosiginps, feiosignulls, cpu.fe.fe_ctyinp);

    if ((inpend = cpu.fe.fe_ctyinp) > 0		/* See if any input waiting */
      || (inpend = fe_ctyintest()) > 0) {	/* Think not, but check OS */

	if (cpu.fe.fe_debug)
	    fprintf(stderr, "[cty_incheck: %d inp]", inpend);

	/* TTY input available!  See if FE needs it */
	if (cpu.fe.fe_mode == FEMODE_CMDRUN) {
	    INTF_SET(cpu.intf_fecty);	/* FE input avail, don't bump intcnt */
	    return;
	}

	/* Get input string and transfer to 10.
	** If didn't transfer it all, activate timer to check
	** again later to see if 10 is accepting input.
	*/
#if KLH10_CPU_KS
	if (((inpend =    cty_sin(inpend)) > 0)
#elif KLH10_CPU_KL
	if (((inpend = dte_ctysin(inpend)) > 0)
#endif
	  || (inpend = fe_ctyintest()) > 0) {
	    clk_tmractiv(ctytmr);		/* Ensure timer active */
	    if (cpu.fe.fe_ctydebug)
		fprintf(stderr, "[cty tmract: %d]", inpend);
	}
    } else if (cpu.fe.fe_debug)
	    fprintf(stderr, "[cty_incheck: null]");

    cpu.fe.fe_ctyinp = inpend;
}

#if KLH10_CPU_KS	/* Rest of code is all for KS */

/*
	FE to KS10 communication routines.

	From arduous trial and error it appears that the FE <-> KS10
interrupt mechanism exists primarily to handle console TTY I/O.

	The FE interrupts the KS10 (sets APRF_FEINT) whenever it has
placed an input character in FECOM_CTYIN.  The KS10 then reads this
word, if non-zero, and clears it.  The FE won't (or shouldn't)
send another char until that location is zero.
[Actually, TOPS-20 relies on it checking only the "valid" bit, 0400]

	The KS10 clears the APRF_FEINT flag, but since it does this
before reading the input character from FECOM_CTYIN, this event
cannot be used to trigger new input checks.

	The KS10 interrupts the FE (sets APRF_INT80 momentarily) whenever
it has placed an output character in FECOM_CTYOT (033).  The FE
picks it up if non-zero, then clears the word.  IT THEN INTERRUPTS
THE KS10 BY SETTING APRF_FEINT to signal its readiness for more.
The only thing distinguishing this from the CTY-input case is the
fact that FECOM_CTYIN is clear; however, a single interrupt may
signal both situations (CTY input waiting, and CTY output ready).

	Unfortunately, the same situation is not true for the other (input)
direction, since the KS10 doesn't bother to set any signal flag when
it's gobbled the input character.  The FE must continually check to
see if FECOM_CTYIN is clear.  Barf.

Actually, T20 does this a bit differently.  It appears that T20
sends a FE interrupt pulse whenever it changes anything, either
when reading a char from CTYIN (T20 merely clears the valid bit, 0400),
or when a validated char has been deposited in CTYOT.
T20 also expects to receive a KS10 interrupt from the FE whenever
the FE has changed anything - this can indicate either that a new
validated char is in CTYIN, or that a char was removed (and invalidated)
from CTYOT.

*/

static int
cty_sin(int cnt)
{
    register vmptr_t vp;
    register int ch, oldch;

    if ((ch = fe_ctyin()) < 0)		/* Get single char */
	return 0;			/* None left */

    vp = vm_physmap(FECOM_CTYIN);
    oldch = vm_pgetrh(vp);		/* See if ready for next char */
    if (oldch & 0400)
	fprintf(stderr, "[CTYI: %o => %o, old %o]",
		    ch, ch | 0400, oldch);
    else if (cpu.fe.fe_ctydebug)
	fprintf(stderr, "[CTYI: %o]", ch);

    /* Drop char in FE communication area */
    vm_psetxwd(vp, 0, (ch |= 0400));

    /* Send interrupt to the KS10 saying stuff is there */
    cpu.aprf.aprf_set |= APRF_FEINT;	/* Crock for now */
    apr_picheck();

    return cnt-1;			/* Only one char at a time, sigh */
}


/* FE_INTRUP - Called from WRAPR (CONO PI,) when a FE interrupt is requested
**	by the KS10 - the KS10 has changed something in the com area.
**
**	Note gross hack here to delay output for a T20 KS.
** Apparently, T20 can't cope with the fact that the KLH10 responds
** "instantaneously", generating a FE->KS interrupt to acknowledge that
** the output char has been gobbled.  The T20 code isn't prepared for
** this until it has executed something like 50 more instructions; it
** will handle the interrupt, but then thinks it needs to wait for
** an interrupt (not knowing that it already arrived and was handled!)
** and stalls.
**	Two workarounds are supported.  The first uses fe_iowait
** (called "cty_iowait" in the UI) to delay at least 50 instructions
** after gobbling output before giving T20 its interrupt.  This works,
** but slows things a lot when using a real-time clock emulation.
**	The second uses cty_lastint to give T20 an extra interrupt
** some amount of time after the last output char of a bufferful has
** gone out.  This is more efficient.
**
**	Obviously the right thing is to fix T20, but we have to support
**	unmodified monitor binaries.
*/
void
fe_intrup(void)
{
    if (cpu.fe.fe_iowait) {		/* If config setting is for delay, */
	cpu.io_ctydelay = cpu.fe.fe_iowait;	/* Start counter */
	return;
    }
    cty_timeout();			/* Else hack it immediately */
}


# if KLH10_CTYIO_ADDINT
static int cty_lasttmo(void *junk);
# endif

/* CTY_TIMEOUT - Gross hack to delay output so that KS T20
**	can catch up.
*/
void
cty_timeout(void)
{
    register int c;
    register vmptr_t vp = vm_physmap(FECOM_CTYOT);	/* Find phys loc */

    /* Check special memory location to see what's there. */
    c = vm_pgetrh(vp);			/* Get RH of physical loc */
    if (c & 0400) {			/* Valid char? */
	c &= 0377;
	if (cpu.fe.fe_ctydebug) fprintf(stderr, "[CTYO: %o]", c);
	fe_ctyout(c);

	/* Done, now send interrupt to KS10 saying output ready */
	cpu.aprf.aprf_set |= APRF_FEINT;
	/* Can skip apr_picheck() as caller will do it */
# if KLH10_CTYIO_ADDINT
	/* If our config wants an additional interrupt later, ensure it's
	   set up.  This looks hairy only because it tries to notice when user
	   modifies interval and changes its timer accordingly.
	*/
	if (cpu.fe.cty_lastint) {	/* If we want since-last-char timer */
	    if (!cpu.fe.cty_lastclk)	/* Get it if first time */
		cpu.fe.cty_lastclk = clk_tmrget(cty_lasttmo, (void *)NULL,
				(int32)(cpu.fe.cty_lastint * 1000));
	    else if (cpu.fe.cty_prevlastint != cpu.fe.cty_lastint) {
		clk_tmrquiet(cpu.fe.cty_lastclk);	/* Changed interval! */
		clk_tmrset(cpu.fe.cty_lastclk,
				(int32)(cpu.fe.cty_lastint * 1000));
	    } else {
		clk_tmrquiet(cpu.fe.cty_lastclk);	/* Restart timer */
		clk_tmractiv(cpu.fe.cty_lastclk);	/* using same interv */
	    }
	    cpu.fe.cty_prevlastint = cpu.fe.cty_lastint; /* Remember interval*/
	} else if (cpu.fe.cty_lastclk) {	/* Don't want, ensure clear */
	    clk_tmrkill(cpu.fe.cty_lastclk);
	    cpu.fe.cty_lastclk = NULL;
	}
# endif
    }
    vm_psetxwd(vp, 0, 0);		/* Clear word */
}

# if KLH10_CTYIO_ADDINT
/* CTY_LASTTMO - Timeout that sends a spurious FE interrupt to the 10 a
**	certain length of time after the *last* output character of a series.
**	This is an alternative to fe_iowait for getting around the T20
**	monitor bug that loses interrupts on the KLH10's "instantaneous"
**	CTY output.
*/
static int
cty_lasttmo(void *junk)
{
    cpu.aprf.aprf_set |= APRF_FEINT;	/* Give CPU a spurious CTY int */
    apr_picheck();
    return CLKEVH_RET_QUIET;		/* Go quiescent afterwards */
}
# endif	/* KLH10_CTYIO_ADDINT */

#endif /* KLH10_CPU_KS */
