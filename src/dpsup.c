/* DPSUP.C - Device sub-Process Support facilities (OSD) for KLH10
*/
/* $Id: dpsup.c,v 2.5 2003/02/23 18:16:05 klh Exp $
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
 * $Log: dpsup.c,v $
 * Revision 2.5  2003/02/23 18:16:05  klh
 * Tweak casts to avoid warnings on NetBSD/Alpha.
 *
 * Revision 2.4  2002/04/24 07:46:15  klh
 * Change dpx_cnt from int to size_t, modify prototypes to match
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include "klh10.h"	/* Get config params */

#if !KLH10_DEV_DP && CENV_SYS_DECOSF
	/* Stupid gubbish needed to prevent OSF/1 AXP compiler from
	** halting merely because compiled file is empty!
	*/
static int decosfcclossage;
#endif

#if KLH10_DEV_DP		/* Moby conditional for entire file */

#include <stdio.h>
#include <string.h>		/* For strerror() if present */

#include "dpsup.h"

#if CENV_SYS_DECOSF || CENV_SYS_SUN || CENV_SYS_SOLARIS || CENV_SYS_XBSD || CENV_SYS_LINUX
# include <sys/types.h>
# include <sys/ipc.h>		/* SysV stuff */
# include <sys/shm.h>		/* SysV stuff */
# include <sys/wait.h>
# include <sys/mman.h>
# include <unistd.h>
# include <signal.h>
# if CENV_SYS_SUN || CENV_SYS_SOLARIS
#  define SIGMAX MAXSIG		/* Different wording on Sun */
# elif CENV_SYS_FREEBSD
#  define SIGMAX NSIG
# elif CENV_SYS_NETBSD || CENV_SYS_LINUX
#  define SIGMAX _NSIG
# endif
#endif /* CENV_SYS_DECOSF || CENV_SYS_SUN || CENV_SYS_SOLARIS || CENV_SYS_XBSD || CENV_SYS_LINUX */

#ifdef RCSID
 RCSID(dpsup_c,"$Id: dpsup.c,v 2.5 2003/02/23 18:16:05 klh Exp $")
#endif

static int dp_cxinit(struct dpc_s *, int, int, int, size_t, size_t);

/* DP_INIT - Called from superior (KLH10) to initialize device subprocess
**	context and shared memory area.
**
**	Note that the shared area is divided into three parts in this
**	order:
**		(1) DPC structure (of size dpcsiz)
**		(2) Output buffer 10->DP (of size outsiz)
**		(3) Input buffer  10<-DP (of size insiz)
**
**	*** THE READ-REVERSE CODE FOR TM02/TM03 DEPENDS ON THIS ORDERING! ***
**		(Check references to dptm_revpad)
*/
int dp_init(register struct dp_s *dp, size_t dpcsiz,
		int intyp, int inarg, size_t insiz,
		int outtyp, int outarg, size_t outsiz)
{
    register size_t totsiz;
    register int adj;
    register key_t shmid;
    register struct dpc_s *dpc;

    dp->dp_type = 0;		/* Ensure cleared in case bomb out */
    dp->dp_shmid = -1;
    dp->dp_adr = NULL;
    dp->dp_chpid = 0;

    /* Ensure all sizes are aligned to satisfy maximum restrictions */
    if (adj = (dpcsiz % sizeof(double)))
	dpcsiz += sizeof(double) - adj;
    if (adj = (insiz % sizeof(double)))
	insiz += sizeof(double) - adj;
    if (adj = (outsiz % sizeof(double)))
	outsiz += sizeof(double) - adj;

    totsiz = dpcsiz + insiz + outsiz;

    /* Create shared mem segment of given size */
    if (dpcsiz < sizeof(struct dpc_s)) { /* Ensure big enough for std stuff */
	fprintf(stderr, "[dp_init: dpcsiz %ld]\r\n", (long)dpcsiz);
	return FALSE;
    }

    /* Create a shared mem seg.  Set perms to owner-only RW. */
    if ((shmid = shmget(IPC_PRIVATE, (u_int)totsiz, 0600)) == -1) {
	fprintf(stderr, "[dp_init: shmget failed - %d]\r\n", errno);
	return FALSE;
    }

    /* Attempt to attach segment into our address space */
    dpc = (struct dpc_s *)shmat(shmid, (void *)0, SHM_RND);
    if (dpc == (struct dpc_s *)-1) {
	shmctl(shmid, IPC_RMID, (struct shmid_ds *)NULL);
	fprintf(stderr, "[dp_init: shmat failed - %d]\r\n", errno);
	return FALSE;
    }

    /* Won, init the shared DPC struct */
    memset((char *)dpc, 0, totsiz);
    strncpy(dpc->dpc_magic, DPC_MAGIC, sizeof(dpc->dpc_magic));
    dpc->dpc_fmtver = DPSUP_VERSION;	/* Set to current version */

    /* Set up output then input xfer stuff */
    if (!dp_cxinit(dpc, 1, outtyp, outarg, dpcsiz, outsiz)
      || !dp_cxinit(dpc, 0, intyp, inarg, dpcsiz+outsiz, insiz)) {

	shmdt((caddr_t)dpc);		/* Detach attached segment */
	shmctl(shmid, IPC_RMID, (struct shmid_ds *)NULL);
	fprintf(stderr, "[dp_init: xinit failed]\r\n");
	return FALSE;
    }

    /* Finally init the DP struct itself */
    dp->dp_type = DP_XT_MSIG;
    dp->dp_adr = dpc;
    dp->dp_shmid = shmid;

    return TRUE;
}

