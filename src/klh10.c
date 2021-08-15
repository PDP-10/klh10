/* KLH10.C - Main for KLH10 (also Front End Console for now)
*/
/* $Id: klh10.c,v 2.9 2002/05/21 16:54:32 klh Exp $
*/
/*  Copyright � 1992, 1993, 2001 Kenneth L. Harrenstien
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
 * $Log: klh10.c,v $
 * Revision 2.9  2002/05/21 16:54:32  klh
 * Add KLH10_I_CIRC to allow any sys to have CIRC
 *
 * Revision 2.8  2002/05/21 10:01:22  klh
 * Add ub_debug param.
 * Allow access to HZ vars even if using synch clock.
 *
 * Revision 2.7  2002/04/24 07:56:08  klh
 * Add os_msleep, using nanosleep
 *
 * Revision 2.6  2002/03/28 16:51:04  klh
 * Tweak to fc_quit
 *
 * Revision 2.5  2002/03/21 09:50:08  klh
 * Mods for CMDRUN (concurrent mode)
 *
 * Revision 2.4  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include "klh10.h"

#include <stdio.h>
#include <stdlib.h>	/* Malloc and friends */
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>	/* For error-reporting functions */

#include "kn10mac.h"	/* FLD macros */
#include "kn10def.h"
#include "kn10dev.h"
#include "kn10ops.h"
#include "kn10cpu.h"
#include "wfio.h"
#include "fecmd.h"
#include "feload.h"
#include "cmdline.h"
#include "prmstr.h"
#include "dvcty.h"	/* For cty_ functions */

#if KLH10_CPU_KS
# include "dvuba.h"	/* So can get at device info */
#endif

#if KLH10_DEV_LITES
# include "dvlites.h"
#endif

#ifdef RCSID
 RCSID(klh10_c,"$Id: klh10.c,v 2.9 2002/05/21 16:54:32 klh Exp $")
#endif

/* Exported functions */
#include "klh10exp.h"

/* Local function kept external for easier debug access  */
void errpt(void);


/* Local variables */

int proc_bkgd = FALSE;	/* TRUE if want to run in background */
int fedevchkf = FALSE;	/* TRUE to do periodic device attention checks */
ospri_t proc_pri = 0;	/* CPU process priority on host OS */
int cmdpromptnew = TRUE;	/* Set whenever cmdprompt changed */
char *cmdprompt =	/* Set initial command prompt */
#ifdef KLH10_CMDPROMPT
	KLH10_CMDPROMPT;
#else
	":KLH10# :KLH10> :KLH10>> ";
#endif

char *ld_fmt = NULL;	/* Current load file format */
char *ld_dfmt =		/* Default format if none specified */
#if KLH10_SYS_ITS
		"u36";	/* Alan's Unixified format */
#else
		"c36";	/* Core-Dump format */
#endif
struct loadinfo ld_inf;	/* Args to and results from loader */

enum fevmmode {		/* FEVM_XMAP memory mapping mode defs */
	FEVM_DFLT,	/* Use FE default mode */
	FEVM_CUR,	/* Current machine mode mapping */
	FEVM_PHYS,	/* Force physical map */
	FEVM_EXEC,	/* Force exec map */
	FEVM_USER,	/* Force user map */
	FEVM_ACB	/* Select AC block */
};

/* DDT mode variables */
vaddr_t ddt_loadsa;		/* Last loaded start address */
vaddr_t ddt_cloc;		/* Current location (.) */
enum fevmmode ddt_clmode;	/* Current location FEVM mode */
w10_t ddt_val;			/* Last value */

/* Flags to pinstr() */
#define PINSTR_OPS 01	/* Show operands of instr */
#define PINSTR_EA 02	/* Use E furnished */
void pinstr(FILE *, w10_t, int, vaddr_t);

/* Local Function predeclarations */

static void klh10_init(void);
static void mem_init(void), mem_term(void);
static int mem_setlock(FILE *, FILE *, int);
static void swinit(int, char **);
static int aprhalted(void);
static void wd1print(FILE *, w10_t);
static void wd2print(FILE *, w10_t);
static void addrprint(FILE *, vaddr_t, enum fevmmode);
static void nextinsprint(FILE *, int);
static void easymprint(FILE *, vaddr_t);
static int addrparse(char *, vaddr_t *, enum fevmmode *);
static char *strf6(char **, w10_t);
static struct tm *timefrits(struct tm *, w10_t);

vmptr_t fevm_xmap(vaddr_t, enum fevmmode);	/* FE memory mapping */

/****************** Command Tables and dispatch ******************/

/* New version of command parser -- succumb to GDB, DBX, UNIX influence.
**
** Old version used an even simpler one-character instant-action parser.
*/

static FILE *cminfile = NULL;	/* Set if reading commands from file */
static char *cminfname;		/* Filename */
static int cminchar(void);	/* Funct to read from file or TTY */

#define CMDQCHAR '\\'	/* Quote char for token parsing */


static char cmdbuf[CMDBUFLEN];	/* Original command string buffer */
#if 0
static char cmdwbf[CMDBUFLEN];	/* Working buffer */
#endif

static void slinlim(char *);

static struct cmd_s command;

CMDDEF(cd_ques,  fc_ques,   CMRF_NOARG,	NULL,
				"How to get help", "")
CMDDEF(cd_help,  fc_help,   CMRF_TOKS,	NULL,
				"Basic help", "")
CMDDEF(cd_quit,  fc_quit,   CMRF_NOARG,	NULL,
				"Quit emulator", "")
CMDDEF(cd_rquit, fc_rquit,  CMRF_NOARG,	NULL,
				"Really quit!", "")
CMDDEF(cd_load,  fc_load,   CMRF_TOKS,	"<file>",
				"Load binary into KN10", "")
CMDDEF(cd_dump,  fc_dump,   CMRF_TOKS,	"<file>",
				"Dump binary from KN10", "")
CMDDEF(cd_go,	 fc_go,     CMRF_TLIN, 	"[<addr>]",
				"Start KN10 at address", "")
CMDDEF(cd_shutdown,fc_shutdown,CMRF_NOARG,NULL,
				"Halt OS gracefully", "")
CMDDEF(cd_reset, fc_reset,  CMRF_NOARG,	NULL,
				"Halt & Reset KN10", "")
CMDDEF(cd_exa,	 fc_exa,    CMRF_TLIN,	"[<addr>]",
				"Show word at address", "")
CMDDEF(cd_exnext,fc_exnext, CMRF_NOARG,	NULL,
				"Show Next word", "")
CMDDEF(cd_exprev,fc_exprev, CMRF_NOARG,	NULL,
				"Show Previous word", "")
CMDDEF(cd_dep,   fc_dep,    CMRF_TOKS,	"<addr> <val>",
				"Deposit value at address", "")
CMDDEF(cd_bkpt,  fc_bkpt,   CMRF_TLIN,	"<addr>",
				"Set breakpoint at PC loc", "")
#if 1 /* New setup */
CMDDEF(cd_step,  fc_step,   CMRF_TLIN,	"<#>",
				"Single-Step # KN10 instrs", "")
CMDDEF(cd_proc,  fc_proc,   CMRF_NOARG,	NULL,
				"Proceed KN10 without CTY", "")
#else
CMDDEF(cd_step,  fc_step,   CMRF_NOARG,	NULL,
				"Single-Step KN10", "")
CMDDEF(cd_proc,  fc_proc,   CMRF_TLIN,	"<#>",
				"Proceed # instrs", "")
#endif
CMDDEF(cd_cont,  fc_cont,   CMRF_NOARG,	NULL,
				"Continue KN10", "")
CMDDEF(cd_view,  fc_view,   CMRF_NOARG,	NULL,
				"View KN10 status", "")
CMDDEF(cd_set,   fc_set,    CMRF_TLIN,	"[<var> [<val>]]",
				"Set/show KLH10 variables", "")
CMDDEF(cd_trace, fc_trace,  CMRF_NOARG,	NULL,
				"Toggle execution trace", "")
CMDDEF(cd_halt,  fc_halt,   CMRF_NOARG,	NULL,
				"Halt KN10 immediately", "")
CMDDEF(cd_zero,  fc_zero,   CMRF_NOARG,	NULL,
				"Zero first 256K memory", "")
CMDDEF(cd_devload,fc_devload, CMRF_TLIN,
			"<New-drivername> <pathname> <initsym> <comments>",
			"Load dynamic-library device driver", "")
CMDDEF(cd_devdef,fc_devdef, CMRF_TLIN,
			"<New-devid> {ub<#>,<dev#>} <drivername> <params>",
			"Define, bind, and initialize device", "")
CMDDEF(cd_devshow,fc_devshow, CMRF_TLIN,
			"[<devid>]",
			"Show device driver & definition binding info", "")
#if KLH10_EVHS_INT
CMDDEF(cd_devevshow,fc_devevshow, CMRF_TLIN,
			"[<devid>]",
			"Show device event registration info", "")
#endif
CMDDEF(cd_dev_cmd,fc_dev_cmd,    CMRF_TLIN,
			"<devid> <device-command-line>",
			"Execute device-dependent command", "")

CMDDEF(cd_devboot,fc_devboot,    CMRF_TLIN,
			"<devid> [halt]",
			"Boot from device", "")
CMDDEF(cd_devmnt,fc_devmnt,      CMRF_TLIN,
			"<devid> <pathname> [<options>]",
			"Mount device media", "")
CMDDEF(cd_devunmnt,fc_devunmnt,  CMRF_TLIN,
			"<devid>",
			"Unmount device media", "")
CMDDEF(cd_devdbg,fc_devdbg,    CMRF_TLIN,
			"<devid> [<debugval>]",
			"Set device debug value (0=none)", "")
CMDDEF(cd_devwait,fc_devwait,   CMRF_TLIN,
			"[<devid>] [<secs>]",
			"Wait for device (or all devs)", "")
#if KLH10_DEV_LITES
CMDDEF(cd_lights,  fc_lights,   CMRF_TLIN,	"<hexaddr>|usb",
				"Set console lights I/O base address", "")
#endif


KEYSBEGIN(fectbkeys)
    KEYDEF("?",		cd_ques)
    KEYDEF("help",	cd_help)
    KEYDEF("exit",	cd_quit)
    KEYDEF("quit",	cd_quit)
    KEYDEF("really-quit",	cd_rquit)
    KEYDEF("load",	cd_load)
    KEYDEF("dump",	cd_dump)
    KEYDEF("go",	cd_go)
    KEYDEF("shutdown",	cd_shutdown)
    KEYDEF("reset",	cd_reset)
    KEYDEF("examine",	cd_exa)
    KEYDEF("next-examine",	cd_exnext)
    KEYDEF("^-examine",	cd_exprev)
    KEYDEF("deposit",	cd_dep)
    KEYDEF("breakpt",	cd_bkpt)
    KEYDEF("1-step",	cd_step)
    KEYDEF("proceed",	cd_proc)
    KEYDEF("continue",	cd_cont)
    KEYDEF("view",	cd_view)
    KEYDEF("set",	cd_set)
    KEYDEF("trace-toggle",	cd_trace)
    KEYDEF("halt",	cd_halt)
    KEYDEF("zero",	cd_zero)
    KEYDEF("devdefine",	cd_devdef)
    KEYDEF("devdebug",  cd_devdbg)
    KEYDEF("devboot",   cd_devboot)
    KEYDEF("devmount",	cd_devmnt)
    KEYDEF("devunmount",cd_devunmnt)
    KEYDEF("devwait",   cd_devwait)
    KEYDEF("devshow",	cd_devshow)
#if KLH10_EVHS_INT
    KEYDEF("devevshow",	cd_devevshow)
#endif
    KEYDEF("dev",	cd_dev_cmd)
    KEYDEF("devload",	cd_devload)
#if KLH10_DEV_LITES
    KEYDEF("lights",	cd_lights)
#endif
KEYSEND

static void error(char *, ...), syserr(int, char *, ...);

jmp_buf errenv;

int initdone = 0;

void
errpt(void)		/* Call this to restart loop */
{
    if (!initdone) {
	fprintf(stderr, "Died during startup... goodbye!\n");
	os_exit(1);
    }

    /* Return to main KLH10 command processor loop.
    ** All implementations of longjmp() had better know how to jump out of
    ** a signal handler context!
    */
    longjmp(errenv, 1);
}

/* Print out KLH10 compile-time configuration info
 */
#include "klh10s.h"	/* Define string equivs to config params! */

