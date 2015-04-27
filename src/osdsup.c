/* OSDSUP.C - OS-Dependent Support for KLH10
*/
/* $Id: osdsup.c,v 2.11 2003/02/23 18:18:01 klh Exp $
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
 * $Log: osdsup.c,v $
 * Revision 2.11  2003/02/23 18:18:01  klh
 * Tweak cast to avoid warning on NetBSD/Alpha.
 *
 * Revision 2.10  2002/05/21 16:27:36  klh
 * (MRC) Another os_tmget tweak - T20 expects DTE timezone to ignore DST.
 *
 * Revision 2.9  2002/04/24 07:56:08  klh
 * Add os_msleep, using nanosleep
 *
 * Revision 2.8  2002/03/26 06:18:24  klh
 * Add correct timezone to DTE's time info
 *
 * Revision 2.7  2002/03/21 09:50:08  klh
 * Mods for CMDRUN (concurrent mode)
 *
 * Revision 2.6  2001/11/19 12:09:58  klh
 * Disable shared mem hacking if no DPs
 *
 * Revision 2.5  2001/11/19 10:43:28  klh
 * Add os_rtm_adjust_base for ITS on Mac
 *
 * Revision 2.4  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include <stdio.h>
#include <string.h>	/* For strrchr */

#include "klh10.h"
#include "kn10def.h"
#include "osdsup.h"
#include "kn10ops.h"

#if CENV_SYS_UNIX
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <sys/file.h>

#  include <signal.h>
#  include <errno.h>
#  include <time.h>		/* For struct tm, localtime() */

#  if CENV_SYSF_BSDTIMEVAL
#    include <sys/time.h>	/* BSD: For setitimer() */
#    include <sys/resource.h>	/* BSD: For getrusage() */
#  endif

#  if CENV_SYS_SOLARIS
#    include <sys/stream.h>
#    include <sys/stropts.h>
#  endif
#  if CENV_SYS_SOLARIS || CENV_SYS_XBSD || CENV_SYS_LINUX
#    if CENV_SYS_LINUX
#      include <sys/ioctl.h>	/* For FIONREAD */
#    else
#      include <sys/filio.h>	/* For FIONREAD */
#    endif
#    include <sys/time.h>	/* BSD: For setitimer() */
#    include <sys/times.h>	/* No getrusage(), use times() */
#    include <limits.h>		/* For CLK_TCK */
#  endif

#  if CENV_SYSF_TERMIOS
#    include <termios.h>	/* SV4: For terminal stuff */
#  else
#    include <sgtty.h>
#  endif

    /* Declare syscalls used */
#  if CENV_SYS_UNIX && !CENV_SYS_V7
#    include <unistd.h>		/* read, write, lseek */
#    include <sys/ioctl.h>	/* ioctl, stty, gtty */
#    include <sys/resource.h>	/* getrusage */
				/* sys/stat.h: fstat */
				/* sys/time.h: gettimeofday, setitimer */
#  else
    extern int gtty(), stty(), ioctl(), read(), write(), lseek(), fstat();
    extern int gettimeofday(), setitimer(), getrusage();
#  endif

#elif CENV_SYS_MAC
#  include <types.h>
#  include <console.h>
#  include <fcntl.h>
#  include <errno.h>
#  include <signal.h>
#  include <time.h>
#  include <unix.h>

#  include "timer-glue.h"

#if CENV_USE_COMM_TOOLBOX
    extern void InitializeMac();
    extern void (*HaltRoutine)();
    extern void CheckEvents(Boolean idle);
    extern Boolean tty_crlf_mode;
    extern int keyboard_buffer_fill_pointer;
#endif /* CENV_USE_COMM_TOOLBOX */
#endif /* CENV_SYS_MAC */

#if CENV_SYSF_STRERROR
  extern char *strerror(int);	/* Not always declared in string.h */
#endif

extern void fe_shutdown(void);

#ifdef RCSID
 RCSID(osdsup_c,"$Id: osdsup.c,v 2.11 2003/02/23 18:18:01 klh Exp $")
#endif

/* OS-dependent program startup and exit.
**	Surprisingly, "main" is really an OSD function!
**	Note that klh10_main is a void function; exit is performed by calling
**	os_exit() with a status value.
**	If it becomes necessary for program exit to do so by returning
**	a value from main(), this can be accomplished by having os_exit
**	do a longjmp back to the main() here.
*/

extern void klh10_main(int, char **);	/* In klh10.c */

int
main(int argc, char **argv)
{
# if CENV_SYS_MAC		/* Increase stack size on a Mac */
    size_t *base = (size_t *) 0x000908;
    SetApplLimit ((Ptr) (*base - (size_t) 65535L));
# endif
    klh10_main(argc, argv);
    return 0;
}

void
os_init(void)
{
#if CENV_SYS_UNIX		/* Just return error if write to pipe fails */
    osux_signal(SIGPIPE, SIG_IGN);
#elif CENV_SYS_MAC
    InitializeMac();
#endif
}

void
os_exit(int status)
{
#if CENV_SYS_MAC && CENV_USE_COMM_TOOLBOX
    os_timer(0, NULL, 0, NULL);		/* Stop the timer.  Important! */
    if (status) {
	os_ttycmforce();		/* Badness.  Give user a chance */
	for (;;) CheckEvents(TRUE);	/* to read final message */
    }
#endif
    exit(status);
}


char *
os_strerror(int err)
{
    if (err == -1 && errno != err)
	return os_strerror(errno);
#if CENV_SYSF_STRERROR
    return strerror(err);
#else
#  if CENV_SYS_UNIX
    {
#  if !CENV_SYS_XBSD		/* Already in signal.h */
	extern int sys_nerr;
	extern char *sys_errlist[];
#  endif
	if (0 < err &&  err <= sys_nerr)
	    return sys_errlist[err];
    }
#  endif
    if (err == 0)
	return "No error";
    else {
	static char ebuf[30];
	sprintf(ebuf, "Unknown-error-%d", err);
	return ebuf;
    }
#endif /* !CENV_SYSF_STRERROR */
}

/* Controlling terminal stuff
**
** Note hack to support background mode; see fecmd.c
*/

static int osttyback = FALSE;	/* Initially false, not in background */

#if CENV_SYS_UNIX
static int ttystated = FALSE;

static struct ttystate {
# if CENV_SYSF_TERMIOS
	struct termios tios;
# else
	struct sgttyb sg;
#  if CENV_SYSF_BSDTTY
	struct tchars t;
	struct ltchars lt;
#  endif
# endif
}
    inistate,	/* Initial state at startup, restored on exit */
    cmdstate,	/* State while waiting for command line */
    cmrstate,	/* State while collecting command line & KN10 running */
    runstate;	/* State while KN10 running, CTY I/O */

static void
ttyget(register struct ttystate *ts)
{
# if CENV_SYSF_TERMIOS
    (void) tcgetattr(0, &(ts->tios));	/* Get all terminal attrs */
# else

    gtty(0, &(ts->sg));		/* Get basic sgttyb state */
#  if CENV_SYSF_BSDTTY
    ioctl(0, TIOCGETC, &(ts->t));	/* Get tchars */
    ioctl(0, TIOCGLTC, &(ts->lt));	/* Get ltchars */
#  endif /* CENV_SYSF_BSDTTY */    

# endif /* !CENV_SYSF_TERMIOS */
}