static int dp_cxinit(register struct dpc_s *dpc,
		     int dir, int type, int arg, size_t off, size_t siz)
{
    register struct dpx_s *dx;

    if (type != DP_XT_MSIG)
	return FALSE;			/* Unknown xfer type */

    /* Arg is signal # to use for this direction */
    if (arg <= 0 || SIGMAX <= arg)
	return FALSE;			/* Bad signal # */

    if (dir) {
	dx = &dpc->dpc_todp;	/* Output to DP */
	dx->dpx_dontyp = type;
	dx->dpx_donflg = 0;
	dx->dpx_donsig = arg;		/* Say how to ack sender (10) */
	dx->dpx_donpid = getpid();
	dx->dpx_sbuf = (unsigned char *)dpc + off;

    } else {
	dx = &dpc->dpc_frdp;	/* Input from DP */
	dx->dpx_waktyp = type;
	dx->dpx_wakflg = 0;
	dx->dpx_waksig = arg;		/* Say how to wakeup rcpt (10) */
	dx->dpx_wakpid = getpid();
	dx->dpx_rbuf = (unsigned char *)dpc + off;
    }

    dx->dpx_type = type;	/* Is this necessary? */

    dx->dpx_len = siz;		/* Size of buffer */
    dx->dpx_off = off;		/* Offset of buffer from start of seg */

    dx->dpx_rdyf = 0;		/* No data */
    dx->dpx_cmd = 0;		/* No command */

    return TRUE;
}


int dp_start(register struct dp_s *dp, char *prog)
{
    int err;
    int pid;
    char idbuf[20];
    sigset_t allmask, oldmask;
    int debug = (dp->dp_adr ? dp->dp_adr->dpc_debug : 0);

    /* Set up args for DP proc */
    sprintf(idbuf, "-DPM:%ld", (long)dp->dp_shmid);

    /* Check xct access for program */
    if ((err = access(prog, X_OK)) < 0) {
	fprintf(stderr, "[dp_exec: Cannot access \"%s\" - %s]\r\n",
			prog ? prog : "(nullptr)", dp_strerror(err));
	return FALSE;
    }

    /* Block all signals momentarily so new process isn't killed
    ** by asynch signals.
    */
    sigfillset(&allmask);
    (void) sigprocmask(SIG_SETMASK, &allmask, &oldmask);

    if (debug)
	fprintf(stderr, "[dp_start: Forking...]");
    if ((pid = fork()) < 0) {
	/* Cannot fork */
	fprintf(stderr, "[dp_exec: Cannot fork for \"%s\" - %s]\r\n",
			prog ? prog : "(nullptr)", dp_strerror(-1));
	(void) sigprocmask(SIG_SETMASK, &oldmask, (sigset_t *)NULL);
	return FALSE;
    }
    if (debug)
	fprintf(stderr, "[dp_start: Forked %d]\r\n", pid);
    if (pid == 0) {
	/* We're the child process, start up specified program */
	if (debug) {
	    fprintf(stderr,
		    "[dp_start: execing \"%s\" \"%s\" \"-debug\"]\r\n",
		    prog, idbuf);
	    execl(prog, prog, idbuf, "-debug", (char *)NULL);
	} else
	    execl(prog, prog, idbuf, (char *)NULL);

	fprintf(stderr, "[dp_exec: execl failed - %s]\r\n",
		dp_strerror(-1));
	_exit(1);		/* Not exit(), to avoid muckage */
    }
    dp->dp_chpid = pid;		/* Parent, remember child's PID */

#if 0	/* This has been needed sometimes to get child going!!! */
  {
    int i;
    for (i = (1<<28); --i > 0;);	/* Should take 8 sec or so */
  }
#endif
    (void) sigprocmask(SIG_SETMASK, &oldmask, (sigset_t *)NULL);

    return TRUE;
}


