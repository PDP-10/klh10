/* OSDSUP.H - OS-Dependent Support defs for KLH10
*/
/* $Id: osdsup.h,v 2.9 2002/04/24 07:56:08 klh Exp $
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
 * $Log: osdsup.h,v $
 * Revision 2.9  2002/04/24 07:56:08  klh
 * Add os_msleep, using nanosleep
 *
 * Revision 2.8  2002/03/28 16:52:02  klh
 * First pass at using LFS (Large File Support)
 *
 * Revision 2.7  2002/03/26 06:18:24  klh
 * Add correct timezone to DTE's time info
 *
 * Revision 2.6  2002/03/21 09:50:08  klh
 * Mods for CMDRUN (concurrent mode)
 *
 * Revision 2.5  2001/11/19 10:43:28  klh
 * Add os_rtm_adjust_base for ITS on Mac
 *
 * Revision 2.4  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#ifndef OSDSUP_INCLUDED
#define OSDSUP_INCLUDED 1

#ifdef RCSID
 RCSID(osdsup_h,"$Id: osdsup.h,v 2.9 2002/04/24 07:56:08 klh Exp $")
#endif

#include "word10.h"	/* Needed for protos */

/* General stuff */
#if CENV_SYS_MAC
    extern int errno;	/* This should come from an ANSI <errno.h> file */
#endif /* MAC */

extern void os_init(void);
extern void os_exit(int);
extern char *os_strerror(int);


/* General I/O facilities */
#include <stdio.h>
#if CENV_SYS_UNIX
#  include <fcntl.h>	/* For open() */
#  include <errno.h>
#  include <sys/file.h>	/* For L_SET */
    typedef int osfd_t;			/* OS open file descriptor */
# if CENV_SYSF_LFS
    typedef off_t osdaddr_t;		/* OS disk address */
# else
    typedef unsigned long osdaddr_t;	/* OS disk address */
# endif
#   define OSDADDR_FMT CENV_SYSF_LFS_FMT
#   define OS_MAXPATHLEN 512	/* MAXPATHLEN? */
#elif CENV_SYS_MAC
    typedef short osfd_t;	/* OS open file descriptor (ioFRefNum) */
    typedef long osdaddr_t;	/* OS disk address (ioPosOffset) */
#   define OS_MAXPATHLEN 256	/* Random guess */
#endif

extern int os_fdopen(osfd_t *, char *, char *);
extern int os_fdseek(osfd_t, osdaddr_t);
extern int os_fdread(osfd_t, char *, size_t, size_t *);
extern int os_fdwrite(osfd_t, char *, size_t, size_t *);
extern int os_fdclose(osfd_t);


/* Compatibility macro definitions */
#if !(CENV_SYS_UNIX) || CENV_SYS_SOLARIS
#  define _setjmp setjmp	/* Not everyone has fast version */
#  define _longjmp longjmp
#endif

/* Signal facilities.  Not provided on all environments.
*/

#if CENV_SYS_UNIX || CENV_SYS_MAC
# include <signal.h>
#endif

/* For consistency & convenience, define a "signal handler" function type
   that we'll use throughout; code doing anything different will have
   to use explicit casts.
*/
typedef void ossighandler_t(int);

#ifndef SIG_ERR
# define SIG_ERR ((ossighandler_t *)-1)
#endif

