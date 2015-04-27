/* FECMD.C - FE command processing and CTY utilities
*/
/* $Id: fecmd.c,v 2.2 2002/05/21 09:43:39 klh Exp $
*/
/*  Copyright © 2002 Kenneth L. Harrenstien
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
 * $Log: fecmd.c,v $
 * Revision 2.2  2002/05/21 09:43:39  klh
 * Tweak so will compile without KLH10_CTYIO_INT
 *
 * Revision 2.1  2002/03/21 09:46:52  klh
 * Initial distribution checkin
 *
 */

#include <stdio.h>
#include <stdlib.h>	/* Malloc and friends */
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>	/* For error-reporting functions */

#include "klh10.h"
#include "kn10mac.h"	/* FLD macros */
#include "kn10def.h"
#include "kn10dev.h"
#include "kn10ops.h"
#include "wfio.h"
#include "feload.h"
#include "prmstr.h"
#include "dvcty.h"	/* For cty_ functions */
#include "fecmd.h"

#if KLH10_CPU_KS
# include "dvuba.h"	/* So can get at device info */
#endif

#ifdef RCSID
 RCSID(fecmd_c,"$Id: fecmd.c,v 2.2 2002/05/21 09:43:39 klh Exp $")
#endif

/* Imported data */
extern int proc_bkgd;	/* From klh10.c */


/*
	FE command parsing and CTY utilities

The distinction between the various modules that are interested in
the user's TTY can get blurry.  Here is a rough outline of how the
duties are divided up:

Main - Top level setup (KLH10.C).
	Handles command line switches, including init file setup.
	The init file becomes the first FE input, before any
	TTY input is read.

FE - the "Front End" (FECMD.C).
	The KLH10 "FE" handles all terminal I/O for
	the KLH10 user interface, as well as passing it
	to and from the PDP-10 console when in "user" mode.
	It is not intended to resemble either a KL FE (a PDP-11)
	or a KS FE (a 8080).
	Note that the actual emulation of the 11 or 8080 side of
	communications with the PDP-10 is carried out by DVDTE.C
	(for the KL) or DVCTY.C (for the KS).

CTY - the PDP-10 Console TTY (DVCTY.C).
	For KA/KI/6 this is an actual device instance, which
	interacts with FE code to do I/O.

	For the KL this is not a device, but rather contains
	glue to hook up FE I/O with a true device (DVDTE)
	that provides the "console I/O" method the 10 expects.

	The KS is similar to the KL but there is no DVDTE and
	the "console I/O" method is entirely done by DVCTY.


OS_TTY* - provides access to the native host's TTY functions.
	These should normally only be called by FECMD when needed,
	so that the FE code can decide where I/O should actually
	be going.

 */

/* Control flow:

	At top level in klh10_main() there is a loop that reads and excecutes
FE commands, some of which will start the KN10 running by calling aprcont(),
which calls apr_run().  The KN10 continues to run until something halts it 
by causing a return from apr_run() with haltcode value.  Control then
returns back to the caller of aprcont() which is normally a FE command
routine that returns to the top level loop.

If fe_runenable is not set, all FE I/O is simply passed on to the 10 CTY.
Commands are not accumulated or executed, and control only returns to the FE if
the CPU halts for some reason (including a FE interrupt char) as described
above.

	This works by having the FE interrupt char signal handler set
	the cpu.intf_fecty flag, which causes the APR run loop to
	halt with the haltcode HALT_FECTY.

But if fe_runenable is set... the FE can be in an additional mode where
TTY input is accumulated in a command buffer rather than passed to the
10 CTY.  In this mode:

	aprcont() has called apr_run() and the CPU is running.
	The TTY remains set up for normal command-line mode, after
		an initial prompt has been output.
	TTY input is accumulated into a command buffer.  This is done
		using the line edit facility of the native OS, and assumes
		we will get either a SIGIO or a positive # chars waiting
		as soon as an input line is completed.
	CTY output is suppressed (not discarded) if possible.

    When a full command is received:
	The FE input code tickles cpu.intf_fecty to simulate a FE interrupt.
	This is distinguished from a real signal by a cause value in
	the feregs struct, and a different haltcode is returned.

	The top-level loop will notice on return from the FE command
	that the command buffer is set up and will parse/execute that new
	command, which may do anything including halting or restarting the CPU.

	If the mode remains the same, the top-level loop will then invoke
		aprcont() directly to proceed with KN10 execution,
		instead of blocking to obtain a new command line from the
		TTY.

Control does not necessarily have to return to the top level loop
for each command.  However, it DOES have to return from apr_run in order
to execute commands, because there is no way to predict what the command
might change in terms of state, and the loop that apr_run is actually
using internally depends on this state.  For example, setting a breakpoint
or turning tracing on requires a fresh invocation of apr_run.

So the FE cannot simply do an internal execution of the commands it
receives during KN10 operation.

 */



/* Called just before KN10 starts running
 */