int dp_reset(register struct dp_s *dp)
{

#if 0
    if (dp->dp_chpid) {
	kill(dp->dp_chpid, SIGKILL);
    }
#endif
    return 1;
}

/* DP_TERM - Terminate entire subproc hackery - opposite of dp_init.
*/
int dp_term(register struct dp_s *dp, int timeout)
{
    dp_stop(dp, timeout);	/* Stop, kill subproc */

    /* Try to kill shared mem segment */
    if (dp->dp_type == DP_XT_MSIG) {
	shmdt((caddr_t)(dp->dp_adr));		/* Detach attached segment */
	shmctl(dp->dp_shmid, IPC_RMID,		/* then try to flush it */
			(struct shmid_ds *)NULL);
	dp->dp_adr = NULL;
	dp->dp_shmid = 0;
	dp->dp_type = 0;
    }
    return 1;
}

static int dp_killchild(pid_t pid, int timeout)
{
    int status, res, cnt;

    kill(pid, SIGKILL);
    cnt = timeout ? timeout : 1;
    for (; --cnt >= 0;) {
	res = waitpid(pid, &status, WNOHANG);
	if (res == -1) {
	    if (errno == EINTR) continue;
	    return (errno == ECHILD);	/* TRUE if won */
	}
	if (res != 0)	/* Nonzero result means got stopped proc */
	    return 1;		/* Won! */
	if (cnt > 0)
	    dp_sleep(1);	/* Urgh, wait a bit, big crock */
    }
    return 0;			/* Timed out, failed */
}

int dp_stop(register struct dp_s *dp, int timeout)
{
    pid_t pid, pid2;

    switch (dp->dp_type) {
    case DP_XT_MSIG:
	if (pid = dp->dp_chpid) {
	    (void) dp_killchild(pid, timeout);

	    /* For now, flush pid even if didn't find it when waited. */
	    dp->dp_chpid = 0;
	}

	/* Check for presence of 2nd child */
	if ((pid2 = dp->dp_adr->dpc_frdp.dpx_donpid)
	  && (pid != pid2)) {
	    (void) dp_killchild(pid2, timeout);

	    /* For now, flush pid even if didn't find it when waited. */
	    dp->dp_adr->dpc_frdp.dpx_donpid = 0;
	}
	break;
    }

    /* Clear up all xfer stuff from this side */
    return 1;
}

/* Called from subprocess (dp) */

/*
	The current method for signalling the subproc uses signals
combined with a "ready" flag.  The ready flag is the true state indicator;
the signal merely serves as a method of waking up the subproc in case it
is run-blocked, either waiting for the flag to change --OR-- for some
system call to complete.

	Normally the subproc runs with the wakeup signal masked (blocked),
and only permits it to interrupt when it makes a sigpause(0) call as part
of the flag check-and-wait procedure.  In this respect the signal amounts
to little more than a semaphore.
	However, the subproc also has the option of unmasking the signal
so that it can permit selected system calls to be interrupted.  For this
reason, the sigaction which sets up the signal configures it to NOT
restart system calls.
	The latter feature (along with avoiding the cleanup problems for
SYSV semaphores) is why signals are used instead of OS semaphores.

*/