static void
pconfig(FILE *f)
{
    fprintf(f, "Compiled for %s on %s with word model %s\n",
			KLH10S_CENV_SYS_, KLH10S_CENV_CPU_, WORD10_MODEL);
    fputs("Emulated config:\n", f);
    fprintf(f, "\t CPU: %s   SYS: %s   Pager: %s  APRID: %ld\n",
			KLH10S_CPU_, KLH10S_SYS_, KLH10S_PAG_,
			(long)KLH10_APRID_SERIALNO);
    fprintf(f, "\t Memory: %ld pages of %d words  (%s)\n",
			(long)PAG_MAXPHYSPGS, (int)PAG_SIZE,
			(KLH10_MEM_SHARED ? "SHARED" : "private"));
    fprintf(f, "\t Time interval: %s   Base: %s",
			KLH10S_ITIME_, KLH10S_RTIME_);
#if KLH10_SYS_ITS
    fprintf(f, "   Quantums: %s", KLH10S_QTIME_);
#endif
#if KLH10_CLK_ITHZFIX
    fprintf(f, "\n\t Interval default: %dHz\n", KLH10_CLK_ITHZFIX);
#else
    fprintf(f, "\n\t Interval default: set by 10\n");
#endif
    fprintf(f, "\t Internal clock: %s\n", KLH10S_CLKTRG_);

    /* Show hardware emulation stuff first, then software features */
    fprintf(f, "\t Other:%s\n",
	    KLH10S_MCA25
	    KLH10S_I_CIRC
	    KLH10S_JPC
	    KLH10S_DEBUG
	    KLH10S_PCCACHE
	    KLH10S_CTYIO_INT
	    KLH10S_IMPIO_INT
	    KLH10S_EVHS_INT
	    );

    /* Show peripheral device drivers known at compile time */
    fprintf(f, "\t Devices:%s\n",
	    KLH10S_DEV_DTE
	    KLH10S_DEV_RH
	    KLH10S_DEV_RPXX
	    KLH10S_DEV_TM03
	    KLH10S_DEV_NI20
	    KLH10S_DEV_DZ11
	    KLH10S_DEV_CH11
	    KLH10S_DEV_LHDH
	    );
}

static void
pversion(FILE *f)
{
    fputs("KLH10", f);
#ifdef KLH10_VERSION      
    fprintf(f, " %s", KLH10_VERSION);
#endif
#ifdef KLH10_CLIENT
    fprintf(f, " (%s)", KLH10_CLIENT);
#endif
#if defined(__DATE__) && defined(__TIME__)
    fprintf(f, " built %s %s", __DATE__, __TIME__);
#endif
    fputc('\n', f);
}

static void
pgreeting(FILE *f)
{
    pversion(f);
#ifdef KLH10_COPYRIGHT
    fprintf(f, "%s\n", KLH10_COPYRIGHT);
#endif
#ifdef KLH10_WARRANTY
    fprintf(f, "%s\n", KLH10_WARRANTY);
#endif
    fputc('\n', f);
    pconfig(f);
}


void
klh10_main(int argc,
	   char **argv)
{
    /* Handle command line args/switches, if any */
    swinit(argc, argv);

    os_init();			/* Initialize any OS-dependent stuff */

    /* Ensure stdout is unbuffered, like stderr, to avoid the otherwise
    ** confusing skews between the two streams.  This must be done prior to
    ** any output on stdout.
    */
    setbuf(stdout, (char *)NULL);
    if (proc_bkgd)
	fprintf(stderr, "[Running in background]\n");
    pgreeting(stdout);		/* Print greeting message if one */

    klh10_init();		/* Do machine init and configuring */
    fe_ctyinit();		/* Initialize console, KLH10 UI */

    if (!setjmp(errenv)) {
	/* Once-only first-time stuff */
	initdone = TRUE;	/* errenv jmpbuf now set */

    } else {
	/* Recover from error catch - something called errpt(). */
	printf("<INT>");
	/* If reading from command file, abort it */
	if (cminfile) {
	    fclose(cminfile);
	    cminfile = NULL;
	    printf("[Aborted input from \"%s\"]\n", cminfname);
	}
    }
    fe_cmdloop();
}

void
fe_cmdloop(void)
{
    enum femode omode = -1;
    int prompted = FALSE;

    fe_ctyreset();		/* Reset our terminal mode stuff */
    printf("\n");		/* Make sure we prompt on a new line */

    /* Enter command parser loop */
    for (;;) {

	/* Determine new prompt if necessary */
	if (cmdpromptnew || (omode != cpu.fe.fe_mode)) {
	    cmdinit(&command, fectbkeys, fe_cmprompt(cpu.fe.fe_mode),
		    cmdbuf, sizeof(cmdbuf));
	    omode = cpu.fe.fe_mode;
	    prompted = FALSE;
	    cmdpromptnew = FALSE;
	}
	if (cpu.fe.fe_debug)
	    fprintf(stderr, "[femode %d prompt %d]", omode, prompted);

	switch (omode) {
	case FEMODE_CMDCONF:
	case FEMODE_CMDHALT:
	    if (prompted)
		command.cmd_flags |= CMDF_NOPRM;
	    else
		command.cmd_flags &= ~CMDF_NOPRM;
	    if (!cmdlsetup(&command)) {	/* Read typein command line */
		printf("\n");		/* If failed, try again */
		break;
	    }
	    if (fedevchkf)			/* If checking for dev attn, */
		fedevchkf = dev_dpchk_ctl(TRUE);	/* do so here */
	    (void) cmdexec(&command);	/* Parse and execute */
	    break;

	case FEMODE_CMDRUN:
	    /* See if input available.  If so, parse and execute, else
	       run the CPU.
	    */
	    if (cminfile || fe_ctyintest()) {
		if (prompted)
		    command.cmd_flags |= CMDF_NOPRM;
		else
		    command.cmd_flags &= ~CMDF_NOPRM;
		if (!cmdlsetup(&command)) {	/* Read input cmd line */
		    printf("\n");		/* If failed, try again */
		    break;
		}
		(void) cmdexec(&command);	/* Parse and execute */
		break;
	    }

	    /* Nothing to do, start running! */
	    if (!prompted) {
		fputs(fe_cmprompt(cpu.fe.fe_mode), stdout);
		prompted = TRUE;
	    }
	    fe_aprcont(FEMODE_CMDRUN, 0, 0, 0);	/* Resume KN10 */
	    continue;		/* No cmd done, so don't reset "prompted" */

	case FEMODE_CTYRUN:	/* Should not happen */
	default:
	    cpu.fe.fe_mode = FEMODE_CMDHALT;
	    error("[FE invalid mode %d]", omode);
	}
	prompted = FALSE;
    }
}


void
fc_quit(struct cmd_s *cm)
{
    if (!aprhalted())
	printf("KN10 still running!\n");
    printf("Are you sure you want to quit? [Confirm]");
    fe_ctycmforce();
    switch (cminchar()) {
    case 'y':
    case 'Y':
    case EOF:		/* EOF */
	break;
    default:		/* Anything else prevents quit */
	return;
    }
    printf("Shutting down...");

    dev_term();		/* Power off all devices that might need it */

    mem_term();		/* Flush memory in case shared */

    printf("Bye!\n");
    os_exit(0);
}

void
fc_rquit(struct cmd_s *cm)
{
    dev_term();
    mem_term();
    os_exit(0);
}

/* FE_SHUTDOWN - Attempt to bring down PDP-10 OS and quit emulator as
**	gracefully as possible *without* any user interaction.
**
** This is intended to be used when operating as a background process,
** when errors that would normally halt and await input should
** instead attempt to give both the PDP-10 OS and the emulator a
** chance to clean up before the process is killed.  This currently
** means:
** 	(1) Any request for TTY input - in bkgd mode that implies something
**		went wrong.
**	(2) Receipt of a SIGTERM software termination signal.
**
** Probably better would be a way to suspend operations and then allow
** re-attaching a TTY to the emulator -- but UNIX sucks in that regard
** and still hasn't implemented technology that existed 25 years ago!
*/

static int
fe_shuttmo(void *arg)	/* arg is ignored */
{
    printf("[Auto-shutdown timed out]\n");
    fe_shutdown();			/* Re-invoke next phase of shutdown */
    return CLKEVH_RET_KILL;		/* Won't actually return */
}

void
fe_shutdown(void)
{
    static int shutstate = 0;	/* Initial shutdown state */

    switch (shutstate) {
    case 0:	
	++shutstate;
	/* First determine whether a PDP-10 OS ever actually seemed to get
	** going; a good heuristic is to see if paging mode is on.
	** If so, attempt to trigger a shutdown.
	*/
	if (cpu.mr_paging) {
	    /** Attempt OS shutdown!
	    ** Set a clock timeout of N seconds, after which to force shutdown
	    ** anyway.  For now, set N = 3.
	    */
	    printf("[Attempting auto-shutdown]\n");
	    (void) clk_tmrget(fe_shuttmo, (void *)NULL,
					(int32) CLK_USECS_PER_SEC * 3);
	    fc_shutdown((struct cmd_s *)NULL); /* No input required for this */
	    /* Will not return */
	}

    case 1:
	++shutstate;

    case 2:
	++shutstate;
	printf("[Starting auto-quit]\n");
	dev_term();	/* Power off all devices that might need it */
			/* This should kill all DP subprocs */
    case 3:	
	++shutstate;
	mem_term();	/* Flush memory in case using shared segs */

    case 4:
	++shutstate;
	printf("[Exiting]\n");
    }
    os_exit(1);			/* Die with an error */
}

/* ERROR - Called only by FE code to report some error in interacting
**	with the user.
*/
static void
error(char *fmt, ...)
{
    fprintf(stderr, "\n");
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fprintf(stderr, "\n");

    errpt();
}


/* SYSERR - Called only by FE code to report some OS error in interacting
**	with the user.
*/
static void
syserr(int num, char *fmt, ...)
{
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fprintf(stderr, " - %s\n", os_strerror(num));

    errpt();
}


/* PANIC - called while the KLH10 is actually running, whenever something
**	 detects a situation that should be impossible or cannot be handled.
*/
void
panic(char *fmt, ...)
{
    fprintf(stderr, "\r\nKLH10 PANIC: ");
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fprintf(stderr, "\r\n  Current PC= %#lo\r\n", (long) PC_30);

    errpt();			/* For now, break directly to main loop */
}

/* Startup arg parsing and default setting.
**	For now, all we do with args is interpret the first as a command file.
**	If no arg, look for KLH10_INITFILE as default init command file.
*/

#define KLH10_SWITCHES \
    swidef(SWI_BKGD,    "background"),	/* Run in bkgnd mode */\
    swidef(SWI_HELP,    "help"),	/* Not yet */\
    swidef(SWI_VERSION, "version")	/* Not yet */
enum {
# define swidef(i,s) i
	KLH10_SWITCHES
# undef swidef
};

static char *switab[] = {
# define swidef(i,s) s
	KLH10_SWITCHES
# undef swidef
	, NULL
};

static char usage[] = "Usage: klh10 [-background] [initfile]\n";

void
swinit(int ac, char **av)
{
    FILE *f;
    char *initfile = NULL;
    char *cp;
    int res, kx1, kx2;

    while (--ac > 0) {
	cp = *(++av);
	if (*cp != '-') {
	    /* Arg that isn't a switch is assumed to be init filename */
	    if (initfile) {		/* If already have it, error */
		fprintf(stderr, "%s", usage);
		os_exit(1);
	    }
	    initfile = cp;
	    continue;
	}
	res = s_xkeylookup(cp+1, (void *)switab, sizeof(switab[0]),
			   (voidp_t *)NULL, (voidp_t *)NULL, &kx1, &kx2);
	if (res == 1) switch (kx1) {
	    case SWI_BKGD:
		proc_bkgd = TRUE;
		continue;
	}

	/* Fall through to here if bad switch or something else wrong */
	fprintf(stderr, "Unknown switch \"%s\"\n", cp);
	fprintf(stderr, "%s", usage);
	os_exit(1);
    }

    if (!initfile) {			/* If no init file explicitly given */
	initfile = KLH10_INITFILE;	/* Use default, but */
	f = fopen(initfile, "r");	/* if not there, don't complain */
    } else if (!(f = fopen(initfile, "r"))) {
	syserr(-1, "Cannot open \"%s\"", initfile);
	/* Doesn't return since init not yet finished */
    }

    if (f) {
	cminfile = f;		/* Set - read cmds from file until gone */
	cminfname = initfile;	/* Remember its name */
    }

    /* Set any other defaults necessary */
    ld_fmt = ld_dfmt;		/* Set current load/dump format to default */
}