void
fe_ctyenable(int mode)
{
    switch (cpu.fe.fe_mode = mode) {
    case FEMODE_CMDRUN:
	fe_ctycmdrunmode();
	break;

    default:
	fe_ctyrunmode();
#if KLH10_CTYIO_INT
	fe_iosig(0);		/* Trigger initial test */
#endif
	break;
    }
}

/* Called just after KN10 stops running
 */
void
fe_ctydisable(int mode)
{
    if (cpu.fe.fe_mode == mode)
	return;			/* No change, nothing needed */

    switch (cpu.fe.fe_mode = mode) {
    case FEMODE_CMDRUN:
	fe_ctycmdrunmode();
	break;
    case FEMODE_CMDHALT:
    default:
	fe_ctycmdmode();
    }
}

/* Controlling terminal stuff
**
** Note hack to support background mode.
** If the local "ttyback" flag is set, all os_tty functions
** pretend to work but don't actually do anything other than output, which
** is assumed to have been redirected on the command line to some file/pipe.
**
** It is legal to run in the background with FEMODE_CMDRUN; input
** may come from sources other than the TTY.
**
** During FEMODE_CMDCONF and FEMODE_CMDHALT:
**	FE input comes from file if present, else TTY if not in background.
**	FE output goes to stdout (normally TTY)
**	CTY I/O is disabled.
** During FEMODE_CMDRUN:
**	FE input comes from file if present, else TTY if not in background.
**	FE output goes to stdout (normally TTY)
**	CTY I/O is disabled.
** During FEMODE_CTYRUN:
**	FE input comes from TTY (if not in background). [1]
**	FE output goes to stdout (normally TTY)
**	CTY input comes from FE input.
**	CTY output goes to FE output.
**
** [1] a possible enhancement would be to allow file input, either
**	anytime the KN10 seems ready to accept input or in response to
**	a certain sequence of KN10 output.  Usual scripting stuff.
*/

static int ttyback = FALSE;	/* Initially false, not in background */

static void
fe_sigtermhdl(int sig)
{
    if (ttyback) {
	printf("[Termination signal received - invoking auto-shutdown!]\n");
	fe_shutdown();
    }
}

void
int_fecty(int sig)
{
    INTF_SET(cpu.intf_fecty);	/* Say FE CTY interrupt went off */
    INSBRKSET();		/* Say something happened */
    cpu.fe.fe_intcnt++;		/* Remember # seen */
}

/* SIGIO interrupt handler.
**	Intention here is that this indicate input available on the TTY
** input fd.
**	It needs to read all available input as SIGIO only happens when
** the input state changes from none to some.  Sets a flag which remembers
** whether the last check indicated more input or not.
**	For proper operation, this signal must go off differently in the two
** running modes:
** CMDRUN - triggers when a complete input line is ready.
** CTYRUN - triggers as soon as any input available.
**
** Don't try to do any debug output from inside this or SIGIO will
** be triggered infinitely!
*/
int feiosignulls = 0;
int feiosiginps = 0;
int feiosigtests = 0;

#if KLH10_CTYIO_INT

void
fe_iosig(int junk)
{
    if ((cpu.fe.fe_ctyinp = fe_ctyintest()) <= 0) {
	++feiosignulls;
	return;
    }
    feiosiginps++;
    if (cpu.fe.fe_mode == FEMODE_CMDRUN) {
	INTF_SET(cpu.intf_fecty);	/* FE input avail, don't bump intcnt */
    } else
	INTF_SET(cpu.intf_ctyio);	/* CTY input available */
    INSBRKSET();
}

void
fe_iosigtest(void)
{
    ++feiosigtests;
    fe_iosig(0);
}
#endif


/* Return appropriate haltcode to apr_check when it
** determines that a FE interrupt is happening, either directly due
** to interrupt char or indirectly via input ready.
*/
enum haltcode
fe_haltcode(void)
{
    register int hcode;
    register int ocnt;

    switch (ocnt = cpu.fe.fe_intcnt) {	/* FE console interrupt? */
    case 0:
	/* Not a direct int, so assume it was manually tickled by
	   an input-ready condition.
	*/
	hcode = HALT_FECMD;		/* FE input available */
	break;

    case 1:
	/* Single interrupt - return to cmd mode if allowed */
	if (cpu.fe.fe_runenable) {
	    cpu.fe.fe_intcnt = 0;
	    hcode = HALT_FECMD;
	    break;
	}
	/* Fall thru */
    default:
	/* Multiple interrupts or not allowing cmdrun */
	cpu.fe.fe_intcnt = 0;
	hcode = HALT_FECTY;
	break;
    }

    if (cpu.fe.fe_debug)
	fprintf(stderr,
		"[haltcode: %s cnt %d isigs %d nullsigs %d intf_fecty %d]",
		(hcode==HALT_FECMD) ? "FECMD" : "FECTY",
		ocnt, feiosiginps, feiosignulls, cpu.intf_fecty);
    return hcode;
}