static void dp_subsighan(int);
static int dp_signal(int sig, void (*func)(int));

int dp_main(register struct dp_s *dp, int argc, char **argv)
{
    long shmarg;
    register struct dpc_s *dpc;
    register struct dpx_s *dpx;
    int tosig, frsig;
    sigset_t mask;

    if ((argc < 2) || (strncmp(argv[1], "-DPM:", 5) != 0)) {
	fprintf(stderr, "[%s: need -DPM: arg]\r\n",
		(argc > 0 ? argv[0] : "(?) dp_main"));
	return 0;
    }

    if (1 != sscanf(&argv[1][5], "%ld", &shmarg)) {
	fprintf(stderr, "[%s: Couldn't parse \"%s\"]\r\n",
				argv[0], argv[1]);
	return 0;
    }

    /* Got shmid for segment from our parent, try attaching it! */
    dpc = (struct dpc_s *)shmat((int)shmarg, (void *)NULL, SHM_RND);
    if (dpc == (struct dpc_s *)-1) {
	fprintf(stderr, "[%s: Couldn't attach shmid 0x%lx]\r\n",
				argv[0], shmarg);
	return 0;
    }

    /* Verify that we got a DP memory structure */
    if (strncmp(dpc->dpc_magic, DPC_MAGIC, sizeof(dpc->dpc_magic)) != 0) {
	fprintf(stderr, "[%s: Invalid DPM seg %ld - bad magic ID]\r\n",
				argv[0], (long)shmarg);
	return 0;
    }

    /* Verify what format the superior expects to be using.
       Should this ignore patch revs?
     */
    switch (dpc->dpc_fmtver) {
	case DPSUP_VERSION:
	    break;
	default:
	    fprintf(stderr,
		"[%s: Incompatible DP versions - sup %d.%d.%d, sub %d.%d.%d]\r\n",
				argv[0],
			DPC_GV_MAJ(dpc->dpc_fmtver),
			DPC_GV_MIN(dpc->dpc_fmtver),
			DPC_GV_PAT(dpc->dpc_fmtver),
			DPC_GV_MAJ(DPSUP_VERSION),
			DPC_GV_MIN(DPSUP_VERSION),
			DPC_GV_PAT(DPSUP_VERSION));

	    return 0;
    }

    /* Hurray, we're winning... set up our stuff */
    dp->dp_type = DP_XT_MSIG;	/* Should set this differently later */
    dp->dp_adr = dpc;
    dp->dp_shmid = shmarg;

    /* Set up I/O to 10 - assume DP_XT_MSIG for now */
    tosig = frsig = SIGURG;		/* Set up signal(s) to use */
    sigemptyset(&mask);
    sigaddset(&mask, tosig);		/* and combined mask */
    sigaddset(&mask, frsig);
#if 1
    sigprocmask(SIG_BLOCK, &mask, (sigset_t *)NULL);	/* Ensure blocked */
#endif
					/* Safe now if superior jumps gun */

    dpx = &dp->dp_adr->dpc_frdp;	/* Output from DP to 10 */
    dpx->dpx_dontyp = DP_XT_MSIG;	/* Say how to ack sender (dp) */
    dpx->dpx_donsig = frsig;		/* Use this signal # */
    sigemptyset(&dpx->dpx_donmsk);	/* Set corresponding mask bit */
    sigaddset(&dpx->dpx_donmsk, dpx->dpx_donsig);
    dpx->dpx_donpid = getpid();
    dpx->dpx_sbuf = (unsigned char *)dpc + dpx->dpx_off;

    dpx = &dp->dp_adr->dpc_todp;	/* Input to DP from 10 */
    dpx->dpx_waktyp = DP_XT_MSIG;
    dpx->dpx_waksig = tosig;		/* Say how to wakeup rcpt (dp) */
    sigemptyset(&dpx->dpx_wakmsk);	/* Set corresponding mask bit */
    sigaddset(&dpx->dpx_wakmsk, dpx->dpx_waksig);
    dpx->dpx_wakpid = getpid();
    dpx->dpx_rbuf = (unsigned char *)dpc + dpx->dpx_off;

    if (dp_signal(frsig, dp_subsighan) == -1) {
	fprintf(stderr, "[%s: Couldn't set signal handler]\r\n", argv[0]);
	return 0;
    }
    if (tosig != frsig			/* Do second if necessary */
      && (dp_signal(tosig, dp_subsighan) == -1)) {
	fprintf(stderr, "[%s: Couldn't set signal handler]\r\n", argv[0]);
	return 0;
    }

#if 1
    /* Paranoia: check for pending signals before unblocking */
  {
    int sig;
    int npend = 0;
    sigset_t pendmask;
    sigpending(&pendmask);
    for (sig = 1; sig < SIGMAX; sig++) {
	if (sigismember(&pendmask, sig)) {
	    if (npend++ == 0)
		fprintf(stderr, "[%s: WARNING! sigpend %d", argv[0], sig);
	    else
		fprintf(stderr, ", %d", sig);
	}
    }
    if (npend)
	fprintf(stderr, "]\r\n");
  }
#endif

    /* Unblock all but the DP sigs.  Instead of restoring mask as it was
    ** at start of call, this actually CLEARS everything (except the DP sigs),
    ** in order to balance the suspend-everything action of dp_start for
    ** the child fork.
    */
    sigprocmask(SIG_SETMASK, &mask, (sigset_t *)NULL);

    /* Finally, check new MEMLOCK flag to see if superior wants
    ** us to propagate a memory-locked condition.
    */
    if (dpc->dpc_flags & DPCF_MEMLOCK) {
	/* Can only lock mem if superuser, but don't bother checking
	** beforehand - only warn if an error isn't EPERM and hence is
	** unusual.
	*/
#if CENV_SYS_DECOSF || CENV_SYS_SOLARIS || CENV_SYS_LINUX
	if (mlockall(MCL_CURRENT|MCL_FUTURE) != 0) {
	    if (errno != EPERM)
		fprintf(stderr, "[%s: mlockall failed - %s]\r\n",
				argv[0], dp_strerror(-1));
	}
#endif
    }

    return 1;
}