/* Command parsing routines.
**	New full-line version, still simple-minded.
**	Eventually can go to CCMD-like or GDB-like package, but for now keep
**	the overall size small.
**
**	Some of the parsing functions are in PRMSTR so they can readily be
**	called by other modules (specifically device emulation code).
*/

void cmdreset(struct cmd_s *);
void cmdspfls(struct cmd_s *);
static void stolower(char *);
static int smatch(char *, char *);

static int cmdargs_none(struct cmd_s *cm);
static int cmdargs_one(struct cmd_s *cm, char **);
static int cmdargs_two(struct cmd_s *cm, char **, char **);
static int cmdargs_all(struct cmd_s *cm);
static int cmdargs_n(struct cmd_s *cm, int n);

void cmdinit(struct cmd_s *cm,
	     struct cmkey_s *keys,
	     char *prompt,
	     char *ibuf,
	     size_t ilen)
{
    cm->cmd_keys = keys;
    cm->cmd_flags = 0;
    cm->cmd_prm = prompt;
    cm->cmd_buf = ibuf;
    cm->cmd_blen = ilen;
    cmdreset(cm);
}

void
cmdreset(struct cmd_s *cm)
{
    cm->cmd_flags &= ~(CMDF_ACTIVE|CMDF_INACCUM);
    cm->cmd_left = cm->cmd_blen;
    cm->cmd_inp = cm->cmd_rdp = cm->cmd_buf;
    cm->cmd_inp[0] = '\0';
    cm->cmd_rleft = 0;
    cm->cmd_arglin = NULL;
    cm->cmd_argc = 0;
    cm->cmd_argv[0] = NULL;
    cm->cmd_tdp = cm->cmd_tokbuf;
    cm->cmd_tleft = sizeof(cm->cmd_tokbuf)-1;
}

static int
cminchar(void)
{
    register int ch;

    if (!cminfile)
	return fe_ctyin();

    if ((ch = getc(cminfile)) == EOF) {
	fclose(cminfile);
	cminfile = NULL;
	fprintf(stdout, "[EOF on \"%s\"]\n", cminfname);
	ch = '\n';		/* Try to end gracefully */
    } else {
	putc(ch, stdout);	/* Echo file input char */
	fe_ctycmforce();	/* Ensure it's out */
    }
    return ch;
}

int
cmdaccum(struct cmd_s *cm)
{
    register int ch;

    /* Output prompt if not already there */
    if ((cm->cmd_flags & CMDF_INACCUM) == 0) {
	printf("%s", cm->cmd_prm ? cm->cmd_prm : ">");
	cm->cmd_flags |= CMDF_INACCUM;
	fe_ctycmforce();	/* Force out any pending tty output */
    }

    for (;;) {
	if ((ch = cminchar()) < 0)	/* Get char from file or TTY */
	    break;			/* Break out if EOF */

	if (cm->cmd_left <= 1) {
	    error("Command line overflow (%d chars!); \"%.10s...\" flushed.\n",
			(int)cm->cmd_blen, cm->cmd_buf);
#if 0
	    fe_ctyinflush();		/* Flush any pending input */
#endif
	    cmdreset(cm);
	    return cmdaccum(cm);
	}
	
	*(cm->cmd_inp)++ = ch;
	--(cm->cmd_left);

	/* Again, simple built-in handling; no dynamic specification of
	** activation chars.
	*/
	switch (ch) {
	    /* See if last char was activation char */
	case '\r':
	case '\n':
	    cm->cmd_flags |= CMDF_ACTIVE;	/* Got one! */
	    *(cm->cmd_inp) = '\0';
	    return TRUE;
	}
    }
    return FALSE;		/* No more input */
}

/* CMDEXEC - Called when we're known to have an activated command in buffer.
**	Parse first keyword to see if it's anything recognizable, and
**	invoke appropriate function if so.
*/
int
cmdexec(struct cmd_s *cm)
{
    register char *cp;
    char tokbuf[100];
    char *tcp = tokbuf;
    size_t tcnt = sizeof(tokbuf);
    size_t fcnt = cm->cmd_inp - cm->cmd_rdp;	/* # chars left to read */
    struct cmkey_s *key, *key2;
    register struct cmrtn_s *cd;
    int argc;

    if (cpu.fe.fe_debug) {
	*(cm->cmd_inp) = '\0';
	fprintf(stderr, "[cmdexec \"%s\"]", cm->cmd_rdp);
    }

    /* Get first token on line */
    cp = s_1token(&tcp, &tcnt, &cm->cmd_rdp, &fcnt);
    if (!cp) {				/* If no token (empty line) */
	return 0;			/* just return having done nothing */
    }
    if (*cp == '\n' || *cp == '\r')	/* If EOL, just return */
	return 0;			/* without adding our own echo */
    if (*cp == ';')			/* If start of comment, */
	return 0;			/* ditto */

    /* Have token, see if it's a command */
    stolower(cp);			/* Force lowercase for lookup */
    argc = s_keylookup(cp, cm->cmd_keys, sizeof(struct cmkey_s),
		      (void *)&key, (void *)&key2);
    if (argc <= 0) {
	printf("Unknown command: \"%s\"\n", cp);
	return 0;
    }
    if (argc > 1) {
	printf("Ambiguous command: \"%s\" => %s, %s%s\n",
		cp, key->cmk_key, key2->cmk_key,
		argc > 2 ? ", ..." : "");
	return 0;
    }

    /* Found a single match!  Execute it... */
    /* First set up its args as indicated by flags */
    cd = &(key->cmk_p->cmn_rtn);	/* Get ptr to routine definition */
    cmdspfls(cm);			/* Flush initial spaces from line */
    cm->cmd_arglin = cm->cmd_rdp;
    cm->cmd_rleft = strlen(cm->cmd_rdp);
    if (cd->cmr_flgs & CMRF_CMPTR) {
	(*cd->cmr_vect)(cm);		/* Special handling, no arg hackery */
	return 1;
    }
    slinlim(cm->cmd_rdp);		/* Ensure no EOL in line */
    cm->cmd_rleft = strlen(cm->cmd_rdp);
    if (cd->cmr_flgs & CMRF_NOARG) {
	if (cm->cmd_rleft) {
	    printf("Bad syntax - no args allowed\n");
	    return 0;
	}
	(*cd->cmr_vect)(cm);
	return 1;
    } else if (cd->cmr_flgs & CMRF_TLIN) {
	/* That's all it wants - cmd_rleft will be set but cmd_argc is 0 */
	(*cd->cmr_vect)(cm);
	return 1;
    }
    argc = cmdargs_all(cm);
    if (cd->cmr_flgs & CMRF_TOKS) {
	(*cd->cmr_vect)(cm);	/* Invoke with all tokens set up */
	return 1;
    } else return 0;

    return 1;
}

/*
** Fetch a command line from the front end console, or from a command
** file.
*/
char *
cmdlsetup(struct cmd_s *cm)
{
    char *cp;
    size_t len;

    if (cpu.fe.fe_debug)
	fprintf(stderr, "[cmdlsetup]");

    if (!(cm->cmd_flags & CMDF_NOPRM) && cm->cmd_prm) {
	fputs(cm->cmd_prm, stdout);
    }

    fe_ctycmforce();		/* Force out any pending tty output */
    cmdreset(cm);
    if (cminfile) {
	cp = fgets(cm->cmd_inp, (int)cm->cmd_left-1, cminfile);
	if (!cp) {
	    fprintf(stdout, "[EOF on %s]\n", cminfname);
	    fclose(cminfile);
	    cminfile = NULL;
	    return cmdlsetup(cm);	/* Try again using TTY */
	}
	fputs(cp, stdout);		/* Echo the input line */
    } else {
	cp = fe_ctycmline(cm->cmd_inp, (int)cm->cmd_left-1);
	if (cp == NULL) {
	    if (feof(stdin) || ferror(stdin)) {
		cp = strncpy(cm->cmd_inp, "exit", (int)cm->cmd_left-1);
		fprintf(stdout, "exit\n");
		clearerr(stdin);
	    } else {
		return NULL;
	    }
	}
    }

    len = strlen(cp);
    cm->cmd_left -= len;
    cm->cmd_inp += len;

    return cp;
}

/* CMDLCOPY - Copy a command line into the previously indicated buffer.
*/
char *
cmdlcopy(struct cmd_s *cm, char *line)
{
    int len = strlen(line);
    if (len > cm->cmd_blen) {
	len = cm->cmd_blen - 1;
    }

    if (cpu.fe.fe_debug)
	fprintf(stderr, "[cmdlcopy]");

    strncpy(cm->cmd_buf, line, len);
    cm->cmd_buf[len] = '\0';

    cm->cmd_rdp = cm->cmd_buf;
    cm->cmd_inp = cm->cmd_buf + len;
    cm->cmd_rleft = len;
    cm->cmd_left = cm->cmd_blen - len;

    return cm->cmd_buf;
}


/* CMDFLS - Flush whitespace from current command pos
*/
void
cmdspfls(register struct cmd_s *cm)
{
    register char *cp = cm->cmd_rdp;

    if (cp)
	while (isspace(*cp)) ++cp;
    cm->cmd_rdp = cp;
}

/* Helper functions for FC_ commands */

static int
cmdargs_none(register struct cmd_s *cm)
{
    if (cm->cmd_rleft || cm->cmd_argc > 0) {
	printf("Bad syntax - no args allowed\n");
	return 0;
    }
    return 1;
}

static int
cmdargs_one(register struct cmd_s *cm, char **as1)
{
    if (cm->cmd_argc == 1) {
	*as1 = cm->cmd_argv[0];
	return 1;
    }
    if (cm->cmd_argc > 1) {
	printf("Bad syntax - too many args\n");
    } else
	printf("Bad syntax - no arg\n");
    return 0;
}

static int
cmdargs_two(register struct cmd_s *cm, char **as1, char **as2)
{
    if (cm->cmd_argc == 2) {
	*as1 = cm->cmd_argv[0];
	*as2 = cm->cmd_argv[1];
	return 1;
    }
    printf("Bad syntax - need two args\n");
    return 0;
}

static int
cmdargs_all(register struct cmd_s *cm)
{
    /* See if haven't yet tokenized line and there's something on it */
    if (cm->cmd_argc == 0 && cm->cmd_rleft) {
	cm->cmd_argc = s_tokenize(cm->cmd_argv,
				      CMDMAXARG,
				      &cm->cmd_tdp, &cm->cmd_tleft,
				      &cm->cmd_rdp, &cm->cmd_rleft);
	cm->cmd_argv[cm->cmd_argc] = NULL;
    }
    return cm->cmd_argc;	/* Return # of tokens */
}

static int
cmdargs_n(register struct cmd_s *cm, int n)
{
    /* See if haven't yet tokenized line and there's something on it */
    if (cm->cmd_argc == 0 && cm->cmd_rleft) {
	if (n > CMDMAXARG)
	    n = CMDMAXARG;
	cm->cmd_argc = s_tokenize(cm->cmd_argv,
				  n,
				  &cm->cmd_tdp, &cm->cmd_tleft,
				  &cm->cmd_rdp, &cm->cmd_rleft);
	cm->cmd_argv[cm->cmd_argc] = NULL;
    }
    return cm->cmd_argc;	/* Return # of tokens parsed */
}

#ifndef CMVPAR_INCLUDED		/* Using external parser stuff? */
# define CMVPAR_INCLUDED 0	/* Nope, use our own */
#endif

#if !CMVPAR_INCLUDED
	/* Use own internal parsing code */

struct cmkey_s *
cmdkeylookup(char *cp,
	     register struct cmkey_s *keytab,
	     register struct cmkey_s **key2)
{
    struct cmkey_s *key;

    (void) s_keylookup(cp, (voidp_t)keytab, sizeof(struct cmkey_s),
			(voidp_t*)&key, (voidp_t*)key2);
    return key;
}

#endif /* !CMVPAR_INCLUDED */

static void
fc_ques(struct cmd_s *cm)
{
    printf("Type \"help\" or \"help <command>\" for help.\n");
}

void
fc_gques(struct cmd_s *cm)
{
    fc_ques(cm);
}

static void
helpline(register struct cmkey_s *kp)
{
    int cols;
    register char *cp;

    if ((cols = 20 - strlen(kp->cmk_key)) < 0)
	cols = 0;
    if (!(cp = kp->cmk_p->cmn_rtn.cmr_synt))
	cp = "";
    if (cols < strlen(cp))	/* Key and syntax too long? */
	printf("%s %s\n                      %s\n",
		kp->cmk_key, cp, kp->cmk_p->cmn_rtn.cmr_help);
    else
	printf("%s %-*s %s\n",
		kp->cmk_key, cols, cp, kp->cmk_p->cmn_rtn.cmr_help);
}