static void
ttyset(register struct ttystate *ts)
{
# if CENV_SYSF_TERMIOS
    (void) tcsetattr(0, TCSANOW, &(ts->tios));	/* Set all terminal attrs */
						/* Note change is immediate */
# else

    stty(0, &(ts->sg));		/* Set basic sgttyb state */
#  if CENV_SYSF_BSDTTY
    ioctl(0, TIOCSETC, &(ts->t));	/* Set tchars */
    ioctl(0, TIOCSLTC, &(ts->lt));	/* Set ltchars */
#  endif /* CENV_SYSF_BSDTTY */    

# endif /* !CENV_SYSF_TERMIOS */
}
#endif /* CENV_SYS_UNIX */

/* This routine is needed on systems that inherited the original unix
** genetic defect wherein a caught signal de-installs
** the original handler.  
*/
#if CENV_SYS_MAC && !CENV_USE_COMM_TOOLBOX
static void (*intsighan)();
static void
intresig(void)
{
    signal(SIGINT, intresig);	/* Re-install self, sigh! */
    (*intsighan)();		/* Invoke intended handler */
}
#endif

/* TTY background mode stuff
 */
void
os_ttybkgd(ossighandler_t *rtn)
{
    if (rtn && !osttyback) {
	/* Turn on background mode */
	osttyback = TRUE;	/* Say running in bkgd */
#if CENV_SYS_UNIX
	/* Clear TTY state in case it's ever referenced by accident */
	memset((void *)&inistate, 0, sizeof(inistate));
	ttystated = TRUE;
	(void) osux_signal(SIGTERM, rtn);
#endif /* CENV_SYS_UNIX */

    } else if (!rtn && osttyback) {
	/* Turn off background mode */
	osttyback = FALSE;
#if CENV_SYS_UNIX
	ttystated = FALSE;	/* Must get new tty state */
	(void) osux_signal(SIGTERM, SIG_DFL);
#endif /* CENV_SYS_UNIX */
    }
}

/* Initialize controlling TTY.
** This routine now takes an attribute struct!
*/
void
os_ttyinit(osttyinit_t *osti)
{
#if CENV_SYS_UNIX
    /* Turn off SIGQUIT since the last thing we want to do with
    ** a 32MB data segment is dump core!
    */
    osux_signal(SIGQUIT, SIG_IGN);

    /* Set up SIGINT to trap to FE (KLH10 command processor).
    ** SIGINT is used instead of (eg) SIGQUIT because SIGINT is the
    ** only appropriate value available from the ANSI C standard.
    */
    osux_signal(SIGINT, osti->osti_inthdl);	/* Use native SIGINT */

    if ((osti->osti_attrs & OSTI_BKGDF)
      && osti->osti_bkgdf) {
	os_ttybkgd(osti->osti_trmhdl);	/* This will set ttystated=TRUE */
    }

    if (!ttystated) {		/* If first time, */
	ttyget(&inistate);	/* remember initial TTY state */
	ttystated = TRUE;
    }

    /* Now set up various states as appropriate */
    cmdstate = cmrstate = runstate = inistate;	/* Start with known state */

  if (!osttyback) {
# if CENV_SYSF_TERMIOS
    runstate.tios.c_iflag = IMAXBEL;		/* Minimal input processing */
    runstate.tios.c_oflag &= ~OPOST;		/* No output processing */
    runstate.tios.c_cflag &= ~(CSIZE|PARENB|PARODD);
    runstate.tios.c_cflag |= CS8;		/* No parity, 8-bit chars */
    runstate.tios.c_lflag = ISIG;		/* Allow sigs for VINTR */
    memset(runstate.tios.c_cc, -1, sizeof(runstate.tios.c_cc));
    runstate.tios.c_cc[VINTR] = osti->osti_intchr;
    runstate.tios.c_cc[VMIN] = 1;
    runstate.tios.c_cc[VTIME] = 0;

    /* Command-run mode settings */
    cmrstate.tios.c_lflag |= ISIG;		/* Allow sigs for VINTR */
    cmrstate.tios.c_cc[VINTR] = osti->osti_intchr;
    cmdstate = cmrstate;			/* Duplicate for cmd mode */

# else
    cmdstate.sg.sg_flags |= CBREAK;		/* Want CBREAK for cmds */
    runstate.sg.sg_flags |= CBREAK;		/* and running */
    runstate.sg.sg_flags &= ~(ECHO|CRMOD);	/* no echo when running */

# if CENV_SYSF_BSDTTY
    /* If a specific command escape/interrupt char is set, use that and
    ** turn off everything else.  If 0, not set, all chars available for
    ** possible debugging.
    */
    if (cpu.fe.fe_intchr) {
	runstate.t.t_intrc = osti->osti_intchr;
	runstate.t.t_quitc =
	runstate.t.t_startc =
	runstate.t.t_stopc =
	runstate.t.t_eofc = -1;
	
	/* At this point, OK for cmd and cmdrun input */
	cmrstate.t = cmdstate.t = runstate.t;
	cmrstate.lt = cmdstate.lt = runstate.lt;

	/* Now finish clearing decks for run state */
	runstate.t.t_brkc = -1;
	runstate.lt.t_suspc =
	runstate.lt.t_dsuspc =
	runstate.lt.t_rprntc =
	runstate.lt.t_flushc =
	runstate.lt.t_werasc =
	runstate.lt.t_lnextc = -1;
    }
#  endif /* CENV_SYSF_BSDTTY */
# endif /* !CENV_SYSF_TERMIOS */

  }	/* end of if(!osttyback) */

#elif CENV_SYS_MAC
# if CENV_USE_COMM_TOOLBOX
    HaltRoutine = osti->osti_inthdl;	/* Command-. calls HaltRoutine, */
    tty_crlf_mode = FALSE;		/*    returning to FE cmd level */
# else
    intsighan = osti->osti_inthdl;	/* Remember actual handler to use */
    signal(SIGINT, intresig);		/* Command-. returns to FE cmd level */
# endif

#else
# error "Unimplemented OS routine os_ttyinit()"
#endif
}

#if KLH10_CTYIO_INT
/* Set TTY I/O signal handler
 */
void
os_ttysig(ossighandler_t *rtn)
{
# if CENV_SYS_DECOSF || CENV_SYS_SUN || CENV_SYS_XBSD || CENV_SYS_LINUX
    osux_signal(SIGIO, rtn);
# elif CENV_SYS_SOLARIS
    osux_signal(SIGPOLL, rtn);
# else
#  error "Unimplemented OS routine os_ttysig()"
# endif
}

/* Enable TTY I/O signalling
 */