void dp_exit(register struct dp_s *dp, int res)
{
    if (dp->dp_chpid) {
	kill(dp->dp_chpid, SIGKILL);
	/* Perhaps later wait for that specific child */
    }
    exit(res);
}

static void dp_subsighan(int junk)
{
    /* Do nothing -- call merely breaks out of sigpause(0) */
}

/* Called from both KLH10 and device subprocess */

int dp_xstest(register struct dpx_s *dx)	/* TRUE if can send */
{
    switch (dx->dpx_type) {
    case DP_XT_MSIG:
	return dp_xtmsig_stest(dx);
    }
    return FALSE;
}

void dp_xsblock(register struct dpx_s *dx)	/* Block for a later test */
{
    switch (dx->dpx_type) {
    case DP_XT_MSIG:
	dp_xtmsig_sblock(dx);
	return;
    }
    return;
}

int dp_xswait(register struct dpx_s *dx)	/* Wait until can send */
{
    switch (dx->dpx_type) {
    case DP_XT_MSIG:
	dp_xtmsig_swait(dx);
	return TRUE;
    }
    return FALSE;
}


unsigned char *dp_xsbuff(register struct dpx_s *dx,
			 register size_t *asiz)	/* Get buffer for send data */
{
    switch (dx->dpx_type) {
    case DP_XT_MSIG:
	return dp_xtmsig_sbuff(dx, asiz);
    }
    if (asiz)
	*asiz = 0;
    return NULL;
}

void dp_xswake(register struct dpx_s *dx)	/* Send; say message ready */
{
    switch (dx->dpx_type) {
    case DP_XT_MSIG:
	dp_xtmsig_swake(dx);
	return;
    }
}

void dp_xsend(register struct dpx_s *dx,
	      int cmd, size_t cnt)		/* Send cmd and data */
{
    switch (dx->dpx_type) {
    case DP_XT_MSIG:
	dp_xtmsig_send(dx, cmd, cnt);
	return;
    }
}


int dp_xrtest(register struct dpx_s *dx)	/* TRUE if can receive */
{
    switch (dx->dpx_type) {
    case DP_XT_MSIG:
	return dp_xtmsig_rtest(dx);
    }
    return FALSE;
}