void
fe_ctyinit(void)
{
    osttyinit_t osti;
    extern char *cmdprompt;

    fe_cmpromptset(cmdprompt);

    osti.osti_intchr = cpu.fe.fe_intchr;
    osti.osti_inthdl = int_fecty;
    osti.osti_attrs = (OSTI_INTCHR|OSTI_INTHDL);
    if (proc_bkgd) {
	ttyback = TRUE;
	osti.osti_attrs |= OSTI_BKGDF;
	osti.osti_bkgdf = TRUE;
	osti.osti_trmhdl = fe_sigtermhdl;
    }
    os_ttyinit(&osti);
}

#if KLH10_CTYIO_INT
void
fe_ctysig(ossighandler_t *rtn)
{
    if (!ttyback)
	os_ttysig(rtn);
}
#endif /* KLH10_CTYIO_INT */

void
fe_ctyreset(void)
{
    if (!ttyback)
	os_ttyreset();
}

void
fe_ctycmdmode(void)
{
    if (!ttyback)
	os_ttycmdmode();
}

void
fe_ctycmdrunmode(void)
{
    if (!ttyback)
	os_ttycmdrunmode();
}

void
fe_ctyrunmode(void)
{
    if (!ttyback)
	os_ttyrunmode();
}

/* FE_CTYINTEST - See if any TTY input available, and returns count of chars.
**	If count unknown, OK to return just 1; routine will be invoked
**	frequently (either by clock timeout or by I/O signal).
*/
int
fe_ctyintest(void)
{
    if (!ttyback)
	return os_ttyintest();
    return 0;
}

int
fe_ctyin(void)
{
    if (ttyback) {
	printf("[TTYIN while in background; invoking auto-shutdown!]\n");
	fe_shutdown();
	/* Never returns */
    }
    return os_ttyin();
}

int
fe_ctyout(int ch)
{
    return os_ttyout(ch);
}

/* TTY String output.
**	Note assumption that 8th bit is already masked, if desired.
**	Call may block, sigh.
*/
int
fe_ctysout(char *buf, int len)		/* Note length is signed int */
{
    return os_ttysout(buf, len);
}

/* Top-level command character input
**	May want to be different from fe_ctyin().
*/
int
fe_ctycmchar(void)
{
    if (ttyback) {
	printf("[TTYCMCHAR while in background; invoking auto-shutdown!]\n");
	fe_shutdown();
	/* Never returns */
    }
    return os_ttycmchar();
}

char *
fe_ctycmline(char *buffer, int size)
{
    if (ttyback) {
	printf("[TTYCMLINE while in background; invoking auto-shutdown!]\n");
	fe_shutdown();
	/* Never returns */
    }
    return os_ttycmline(buffer, size);
}

void
fe_ctycmforce(void)
{
    os_ttycmforce();
}

/* FE command prompt stuff
 *	Prompt string has special format.  1st char is delimiter
 *	dividing string into 3 prompts (1 for each mode of
 *	CMDCONF, CMDHALT, CMDRUN).
 */

#define NPROMPTS 3
#define PROMPTSIZ 40*NPROMPTS
static struct promptent {
    int siz;
    char *ptr;
} prompttab[NPROMPTS] = {
    { 7, "KLH10# " },
    { 7, "KLH10> " },
    { 8, "KLH10>> " }
};
static char promptbuf[PROMPTSIZ];

void
fe_cmpromptset(char *str)
{
    register char *cp;
    char *fcp = str;
    size_t flen = strlen(str);
    char *tcp = promptbuf;
    size_t left = sizeof(promptbuf)-1;
    int idx = 0;
    char *tab[NPROMPTS];
    int delim = 0;

    /* Get prompt into buffer */
    if (flen >= left)
	flen = left-1;
    memcpy(promptbuf, str, flen);
    promptbuf[flen] = '\0';

    delim = *tcp++;
    --flen;
    for (idx = 0; idx < NPROMPTS; ++idx) {
	prompttab[idx].ptr = tcp;
	if (flen && (cp = strchr(tcp, delim))) {
	    flen -= (prompttab[idx].siz = cp - tcp) + 1;
	    *cp++ = '\0';
	    tcp = cp;
	    continue;
	}
	if (idx == 0) {			/* If nothing but 1 prompt */
	    prompttab[idx].ptr = --tcp;
	    prompttab[idx].siz = ++flen;
	    flen = 0;
	} else if (flen) {		/* Mult prompts and not 1st */
	    prompttab[idx].siz = flen;
	    flen = 0;
	} else				/* Empty, use previous */
	    prompttab[idx] = prompttab[idx-1];
    }
}

char *
fe_cmprompt(int mode)
{
    switch (mode) {
    case FEMODE_CMDCONF:
	return prompttab[0].ptr;
    case FEMODE_CMDHALT:
	return prompttab[1].ptr;
    case FEMODE_CMDRUN:
	return prompttab[2].ptr;
    }
    return "";
}