static void
tty_iosigon(void)
{
#if CENV_SYS_DECOSF || CENV_SYS_SUN || CENV_SYS_XBSD || CENV_SYS_LINUX
    fcntl(0, F_SETFL, FASYNC);	/* Set asynch-operation flag */
# if CENV_SYS_FREEBSD || CENV_SYS_NETBSD || CENV_SYS_LINUX
    /* FreeBSD for sure, NetBSD probably, Linux almost certainly */
    /* On these systems, it isn't sufficient to simply set FASYNC
       (what they now call O_ASYNC).  The F_SETOWN function must *ALSO*
       be called in order to set the process (or process group) that will
       receive the SIGIO or SIGURG signal; it doesn't default!  Argh!!
       On OSF/1 this applies only to SIGURG, which we aren't using.
     */
    {
	static int doneonce = 0;
	if (!doneonce) {
	    fcntl(0, F_SETOWN, getpid());
	    doneonce = TRUE;
	}
    }
# endif

#elif CENV_SYS_SOLARIS
    ioctl(0, I_SETSIG, S_INPUT);	/* Set stream to signal on input */

#else
# error "Unimplemented OS routine tty_iosigon()"
#endif
}

/* Disable TTY I/O signalling
 */
static void
tty_iosigoff(void)
{
#if CENV_SYS_DECOSF || CENV_SYS_SUN || CENV_SYS_XBSD || CENV_SYS_LINUX
    fcntl(0, F_SETFL, 0);	/* Clear asynch-operation flag */
#elif CENV_SYS_SOLARIS
    ioctl(0, I_SETSIG, 0);	/* Clear TTY stream signal behavior */
#else
# error "Unimplemented OS routine tty_iosigoff()"
#endif
}

#endif /* KLH10_CTYIO_INT */

void
os_ttyreset(void)
{
#if CENV_SYS_UNIX
# if KLH10_CTYIO_INT
    tty_iosigoff();		/* Turn off TTY I/O signalling */
# endif
    ttyset(&inistate);		/* Restore original TTY state */

#elif CENV_SYS_MAC
# if CENV_USE_COMM_TOOLBOX
    os_ttycmforce();
    tty_crlf_mode = FALSE;
# else
    csetmode(C_ECHO, stdin);	/* line buffering */
# endif

#else
# error "Unimplemented OS routine os_ttyreset()"
#endif
}

void
os_ttycmdmode(void)
{
#if CENV_SYS_UNIX
# if KLH10_CTYIO_INT
    tty_iosigoff();		/* Turn off TTY I/O signalling */
# endif
    ttyset(&cmdstate);		/* Restore command-mode TTY state */

#elif CENV_SYS_MAC
# if CENV_USE_COMM_TOOLBOX
    os_ttycmforce();
    tty_crlf_mode = FALSE;
# else
    csetmode(C_CBREAK, stdin);	/* no line buffering */
# endif

# else
#  error "Unimplemented OS routine os_ttycmdmode()"
#endif
}

/* Hybrid mode: line command input, but signal if possible when ready.
 */
void
os_ttycmdrunmode(void)
{
#if CENV_SYS_UNIX
# if KLH10_CTYIO_INT
    tty_iosigon();		/* Turn on TTY I/O signalling */
# endif
    ttyset(&cmrstate);		/* Change to command-run TTY state */

#elif CENV_SYS_MAC
    os_ttycmdmode();

#else
#  error "Unimplemented OS routine os_ttyrunmode()"
#endif
}


void
os_ttyrunmode(void)
{
#if CENV_SYS_UNIX
# if KLH10_CTYIO_INT
    tty_iosigon();		/* Turn on TTY I/O signalling */
# endif
    ttyset(&runstate);		/* Change to "run" TTY state */

#elif CENV_SYS_MAC
# if CENV_USE_COMM_TOOLBOX
    os_ttycmforce();
    tty_crlf_mode = TRUE;
# else
    csetmode(C_RAW, stdin);	/* no line buffering */
# endif

#else
#  error "Unimplemented OS routine os_ttyrunmode()"
#endif
}

#if CENV_SYS_MAC && !CENV_USE_COMM_TOOLBOX
static int macsavchar = -1;
#endif

/* OS_TTYINTEST - See if any TTY input available, and returns count of chars.
**	If count unknown, OK to return just 1; routine will be invoked
**	frequently (either by clock timeout or by I/O signal).
*/
int
os_ttyintest(void)
{
#if CENV_SYS_UNIX
  {
    /* OSD WARNING: the FIONREAD ioctl is defined to want a "long" on SunOS
    ** and presumably old BSD, but it uses an "int" on Solaris and DEC OSF/1!
    ** Leave undefined for unknown systems to ensure this is checked on
    ** each new port.
    */
#if CENV_SYS_SUN
    long retval;
#elif CENV_SYS_SOLARIS || CENV_SYS_DECOSF || CENV_SYS_XBSD || CENV_SYS_LINUX
    int retval;
#endif
    if (ioctl(0, FIONREAD, &retval) != 0)	/* If this call fails, */
	return 0;				/* assume no input waiting */
    return (int) retval;
  }
#elif CENV_SYS_MAC
# if CENV_USE_COMM_TOOLBOX
    if (stdin->buffer_len)
        return stdin->buffer_len;

    CheckEvents(FALSE);
    return (keyboard_buffer_fill_pointer);
# else
  {
    unsigned char c;
    if (macsavchar >= 0)	/* If previously input char is there, */
	return 1;		/* say so. */
    if (mactick)
	(*mactick)();		/* Do a simulated clock tick (KLH: ?!!) */

    if (read(0, (char *)&c, 1) != 1)	/* If this call fails, */
	return 0;			/* assume no input waiting */
    switch (c &= 0177) {		/* what did we get? */
	case 034:			/* CTRL-\ */
	    raise(SIGINT);		/* request the console */
	    return 0;			/* Allow for possible return */
	case 010:			/* backspace becomes delete (sigh) */
	    c = 0177;
	default:
	    macsavchar = c;		/* save character */
    }
    return 1;				/* have something to read */
  }
# endif

#else
# error "Unimplemented OS routine os_ttyintest()"
#endif
}

int
os_ttyin(void)
{
#if CENV_SYS_UNIX
  {
    unsigned char buf;
    if (read(0, (char *)&buf, 1) != 1)	/* If this call fails, */
	return -1;			/* assume no input waiting */
    return buf;
  }
#elif CENV_SYS_MAC
# if CENV_USE_COMM_TOOLBOX
    if (os_ttyintest())
    return getc(stdin);
    else return -1;
# else
  {
    int c = macsavchar;
    macsavchar = -1;			/* read the character */
    return c;
  }
# endif

#else
# error "Unimplemented OS routine os_ttyin()"
#endif
}

int
os_ttyout(int ch)
{
#if CENV_SYS_UNIX || CENV_SYS_MAC
    char chloc = ch & 0177;		/* Sigh, must mask off T20 parity */
# if CENV_USE_COMM_TOOLBOX
    /* Event check to allow stopping runaway typeout on Mac */
    static int ttyout_event_check_counter = 1;
    if (--ttyout_event_check_counter <= 0) {
      CheckEvents(FALSE);
      ttyout_event_check_counter = 100;
    }
# endif
    return write(1, &chloc, 1) == 1;

#else
# error "Unimplemented OS routine os_ttyout()"
#endif
}