static void
fc_help(struct cmd_s *cm)
{
    struct cmkey_s *kp, *key2;
    int cols;
    register char *cp;

    /* Get first token on line.  OK if NULL. */
    cp = cm->cmd_argv[0];

    if (!cp || !*cp) {		/* If no specific arg, show everything */
	for (kp = cm->cmd_keys; kp->cmk_key; ++kp) {
	    helpline(kp);
	}
	return;
    }

    (void) s_keylookup(cp, (voidp_t)cm->cmd_keys, sizeof(struct cmkey_s),
				(voidp_t *)&kp, (voidp_t *)&key2);
    if (!kp) {
	printf("Unknown command: \"%s\"\n", cp);
	return;
    }
    if (!key2) {		/* Just one match, so show it in detail */
	helpline(kp);
	if ((cp = kp->cmk_p->cmn_rtn.cmr_desc) && *cp)
	    printf("%s\n", cp);
	return;
    }

    /* More than one match, show each one */
    for (kp = cm->cmd_keys; kp->cmk_key; ++kp) {
	if (smatch(cp, kp->cmk_key) > 0)
	    helpline(kp);
    }
}

void
fc_ghelp(struct cmd_s *cm)
{
    fc_help(cm);
}

/* SLINLIM - Limit string to 1 line by chopping it at first EOL seen.
*/
static void
slinlim(register char *s)
{
    for (; *s; ++s)
	if (*s == '\r' || *s == '\n') {
	    *s = '\0';
	    break;
	}
}

static void
stolower(register char *s)
{
    for (; *s; ++s)
	if (isupper(*s))
	    *s = tolower(*s);
}

/* SMATCH - compare strings
**	Returns 0 if mismatched
**	        1 if S1 is initial substring of S2
**		2 if S1 is exact match of S2
*/
static int
smatch(register char *s1,
       register char *s2)
{
    while (*s1) {
	if (*s1++ != *s2++)
	    return 0;
    }
    return *s2 ? 1 : 2;
}

/* KLH10_INIT - Actually an internal routine to initialize and configure
**	the emulator.  Could be considered analogous to "loading the
**	microcode"...
*/
static void
klh10_init(void)
{

#if KLH10_SYS_T20 && KLH10_CPU_KS
# if KLH10_CTYIO_ADDINT
    cpu.fe.cty_lastint = 20;	/* 20ms delay to add extra output-done int */
# else
    cpu.fe.fe_iowait = 50;	/* Alternative for avoiding T20 CTY lossage */
# endif
#endif
    cpu.fe.fe_intchr = '\034';	/* Default int char is FS (ctrl-\) */
    cpu.mr_exsafe = 2;		/* Default to exec-mode safety check/halt */

    /* Move this to pag_init? */
    /* Some checking that couldn't be done at compile time */
    if (VMF_ACC & PAG_PAMSK) {
	error("Pager access bits (%lx) overlap phys page mask (%lx)!\n",
			(long)VMF_ACC, (long)PAG_PAMSK);
    }


    op_init();		/* Initialize runtime opcode dispatch tables */
#if (KLH10_CPU_KS || KLH10_CPU_KL) && (KLH10_SYS_T10 || KLH10_SYS_T20)
  {
    extern void inexts_init(void);
    inexts_init();		/* Initialize EXTEND instruction stuff */
  }
#endif

    /* Now can add in any special runtime cases */

    mem_init();		/* Initialize memory */
    apr_init();		/* Initialize APR stuff (includes internal devs) */
    dev_init();		/* And general IO device stuff */
    cty_init();		/* And console TTY stuff */
}

/* MEM_INIT - Allocate & initialize PDP10 memory.
**	Perhaps move this to pag_init?
*/
static void
mem_init(void)
{
    size_t memsiz;
    char *ptr;

    /* Determine amount of phys mem to get */
    memsiz = (size_t)PAG_SIZE * PAG_MAXPHYSPGS * sizeof(w10_t);

    /* Always attempt to get shared memory first.  If that fails
    ** (either because OS can't do it, or KLH10 was compiled without
    ** support), then go for private memory.
    */
    fprintf(stdout, "[MEM: Allocating %ld pages ", (long)PAG_MAXPHYSPGS);
    if (os_mmcreate(memsiz, &cpu.mm_physegid, &ptr)) {
	cpu.mm_shared = TRUE;
	fprintf(stdout, "shared memory, clearing...");
    } else {
	/* Failed - routine already barfed about it, if OS failure. */
	cpu.mm_shared = FALSE;

	/* Allocate private 10 memory */
	cpu.mm_physegid = 0;
	fprintf(stdout, "private memory, clearing...");
	if (!(ptr = malloc(memsiz))) {
	    syserr(errno, "MEM: cannot malloc KN10 physical memory!\n");
	}
    }

    cpu.physmem = (vmptr_t)ptr;

    /* To avoid unpleasant surprises to code that expects unused bits
    ** of w10_t words to be zero, we clear it all to begin with.
    */
    memset(ptr, 0, memsiz);		/* Clear it all out */
    fprintf(stdout, "done]\n");
}

/* MEM_TERM - Terminate (de-init) memory stuff.
**	Mainly intended to clean up shared-seg stuff.  May later be useful
**	for re-configuring memory.
**	Will of course bomb completely if anything refs phys mem after
**	this call...
*/
static void
mem_term(void)
{
    if (cpu.mm_shared) {
	os_mmkill(cpu.mm_physegid, (char *)cpu.physmem);
	cpu.mm_physegid = 0;
    } else {
	free((char *)cpu.physmem);
    }
    cpu.physmem = NULL;
    cpu.mm_shared = FALSE;
}

static int
mem_setlock(FILE *of, FILE *ef, int nlockf)
{
    if (nlockf == cpu.mm_locked) {
	if (of)
	    fprintf(of, "Memory already %slocked\n", nlockf ? "" : "un");
	return TRUE;
    }
    if (os_memlock(nlockf)) {
	if (of)
	    fprintf(of, "Memory %slocked\n", nlockf ? "" : "un");
	cpu.mm_locked = nlockf;
	return TRUE;
    } else {
	if (ef)
	    fprintf(ef, "Memory could not be %slocked - %s\n",
				(nlockf ? "" : "un"), os_strerror(-1));
	return FALSE;
    }
}

/* Support routines for FC_SET - parameter parsing
*/

static int cmvp_prompt(struct prmvcx_s *);
static int cmvp_sethz(struct prmvcx_s *);
static int cmvp_setpri(struct prmvcx_s *);
static int cmvp_memlock(struct prmvcx_s *);
static int cmvp_serialno(struct prmvcx_s *);

extern int ld_debug;	/* From feload.c */
#if KLH10_DEBUG && KLH10_CPU_KS
extern int tim_debug;	/* From kn10cpu.c */
#endif
#if KLH10_CPU_KS
extern int ub_debug;	/* From dvuba.c */
#endif

struct prmvar_s fecmvars[] = {
#if KLH10_CPU_KL || KLH10_CPU_KS
    PRMVAR("serialno", "CPU serial number",
				PRMVT_DEC, &cpu.mr_serialno,
							cmvp_serialno, NULL),
#endif
    PRMVAR("sw", "Data switch word",
				PRMVT_WRD, &cpu.mr_dsw, NULL, NULL),
    PRMVAR("mem_lock", "Set on to attempt locking memory",
				PRMVT_BOO, &cpu.mm_locked, cmvp_memlock, NULL),
    PRMVAR("proc_pri", "CPU process priority",
				PRMVT_DEC, &proc_pri, cmvp_setpri, NULL),
    PRMVAR("cpu_debug", "General CPU debug trace",
				PRMVT_BOO, &cpu.mr_debug, NULL, NULL),
    PRMVAR("cpu_exsafe", "Enable exec mode safety halts",
				PRMVT_OCT, &cpu.mr_exsafe, NULL, NULL),
    PRMVAR("fe_intchr", "KLH10 cmd escape char",
				PRMVT_OCT, &cpu.fe.fe_intchr, NULL, NULL),
    PRMVAR("fe_prompt", "KLH10 cmd prompt",
				PRMVT_STR, &cmdprompt, cmvp_prompt, NULL),
    PRMVAR("fe_runenable", "Enable running KN10 during KLH10 cmd processing",
				PRMVT_BOO, &cpu.fe.fe_runenable, NULL, NULL),
    PRMVAR("fe_debug", "FE debug trace",
				PRMVT_BOO, &cpu.fe.fe_debug, NULL, NULL),
    PRMVAR("cty_debug", "CTY debug trace",
				PRMVT_BOO, &cpu.fe.fe_ctydebug, NULL, NULL),
#if KLH10_SYS_T20 && KLH10_CPU_KS
    PRMVAR("cty_iowait", "CTY output delay, usec",
				PRMVT_DEC, &cpu.fe.fe_iowait, NULL, NULL),
# if KLH10_CTYIO_ADDINT
    PRMVAR("cty_lastint", "CTY last-char extra output int, msec",
				PRMVT_DEC, &cpu.fe.cty_lastint,	NULL, NULL),
# endif
#endif
#if KLH10_DEBUG && KLH10_CPU_KS
    PRMVAR("tim_debug", "KS timebase debug trace",
				PRMVT_BOO, &tim_debug, NULL, NULL),
#endif
#if KLH10_CPU_KS
    PRMVAR("ub_debug", "KS unibus debug info (bad ctl/addr warnings)",
				PRMVT_BOO, &ub_debug, NULL, NULL),
#endif
    PRMVAR("ld_fmt", "LOAD/DUMP word format",
				PRMVT_STR, &ld_fmt, NULL, NULL),
    PRMVAR("ld_debug", "LOAD debug trace",
				PRMVT_BOO, &ld_debug, NULL, NULL),
    PRMVAR("insbreak", "APR loop interrupt",
				PRMVT_DEC, &cpu.mr_insbreak, NULL, NULL),
#if KLH10_CLKTRG_COUNT
    PRMVAR("clk_ipms", "Instrs per virt msec",
				PRMVT_DEC, &cpu.clk.clk_ipmsrq,
	  						cmvp_sethz,NULL),
#endif
#if 1 /* KLH10_CLKTRG_OSINT */
    PRMVAR("clk_ithz", "OS interval timer - current value in Hz",
				PRMVT_DEC, &cpu.clk.clk_ithzcmreq,
							cmvp_sethz, NULL),
    PRMVAR("clk_ithzfix", "ITimer value fixed at this if non-zero",
				PRMVT_DEC, &cpu.clk.clk_ithzfix,
							cmvp_sethz, NULL),
    PRMVAR("clk_ithzosreq", "ITimer value last requested by OS",
				PRMVT_DEC, &cpu.clk.clk_ithzosreq,
				NULL, NULL),
#endif
    PRMVAR("pisys_on", "Set if PI sys on",
				PRMVT_OCT, &cpu.pi.pisys_on, NULL, NULL),
    PRMVAR("pilev_on", "Levs enabled",
				PRMVT_OCT, &cpu.pi.pilev_on, NULL, NULL),
    PRMVAR("pilev_pip", "Levs PI in Progress",
				PRMVT_OCT, &cpu.pi.pilev_pip, NULL, NULL),
    PRMVAR("pilev_preq", "Prog PI reqs",
				PRMVT_OCT, &cpu.pi.pilev_preq, NULL, NULL),
    PRMVAR("pilev_aprreq", "APR PI reqs",
				PRMVT_OCT, &cpu.pi.pilev_aprreq, NULL, NULL),
    PRMVAR("pilev_dreq", "Device PI reqs",
				PRMVT_OCT, &cpu.pi.pilev_dreq, NULL, NULL),
#if KLH10_CPU_KS
    PRMVAR("pilev_ub1req", "UBA #1 PI reqs",
				PRMVT_OCT, &cpu.pi.pilev_ub1req, NULL, NULL),
    PRMVAR("pilev_ub3req", "UBA #3 PI reqs",
				PRMVT_OCT, &cpu.pi.pilev_ub3req, NULL, NULL),
#endif
#if KLH10_CPU_KL
    PRMVAR("pilev_rhreq", "RH20 PI reqs",
				PRMVT_OCT, &cpu.pi.pilev_rhreq, NULL, NULL),
    PRMVAR("pilev_dtereq", "DTE20 PI reqs",
				PRMVT_OCT, &cpu.pi.pilev_dtereq, NULL, NULL),
#endif
    PRMVAR("feiosignulls", "# SIGIOs with no input",
				PRMVT_DEC, &feiosignulls, NULL, NULL),
    PRMVAR("feiosiginps", "# SIGIOs with CTY input",
				PRMVT_DEC, &feiosiginps, NULL, NULL),
    PRMVAR("feiosigtests", "# non-SIGIO tests in above",
				PRMVT_DEC, &feiosigtests, NULL, NULL),
    PRMVAR(NULL, "", PRMVT_NULL, NULL, NULL, NULL)
};