void dp_xrblock(register struct dpx_s *dx)	/* Block for a later test */
{
    switch (dx->dpx_type) {
    case DP_XT_MSIG:
	dp_xtmsig_rblock(dx);
	return;
    }
    return;
}


int dp_xrwait(register struct dpx_s *dx) /* Wait until can definitely recv */
{
    switch (dx->dpx_type) {
    case DP_XT_MSIG:
	dp_xtmsig_rwait(dx);
	return TRUE;
    }
    return FALSE;
}


unsigned char *dp_xrbuff(register struct dpx_s *dx,
			 register size_t *asiz)	/* Get buffer for recv data */
{
    switch (dx->dpx_type) {
    case DP_XT_MSIG:
	return dp_xtmsig_rbuff(dx, asiz);
    }
    if (asiz)
	*asiz = 0;
    return NULL;
}

void dp_xrdone(register struct dpx_s *dx)	/* Done, ready for next msg */
{
    switch (dx->dpx_type) {
    case DP_XT_MSIG:
	dp_xtmsig_rdone(dx);
	return;
    }
}

void dp_xrdoack(register struct dpx_s *dx,	/* Done, ready for next msg */
		int res)
{
    switch (dx->dpx_type) {
    case DP_XT_MSIG:
	dp_xtmsig_rdoack(dx, res);
	return;
    }
}

int dp_xrcmd(register struct dpx_s *dx)		/* Get command */
{
    switch (dx->dpx_type) {
    case DP_XT_MSIG:
	return dp_xtmsig_rcmd(dx);
    }
    return 0;
}

size_t dp_xrcnt(register struct dpx_s *dx)	/* Get data count */
{
    switch (dx->dpx_type) {
    case DP_XT_MSIG:
	return dp_xtmsig_rcnt(dx);
    }
    return 0;
}

/* Same as os_strerror() from osdsup.c, put here to avoid having to
** grab the entire OSDSUP package when being built for DP procs.
*/

char *
dp_strerror(int err)
{
    if (err == -1 && errno != err)
	return dp_strerror(errno);
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
	    return (char *)sys_errlist[err];
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


/* Likewise copied from OSDSUP.C */

static int
dp_signal(int sig, void (*func)(int))
{
#if CENV_SYSF_SIGSET
    struct sigaction act, oact;

    act.sa_handler = func;
    act.sa_flags = 0 /*SA_RESTART*/;	/* Do *NOT* ask for restart! */
    sigemptyset(&act.sa_mask);
    sigaddset(&act.sa_mask, sig);	/* Suspend this sig during handler */
    return sigaction(sig, &act, &oact);
#elif CENV_SYS_BSD
    /* If really BSD, probably should use sigvec instead */
    return (signal(sig, func) == (void (*)())-1) ? -1 : 0;
#else
    *** ERROR *** need signal support
#endif
}

void
dp_sigwait(void)
{
    sigset_t nomsk;

    sigemptyset(&nomsk);	/* Clear out mask */
    sigsuspend(&nomsk);		/* Block until something goes off */
}

/* DP_SLEEP - Sleep for N seconds.  Copied from OSDSUP's os_sleep(),
**	see that for more comments.
*/
void
dp_sleep(int secs)
{
#if CENV_SYS_DECOSF
    sleep(secs);		/* Independent of interval timers! */

#elif CENV_SYSF_BSDTIMEVAL && CENV_SYSF_SIGSET
    /* Must save & restore ITIMER_REAL & SIGALRM, which conflict w/sleep() */
    struct itimerval ztm, otm;
    struct sigaction act, oact;

    timerclear(&ztm.it_interval);
    timerclear(&ztm.it_value);
    setitimer(ITIMER_REAL, &ztm, &otm);

    act.sa_handler = SIG_IGN;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaction(SIGALRM, &act, &oact);

    sleep(secs);			/* Do the gubbish */

    /* Now restore the world */
    sigaction(SIGALRM, &oact, (struct sigaction *)NULL);
    setitimer(ITIMER_REAL, &otm, (struct itimerval *)NULL);

#else
# error "Need implementation for dp_sleep()"
#endif
}

#endif /* KLH10_DEV_DP */