/* TTY String output.
**	Note assumption that 8th bit is already masked, if desired.
**	Call may block, sigh.
*/
int
os_ttysout(char *buf, int len)		/* Note length is signed int */
{
#if CENV_SYS_UNIX || CENV_SYS_MAC
# if CENV_USE_COMM_TOOLBOX
    /* Event check to allow stopping runaway typeout on Mac */
    CheckEvents(FALSE);
# endif
    return write(1, buf, (size_t)len) == len;

#else
# error "Unimplemented OS routine os_ttysout()"
#endif
}

/* Top-level command character input
**	May want to be different from os_ttyin().
*/
int
os_ttycmchar(void)
{
#if CENV_SYS_MAC && CENV_USE_COMM_TOOLBOX
  {
    int ch;

    while(!os_ttyintest())
	CheckEvents(TRUE);
    ch = getc(stdin);
    os_ttyout(ch);
    os_ttycmforce();
    return ch;
  }
#else
    return getc(stdin);
#endif
}

char *
os_ttycmline(char *buffer, int size)
{
#if CENV_SYS_MAC && CENV_USE_COMM_TOOLBOX
  {
    /*--- Add rubout processing later ---*/
    int i=0, ch;

    os_ttycmforce();
    --size;			/* allow for null at end */
    while (i < size) {
	ch = os_ttycmchar();
	if (ch == 015)
	    break;
	else if (ch == 0177) {
	    printf("XXX\n");
	    os_ttycmforce();
	    i = 0;
	} else
	    buffer[i++] = ch;
    }
    buffer[i] = 0;
    return buffer;
  }
#else
    return fgets(buffer, size, stdin);
#endif
}

void
os_ttycmforce(void)
{
    fflush(stdout);
}

/* General-purpose System-level I/O.
**	It is intended that this level of IO be in some sense the fastest
**	or most efficient way to interact with the host OS, as opposed to
**	the more portable stdio interface.
*/

int
os_fdopen(osfd_t *afd, char *file, char *modes)
{
#if CENV_SYS_UNIX || CENV_SYS_MAC
    int flags = 0;
    if (!afd) return FALSE;
    for (; modes && *modes; ++modes) switch (*modes) {
	case 'r':	flags |= O_RDONLY;	break;	/* Yes I know it's 0 */
	case 'w':	flags |= O_WRONLY;	break;
	case '+':	flags |= O_RDWR;	break;
	case 'a':	flags |= O_APPEND;	break;
	case 'b':	/* Binary */
# if CENV_SYS_MAC
			flags |= O_BINARY;
# endif
						break;
	case 'c':	flags |= O_CREAT;	break;
	/* Ignore unknown chars for now */
    }
# if CENV_SYS_MAC
    if ((*afd = open(file, flags)) < 0)
# else
    if ((*afd = open(file, flags, 0666)) < 0)
# endif
	return FALSE;
    return TRUE;

#else
# error "Unimplemented OS routine os_fdopen()"
#endif
}

int
os_fdclose(osfd_t fd)
{
#if CENV_SYS_UNIX || CENV_SYS_MAC
    return close(fd) != -1;
#else
# error "Unimplemented OS routine os_fdclose()"
#endif
}

int
os_fdseek(osfd_t fd, osdaddr_t addr)
{
#if CENV_SYS_UNIX
    return lseek(fd, addr, L_SET) != -1;
#elif CENV_SYS_MAC
    return lseek(fd, addr, SEEK_SET) != -1;
#else
# error "Unimplemented OS routine os_fdseek()"
#endif
}

int
os_fdread(osfd_t fd,
	  char *buf,
	  size_t len, size_t *ares)
{
#if CENV_SYS_UNIX
    register int res = read(fd, buf, len);
    if (res < 0) {
	if (ares) *ares = 0;
	return FALSE;
    }
#elif CENV_SYS_MAC
    /* This is actually generic code for any system supporting unix-like
    ** calls with a 16-bit integer count interface.
    ** --- I don't think the Mac needs this but I'll leave it anyway --Moon
    */
    register size_t res = 0;
    register unsigned int scnt, sres = 0;

    while (len) {
	scnt = len > (1<<14) ? (1<<14) : len;	/* 16-bit count each whack */
	if ((sres = read(fd, buf, scnt)) != scnt) {
	    if (sres == -1) {		/* If didn't complete, check for err */
		if (ares) *ares = res;	/* Error, but may have read stuff */
		return FALSE;
	    }
	    res += sres;		/* No error, just update count */
	    break;			/* and return successfully */
	}
	res += sres;
	len -= sres;
	buf += sres;
    }
#else
# error "Unimplemented OS routine os_fdread()"
#endif
    if (ares) *ares = res;
    return TRUE;
}

int
os_fdwrite(osfd_t fd,
	   char *buf,
	   size_t len, size_t *ares)
{
#if CENV_SYS_UNIX
    register int res = write(fd, buf, len);
    if (res < 0) {
	if (ares) *ares = 0;
	return FALSE;
    }
#elif CENV_SYS_MAC
    /* This is actually generic code for any system supporting unix-like
    ** calls with a 16-bit integer count interface.
    ** --- I don't think the Mac needs this but I'll leave it anyway --Moon
    */
    register size_t res = 0;
    register unsigned int scnt, sres = 0;

    while (len) {
	scnt = len > (1<<14) ? (1<<14) : len;	/* 16-bit count each whack */
	if ((sres = write(fd, buf, scnt)) != scnt) {
	    if (sres == -1) {		/* If didn't complete, check for err */
		if (ares) *ares = res;	/* Error, but may have written stuff */
		return FALSE;
	    }
	    res += sres;		/* No error, just update count */
	    break;			/* and return successfully */
	}
	res += sres;
	len -= sres;
	buf += sres;
    }
#else
# error "Unimplemented OS routine os_fdwrite()"
#endif
    if (ares) *ares = res;
    return TRUE;
}

/* Support for "atomic" intflag reference.
**	Intended to work like EXCH, not always truly atomic but
**	close enough.  As function to prevent optimizing away
**	the swap.
*/
osintf_t
os_swap(osintf_t *addr, int val)
{
    register int tmp = (int)*addr;
    *addr = val;
    return tmp;
}


/* Support for OS real-time clock

OS Realtime values:

	BSD Unix and SunOS use a timeval structure with two 32-bit
members, tv_sec and tv_usec.  The latter is always modulo 1,000,000
and fits within 20 bits.

	MacOS uses a 64-bit unsigned integer which counts microseconds.

*/

/* OS_RTMGET - Get OS real time
*/
int
os_rtmget(register osrtm_t *art)
{
#if CENV_SYSF_BSDTIMEVAL
    static osrtm_t os_last_rtm = {0,0};
    if (!gettimeofday(art, (struct timezone *)NULL) == 0)
	return FALSE;
				/* did time go backwards? */
    if ((os_last_rtm.tv_sec > art->tv_sec) ||
	((os_last_rtm.tv_sec == art->tv_sec) &&
	 (os_last_rtm.tv_usec > art->tv_usec))) {
				/* yes, advance 1usec from previous */
      art->tv_sec = os_last_rtm.tv_sec;
      art->tv_usec = ++os_last_rtm.tv_usec;
    }
    else {			/* advanced normally, save last time */
      os_last_rtm.tv_sec = art->tv_sec;
      os_last_rtm.tv_usec = art->tv_usec;
    }
    return TRUE;
#elif CENV_SYS_MAC
    Microseconds(art);
    return TRUE;
#else
# error "Unimplemented OS routine os_rtmget()"
#endif
}