/* Various parameter get/set auxiliary functions */

static int
cmvp_prompt(register struct prmvcx_s *cx)
{
    /* First just set requested value normally */
    if (!prmvp_set(cx))
	return FALSE;	/* Problem setting param?  Already reported */

    /* Then must do special re-init stuff */
    fe_cmpromptset(cmdprompt);
    cmdpromptnew = TRUE;
    return TRUE;
}

static int
cmvp_memlock(register struct prmvcx_s *cx)
{
    return mem_setlock(cx->prmvcx_of, cx->prmvcx_ef, cx->prmvcx_val.vi);
}

static int
cmvp_setpri(register struct prmvcx_s *cx)
{
    ospri_t npri = cx->prmvcx_val.vi;
    ospri_t opri;

    if (os_setpriority(npri) == FALSE) {
	if (cx->prmvcx_ef)
	    fprintf(cx->prmvcx_ef, "Could not change priority - %s\n",
					os_strerror(-1));
	return FALSE;
    }
    if (os_getpriority(&opri) == FALSE) {
	if (cx->prmvcx_ef)
	    fprintf(cx->prmvcx_ef, "Could not find priority - %s\n",
					os_strerror(-1));
	return FALSE;
    }
    if (cx->prmvcx_of) {
	if (opri == proc_pri)
	    fprintf(cx->prmvcx_of, "Process priority remains %ld\n",
							(long)opri);
	else
	    fprintf(cx->prmvcx_of, "Process priority changed from %ld to %ld\n",
					(long)proc_pri, (long)opri);
    }
    proc_pri = opri;
    return TRUE;
}

static int
cmvp_sethz(register struct prmvcx_s *cx)
{
    /* First just set requested value normally */
    if (!prmvp_set(cx))
	return FALSE;	/* Problem setting param?  Already reported */

    /* Then must do special re-init stuff */
#if KLH10_CLKTRG_COUNT
    clk_ipmsset(cpu.clk.clk_ipmsrq);
#elif KLH10_CLKTRG_OSINT
    clk_ithzset(cpu.clk.clk_ithzcmreq);
#endif
    return TRUE;
}

static int
cmvp_serialno(register struct prmvcx_s *cx)
{
#if KLH10_CPU_KL
    if (cx->prmvcx_val.vi <= 1024)
	fprintf(cx->prmvcx_of, "KL serial number should be > 1024\n");
    else if (cx->prmvcx_val.vi > 07777) {
	fprintf(cx->prmvcx_of, "Invalid KL serial number %ld\n",
						(long)cx->prmvcx_val.vi);
	return FALSE;
    }
#elif KLH10_CPU_KS
    if (cx->prmvcx_val.vi <= 4096)
	fprintf(cx->prmvcx_of, "KS serial number should be > 4096\n");
    else if (cx->prmvcx_val.vi > 077777) {
	fprintf(cx->prmvcx_of, "Invalid KS serial number %ld\n",
						(long)cx->prmvcx_val.vi);
	return FALSE;
    }
#endif
    if (!prmvp_set(cx))
	return FALSE;	/* Problem setting param?  Already reported */
    apr_init_aprid();		/* Reset APR ID */
    return TRUE;
}



static int
addrparse(register char *str,
	  vaddr_t *vloc,
	  enum fevmmode *mloc)
{
    w10_t w;
    enum fevmmode mode = FEVM_DFLT;
    int local = FALSE, global = FALSE;

    if (!str) return FALSE;
    while (isalpha(*str)) {
	switch (islower(*str) ? toupper(*str++) : *str++) {
	case 'L':	local  = TRUE;		break;
	case 'G':	global = TRUE;		break;
	case 'C':	mode = FEVM_CUR;	break;
	case 'P':	mode = FEVM_PHYS;	break;
	case 'E':	mode = FEVM_EXEC;	break;
	case 'U':	mode = FEVM_USER;	break;
	case 'A':	mode = FEVM_ACB;	break;
	default:
		return 0;	/* Bad syntax, unknown char */
	}
    }
    *mloc = mode;

    /* Note distinction between 123456 and 0,,123456
    ** (ie explicit specification of section #)
    */
    switch (s_towd(str, &w)) {
    default:			/* Bad syntax of address */
	return FALSE;
    case 1:			/* One value: <one> */
	if (!local && !global)	/* If not otherwise specified, */
	    local = TRUE;	/* defaults to local */
	break;
    case 2:			/* Two values: <one>,,<two> */
	if (!local && !global)	/* If not otherwise specified, */
	    global = TRUE;	/* defaults to global */
	break;
    }

    /* Problem if local - where does default section # come from?
    ** Depends on mode, but even the mode gets hairy.
    */
    if (mode == FEVM_ACB) {		/* Special hack, ac block # in LH */
	if ((LHGET(w) & ~(h10_t)07)	/* Only allow blocks 0-7 */
	  || (RHGET(w) & ~(h10_t)AC_MASK))	/* and ACs 0-017 */
	    return FALSE;
	va_hmake(*vloc, LHGET(w), RHGET(w));
	return TRUE;
    }
    if (local)
	va_lmake(*vloc, LHGET(w) & VAF_SMSK, RHGET(w));
    else
	va_gmake(*vloc, LHGET(w) & VAF_SMSK, RHGET(w));
    return TRUE;
}


/* FC_SET - Set/Show KLH10 variables
*/
static void
fc_set(struct cmd_s *cm)
{
    struct prmvar_s *p1, *p2;
    int res;
    char *cp = cm->cmd_arglin;

    if (!cp || !*cp			/* If no arg, show all vars */
      || *cp == '?') {			/* Ditto if starts with ? */
	for (p1 = fecmvars; p1->prmv_name; ++p1)
	    prm_varshow(p1, stdout);
	return;
    }
    if (!strchr(cp, '=')) {		/* If doesn't look like var=val */
	res = s_keylookup(cp, (voidp_t)fecmvars, sizeof(*p1),
			  (voidp_t*)&p1, (voidp_t*)&p2);
	if (res == 1) {
	    prm_varshow(p1, stdout);
	} else if (res > 1) {
	    fprintf(stdout, "Ambiguous variable: \"%s\", \"%s\"%s\n",
		p1->prmv_name, p2->prmv_name, (res > 2 ? ", ..." : ""));
	} else {
	    fprintf(stdout, "Unknown var or bad syntax, must be <var>=<value>\n");
	}
	return;	
    }

    prm_varset(&cp, fecmvars, stdout, stdout);
}


/* Device control commands */

/* FC_DEVLOAD - Load a dynamic/shared library as a device driver,
**	and defines its name for later use in configuration.
**
**    Syntax is:
**	devload <drvname> <path> <initsym> [<comment>]
**
** where
**	<drvname> - Arbitrary device driver name.
**	<path>	  - Pathname for library in native OS.
**	<initsym> - "Entry point" symbol (init/config routine)
**	<comment> - Optional description text
*/
static void
fc_devload(struct cmd_s *cm)
{
    if (cmdargs_n(cm, 3) != 3) {
	printf("?Bad syntax - usage: %s\n", cd_devload.cmr_synt);
	return;
    }
    stolower(cm->cmd_argv[0]);	/* Driver name forced to lowercase */
    (void) dev_drvload(stdout,
		       cm->cmd_argv[0],
		       cm->cmd_argv[1],
		       cm->cmd_argv[2],
		       cm->cmd_rdp);
}


/* FC_DEVDEF - Do initial device configuration
**	Defines the device name and does its initial configuration setup.
**
** Syntax is:
**	devdefine <uniqname> { <10dev> } <drvname> [<devargs>]
**		             { UB<n>   }
** where
**	<name> - Name to identify device
**	<10dev> - A PDP-10 device number, or predefined keyword (CTY, etc)
**			Implies device uses dev-IO instructions.
**	UB<n>  - A Unibus controller (n = 1 or 3)
**			Implies device uses KS IO instructions.
**	<drvname> - Name of a driver module, either builtin or dynamic.
**	<devargs> - Optional args to dev's init routine (rest of line)
**
** NOTE: this replaces old design of:
**	devconf <uniqname> { <10dev> }  {static   <drvname>     }  <devargs>
**		           { UB<n>   }  {extern <path> <initsym>}
**	but the static/extern option has been replaced by DEVLOAD.
*/
static void
fc_devdef(struct cmd_s *cm)
{
    if (cmdargs_n(cm, 3) != 3) {
	printf("?Bad syntax - usage: %s\n", cd_devdef.cmr_synt);
	return;
    }
    stolower(cm->cmd_argv[0]);	/* Driver name forced to lowercase */
    stolower(cm->cmd_argv[1]);	/* Device number ditto */
    stolower(cm->cmd_argv[2]);	/* Driver name ditto */
    (void) dev_define(stdout,
		      cm->cmd_argv[0],
		      cm->cmd_argv[1],
		      cm->cmd_argv[2],
		      cm->cmd_rdp);
}


static void
fc_devshow(struct cmd_s *cm)
{
    /* Pass first token on line.  OK if NULL. */
    (void) cmdargs_n(cm, 1);
    (void) dev_show(stdout, cm->cmd_argv[0], cm->cmd_rdp);
}

#if KLH10_EVHS_INT
static void
fc_devevshow(struct cmd_s *cm)
{
    /* Pass first token on line.  OK if NULL. */
    (void) cmdargs_n(cm, 1);
    (void) dev_evshow(stdout, cm->cmd_argv[0], cm->cmd_rdp);
}
#endif /* KLH10_EVHS_INT */

/* FC_DEVSET - Hack defined device from 10 side
**	For use when need to set device-related things from the 10's side of
**	the fence, for a specific device.
**  Syntax:
**	devset <name> <devvar>=<val> [devvar2>=<val> ...]
*/
static void
fc_devset(char *argline)
{
}


/* FC_DEV_CMD - Generic device command
**    Syntax:	dev <name> <commandline>
**
** Invokes device's command parsing and execution, which can be anything
** the device wants to do with the line.  Most should follow certain
** conventions, however:
**	dev <name> help		- Show what this device understands
**	dev <name> status	- Show status in appropriate form
**	dev <name> show [param] [...]		- Show specific device vars
**	dev <name> set  [param=val] [...]	- Set them
*/
static void
fc_dev_cmd(struct cmd_s *cm)
{
    /* Consume first token on line - must exist */
    if (cmdargs_n(cm, 1) != 1) {	/* If no token */
	printf("Bad dev cmd syntax, need device specifier\n");
	return;
    }
    (void) dev_command(stdout, cm->cmd_argv[0], cm->cmd_rdp);
}


static void
fc_dev_help(struct cmd_s *cm)
{
    /* Pass first token on line.  OK if NULL. */
    (void) cmdargs_n(cm, 1);
    (void) dev_help(stdout, cm->cmd_argv[0], cm->cmd_rdp);
}

static void
fc_dev_status(struct cmd_s *cm)
{
    /* Pass first token on line.  OK if NULL. */
    (void) cmdargs_n(cm, 1);
    (void) dev_status(stdout, cm->cmd_argv[0], cm->cmd_rdp);
}

static void
fc_devmnt(struct cmd_s *cm)
{
    char *spath;

    /* Consume first two tokens on line - must exist */
    switch (cmdargs_n(cm, 2)) {
    case 0:
	printf("Bad syntax - <devID> must be specified\n");
	return;
    case 1:
	spath = "";	/* Ensure 2nd arg non-null as NULL means dismount */
	break;
    default:
	spath = cm->cmd_argv[1];
	break;
    }
    (void) dev_mount(stdout, cm->cmd_argv[0], spath, cm->cmd_rdp);
    fedevchkf = dev_dpchk_ctl(TRUE);	/* Start periodic checks if nec */
}

static void
fc_devunmnt(struct cmd_s *cm)
{
    /* Consume first token on line - must exist */
    if (cmdargs_n(cm, 1) != 1) {	/* If no token */
	printf("Bad syntax - <devID> must be specified\n");
	return;
    }
    (void) dev_mount(stdout, cm->cmd_argv[0], (char *)NULL, cm->cmd_rdp);
    fedevchkf = dev_dpchk_ctl(TRUE);	/* Start periodic checks if nec */
}