#if CENV_SYSF_SIGSET
# define ossigset_t sigset_t
# define os_sigemptyset(set) sigemptyset(set)
# define os_sigfillset(set)  sigfillset(set)
# define os_sigaddset(set,s) sigaddset(set,s)
# define os_sigdelset(set,s) sigdelset(set,s)
# define os_sigismember(set,s) sigismember(set,s)
# define os_sigsetmask(new,old) sigprocmask(SIG_SETMASK,new,old)
# define os_sigblock(new,old) sigprocmask(SIG_BLOCK,new,old)
#else
# ifndef sigmask
#  define sigmask(m) (1L << ((m) - 1))
# endif
# define ossigset_t unsigned long
# define os_sigemptyset(set) (*(set) = 0)
# define os_sigfillset(set)  (*(set) = ~0L)
# define os_sigaddset(set,s) (*(set) |= sigmask(s))
# define os_sigdelset(set,s) (*(set) &= ~sigmask(s))
# define os_sigismember(set,s) (*(set) & sigmask(s))
# define os_sigsetmask(new,old) (((old) ? (*(old) = sigsetmask(*(new))) \
				        : sigsetmask(*(new))), 0)
# define os_sigblock(new,old) (((old) ? (*(old) = sigblock(*(new))) \
				        : sigblock(*(new))), 0)
#endif

typedef struct {
    int ossa_sig;
#if CENV_SYSF_SIGSET
    struct sigaction ossa_sa;
#else
    ossighandler_t *ossa_handler;
#endif
} ossigact_t;

extern int osux_signal(int, ossighandler_t *);	/* UNIX only */
extern int osux_sigact(int, ossighandler_t *, ossigact_t *);
extern int osux_sigrestore(ossigact_t *);

/* TTY facilities */

typedef struct {
    int osti_attrs;			/* Attr flags for future use */ 
# define OSTI_BKGDF	0x1
# define OSTI_INTCHR	0x2
# define OSTI_INTHDL	0x4
# define OSTI_TRMHDL	0x8
    int osti_bkgdf;			/* TRUE if in background mode */
    int osti_intchr;			/* Interrupt char */
    ossighandler_t *osti_inthdl;	/* Called when intchr detected */
    ossighandler_t *osti_trmhdl;	/* Called on SIGTERM or equiv */
} osttyinit_t;


extern void os_ttybkgd(ossighandler_t *rtn);	/* Say TTY in backgnd mode */
extern void os_ttyinit(osttyinit_t *);

extern void os_ttysig(ossighandler_t *rtn);
extern void
	os_ttyreset(void),
	os_ttycmdmode(void),
	os_ttycmdrunmode(void),
	os_ttyrunmode(void);
extern int os_ttyintest(void),
	os_ttyin(void),
	os_ttyout(int),
	os_ttysout(char *, int),
	os_ttycmchar(void);
extern char *os_ttycmline(char *, int);
extern void os_ttycmforce(void);


/* Special event/condition flag macros.
**	In order to work in a true threaded environment, these facilities
** will need to have lock/unlock functions added.  The flags are cleared
** in only one place in kn10cpu.c, where the flagged actions are handled.
*/

typedef int osintf_t;		/* Type to use for an interrupt flag */
extern osintf_t os_swap(osintf_t *, int);
#define INTF_INIT(flag) ((flag) = 0)
#define INTF_SET(flag) ((flag) = 1)
#define INTF_TEST(flag) ((flag) != 0)
#define INTF_ACTBEG(flag) do { (flag) = 2
#define INTF_ACTEND(flag) } while (os_swap(&(flag), 0) != 2)


/* Time facilities */

#include <time.h>	/* For os_tmget(), for KL DTE */

/* Real-time - osrtm_t
 */
#if CENV_SYSF_BSDTIMEVAL		/* timeval is a BSD artifact */
#  include <sys/time.h>
   typedef struct timeval osrtm_t;
#  define OS_RTM_SEC(rtm) ((rtm).tv_sec)
#  define OS_RTM_USEC(rtm) ((rtm).tv_usec)
#elif CENV_SYS_MAC
    /* An unsigned 64-bit number of microseconds on the Mac */
    typedef UnsignedWide osrtm_t;
#  define OS_RTM_SEC(rtm) (os_rtm_to_secs(rtm))
#  define OS_RTM_USEC(rtm) (os_rtm_to_usecs(rtm))
#  define RTM_PTR_TO_LONG_LONG(rtmptr) (*(unsigned long long*)(rtmptr))
#else
    -- ERROR --