/* OS_VRTMGET - Get OS virtual (user CPU) time, in same units as real time.
*/
int
os_vrtmget(register osrtm_t *art)
{
#if CENV_SYS_SOLARIS		/* Precision sucks, but it's all we have */
    struct tms tms;

    if (times(&tms) == 0) {
	art->tv_sec = tms.tms_utime / CLK_TCK;
	art->tv_usec = (tms.tms_utime % CLK_TCK)
				* (1000000/CLK_TCK);	/* Turn into usec */
	return TRUE;
    }
    return FALSE;
#elif CENV_SYSF_BSDTIMEVAL
    /* WARNING!!!  Some systems turn out not to report getrusage runtime in a
    ** monotonically increasing way!  This can result in negative deltas
    ** from one get to the next.
    ** In particular, this was still true of FreeBSD as of 3.3.
    ** See quant_freeze() which contains code to check and recover from this
    ** regardless of native OS.
    */
    struct rusage rus;

    if (getrusage(RUSAGE_SELF, &rus) == 0) {
	*art = rus.ru_utime;		/* Return user-time used */
	return TRUE;
    }
    return FALSE;
#elif CENV_SYS_MAC
    Microseconds(art);
    return TRUE;
#else
# error "Unimplemented OS routine os_vrtmget()"
#endif
}

/* OS_RTMSUB - Find difference in OS realtime values
**	Does A = A - B;
*/
void
os_rtmsub(register osrtm_t *a, register osrtm_t *b)
{
#if CENV_SYSF_BSDTIMEVAL
    a->tv_sec -= b->tv_sec;
    if ((a->tv_usec -= b->tv_usec) < 0) {
	--a->tv_sec;
	a->tv_usec += 1000000;
    }
#elif CENV_SYS_MAC
    unsigned long long avalue, bvalue;
    avalue = RTM_PTR_TO_LONG_LONG(a);
    bvalue = RTM_PTR_TO_LONG_LONG(b);
    avalue -= bvalue;
    RTM_PTR_TO_LONG_LONG(a) = avalue;
#else
# error "Unimplemented OS routine os_rtmsub()"
#endif
}

/* OS_RTM_ADJUST_BASE - Convert an OS realtime value between
 * relative and absolute time
 */
void
os_rtm_adjust_base(osrtm_t *in, osrtm_t *out, int b_absolute)
{
#if CENV_SYS_MAC
    /* OS realtimes are relative to system boot on MacOS */

    unsigned long secs;
    unsigned long long longusecs, boot_time;
    osrtm_t curtime_relative;

    Microseconds(&curtime_relative);
    GetDateTime(&secs);
    longusecs = (unsigned long long)secs * (unsigned long long)1000000;
    boot_time = longusecs - RTM_PTR_TO_LONG_LONG(&curtime_relative);
    
    if (b_absolute)
    {
	/* Make a relative time absolute */
	
	RTM_PTR_TO_LONG_LONG(out) = RTM_PTR_TO_LONG_LONG(in) + boot_time;
    }
    else
    {
	/* Make an absolute time relative */

	RTM_PTR_TO_LONG_LONG(out) = RTM_PTR_TO_LONG_LONG(in) - boot_time;
    }   
#else
    /* OS realtimes are always absolute on other OS's */

    *out = *in;
#endif
}

/* OS_RTM_TO_SECS, OS_RTM_TO_USECS - Simulate Unix format of
 * OS realtime value on Macintosh
 */
#if CENV_SYS_MAC
unsigned long os_rtm_to_secs(osrtm_t rtm)
{
    unsigned long long value = RTM_PTR_TO_LONG_LONG(&rtm);
    return value / 1000000;
}

unsigned long os_rtm_to_usecs(osrtm_t rtm)
{
    unsigned long long value = RTM_PTR_TO_LONG_LONG(&rtm);
    return value % 1000000;
}
#endif /* CENV_SYS_MAC */


/* OS_TIMER - Set interval timer interrupt, given interval time in usec.
**	Note interval is passed as a uint32.  This is big enough for
** 4.29 seconds; all known monitors interrupt at a much faster rate!
**	If interval is 0, turn timer interrupt off; the caller has to
** make sure it doesn't pass this value unless that is what is intended!
*/
void
os_timer(int type,
	 ossighandler_t *irtn,
	 register uint32 usecs,
	 ostimer_t *ostate)
{
#if CENV_SYSF_BSDTIMEVAL
    struct itimerval itm;

    if (type != ITIMER_VIRTUAL)
	type = ITIMER_REAL;	/* Default is real-time */
    if (ostate)
	ostate->ostmr_type = type;

    if (usecs == 0) {
	/* Turn timer off */
	timerclear(&itm.it_interval);
	timerclear(&itm.it_value);
	/* Ignore signals prior to clearing interval timer.
	** Used to be SIG_DFL but this created a danger window since default
	** behavior of both signals is to exit process.
	*/
	(void) osux_sigact(((type == ITIMER_VIRTUAL) ? SIGVTALRM : SIGALRM),
			   SIG_IGN,
			   ostate ? &ostate->ostmr_sigact : NULL);
    } else {
	itm.it_interval.tv_sec  = itm.it_value.tv_sec  = usecs / 1000000;
	itm.it_interval.tv_usec = itm.it_value.tv_usec = usecs % 1000000;
	(void) osux_sigact(((type == ITIMER_VIRTUAL) ? SIGVTALRM : SIGALRM),
			   irtn,
			   ostate ? &ostate->ostmr_sigact : NULL);
    }
    
    if (setitimer(type, &itm, ostate ? &ostate->ostmr_itm : NULL) != 0) {
	panic("os_timer: setitimer() failed - %s", os_strerror(errno));
    }

#elif CENV_SYS_MAC
    /* Use the MacOS Time Manager */
    static interval_timer* timer = NULL;

    if (usecs == 0)
    {	/* Turn timer off */
 	if (timer) stop_interval_timer(timer);
    }
    else
    {	/* Turn timer on (create timer on first use) */
	if (timer == NULL) timer = make_interval_timer(irtn);
	start_interval_timer(timer, usecs);
    };

#else
# error "Unimplemented OS routine os_timer()"
#endif
}

void
os_rtimer(ossighandler_t *irtn, uint32 usecs)
{
    os_timer(OS_ITIMER_REAL, irtn, usecs, (ostimer_t *)NULL);
}

void
os_vtimer(ossighandler_t *irtn, uint32 usecs)
{
    os_timer(OS_ITIMER_VIRT, irtn, usecs, (ostimer_t *)NULL);
}