static void
fc_devdbg(struct cmd_s *cm)
{
    /* Pass first two tokens on line.  OK if NULL. */
    if (cmdargs_n(cm, 2) < 1)
	cm->cmd_argv[1] = NULL;
    (void) dev_debug(stdout, cm->cmd_argv[0], cm->cmd_argv[1], cm->cmd_rdp);
}

static void
fc_devwait(struct cmd_s *cm)
{
    char *dev;
    long totsec = -1;
    osstm_t stm;	/* Sleep time spec */

    /* Consume first two tokens on line.  OK if NULL. */
    switch (cmdargs_n(cm, 2)) {
    case 0:
	dev = NULL;		/* All devices */
	totsec = -1;		/* Indefinitely */
	break;
    case 1:		/* One arg, either <dev> or <secs> */ 
	dev = cm->cmd_argv[0];
	if (s_todnum(dev, &totsec))
	    dev = NULL;		/* Numeric, is <secs> */
	else
	    totsec = -1;	/* Not numeric, is <dev> */
	break;
    default:
	dev = cm->cmd_argv[0];
	if (!s_todnum(cm->cmd_argv[1], &totsec)) {
	    printf("Bad syntax - \"%s\" must be timeout in secs\n",
		   cm->cmd_argv[1]);
	    return;
	}
	break;
    }

    /* OK, now start the wait. */
    OS_STM_SET(stm, totsec);
    while (dev_waiting(stdout, dev)) {
	if (os_msleep(&stm) <= 0)
	    break;		/* Stop waiting if timed out */
    }
}

/* FC_DEVBOOT - Boot using specified device.
**	devboot <devid> [halt]
**		Uses device's "read-in mode" to read in a small piece of
**		bootstrap code and starts execution there.
**		If option "halt" given, doesn't actually start PDP-10,
**		just sets start addr so if continued will start bootstrap.
*/
static void
fc_devboot(struct cmd_s *cm)
{
    int opthalt = FALSE;
    vaddr_t bootsa;

    if (!aprhalted()) {
	printf("KN10 still running!  Halt or Reset it first.\n");
	return;
    }

    /* Consume first two tokens on line */
    switch (cmdargs_n(cm, 2)) {
    case 0:
	printf("Bad syntax - <devID> must be specified\n");
	return;
    case 1:
	break;
    default:
	if (strcasecmp(cm->cmd_argv[1], "halt")==0)
	    opthalt = TRUE;
	else {
	    printf("Unknown option \"%s\"\n", cm->cmd_argv[1]);
	    return;
	}
	break;
    }

    if (!dev_boot(stdout, cm->cmd_argv[0], &bootsa)) {
	printf("Bootstrap readin of %s failed\n", cm->cmd_argv[0]);
	return;
    }

    /* Success!  Set up returned boot address as both new PC and new
       start address, and maybe go.
    */
    ddt_loadsa = bootsa;
    PC_SET(bootsa);
    if (opthalt) {
	printf("Bootstrap read in, halted with PC = %lo\n", (long)bootsa);
	return;
    }
    printf("Bootstrap read in\n");
    fe_aprcont(FEMODE_CTYRUN, FEAPRF_START, bootsa, 0);
}

/* FC_RESET - Halts and Resets PDP-10 (clears all status)
*/
static void
fc_reset(struct cmd_s *cm)
{
    fc_halt(cm);		/* Ensure halted if not already */
    apr_init();
}

/* FC_GO - Starts PDP-10 at given location.  Defaults to last
**	loaded start address, if any.
*/
static void
fc_go(struct cmd_s *cm)
{
    vaddr_t loc;
    enum fevmmode mode;
    char *sloc = cm->cmd_arglin;

    if (!aprhalted()) {
	printf("KN10 still running!  Halt or Reset it first.\n");
	return;
    }

    if (sloc && *sloc) {
	if (!addrparse(sloc, &loc, &mode)) {
	    printf("?Bad address\n");
	    return;
	} else if (mode != FEVM_DFLT && mode != FEVM_CUR) {
	    printf("?Bad address mode - only Current allowed\n");
	    return;
	} else if (va_isglobal(loc)) {
	    printf("?Bad address - only local allowed\n");
	    return;
	}
	ddt_loadsa = loc;	/* Mode will always be current */
    }
    fe_aprcont(FEMODE_CTYRUN, FEAPRF_START, ddt_loadsa, 0);
}

/* FC_CONT - Continues PDP-10 at current PC.
**	Safe to call this even if KN10 is running.
**	
*/
static void
fc_cont(struct cmd_s *cm)
{
    fe_aprcont(FEMODE_CTYRUN,
	       (aprhalted() ? FEAPRF_VERBOSE : 0),
	       0, 0);
}

/* FE_APRCONT - Resume execution.
**	(old startf: +1 start, 0 continue, -1 continue verbosely)
*/
void
fe_aprcont(int femode,
	   int startf,
	   vaddr_t newpc,
	   int nsteps)
{
    int res;

    if (cpu.fe.fe_debug)
	fprintf(stderr, "[aprcont %o 0x%x %d, intf_fecty %d]",
		femode, startf, nsteps, cpu.intf_fecty);

    if (fedevchkf)
	fedevchkf = dev_dpchk_ctl(FALSE);	/* Turn off dev checking! */
    if (startf & FEAPRF_START)
	PC_SET(newpc);

    if (startf & (FEAPRF_START | FEAPRF_VERBOSE))
	printf("%s KN10 at loc %#lo...\n",
			((startf&FEAPRF_START) ? "Starting" : "Continuing"),
			(long)PC_30);

    /* Set up TTY handling appropriately for running */
    fe_ctyenable(femode);	/* No echo, no CR/LF hacks, no delay */
    cpu.mr_running = TRUE;
    cpu.mr_1step = nsteps;
    res = apr_run();		/* Go! */
    cpu.mr_running = FALSE;
    fe_ctydisable((res == HALT_FECMD)	/* Restore TTY mode if necessary */
		  ? FEMODE_CMDRUN
		  : FEMODE_CMDHALT);
    switch (res) {
	case HALT_PROG:		/* Program halt (JRST 4,) */
	    printf("[HALTED: Program Halt, PC = %lo]\n", (long)PC_30);
	    break;

	case HALT_FECTY:	/* FE Console interrupt */
	    printf("[HALTED: FE interrupt]\n");
	    break;

	case HALT_FECMD:	/* FE Console command ready to execute */
	    break;

	case HALT_BKPT:		/* Hit breakpoint */
	    printf("[HALTED: Breakpoint]\n");
	    break;

	case HALT_STEP:		/* Single-Stepping */
	    break;

	case HALT_EXSAFE:	/* Something bad in exec mode */
	    printf("[HALTED: Exec program error? (\"set cpu_exsafe=1\" to continue)]\n");
	    break;

	case HALT_PANIC:	/* Panic - internal error, bad state */
	    printf("[HALTED: Panic - may be in inconsistent state]\n");
	    break;
    }
}


static int aprhalted(void)
{
    if (cpu.fe.fe_mode == FEMODE_CMDRUN)
	return FALSE;
    return TRUE;
}

/* FC_SHUTDOWN - Halts PDP-10 OS gracefully if possible, by
**	putting cruft in the shutdown location of the FECOM area.
**	Assumes physical mapping, as does the CTY code.
**
** KA/KI versions would probably set the data switches here.
*/
static void
fc_shutdown(struct cmd_s *cm)
{
    register vmptr_t vp;

    vp = vm_physmap(FECOM_SWIT0);	/* Find loc for data-switch 0 sim */
    op10m_seto(*vp);			/* Set word to ones */
    fc_cont(cm);			/* Resume CPU, let OS do rest */
}

/* FC_HALT - Halts PDP-10 violently.
**	Not needed until true threads/subprocess version exists, since
**	currently any interaction with KLH10 commands means the 10 is
**	already stopped!
*/
static void
fc_halt(struct cmd_s *cm)
{
    if (aprhalted()) {
	printf("KN10 already halted.\n");
	return;
    }
    cpu.fe.fe_mode = FEMODE_CMDHALT;
    printf("[HALTED: FE command, PC = %lo]\n", (long)PC_30);
}


/* Various commands primarily for debugging */

/* FC_ZERO - Clears first 256K of physical memory
*/
static void
fc_zero(struct cmd_s *cm)
{
    memset((char *)vm_physmap(0), 0, sizeof(w10_t)*(H10MASK+1)); /* Zap! */
    printf("OK\n");
}

/* FC_TRACE - Toggles execution tracing
*/
static void
fc_trace(struct cmd_s *cm)
{
    if (cpu.mr_dotrace) {
	cpu.mr_dotrace = 0;
	printf("Tracing now off\n");
    } else {
	cpu.mr_dotrace = TRUE;
	printf("Tracing now ON\n");
    }
}

#if 0	/* Not bound to a command now, use SET. */

/* FC_DEBUG - Toggles general debug flag
*/
static void
fc_debug(void)
{
    if (cpu.mr_debug) {
	cpu.mr_debug = 0;
	printf("Debug now off\n");
    } else {
	cpu.mr_debug = TRUE;
	printf("Debug now ON\n");
    }
}
#endif

/* FC_STEP - Single-steps by N instructions.
*/
static void
fc_step(struct cmd_s *cm)
{
    char *snum = cm->cmd_arglin;
    long n = 1;

    if (!aprhalted()) {
	printf("KN10 still running!  Halt it first.\n");
	return;
    }

    if (snum && *snum)
	if (!s_tonum(snum, &n)) {
	    printf("?Bad count syntax\n");
	    return;
	}
    if (n <= 0) {
	printf("?Bad step count\n");
	return;
    }
    fe_aprcont(FEMODE_CTYRUN, 0, 0, n);
    putchar('\n');
    nextinsprint(stdout, PINSTR_OPS);
}

/* FC_BKPT - Sets location to stop at.  0 clears.
*/
static void
fc_bkpt(struct cmd_s *cm)
{
    vaddr_t loc;
    enum fevmmode mode;
    char *sloc = cm->cmd_arglin;

    if (sloc && *sloc) {
	if (!addrparse(sloc, &loc, &mode)) {
	    printf("?Bad address\n");
	    return;
	} else if (mode != FEVM_DFLT && mode != FEVM_CUR) {
	    printf("?Bad address mode - only Current allowed\n");
	    return;
	} else if va_isglobal(loc) {
	    printf("?Bad address - only local allowed\n");
	    return;
	}
	
	cpu.mr_bkpt = loc;	/* Mode will always be current */
    }
}


/* FC_PROC - Proceed KN10 without CTY
*/
static void
fc_proc(struct cmd_s *cm)
{
    if (!aprhalted()) {
	/* Should only happen if already in FEMODE_CMDRUN */
	printf("KN10 already running\n");
	return;
    }
    cpu.fe.fe_mode = FEMODE_CMDRUN;	/* Change mode! */
}




/* FC_EXNEXT - Examine next word
** FC_EXPREV - Previous word
*/
static void
fc_exnext(struct cmd_s *cm)
{
    va_inc(ddt_cloc);
    fc_exa((struct cmd_s *)NULL);
}

static void
fc_exprev(struct cmd_s *cm)
{
    va_dec(ddt_cloc);
    fc_exa((struct cmd_s *)NULL);
}

/* FC_EXA - Examine PDP-10 word
*/
static void
fc_exa(struct cmd_s *cm)
{
    vaddr_t loc;
    enum fevmmode mode;
    register vmptr_t vp;
    char *sloc;

    sloc = (cm ? cm->cmd_arglin : NULL);

    if (sloc && *sloc) {
	if (!addrparse(sloc, &loc, &mode)) {
	    printf("?Bad address\n");
	    return;
	} else if (mode != FEVM_DFLT)
	    ddt_clmode = mode;	/* Set new mode if not default */
	ddt_cloc = loc;
    }

    putchar(' ');
    addrprint(stdout, ddt_cloc, ddt_clmode);
    putchar('/');
    putchar(' ');

    if ((vp = fevm_xmap(ddt_cloc, ddt_clmode))) {
	ddt_val = vm_pget(vp);
	wd1print(stdout, ddt_val);
	if (LHGET(ddt_val) & 0777000) {	/* Opcode exists? */
	    putchar('\t');
	    pinstr(stdout, ddt_val, 0, ddt_cloc/*unused*/);
	}
    } else
	fprintf(stdout, "\?\?");

    putchar('\n');
}