#endif

typedef struct {
    int ostmr_type;
#ifdef ITIMER_REAL
# define OS_ITIMER_REAL ITIMER_REAL
# define OS_ITIMER_VIRT ITIMER_VIRTUAL
#else
# define OS_ITIMER_REAL 0
# define OS_ITIMER_VIRT 1
#endif
    ossigact_t ostmr_sigact;
#if CENV_SYSF_BSDTIMEVAL
    struct itimerval ostmr_itm;
#elif CENV_SYS_MAC
    /* XXX: Mac implem uses static interval_timer* timer instead of a
    ** field here, fix later.
    */
#else
# error ostmr_itm not implemented
#endif    
} ostimer_t;

/* Sleep time - osstm_t
 */
#if CENV_SYSF_NANOSLEEP
typedef struct timespec osstm_t;
# define OS_STM_SEC(stm)  ((stm).tv_sec)
# define OS_STM_USEC(stm) ((stm).tv_nsec/1000)
# define OS_STM_SET(stm, sec) (((stm).tv_sec = sec), ((stm).tv_nsec = 0))
# define OS_STM_MSET(stm, ms) (((stm).tv_sec = (ms/1000)),\
			((stm).tv_nsec = (ms%1000)*1000000))
#else	/* Otherwise assume 1-msec granularity */
typedef long osstm_t;
# define OS_STM_SEC(stm)  ((stm)/1000)
# define OS_STM_USEC(stm) (((stm)%1000)*1000)
# define OS_STM_SET(stm, sec) ((stm) = (sec)*1000)
# define OS_STM_MSET(stm, ms) ((stm) = (ms))
#endif /* !CENV_SYSF_NANOSLEEP */


extern int os_vrtmget(osrtm_t *);
extern int os_rtmget(osrtm_t *);
extern void os_rtm_adjust_base(osrtm_t *in, osrtm_t *out, int b_absolute);
extern void os_rtmsub(osrtm_t *, osrtm_t *);
extern void os_rtm_tokst(osrtm_t *, dw10_t *);
extern unsigned long os_rtm_toqct(osrtm_t *);
extern unsigned long os_rtm_toklt(osrtm_t *);

extern void os_rtimer(ossighandler_t *, uint32);
extern void os_vtimer(ossighandler_t *, uint32);
extern void os_timer(int, ossighandler_t *, uint32, ostimer_t *);
extern void os_timer_restore(ostimer_t *);
extern void os_v2rt_idle(ossighandler_t *);
extern void os_sleep(int);
extern int  os_msleep(osstm_t *);

extern int os_tmget(struct tm *, int *);	/* Only for KL DTE */

#if CENV_SYS_MAC
extern unsigned long os_rtm_to_secs(osrtm_t rtm);
extern unsigned long os_rtm_to_usecs(osrtm_t rtm);
#endif

/* Process priority facilities */
#if CENV_SYS_UNIX
#  include <sys/resource.h>
#endif

typedef int ospri_t;
extern int os_setpriority(ospri_t);	/* Set process priority */
extern int os_getpriority(ospri_t *);	/* Get process priority */


/* Memory Mapping facilities */
#if CENV_SYS_UNIX
#  include <sys/types.h>
#  include <sys/ipc.h>
#  include <sys/shm.h>
#endif

typedef int osmm_t;

extern int os_mmcreate(size_t, osmm_t *, char **);
extern int os_mmshare(osmm_t, char **);
extern int os_mmkill(osmm_t, char *);
extern int os_memlock(int);

/* Dynamic Library Loading facilities */
#if CENV_SYS_DECOSF
    typedef void *osdll_t;
#else
    typedef char *osdll_t;
#endif

extern int os_dlload(FILE *, char *, osdll_t *, char *, void **);
extern int os_dlunload(FILE *, osdll_t);

#endif /* ifndef OSDSUP_INCLUDED */