void
os_timer_restore(ostimer_t *ostate)
{
#if CENV_SYSF_BSDTIMEVAL && CENV_SYSF_SIGSET
    sigset_t blkset, savset;
    int ret;

    /* Prevent sigs from going off between handler and timer restoration */
    sigfillset(&blkset);
    sigprocmask(SIG_BLOCK, &blkset, &savset);
    (void) osux_sigrestore(&ostate->ostmr_sigact);
    ret = setitimer(ostate->ostmr_type, &ostate->ostmr_itm, NULL);
    (void) sigprocmask(SIG_SETMASK, &savset, (sigset_t *)NULL);
    if (ret != 0) {
	panic("os_timer_restore: setitimer() failed - %s", os_strerror(errno));
    }
#elif CENV_SYS_MAC
    /* no Mac code needed --Moon */
#else
# error "Unimplemented OS routine os_timer_restore()"
#endif
}


/* OS_V2RT_IDLE - special function for clk_idle().
**	Idles for an amount of real time equivalent to however much
**	virtual time is left until the next clock interrupt.
** Requires various special hackery - seizes SIGALRM and then assumes
**	nothing else changes it (or if it does, it gets restored).
**	This is to reduce the overhead of setting up the handler every time.
** Note non-modular reference to INSBRKTEST() - may be able to
** flush it if stats show it's rarely a useful test.
** NOTE: perhaps use nanosleep() here if it exists?
*/
void
os_v2rt_idle(ossighandler_t *hdlarg)
{
#if CENV_SYSF_BSDTIMEVAL && CENV_SYSF_SIGSET
    sigset_t allmsk, oldmsk, nomsk;
    struct itimerval ntval, vtval;
    static ossighandler_t *handler = NULL;

    if (handler != hdlarg) {	/* First time with no handler */
	if (hdlarg)
	    osux_signal(SIGALRM, (handler = hdlarg));
	else {
	    /* Forced clearing of handler */
	    osux_signal(SIGALRM, SIG_IGN);
	    handler = NULL;
	}
    }

    sigfillset(&allmsk);		/* Specify all signals */
    sigemptyset(&nomsk);		/* Specify no signals */
    sigprocmask(SIG_BLOCK, &allmsk, &oldmsk);	/* Block them all */
    timerclear(&ntval.it_interval);
    timerclear(&ntval.it_value);
    setitimer(ITIMER_VIRTUAL, &ntval, &vtval);	/* Find & stop virtual timer */
    if (timerisset(&vtval.it_value)) {
	/* Some remaining time left to go in virtual timer.
	** Turn it into a real-time one-shot.
	*/
	ntval.it_value = vtval.it_value;	/* No interv, just one-shot */
	setitimer(ITIMER_REAL, &ntval, (struct itimerval *)NULL);
	while (!INSBRKTEST()) {
	    sigsuspend(&nomsk);			/* Wait for interrupt */
	}
	/* Done, now restore virtual timer appropriately */
	getitimer(ITIMER_REAL, &ntval);
	if (timerisset(&ntval.it_value)) {	/* If didn't time out */
	    vtval.it_value = ntval.it_value;	/* get remaining time */
	    timerclear(&ntval.it_value);	/* Turn off real-time timer */
	    setitimer(ITIMER_REAL, &ntval, (struct itimerval *)NULL);
	} else
	    vtval.it_value = vtval.it_interval;		/* Full restart */
    }
    setitimer(ITIMER_VIRTUAL, &vtval, (struct itimerval *)NULL);

    sigprocmask(SIG_SETMASK, &oldmsk, (sigset_t *)NULL);

#elif CENV_SYS_MAC
    /* Mac does nothing yet to be nice to other processes --Moon */
#else
# error "Unimplemented OS routine os_v2rt_idle()"
#endif
}

/* OS_SLEEP - Sleep for N seconds regardless of signals.
**	This must not conflict with the behavior of os_timer().  Certain
** systems may require special hackery to achieve this.
**	Currently this is only used by dvni20.c, where it is
** OK to suspend all clock interrupts for the duration of the sleep.
**
**	On DEC OSF/1 the sleep() call uses a different mechanism independent
**	of SIGALRM, which is good.
**
**	Solaris sleep() uses ITIMER_REAL and SIGALRM.  This is still true
**		as of Solaris 5.8.
**
**	On FreeBSD sleep(3) is implemented using nanosleep(2).	
*/
void
os_sleep(int secs)
{
#if CENV_SYSF_NANOSLEEP
    osstm_t stm;

    OS_STM_SET(stm, secs);
    while (os_msleep(&stm) > 0) ;

#elif CENV_SYSF_BSDTIMEVAL
    /* Must save & restore ITIMER_REAL & SIGALRM, which conflict w/sleep() */
    ostimer_t savetmr;

    /* Turn off ITIMER_REAL and SIGALRM, saving old state,
       do the sleep, then restore the world.
    */
    os_timer(OS_ITIMER_REAL, SIG_IGN, 0, &savetmr);
    sleep(secs);			/* Do the gubbish */
    os_timer_restore(&savetmr);

#elif CENV_SYS_MAC
    /* I don't think the Mac needs this --Moon */
#else
# error "Unimplemented OS routine os_sleep()"
#endif
}

/* OS_MSLEEP - Sleep for N milliseconds or until interrupted.
**	Returns > 0 if interrupted but time still remains (updates
**		its osstm_t arg to indicate time left).
**	Returns == 0 if time expired.
**	Returns -1 for some other error.
**
** This is typically used where we want to wake up on signals to examine
** our state, but not proceed with KN10 CPU execution until either the
** state is satisfactory or time has expired.
**
**	This must not conflict with the behavior of os_timer().  Certain
** systems may require special hackery to achieve this.
**	Currently this is only used by dvni20.c and dvtm03.c, where it is
** OK to suspend all clock interrupts for the duration of the sleep.
**
** Where possible this should use POSIX nanosleep(),
** which claims not to interfere with other timers or signals.
**
**	Tru64/FreeBSD/NetBSD/Linux support nanosleep().
**	Solaris claims to support nanosleep() but requires link hackery:
**		in 5.5: -lposix4
**		in 5.8: -lrt
**
** NOTE:
**	On some systems, select() can be used for subsecond timeouts.
** However, whether or not the time remaining is returned is OSD,
** so this cannot be relied on to maintain an overall timeout;
** for that, doing several sleeps may have to suffice.
*/
int
os_msleep(osstm_t *stm)
{
#if CENV_SYSF_NANOSLEEP

    if (nanosleep(stm, stm) == 0) {
	stm->tv_sec = 0;	/* Make sure returns zero */
	stm->tv_nsec = 0;
	return 0;
    }
    return (errno == EINTR) ? 1 : -1;

#elif CENV_SYS_MAC
    /* I don't think the Mac needs this --Moon */
#else
# error "Unimplemented OS routine os_msleep()"
#endif
}