/* FC_DEP - Deposit PDP-10 word
*/
static void
fc_dep(struct cmd_s *cm)
{
    w10_t wd;
    vaddr_t loc;
    register vmptr_t vp;
    enum fevmmode mode;
    char *sloc, *sval;

    if (!cmdargs_two(cm, &sloc, &sval))
	return;

    if (sloc && *sloc) {
	if (!addrparse(sloc, &loc, &mode)) {
	    printf("?Bad address\n");
	    return;
	} else if (mode != FEVM_DFLT)
	    ddt_clmode = mode;	/* Set new mode if not default */
	ddt_cloc = loc;
    }

    if (!sval || !*sval || !s_towd(sval, &wd)) {
	printf("?Bad word syntax\n");
	return;
    }
    ddt_val = wd;
    
    if ((vp = fevm_xmap(ddt_cloc, ddt_clmode)))
	vm_pset(vp, ddt_val);
    else {
	printf("?Cannot map address ");
	addrprint(stdout, ddt_cloc, ddt_clmode);
	printf(" - value not deposited\n");
    }
}

static vmptr_t
fevm_map(paddr_t pa)
{
    if ((pa & ~AC_MASK)==0)
	return &cpu.acblk.cur[pa];	/* Use currently active AC block */

    /* For now, assume physical memory mapping.
    ** Later, can use current page map and report error if any failures.
    */
    return vm_physmap(pa & H10MASK);
}

vmptr_t
fevm_xmap(vaddr_t e, enum fevmmode mode)
{
    register pment_t *map;
    register acptr_t acp;
    register int acb;

    switch (mode) {
    default:
    case FEVM_CUR:	acp = cpu.acblk.cur; map = cpu.vmap.cur; break;
    case FEVM_PHYS:	acp = cpu.acblk.cur; map = pr_pmap;	break;
    case FEVM_USER:	acp = cpu.acblk.cur; map = cpu.pr_umap;	break;
    case FEVM_EXEC:	acp = cpu.acblk.cur; map = cpu.pr_emap;	break;
    case FEVM_ACB:
	/* Ensure AC block # in LH is 0-7 and AC # in RH is 0-017 inclusive */
	acb = va_lh(e);
	if ((acb & ~07) || (va_insect(e) & ~(h10_t)AC_MASK))
	    return NULL;		/* Ugh, one of them out of range */
	return &cpu.acblks[acb][va_ac(e)];	/* Use AC in right block */
    }
    return vm_xtrymap(e, VMF_READ, acp, map);
}

static int
fevm_tryeacalc(register w10_t *wp,
	       register acptr_t acp,
	       register pment_t *map,
	       int indcnt)	/* Indirection level count */
{
    register h10_t tmp;
    register vmptr_t vp;

#if KLH10_EXTADR
/*
	* ERROR * Need to revise this for XA
*/
#endif
    for (;;) {
	if ((tmp = LHGET(*wp)) & IW_X) {	/* Indexing? */
	    register w10_t wea;
	    wea = ac_xget(tmp & IW_X, acp);	/* Get c(X) */
	    LHSET(*wp, LHGET(wea));
	    RHSET(*wp, (RHGET(*wp)+RHGET(wea))&H10MASK); /* Add previous Y */
	}
	if (!(tmp & IW_I))			/* Indirection? */
	    return TRUE;			/* Nope, return now! */

	/* Handle indirection */
	if (--indcnt < 0)
	    return FALSE;
	if ((vp = vm_xtrymap(RHGET(*wp), VMF_READ, acp, map)) == NULL)
	    return FALSE;
	*wp = vm_pget(vp);
    }
}

/* FC_VIEW - Show PDP-10 status summary
*/
static void
fc_view(struct cmd_s *cm)
{
    printf("KN10 status: %s %s",
	   (cpu.fe.fe_mode == FEMODE_CMDRUN ? "RUNNING" : "STOPPED"),
	   (cpu.mr_usrmode ? "USER" : "EXEC"));
    if (cpu.mr_inpxct) printf(" PXCT-%o", cpu.mr_inpxct);
    if (cpu.mr_intrap) printf(" TRAP-%o", cpu.mr_intrap);
    if (cpu.mr_injrstf) printf(" JRSTF");	/* Either on or off */
    putchar('\n');

#if KLH10_JPC
    printf("  PC: %lo  [JPC: %lo  UJPC: %lo  EJPC: %lo]\n",
		(long)PC_30,
		(long)cpu.mr_jpc, (long)cpu.mr_ujpc, (long)cpu.mr_ejpc);
#if KLH10_ITS_JPC
    printf("  ITSPAGER UJPC: %lo  EJPC: %lo\n",
		(long)cpu.pag.pr_ujpc, (long)cpu.pag.pr_ejpc);
#endif
#endif /* KLH10_JPC */
    printf("  Flags: %lo\n", (long)cpu.mr_pcflags);

    nextinsprint(stdout, PINSTR_OPS);
}

/* FE_TRACEPRINT called from APR loop if tracing and about to execute
**	an instruction.
*/

void
fe_traceprint(register w10_t instr,
	      vaddr_t e)
{
    pishow(stdout);
    pcfshow(stdout, cpu.mr_pcflags);
    printf("%lo: ", (long)PC_30);

    pinstr(stdout, instr, PINSTR_OPS|PINSTR_EA, e);
    putc('\r', stdout);
    putc('\n', stdout);
}

void
fe_begpcfdbg(FILE *f)
{
    putc('[', f);
    pishow(f);
    pcfshow(f, cpu.mr_pcflags);
    fprintf(f,"%lo: => ", (long) PC_30);
}

void
fe_endpcfdbg(FILE *f)
{
    pishow(f);
    pcfshow(f, cpu.mr_pcflags);
    fprintf(f,"%lo:]\r\n", (long) PC_30);
}

void
pishow(FILE *f)		/* Show PI status */
{
    register int lev, num;
    if (cpu.pi.pilev_pip) {
	num = pilev_nums[cpu.pi.pilev_pip];		/* Find level # */
	fprintf(f, "PI%o", num);
	lev = cpu.pi.pilev_pip & ~pilev_bits[num];	/* And remaining PIPs */
	if (lev) {
	    fputc('[', f);
	    while (lev) {
		num = pilev_nums[lev];		/* Find level # */
		fprintf(f, "%o", num);
		lev &= ~pilev_bits[num];
	    }
	    putc(']', f);
	}
	putc(' ', f);
    }
}

struct { h10_t flag; char ch; } flgtab[] = {
	{ PCF_ARO, 'O'},  /* Arithmetic Overflow (or Prev Ctxt Public) */
	{ PCF_CR0, 'C'},  /* Carry 0 - Carry out of bit 0 */
	{ PCF_CR1, 'c'},  /* Carry 1 - Carry out of bit 1 */
	{ PCF_FOV, 'F'},  /* Floating Overflow */
	{ PCF_FPD, '1'},  /* First Part Done */
	{ PCF_USR, 'U'},  /* User Mode */
	{ PCF_UIO, 'I'},  /* User In-Out (or Prev Ctxt User) */
	{ PCF_PUB, 'P'},  /* Public Mode */
	{ PCF_AFI, 'A'},  /* Addr Failure Inhibit */
#if 0	/* Special-cased */
	{ PCF_TR2, 'x'},  /* Trap 2 (PDL overflow) */
	{ PCF_TR1, 'x'},  /* Trap 1 (Arith overflow) */
#endif
	{ PCF_FXU, 'f'},  /* Floating Exponent Underflow */
	{ PCF_DIV, 'D'}	  /* No Divide */
};

void		/* Show PC Flag status */
pcfshow(FILE *f,
	register h10_t flags)
{
    register int i;

    if (flags & (PCF_TR1|PCF_TR2)) {
	fprintf(f, "TRAP%o ", (int)FLDGET((uint32)flags, (PCF_TR1|PCF_TR2)));
	flags &= ~(PCF_TR1|PCF_TR2);
    }
    for (i = 0; flags && i < (sizeof(flgtab)/sizeof(flgtab[0])); ++i)
	if (flags & flgtab[i].flag) {		/* Found it? */
	    flags &= ~flgtab[i].flag;		/* Turn off */
	    putc(flgtab[i].ch, f);
	}
    if (flags) {			/* If any flags still left, */
	fprintf(f, "[PCF: %#lo]", (long)flags);	/* show those too */
    }
    putc(' ', f);
}

static void
nextinsprint(FILE *f,
	     int argf)
{
    w10_t w;
    vmptr_t vp;
    vaddr_t e;

    fprintf(f, "Next: ");
    pishow(f);
    pcfshow(f, cpu.mr_pcflags);
    fprintf(f, " %lo/ ", (long)PC_30);

    e = PC_VADDR;
    if (!(vp = fevm_xmap(e, FEVM_CUR))) {
	fprintf(f, " \?\?\n");
	return;
    }
    w = vm_pget(vp);
    if (LHGET(w) & 0777000) {
	pinstr(f, w, argf, e/*unused*/);
    } else
	wd2print(f, w);
    fprintf(f, "\n");
}


/* Print address as symbolic if possible.
**	This function isn't called by anything yet.
*/
static void
easymprint(FILE *f,
	   register vaddr_t e)
{

    /* Take care of any randomly set bits in LH */
    if (va_lh(e)) fprintf(f, "%lo,,", (long)va_lh(e));

    /* Now attempt to look up RH value as symbol? */


    fprintf(f, "%lo", (long)va_insect(e));	/* Punt for now */
}

static void
wd1print(FILE *f,
	 w10_t w)
{
    if (LHGET(w)) fprintf(f, "%lo,,", (long)LHGET(w));
    fprintf(f, "%lo", (long)RHGET(w));
}

static void
wd2print(FILE *f,
	 w10_t w)
{
    fprintf(f, "%lo,,%lo", (long)LHGET(w), (long)RHGET(w));
}


static void
addrprint(FILE *f,
	  vaddr_t vloc,
	  enum fevmmode mode)
{
    register pment_t *map = cpu.vmap.cur;
    register int ch;

    switch (mode) {
    default:
    case FEVM_CUR:	ch = 0;		break;
    case FEVM_PHYS:	ch = (map == pr_pmap)     ? 0 : 'P'; break;
    case FEVM_USER:	ch = (map == cpu.pr_umap) ? 0 : 'U'; break;
    case FEVM_EXEC:	ch = (map == cpu.pr_emap) ? 0 : 'E'; break;
    case FEVM_ACB:	ch = 'A';	break;
    }

    if (ch) {
	putc(ch, f);
	putc(' ', f);
    }
    if (va_lh(vloc)) {
	fprintf(f, "%lo,,", (long)va_lh(vloc));
    }
    fprintf(f, "%#lo", (long)va_insect(vloc));
}

static char *
strf6(char **acp,
      register w10_t w)
{
    register int i = 6;
    char *rp = *acp;
    register char *cp = rp;

    while (--i >= 0) {
	if (!LHGET(w) && !RHGET(w)) break;
	w = op10rot(w, 6);	/* Rotate high 6 bits to low 6 */
	*cp++ = (RHGET(w) & 077) + 040;
	RHSET(w, RHGET(w) & ~077);
    }
    *cp++ = 0;			/* Note moves past the nul char! */
    *acp = cp;			/* Update pointer */
    return rp;			/* And return start of original */
}

/* ITS disk-format time: */
#define DFTM_YEAR 0177000	/* LH: 4.7-4.1 Year, mod 100. */
#define DFTM_MON     0740	/* LH: 3.9-3.6 Month, 1=Jan */
#define DFTM_DAY      037	/* LH: 3.5-3.1 Day, 1-31 */
#define DFTM_SECS 0777776	/* RH: 2.9-1.2 Secs in day */
#define DFTM_HSEC      01	/* RH:     1.1 Half-second resolution */

static struct tm *
timefrits(register struct tm *t,
	  register w10_t w)
{
    t->tm_year = (LHGET(w) & DFTM_YEAR) >> 9;
    t->tm_mon = ((LHGET(w) & DFTM_MON) >> 5) - 1;
    t->tm_mday = (LHGET(w) & DFTM_DAY);
    t->tm_hour = RHGET(w) / (60*60*2);
    t->tm_min = (RHGET(w) % (60*60*2)) / 60;
    t->tm_sec = (RHGET(w) % (60*60*2)) % 60;
    return t;
}

/* FC_LOAD - Load given filename into PDP10 physical memory.
**	May use any executable type, stored on disk in any 36-bit format.
*/
int ld_debug = 0;	/* TRUE to show debug info; use SET to change */