#if KLH10_CPU_KS
/*
KS10 Realtime values:

	The KS10 time base is a 71-bit doubleword integer (low sign is
0) that counts at 4.1MHz; i.e. its value increments by 4,100,000 each
second, 4.1 each usec.  Each unit is thus 1/4.1 = 0.24390243902439027 usec.

Comments from KSHACK;KSDEFS:
	; The time is a 71. bit unsigned number.  The bottom
	; 12. bits cannot be set.  The bottom 2 bits cannot
	; even be read.  It increments at 4.1 MHz.  The top
	; 59. bits (the ones you can set) thus measure
	; (almost) milliseconds.  The top 69. bits (the
	; ones you can read) thus measure "short"
	; microseconds.  The time wraps around every 18.
	; million years.  To make the top 59. bits actually
	; measure milliseconds, the clock would have to run
	; at 4.096 MHz.  However it -really- -does- run at
	; exactly 4.1 MHz!

A "short microsecond" from the top 69 bits (ignoring the low 2) is:
	(1/4.1) * 4 = 0.97560975609756106 usec

An ITS quantum unit is as close to 4.096 usec as possible.  By taking the
top 67 bits (ignoring the low 4) we now have a quantum tick:
	(1/4.1) * 16 = 3.9024390243902443 usec

And this is the derivation of the "3.9 usec tick" in the ITS sources.

The equation for converting a Unix usec time into KS ticks would be to
compute:
	((tv_sec*1000000)+tv_usec) * 4.1

  which could be done without floating-point, if one had large enough
  integers, as:
	(tv_sec * 4100000) + (tv_usec * 4) + (tv_usec / 10)

For now we'll borrow PDP-10 words and ops to accomplish this.

*/

/* OS_RTM_TOKST - Convert OS realtime ticks into KS ticks
*/
void
os_rtm_tokst(register osrtm_t *art,
	     dw10_t *ad)
{
    register dw10_t d;
    register w10_t w;

#if CENV_SYSF_BSDTIMEVAL
    LRHSET(w, 017, 0507640);			/* 4,100,000. */
    d = op10xmul(w, op10utow(art->tv_sec));
    w = op10utow((int32)((art->tv_usec << 2) + (art->tv_usec / 10)));
    op10m_udaddw(d, w);		/* Add word into double */

#elif CENV_SYS_MAC
    unsigned long long value = RTM_PTR_TO_LONG_LONG(art);
    value = value * 4 + value / 10;		/* microseconds times 4.1 */
    /* Convert 64-bit integer to pdp-10 double-integer format
       discarding the sign bit */
    d.w[0].lh = (value >> (35 + 18)) & 0377777;
    d.w[0].rh = (value >> 35) & 0777777;
    d.w[1].lh = ((value >> 18) & 0377777);
    d.w[1].rh = value & 0777777;

#else
# error "Unimplemented OS routine os_rtm_tokst()"
#endif
    *ad = d;
}


/* OS_RTM_TOQCT - Convert OS realtime ticks into KS quantum counter ticks (ITS)
**	This code assumes it is always given a time interval, not an
**	absolute time, and thus 32-bit values should be big enough.
**	Note arithmetic is different since we're ignoring the low
**	4 bits of the result; each quantum tick is 16 KS ticks, or
**	approx 3.9 usec.
*/
unsigned long
os_rtm_toqct(register osrtm_t *art)
{
#if CENV_SYSF_BSDTIMEVAL
    return ((unsigned long)(art->tv_sec * 4100000)
		+ (art->tv_usec << 2) + (art->tv_usec/10)) >> 4;
#elif CENV_SYS_MAC
    unsigned long long value = RTM_PTR_TO_LONG_LONG(art);
    return (unsigned long)((value * 4 + value / 10) >> 4);
#else
# error "Unimplemented OS routine os_rtm_toqct()"
#endif
}

#endif /* KLH10_CPU_KS */

#if KLH10_CPU_KL

/*
KL10 Realtime values:

	The KL10 time base hardware counter counts at exactly 1MHz, thus
1 KL tick == 1 usec.
	The counter is a 16-bit quantity, thus 32 bits should be quite
sufficient as a return value.  The real hardware updates an EPT location
if it overflows, but for our purposes the monitor is always going to
read the time explicitly often enough that overflow should never happen.
See io_rdtime().

*/

/* OS_RTM_TOKLT - Convert OS realtime ticks into KL ticks (usecs)
*/
unsigned long
os_rtm_toklt(register osrtm_t *art)
{
#if CENV_SYSF_BSDTIMEVAL
    return ((unsigned long)art->tv_sec * 1000000) + art->tv_usec;
#elif CENV_SYS_MAC
    return art->lo;
#else
# error "Unimplemented OS routine os_rtm_toklt()"
#endif
}

#endif /* KLH10_CPU_KL */


/* Miscellaneous stuff */

/* OS_TMGET - Get current real-world time from OS in TM structure.
**	This includes a painful timezone computation that is intended to be
** portable for every system implementing the standard {gm,local}time()
** facilities.  Unfortunately after many years the standards people STILL
** can't agree on the need for tm_gmtoff, forcing everyone to do contortions
** like this!
**	Note this is invoked only by the DTE code during system startup,
** so efficiency is not an issue; also, the timezone is in hours rather
** than minutes or seconds because that's all that the DTE can handle.
** If anything else needs localtime values on a more frequent or precise
** basis, some changes will be needed.
**	The time zone that the DTE wants is the TOPS-20 timezone, which is
** represented as the number of hours west of UTC in standard time.  For
** example, the west coast of North America is always 8 regardless of
** summer time.  There is a separate flag to indicate that summer time is
** in effect.
**	This differs from what most people think of as a timezone value,
** which is the number of hours/minutes east of UTC in local time.  For
** example, the west coast of North America is -0800 in winter and -0700
** in summer.
*/
int
os_tmget(register struct tm *tm, int *zone)
{
    time_t tad;
    int julian, zn;

    if (time(&tad) == (time_t)-1)
	return 0;

    /* To derive timezone, break TAD down into both local time and GMT (UTC)
     * and find the difference between them.  This code came from MRC.
     */
#if 1
    if (gmtime_r(&tad, tm) == NULL)
        return 0;
    zn = tm->tm_hour * 60 + tm->tm_min;
    julian = tm->tm_yday;
    if (localtime_r(&tad, tm) == NULL)
	return 0;			/* Some problem */
#else
    /* Alternative code if system doesn't have the _r facilities */
  {
    register struct tm *stm;
    if (!(stm = gmtime(&tad)))
        return 0;
    zn = stm->tm_hour * 60 + stm->tm_min;
    julian = stm->tm_yday;
    if (!(stm = localtime(&tad)))
	return 0;			/* Some problem */
    *tm = *stm;				/* Copy static to dynamic stg */
  }
#endif
					/* minus UTC minutes since midnight */
    zn = tm->tm_hour * 60 + tm->tm_min - zn;
    /* julian can be one of:
     *  36x  local time is December 31, UTC is January 1, offset -24 hours
     *    1  local time is 1 day ahead of UTC, offset +24 hours
     *    0  local time is same day as UTC, no offset
     *   -1  local time is 1 day behind UTC, offset -24 hours
     * -36x  local time is January 1, UTC is December 31, offset +24 hours
     */
    if (julian = (tm->tm_yday - julian))
	zn += ((julian < 0) == (abs(julian) == 1)) ? -24*60 : 24*60;

    /* At this point zn contains the numer of minutes east of UTC in local
     * time.  We divide by 60 to get integral hours, negate to make it be
     * hours west of UTC, and add one if summer time to get the standard
     * time hour offset.
     * Resulting zone must be an integral hour.
     */
    *zone = -(zn / 60) + (tm->tm_isdst ? 1 : 0);
    return 1;
}

/* Signal handling support, if needed.
**	The canonical model for signal handling is BSD, where handlers are
**	NOT de-installed when a signal is caught, and system calls are
**	restarted when the handler returns.
*/

#if CENV_SYS_UNIX

int
osux_signal(int sig, ossighandler_t *func)
{
    return osux_sigact(sig, func, (ossigact_t *)NULL);
}

int
osux_sigact(int sig, ossighandler_t *func, ossigact_t *ossa)
{
#if CENV_SYSF_SIGSET
    struct sigaction act;

    act.sa_handler = func;
    act.sa_flags = SA_RESTART;
    sigemptyset(&act.sa_mask);
    sigaddset(&act.sa_mask, sig);	/* Suspend this sig during handler */
    if (ossa)
	ossa->ossa_sig = sig;
    return sigaction(sig, &act, (ossa ? &ossa->ossa_sa : NULL));
#elif CENV_SYS_BSD
    void (*ret)();

    ret = signal(sig, func);
    if (ossa) {
	ossa->ossa_sig = sig;
	ossa->ossa_handler = func;
    }
    return (ret == SIG_ERR) ? -1 : 0;
#else
# error "Unimplemented OS routine osux_sigact()"
#endif
}

int
osux_sigrestore(ossigact_t *ossa)
{
#if CENV_SYSF_SIGSET
    return sigaction(ossa->ossa_sig,
		     &ossa->ossa_sa, (struct sigaction *)NULL);
#elif CENV_SYS_BSD
    return (signal(ossa->ossa_sig, ossa->ossa_handler) == SIG_ERR)
	? -1 : 0;
#else
# error "Unimplemented OS routine osux_sigrestore()"
#endif
}


#endif /* CENV_SYS_UNIX */

/* Process priority facilities
**
*/

int
os_setpriority(ospri_t npri)
{
#if CENV_SYS_UNIX
    if (setpriority(PRIO_PROCESS, 0, npri) == 0)
	return TRUE;
#endif
    return FALSE;
}

int
os_getpriority(ospri_t *aopri)
{
#if CENV_SYS_UNIX
    register ospri_t opri;

    errno = 0;
    if (((opri = getpriority(PRIO_PROCESS, 0)) != -1) || !errno) {
	*aopri = opri;
	return TRUE;
    }
#endif
    return FALSE;
}

/* Memory Mapping facilities
**	These use the SYSV IPC shared memory calls.
**	It's becoming increasingly likely that most unices will have them.
**	Ugh.  Would prefer something like mmap() but it has its own set
**	of problems, the most important of which is the fact there is no
**	control over pages being (uselessly) written out to disk.
*/

int
os_mmcreate(register size_t memsiz,
	    osmm_t *amm,
	    char **aptr)
{
#if CENV_SYS_UNIX && KLH10_DEV_DP

    int shmid;
    char *ptr;

    *amm = 0;
    *aptr = NULL;

    /* Create a shared mem seg.  Set perms to owner-only RW.
    ** Note shmget will lose grossly if on a system where its size arg
    ** (defined as a u_int) is less than 32 bits!
    */
    if ((shmid = shmget(IPC_PRIVATE, (u_int)memsiz, 0600)) == -1) {
	fprintf(stderr, "[os_mmcreate: shmget failed for %ld bytes - %s]\n",
			    (long)memsiz, os_strerror(errno));
	return FALSE;
    }

    /* Attempt to attach segment into our address space */
    ptr = (char *)shmat(shmid, (void *)0, SHM_RND);
    if (ptr == (char *)-1) {
	fprintf(stderr, "[os_mmcreate: shmat failed for %ld bytes - %s]\n",
			    (long)memsiz, os_strerror(errno));

	/* Clean up by flushing seg */
	shmctl(shmid, IPC_RMID, (struct shmid_ds *)NULL);
	return FALSE;
    }

    /* Won, return results */
    *amm = shmid;		/* Remember shared seg ID */
    *aptr = ptr;
    return TRUE;
#else
    errno = 0;			/* No error, just not implemented */
    return FALSE;
#endif
}

int
os_mmshare(osmm_t mm, char **aptr)
{
    return 0;		/* Nothing for now */
}

int
os_mmkill(osmm_t mm, char *ptr)
{
#if CENV_SYS_UNIX && KLH10_DEV_DP
    shmdt((caddr_t)ptr);			/* Detach attached segment */
    shmctl(mm, IPC_RMID,			/* then try to flush it */
		(struct shmid_ds *)NULL);
#endif
    return TRUE;
}

/* Attempt to lock all of our process memory now and in the future.
*/
#if CENV_SYS_DECOSF || CENV_SYS_SOLARIS
# include <sys/mman.h>
#endif

int
os_memlock(int dolock)
{
#if CENV_SYS_DECOSF || CENV_SYS_SOLARIS
    /* Both Solaris and OSF/1 have mlockall() which looks like what we want.
    ** It requires being the super-user, but don't bother to check here,
    ** just return error if it fails for any reason.
    */
    if (dolock)
	return (mlockall(MCL_CURRENT+MCL_FUTURE) == 0);
    else
	return (munlockall() == 0);
#else
    return FALSE;
#endif
}

/* Dynamic/Sharable Library Loading stuff
*/

#if CENV_SYS_DECOSF
# include <dlfcn.h>	/* Defs for dlopen, dlsym, dlclose, dlerror */
#endif

int
os_dlload(FILE *f,		/* Report errors here */
	char *path,		/* Pathname of loadable lib */
	osdll_t *ahdl,		/* Returned handle if any */
	char *isym,		/* Init Symbol to look up */
	void **avec)		/* Returned symbol address */
{
#if CENV_SYS_DECOSF
    void *hdl, *vec;

    if (!(hdl = dlopen(path, RTLD_NOW))) {
	if (f)
	    fprintf(f, "Load of \"%s\" failed: %s\n", path, dlerror());
	return FALSE;
    }
    /* Load won, look up symbol! */
    if (!(vec = dlsym(hdl, isym))) {
	if (f)
	    fprintf(f, "Load of \"%s\" failed, couldn't resolve \"%s\": %s\n",
			path, isym, dlerror());
	dlclose(hdl);		/* Clean up by flushing it, sigh */
	return FALSE;
    }

    /* Everything won, return success! */
    *ahdl = hdl;
    *avec = vec;
    return TRUE;

#else	/* DLLs not supported */
    if (f)
	fprintf(f, "Cannot load \"%s\": Dynamic Libraries not supported\n",
			path);
    return FALSE;
#endif
}

int
os_dlunload(FILE *f, osdll_t hdl)
{
#if CENV_SYS_DECOSF
    dlclose(hdl);		/* No error return?  Foo! */
    return TRUE;		/* Hope this worked */

#else /* DLLs not supported, never loaded! */
    return TRUE;
#endif
}