static void
fc_load(struct cmd_s *cm)
{
    WFILE lwf;
    register FILE *f;
    int res = 0;
    int wft = -1;
    char *farg;

    if (!cmdargs_one(cm, &farg))
	return;

    if (!aprhalted()) {
	printf("KN10 still running!  Halt or Reset it first.\n");
	return;
    }

    /* Open file for reading */
    if (!(f = fopen(farg, "rb")))
	syserr(errno, "Couldn't open load file \"%s\"", farg);

    /* Determine word format to use */
    if (ld_fmt) {
	if ((wft = wf_type(ld_fmt)) < 0)
	    printf("Unknown ld_fmt setting \"%s\", using default.\n", ld_fmt);
    }
    if (wft < 0) {
	if ((wft = wf_type(ld_dfmt)) < 0)
	    wft = WFT_U36;
    }
    wf_init(&lwf, wft, f);		/* Init WFILE */
    printf("Using word format \"%s\"...\n", wf_typnam(&lwf));

    ld_inf.ldi_type = LOADT_UNKNOWN;	/* Init ld_inf (assume unknown) */
    ld_inf.ldi_debug = ld_debug;

    res = fe_load(&lwf, &ld_inf); 
    if (!res) {
	printf("Load failed for \"%s\".\n", farg);
    } else {
	printf("Loaded \"%s\":\n", farg);
	ddt_loadsa = RHGET(ld_inf.ldi_startwd);	/* Set default GO address */
    }
    printf("Format: %s\n", ld_inf.ldi_typname);
    printf("Data: %d, Symwds: %d, Low: %#lo, High: %#lo, Startaddress: %#lo\n",
		ld_inf.ldi_ndata, ld_inf.ldi_nsyms,
		(long)ld_inf.ldi_loaddr, (long)ld_inf.ldi_hiaddr,
		(long) RHGET(ld_inf.ldi_startwd));
    if (ld_inf.ldi_evlen != -1) {
	if (ld_inf.ldi_evlen != ((h10_t)I_JRST<<9))
	    printf("Entvec: %#lo wds at %#lo\n",
			(long) ld_inf.ldi_evlen, (long) ld_inf.ldi_evloc);
	else
	    printf("\
Entvec: JRST (120 ST: %#lo, 124 RE: %#lo, 137 VR: %lo,,%lo)\n",
			(long) vm_pgetrh(fevm_map(0120)),
			(long) vm_pgetrh(fevm_map(0124)),
			(long) vm_pgetlh(fevm_map(0137)),
			(long) vm_pgetrh(fevm_map(0137)));
    }

    if (ld_inf.ldi_aibgot) {
	register w10_t *aib = &ld_inf.ldi_asminf[0];
	char line[100];
	char *s = line;
	struct tm t;

	timefrits(&t, aib[AIB_TIME]);	/* Decompose ITS time */
	printf("\
Assembled by %s on %04d-%02d-%02d %02d:%02d:%02d from file \"%s:%s;%s %s\"\n",
		strf6(&s, aib[AIB_UNAME]),
		t.tm_year + 1900, t.tm_mon+1, t.tm_mday,
		t.tm_hour, t.tm_min, t.tm_sec,
		strf6(&s, aib[AIB_DEV]),
		strf6(&s, aib[AIB_DIR]),
		strf6(&s, aib[AIB_FN1]),
		strf6(&s, aib[AIB_FN2]) );
    }

    fclose(f);
}

/* FC_DUMP - Dump PDP10 physical memory into given filename.
**	Assumes either ITS SBLK or DEC CSAV format, stored on disk using
**	a selectable word mode.
*/
static void
fc_dump(struct cmd_s *cm)
{
    WFILE lwf;
    register FILE *f;
    int res = 0;
    int wft = -1;
    char *farg;

    if (!cmdargs_one(cm, &farg))
	return;
    if (!aprhalted()) {
	printf("KN10 still running!  Halt or Reset it first.\n");
	return;
    }
    /* Open file for writing */
    if (!(f = fopen(farg, "wb")))
	syserr(errno, "Couldn't open dump file \"%s\"", farg);

    /* Determine word format to use */
    if (ld_fmt) {
	if ((wft = wf_type(ld_fmt)) < 0)
	    printf("Unknown ld_fmt setting \"%s\", using default.\n", ld_fmt);
    }
    if (wft < 0) {
	if ((wft = wf_type(ld_dfmt)) < 0)
	    wft = WFT_U36;
    }
    wf_init(&lwf, wft, f);		/* Init WFILE */
    printf("Using word format \"%s\"\n", wf_typnam(&lwf));

#if KLH10_SYS_ITS
    printf("Using dump format ITS-SBLK\n");
    ld_inf.ldi_type = LOADT_SBLK;
#else
    printf("Using dump format DEC-CSAV\n");
    ld_inf.ldi_type = LOADT_DECSAV;
#endif
    ld_inf.ldi_debug = TRUE;
    ld_inf.ldi_loaddr = 0;
    ld_inf.ldi_hiaddr = H10MASK;

    res = fe_dump(&lwf, &ld_inf); 
    if (!res) {
	printf("Dump failed for \"%s\".\n", farg);
    } else {
	printf("Dumped \"%s\":\n", farg);
    }
    printf("Format %s, wftype %s\n", ld_inf.ldi_typname, wf_typnam(&lwf));
    printf("%ld words dumped from range %lo to %lo inclusive.\n",
		(long) ld_inf.ldi_ndata, (long) ld_inf.ldi_loaddr,
		(long) ld_inf.ldi_hiaddr);

    fclose(f);
}


#if KLH10_DEV_LITES
/* FC_LIGHTS - Sets console lights I/O base address
** Currently only allow LPT1 and LPT2 ports on PC.
*/
static void
fc_lights(struct cmd_s *cm)
{
    unsigned long port = 0;
    int c;
    char *sloc = cm->cmd_arglin;

    if (sloc && *sloc) {
        if (strcasecmp(sloc, "usb") == 0) {
	    if (!lites_init(0))
		printf("?Can't init lights -- probably not root\n");
	    return;
	}

        while(isxdigit(c = *sloc++)) {
	    port *= 16;
	    port += c - (isdigit(c) ? '0' : (islower(c) ? 'a' : 'A'));
	}
	if (!c) switch(port) {
	case 0x378:		/* LPT1 */
	case 0x278:		/* LPT2 */
	    if (!lites_init((unsigned int) port))
		printf("?Can't init lights -- probably not root\n");
	    return;
	}
    }
    printf("?Bad address\n");
}
#endif /* KLH10_DEV_LITES */

/* Instruction printing routines */

void
pinstr(FILE *f,
       register w10_t w,	/* Instruction word */
       int flags,		/* PINSTR_ flags */
       vaddr_t e)		/* E to use, if PINSTR_EA given */
{
    register int op, ac, x;
    register vmptr_t vp;
    register int opflg;
    struct opdef *opdf;

    op = iw_op(w) & 0777;		/* Get op, with some paranoia */
    ac = iw_ac(w);

    opdf = opcptr[op];			/* Get ptr to opcode definition */
    opflg = opdf ? opdf->opflg : 0;	/* Find flags */
    if (!opflg)				/* Cover up for unflagged instrs */
	opflg = IF_1X1;			/* Generic c(AC) & c(E) instr */

    if (opflg & IF_IO) {		/* Special IO-format instr? */
	x = ((op & 077)<<1) | ((ac >> 3)&01);	/* Find device */
	op = ac & 07;			/* Find IO operation */
	opflg = opcioflg[op];		/* Get new flags */
	fprintf(f, "%s", opcionam[op]);
	if (opcdvnam[x])
	    fprintf(f, " %s,", opcdvnam[x]);
	else
	    fprintf(f, " %o,", x<<2);	/* Special shift to emulate asmblr */
    } else {
	if (!opdf || !opdf->opstr)
	    fprintf(f, "%3o", op);
	else {
	    fprintf(f, "%s", opdf->opstr);
	    if (opflg & IF_OPN)
		fprintf(f, "-%03o", op);
	}

	/* If AC field is 0, only print it if AC is used. */
	if (ac || ((opflg & IF_AS) && (opflg&IF_AFMASK) != IF_A0))
	    fprintf(f, " %o,", ac);
	else putc(' ', f);
    }

    /* Now show I,X,Y for instruction.  If X is set, don't show a zero Y. */
    if (iw_i(w))
      putc('@', f);
    x = iw_x(w);
    if (iw_y(w) || !x)		/* Do Y only if NZ or no X */
      fprintf(f, "%lo", (long)iw_y(w));
    if (x)
	fprintf(f, "(%o)", x);

    /* Now see whether to show any args of instruction -- tricky part. */
    if ((flags & PINSTR_OPS)==0)
	return;		/* Nope */

    /* Show AC as operand? */
    if (opflg & IF_AS) {
	if (ac || (opflg & IF_AFMASK)) {
	    fprintf(f, "\t%o/ ", ac);
	    switch (opflg & IF_AFMASK) {
	    case IF_A0:
	    case IF_A1:
		wd1print(f, ac_get(ac));	/* Use current AC block */
		break;
	    case IF_A2:
		wd1print(f, ac_get(ac));	/* Use current AC block */
		fputs(" ? ", f);
		wd1print(f, ac_get(ac_off(ac,1)));	/* Show AC+1 */
		break;
	    case IF_A4:
		wd1print(f, ac_get(ac));	/* Use current AC block */
		fputs(" ? ", f);
		wd1print(f, ac_get(ac_off(ac,1)));	/* Show AC+1 */
		fputs(" ? ", f);
		wd1print(f, ac_get(ac_off(ac,2)));	/* Show AC+2 */
		fputs(" ? ", f);
		wd1print(f, ac_get(ac_off(ac,3)));	/* Show AC+3 */
		break;
	    }
	}
    }


    /* Show E as operand? */
    if (opflg & IF_MFMASK) {
	putc('\t', f);			/* Space out */
#if KLH10_EXTADR
/*
	* ERROR * Need to revise for XA eacalc?
*/
#endif
	if ((flags & PINSTR_EA)==0) {	/* Try computing E if not given */
	    flags |= PINSTR_EA;		/* Assume will succeed */
	    if (LHGET(w) & (IW_I|IW_X)) {
		w10_t eawd;
		eawd = w;
		if (!fevm_tryeacalc(&eawd, cpu.acblk.cur, cpu.vmap.cur, 2))
		    flags &= ~PINSTR_EA;	/* Oops, didn't succeed */
		else va_lmake(e, 0, RHGET(eawd));
	    } else va_lmake(e, 0, RHGET(w));
	}
	if ((flags & PINSTR_EA)==0)
	    fprintf(f, "E = \?\?");
	else switch (opflg & IF_MFMASK) {
	    case IF_M1:		/* Operand is 1 word */
	    case IF_M2:		/*   "	2 words (double) */
	    case IF_M4:		/*   "	4 words (quad) */
	    case IF_MIN:	/*   "  instruction at E */

#if KLH10_EXTADR
/*
	* ERROR * Need to revise for XA eacalc?
*/
#endif
		fprintf(f, "%lo/ ", (long)va_insect(e));
		if (!(vp = fevm_xmap(e, FEVM_CUR))) {
		    fprintf(f, "\?\?");
		    break;
		}
		w = vm_pget(vp);
		switch (opflg & IF_MFMASK) {
		case IF_M1:
		    wd1print(f, w);	/* Show word at E */
		    break;
		case IF_M2:		/* Show double at E */
		case IF_M4:
		    wd1print(f, w);	/* Do first word */
		    fprintf(f, " ? ");
		    va_inc(e);		/* Point to next word */
		    if ((vp = fevm_xmap(e, FEVM_CUR)))
			wd1print(f, vm_pget(vp));
		    else fprintf(f, "-\?\?-");
		    break;
		case IF_MIN:	/* Show instruction at E */
		    pinstr(f, w, 0, e);	/* "e" just a handy null vaddr_t */
		    break;
		}
		break;
	    case IF_ME:		/*   "	E (immediate) */
	    case IF_MEIO:	/*   "	IO register (Unibus) */
		fprintf(f, "E = %lo", (long)va_insect(e));
		break;
	    case IF_ME8:	/*   "  signed 8-bit E */
		{   paddr_t pa = va_insect(e);
		    fprintf(f, "E = %lo = %d.", (long)pa,
			(int)((pa&0400) ? (pa | ~0377) : (pa & 0377)));
		}
		break;
	    case IF_MEF:	/*   "	floating immediate */
		fprintf(f, "E = %lo = (?.?)", (long)va_insect(e));
		break;
	}
    }

    /* Show any special stuff for instruction? */
    if (opflg & IF_SPEC) {
    }
}
