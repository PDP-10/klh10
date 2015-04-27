/* DPTM03.C - Device sub-Process for KLH10 TM02/3 Magtape Interface
*/
/* $Id: dptm03.c,v 2.5 2002/04/24 07:52:37 klh Exp $
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
 * $Log: dptm03.c,v $
 * Revision 2.5  2002/04/24 07:52:37  klh
 * Improve hardware tape support, for Linux in particular
 *
 * Revision 2.4  2002/03/13 12:36:44  klh
 * First pass at Linux-specific status support.
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*

	This subprocess is intended to handle either actual hardware
tape drives, or virtual tape files.  The behavior of each is somewhat
different.  The following are the four basic states:

SOFT-OFFLINE: (UNMOUNTED)
	Emulation pretends that a virtual drive exists, without
	a tape in it.
	The only way to change this state is via a manual MOUNT request that
	selects either a virtual tape or a hardware drive.

SOFT-ONLINE:
	A virtual tape spec is online.
	A manual {UN}MOUNT request can change to any of the other three states.
	In addition, the DPTM_UNL (unload) subproc command will change
	this state to the UNMOUNT state.

HARD-OFFLINE:
	A hardware drive spec is mounted but offline.  The process is
	blocked in an open() waiting for the drive to come back online.
	A manual {UN}MOUNT request can change to any of the other three states.
	In addition, if the drive comes online the state will change to
	HARD-ONLINE and the 10 will be signalled.

HARD-ONLINE:
	A hardware drive spec is mounted and a tape is online.
	Emulation attempts to map the hardware state
	into the 10 and vice versa as accurately as possible.
	A manual {UN}MOUNT request can change to any of the other three states.
	In addition, if the DPTM_UNL (unload) subproc command succeeds
	in taking the physical drive offline, the state will change to
	HARD-OFFLINE.



MOUNT/UNMOUNT requests:

	For the time being these must come from the KLH10 process itself
via the DPTM_MOUNT and DPTM_ROMNT commands.
	Eventually it should be possible for such requests to be delivered
asynchronously from outside the KLH10.  There are various possible ways to
do this; the main problem is how to agree on a reasonable rendezvous point
beforehand.  This could be a socket (port number), IPC message queue (key),
shared memory seg (key), file (pathname), whatever.  One appealing thing
about the file approach is that filenames can be much more descriptive;
IPC keys for example are only a single integer value and one can't easily
map from a device name to key, thus requiring another round of mapping.
A simple file-oriented mechanism could be:

	Assume environment variable KLH10_HOME defines home directory of
	the running KLH10 of interest (a system might have more than one).
	In that directory:

	dv-<name>.dev	- Lock file for device <name>
		If succeed in locking file, process reads it to obtain
		running device's PID and signal # to use (and whatever else).
	dv-<name>.req	- When signalled, DP proc looks for this filename.
		If found, reads it and handles request, flushing file.
		Could return result, if one, in:
	dv-<name>.sta

Main problem, as usual, is how to clean up; what happens if DP dies without
a chance to flush the lock file?  Any way for a mounter program to verify
that the info corresponds to a live running KLH10, and thus avoid sending
spurious signals to an innocent bystander process?  Unix is extremely
deficient in ways for a process to inquire about other processes.


MOUNT PROBLEM:
	- How can DP detect when hard-mounted drive comes online?
	If UNLOAD is given, seems to be no way to get MOL again
	other than by periodic checks.

Is there any chance that SIGIO would work here?
	No -- it doesn't.  Worse yet, status query doesn't work
	either!  Once it goes offline, putting in a new tape
	doesn't turn status bits online again!  Have to close
	the FD and open again.

Open attempt will *hang* until drive is online...
	perhaps that's how to notice it?  Could have SIG interrupt
	out of OPEN attempt, so can still handle requests.

Note: if non-blocking (O_NONBLOCK) open is done, will succeed even if
	drive is offline, but status queries WILL NOT SHOW when it becomes
	online again!  Just as for post-UNLOAD state.


ADDITIONAL NOTES:

	If try to open drive while it's turned off, get I/O error (I think).

	If try to open while it's on but offline, block and wait (good).
[dptm03: Mounted tape device "/dev/rmt0a"]

	When put tape in drive, get:
[dptm03: Tape came online: "/dev/rmt0a"]

	But there's some weird state as follows:
	Read tape, then unload tape.
	Nothing happens according to dptm03 stderr.
	Put tape back in drive.  suddenly get:

[dptm03: Cannot open device "/dev/rmt0a":I/O error]
[dptm03: Closing "/dev/rmt0a"]

	WTF?!?!?!  Turns out this was probably the 45-second timeout...

Prog state	  \	DevOff	Offline	Online	Unload
-------------------
open non-blocking	err:5	OK	OK	?
open blocking		err:5	blk(*)	OK	?
open, doing MT op	?	err:5	OK

	blk(*) = blocks for 45 seconds, then returns err:5 (I/O Error)
		Otherwise returns as soon as drive comes online.

Note: returns err:16 (Device Busy) if someone else has drive open (including
	self on a different FD)

[3/10/97] NOTE: as of OSF/1 V4.0 it appears that at least one tape
driver for 1/2" magtape reels has been munged so that attempting to
open the drive for read/write access will *FAIL* with err:13
"Permission denied" if the tape is loaded without a write-ring --
i.e. it is write-locked.  The drive must be opened with a mode of
O_READ rather than O_RDWR!  This does not happen for other drives such
as the 4mm TLZ06.  What the hell were they smoking when they
implemented *this*????

So, algorithm (on DECOSF anyway) should be:

	[0] SOFTOFF STATE.  Attempt mount by opening in R/O non-block mode.
		If success, close FD and go to HARDOFF state (next step).
		If fail, error will be:
			ENOENT: No such file or device
			EIO: I/O Error - Drive powered off
			EBUSY: Device Busy - someone else has drive open
		For all of them, give up and revert to SOFTOFF state.

	[1] HARDOFF state.  Re-try open in R/W blocking mode.  This will block
		for up to 45 seconds.
		If success, go to HARDON state (next step).
		If fail, error should be:
		    EIO: I/O Error - open timed out.  (this is also
			returned if drive is powered off, but we know
			from previous step that it exists).
			Just try again.  If "too many" EIO failures
			(say 10 mins), go back to step [0];
			drive may have been powered off.
		    EACCES: Permission denied - if in R/W mode, *may* mean
			that tape is write-locked, so drive must be
			re-opened read-only.
			Try again in R/O mode.  If that succeeds,
			verify tape is write-locked, and go back to step [0]
			if inconsistent.
		If any other failure, go back to step [0].

	[2] HARDON STATE. When blocking open succeeds, drive should be
		online (medium online).
		Tell 10 it's online and accept commands for drive.

	* Drive state can change for one of these reasons:
		- Command given to UNLOAD tape.  The IOCTL does not appear
			to return until the command is complete, so when it's
			done the state immediately changes to HARDOFF.
		- Mount/Unmount request received.  State changes to SOFTOFF
			and then to whatever results from the mount attempt,
			if one.
		- Physically taken offline (tape out or powered off).
			Any tape operation will return EIO (I/O Error) and
			tape status will show MOL off.  State changes to
			HARDOFF.
		- Actual media I/O errors.  Tape operation will probably
			return EIO just as for going offline, but hopefully
			the tape status will continue to show MOL on; state
			should remain HARDON.
*/

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>

#include "klh10.h"	/* For config params */

#if CENV_SYS_UNIX
# include <unistd.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <sys/file.h>
# include <sys/ioctl.h>
# include <sys/time.h>
# include <sys/mtio.h>
# include <sys/types.h>
# include <sys/ioctl.h>
#endif
#if CENV_SYS_DECOSF
# include <sys/devio.h>		/* To support DEVIOCGET */
#endif
#if CENV_SYS_LINUX
# include <sys/stat.h>		/* To support fstat() */
#endif

#include "dpsup.h"		/* General DP defs */
#include "dptm03.h"		/* TM03 specific defs */
#include "vmtape.h"		/* Virtual tape hackery */

#ifdef RCSID
 RCSID(dptm03_c,"$Id: dptm03.c,v 2.5 2002/04/24 07:52:37 klh Exp $")
#endif

/* Set 1 to compile horrible re-open-on-error hack for Linux.
 * Not used yet in anticipation of st.c driver fixes.
 */
#ifndef DPTM03_LINUX_HACK
#define DPTM03_LINUX_HACK 0 /*CENV_SYS_LINUX*/
#endif

/* Flag args to os_mtopen() */
#define OS_MTOF_READONLY 01	/* Open tape drive read-only */
#define OS_MTOF_NONBLOCK 02	/* Open in non-blocking mode */
#define OS_MTOF_NRCHECK  04	/* Check for no-rewind device */

struct devmt {
	struct dp_s d_dp;	/* Point to shared memory area */
	struct dptm03_s *d_tm;	/* Shorthand ptr to DPTM03 struct in area */
	int d_mntreq;		/* TRUE if mount request pending */
	int d_state;
#define DPTM03_STF_HARD 01	/* 0=virtual, 1=hard */
#define DPTM03_STF_MOL  02	/* 0=offline, 1=online */
#define DPTM03_STA_SOFTOFF 0
#define DPTM03_STA_SOFTON  (DPTM03_STF_MOL)
#define DPTM03_STA_HARDOFF (DPTM03_STF_HARD)
#define DPTM03_STA_HARDON  (DPTM03_STF_HARD|DPTM03_STF_MOL)
	int d_openretry;	/* Retry count for HARDOFF blocking opens */

	int d_istape;		/* Tape type, MTYP_xxx */
	char *d_path;		/* M Tape drive path spec */
#if CENV_SYS_UNIX
	int d_fd;
	int d_isnorewind;	/* TRUE if a no-rewind device path */
#endif
	size_t d_recsiz;	/* Block (record) size to use */
	unsigned char *d_buff;	/* Ptr to buffer loc */
	size_t d_blen;		/* Actual buffer length */
	int mta_bot;
	int mta_eot;
	int mta_eof;
	int mta_mol;
	int mta_wrl;
	int mta_rew;
	int mta_err;
	long mta_erreg;		/* OS-specific err info if any */
	long mta_frms;
	long mta_fileno;	/* Current returned fileno */
	long mta_blkno;		/* Current returned blkno */
	long mta_prevfileno;	/* Previous returned fileno */
	long mta_prevblkno;	/* Previous returned blkno */

	struct vmtape d_vmt;	/* Virtual magtape info */
} devmt;


int os_mtunload(struct devmt *d);
int os_mtrewind(struct devmt *d);
int os_mtspace(struct devmt *d, long unsigned int cnt, int revf);
int os_mtfspace(struct devmt *d, long unsigned int cnt, int revf);
int os_mtopen(struct devmt *d, char *path, int mtof);
int os_mtread(struct devmt *dp);
int os_mtwrite(struct devmt *d, unsigned char *buff, size_t len);
int os_mtclrerr(struct devmt *d);
void os_mtshow(struct devmt *d, FILE *f);
int  os_mtstate(struct devmt *d, int op);
void os_mterrchk(struct devmt *d, int op);
int  os_mtclose(struct devmt *d);
int  os_mtweof(struct devmt *d);
void os_mtflaginit(struct devmt *d);
void os_mtmoveinit(struct devmt *d);
int  osux_mtop(struct devmt *d, int op, int cnt, char *name);


void tmtoten(struct devmt *);
void tentotm(struct devmt *);

int  dptmmount(struct devmt *, char *, char *);
void dptmclear(struct devmt *);
void dptmstat(struct devmt *);

int devmount(struct devmt *, struct vmtattrs *);
int devclose(struct devmt *);
int devread(struct devmt *d);
int devwrite(struct devmt *d, unsigned char *buff, size_t len);
int devweof(struct devmt *);
int devmolwait(struct devmt *d);
int devunload(struct devmt *d);
int devrewind(struct devmt *d);
int devrspace(struct devmt *d, long unsigned int cnt, int revf);
int devfspace(struct devmt *d, long unsigned int cnt, int revf);

void sscattn(struct devmt *d);
void chkmntreq(struct devmt *d);



/* For now, include VMT source directly, so as to avoid compile-time
** switch conflicts (eg with KLH10).
*/
#define os_strerror dp_strerror
#include "vmtape.c"

#define DBGFLG (devmt.d_tm->dptm_dpc.dpc_debug)


/* Error and diagnostic output */

static const char *progname = "dptm03";


static void efatal(int num, char *fmt, ...)
{
    fprintf(stderr, "\n[%s: Fatal error: ", progname);
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fputs("]\r\n", stderr);

    /* DP automatically kills any child as well. */
    dp_exit(&devmt.d_dp, num);
}

static void esfatal(int num, char *fmt, ...)
{
    fprintf(stderr, "\n[%s: Fatal error: ", progname);
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fprintf(stderr, " - %s]\r\n", dp_strerror(errno));

    /* DP automatically kills any child as well. */
    dp_exit(&devmt.d_dp, num);
}

static void dbprint(char *fmt, ...)
{
    fprintf(stderr, "[%s: ", progname);
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fputs("]", stderr);
}

static void dbprintln(char *fmt, ...)
{
    fprintf(stderr, "[%s: ", progname);
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fputs("]\r\n", stderr);
}

static void error(char *fmt, ...)
{
    fprintf(stderr, "\n[%s: ", progname);
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fputs("]\r\n", stderr);
}

static void syserr(int num, char *fmt, ...)
{
    fprintf(stderr, "\n[%s: ", progname);
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fprintf(stderr, " - %s]\r\n", dp_strerror(num));
}

int
main(int argc, char **argv)
{
    register struct devmt *d = &devmt;

    /* General initialization */
    if (!dp_main(&d->d_dp, argc, argv)) {
	efatal(1, "DP init failed!");
    }
    d->d_tm = (struct dptm03_s *)d->d_dp.dp_adr;	/* Make refs easier */

    /* From here on can refer to shared structure */
    if (DBGFLG)
	dbprintln("Started");

    /* Find location and size of record buffer to use */
    d->d_buff = dp_xrbuff(dp_dpxto(&d->d_dp), &d->d_blen);

    /* Set up necessary event handlers for 10-DP communication.
    ** For now this is done by DPSUP.
    */

    /* Ignore TTY cruft so CTY hacking in 10 doesn't bother us */
    signal(SIGINT, SIG_IGN);	/* Ignore TTY cruft */
    signal(SIGQUIT, SIG_IGN);

    /* Open tape drive initially specified, if one; initialize stuff */
    d->d_state = DPTM03_STA_SOFTOFF;
    d->d_istape = MTYP_NONE;
    vmt_init(&d->d_vmt, "DPTM03");

    tentotm(d);		/* Start normal command/response process */
    return 1;		/* Never returns, but silence compiler */
}

/* TMTOTEN - Process to handle unexpected events and report them to the 10.
**	Does nothing for now; later could be responsible for listening
**	and responding to operations from a user tape-interface process.
*/

void tmtoten(register struct devmt *d)
{
    register struct dpx_s *dpx;
    register unsigned char *buff;
    size_t max;
#if 0
    register int cnt;
    int stoploop = 50;
#endif

    dpx = dp_dpxfr(&d->d_dp);		/* Get ptr to from-DP comm rgn */
    buff = dp_xsbuff(dpx, &max);	/* Set up buffer ptr & max count */

#if 0
    for (;;) {

	/* Make sure that buffer is free before clobbering it */
	dp_xswait(dpx);			/* Wait until buff free */

	/* OK, now do a blocking read on external command input! */
	if ((cnt = read(pffd, buff, max)) <= MINREQPKT) {

	    if (cnt < 0 && (errno == EINTR))	/* Ignore spurious signals */
		continue;

	    /* Error of some kind */
	    if (cnt < 0) {
		if (--stoploop <= 0)
		    efatal(1, "Too many retries, aborting");
		syserr(-1, "REQPKT read = %d", cnt); 
	    } else if (cnt > 0)
		dbprintln("REQPKT read = %d, no REQPKT data", cnt);
	    else
		dbprintln("REQPKT read = %d, no REQPKT", cnt);

	    continue;		/* For now... */
	}

	/* Normal packet, pass to 10 via DPC */
	/* Or simply process here, then hand up results */
	dp_xsend(dpx, DPTM_RPKT, cnt);
    }
#endif /* 0 */
}

/* Slave Status Change - Signal attention.
**    Drive came online as a result of either:
**	(1) a successful soft mount request
**	(2) a hard mount came online
*/
void sscattn(register struct devmt *d)
{
    register struct dpx_s *dpx;

    dpx = dp_dpxfr(&d->d_dp);		/* Get ptr to from-DP comm rgn */

    dp_xswait(dpx);			/* Wait until receiver ready */
    dp_xsend(dpx, DPTM_MOUNT, 0);	/* Send note to 10! */

}

void chkmntreq(register struct devmt *d)
{
    d->d_mntreq = FALSE;	/* For now */
}

/* TENTOTM - Main loop for thread handling commands from the 10.
**	Reads DPC message from 10 and interprets it, returning a
**	result code (and/or data).
*/

void tentotm(register struct devmt *d)
{
    register struct dpx_s *dpx;
    register unsigned char *buff;
    size_t max;
    register int rcnt;
    int res;
    int cmd;


    if (DBGFLG)
	dbprint("in tentotm");

    dpx = dp_dpxto(&(d->d_dp));		/* Get ptr to "To-DP" xfer stuff */
    buff = dp_xrbuff(dpx, &max);

    for (;;) {

	/* Wait until 10 has a command for us */
	while (!dp_xrtest(dpx)) {
	    if (d->d_mntreq)
		chkmntreq(d);		/* Check out possible mount req */
	    else if (d->d_state == DPTM03_STA_HARDOFF)
		devmolwait(d);		/* Hard offline, wait for change */
	    else
		dp_xrblock(dpx);	/* Block until something happens */
	}

	/* Reset some stuff for every command */
	res = DPTM_RES_SUCC;		/* Default is successful op */
	d->d_tm->dptm_col = FALSE;
	d->d_tm->dptm_err = 0;

	/* Process command from 10! */
	switch (cmd = dp_xrcmd(dpx)) {

	default:
	    error("Unknown cmd %o", dp_xrcmd(dpx));
	    res = DPTM_RES_FAIL;
	    break;

	case DPTM_RESET:	/* Reset DP */
	    /* Attempt to do complete reset */
	    dbprintln("Reset request");
#if 0
	    dptm_restart(2);
#endif
	    break;

	case DPTM_MOUNT:	/* Mount tape specified by path/arg strings */
	    if (!dptmmount(d,
		  (d->d_tm->dptm_pathx
		   ? (char *)&buff[d->d_tm->dptm_pathx] : NULL),
		  (d->d_tm->dptm_argsx
		   ? (char *)&buff[d->d_tm->dptm_argsx] : NULL))) {
		res = DPTM_RES_FAIL;
	    }
	    break;

	case DPTM_NOP:	/* No operation */
	    break;

	case DPTM_UNL:	/* Unload (implies rewind & unmount) */
	    if (!devunload(d)) {	/* Unload/Unmount tape */
		res = DPTM_RES_FAIL;
	    }
	    break;

	case DPTM_REW:	/* Rewind tape */
	    if (!devrewind(d)) {	/* Rewind tape */
		res = DPTM_RES_FAIL;
	    }
	    break;

	case DPTM_WTM:	/* Write Tape Mark (EOF) */
	    if (!devweof(d)) {		/* Write EOF */
		res = DPTM_RES_FAIL;
	    }
	    break;

	case DPTM_SPF:	/* Space N records forward */
	case DPTM_SPR:	/* Space N records reverse */
	    rcnt = dp_xrcnt(dpx);		/* Get count */
	    if (!devrspace(d,
			(unsigned long)rcnt,
			(cmd==DPTM_SPR ? 1 : 0)))
		res = DPTM_RES_FAIL;
	    break;

	case DPTM_SFF:	/* Space N files forward */
	case DPTM_SFR:	/* Space N files reverse */
	    rcnt = dp_xrcnt(dpx);		/* Get count */
	    if (!devfspace(d,
			(unsigned long)rcnt,
			(cmd==DPTM_SFR ? 1 : 0)))
		res = DPTM_RES_FAIL;
	    break;

	case DPTM_ER3:	/* Erase 3 inches */
	    /* Ignored for now, but later may be able to support */
	    break;

	case DPTM_WRT:	/* Write record of N bytes */
	    rcnt = dp_xrcnt(dpx);		/* Get length to write */
	    if (!devwrite(d, buff, rcnt)) {
		/* Handle write error of some kind */
		res = DPTM_RES_FAIL;
		d->d_tm->dptm_err = 1;
	    }
	    break;

	case DPTM_RDF:	/* Read forward up to N bytes */
	    if (DBGFLG)
		dbprintln("Read");
#if 0
	    rcnt = dpx->dpx_len;		/* Get max len can read */
#endif
	    if (!devread(d)) {
		/* Handle read error of some kind */
		/* Could be EOF, EOT, or error; let dptmstat handle it */
	    }
	    break;

	case DPTM_RDR:	/* Read reverse up to N bytes */
	    /* No Unix system provides Read-reverse to the users.  What
	    ** this code does is simulate it by backspacing over one
	    ** record and reading it in.  Note the backspace operation
	    ** succeeds even if BOT or EOF is encountered instead of
	    ** a record, so we check the frame count to make sure it
	    ** went over a record.  Operation as a whole always succeeds
	    ** unless there is some gross error.
	    ** Note special hack to return correct frame count.
	    */
	    if (DBGFLG)
		dbprintln("Read-reverse");
	    if (!devrspace(d, (long)1, 1)) {
		res = DPTM_RES_FAIL;
		d->d_tm->dptm_err = 1;
		break;
	    }
	    dptmstat(d);		/* Update vars, to check frms */
	    if (d->d_tm->dptm_frms) {
		/* Space succeeded, so read record in */
		long rrfrms;
		devread(d);		/* Read the record */
		dptmstat(d);		/* Update vars to get data frames */
		rrfrms = d->d_tm->dptm_frms;	/* Remember them */
		devrspace(d, (long)1, 1);	/* Back up 1 record again */
		dptmstat(d);		/* Update vars one last time */

		/* Ugly crock - in order to set frame count explicitly after
		   dptmstat() is done, we have to skip the normal
		   end-of-command processing (or rather, replicate it here).
		*/
		d->d_tm->dptm_frms = rrfrms;	/* Restore data frame cnt */
		dp_xrdoack(dpx, res);		/* ACK the command */
		continue;			/* and resume loop */
	    }
	    break;

	case DPTM_SNS:	/* Sense? */
	    error("Sense not supported");
	    res = DPTM_RES_FAIL;
	    break;

	}
	dptmstat(d);		/* Update most status vars */

	/* Command done, return result and tell 10 we're done */
	dp_xrdoack(dpx, res);
    }
}

int
dptmmount(register struct devmt *d,
	  char *path,
	  char *args)
{
    struct vmtattrs v;

    if (DBGFLG)
	dbprintln("Mount request \"%s\" \"%s\"",
		(path ? path : ""),
		(args ? args : ""));

    devclose(d);	/* Force unmount of any existing tape */
    dptmclear(d);
    d->d_tm->dptm_type = d->d_istape;	/* Update tape type (shd be NONE) */

    if (!path)		/* Just wanted unmount, that's all! */
	return TRUE;

    /* Set up initial path */
    if (strlen(path) >= sizeof(v.vmta_path)) {
	error("Mount path too long (%d max)", (int)sizeof(v.vmta_path));
	return FALSE;
    }
    strcpy(v.vmta_path, path);	/* Set path */
    v.vmta_mask = VMTA_PATH;	/* Initialize attribs */

    /* Parse args if any, adding to attribs */
    if (args && !vmt_attrparse(&d->d_vmt, &v, args)) {
	error("Mount failed, bad args");
	return FALSE;
    }

    /* Do it! */
    if (!devmount(d, &v))
	return FALSE;

    d->d_tm->dptm_type = d->d_istape;	/* Report back on type */

    return TRUE;
}


/* DPTMSTAT - Set shared status values
*/
void dptmstat(register struct devmt *d)
{
    register struct dptm03_s *dptm = d->d_tm;
    register struct vmtape *t = &d->d_vmt;

    switch (d->d_istape) {
    case MTYP_NONE:
	dptm->dptm_mol = FALSE;		/* Medium online */
	dptm->dptm_wrl = FALSE;		/* Write-locked */
	dptm->dptm_bot = FALSE;
	dptm->dptm_eot = FALSE;
	dptm->dptm_eof = FALSE;
	dptm->dptm_frms = 0;
	break;

    case MTYP_VIRT:
	dptm->dptm_mol = vmt_ismounted(t);	/* Medium online */
	dptm->dptm_wrl = !vmt_iswritable(t);	/* Write-locked */
	dptm->dptm_bot = vmt_isatbot(t);
	dptm->dptm_eot = vmt_isateot(t);
	dptm->dptm_eof = vmt_isateof(t);
	dptm->dptm_err = vmt_errors(t);
	dptm->dptm_frms = vmt_framecnt(t);
	break;

    default:
	if (dptm->dptm_mol = d->mta_mol) {
	    if (d->d_state == DPTM03_STA_HARDOFF) {
		if (1 /*DBGFLG*/)
		    dbprintln("Tape came online: \"%s\" %s", d->d_path,
			      dptm->dptm_wrl ? "write-locked" : "writable");
		dptm->dptm_col = TRUE;		/* Special "Came-Online" flg */
		sscattn(d);			/* Signal 10 re change */

	    }
	    d->d_state = DPTM03_STA_HARDON;
	} else {
	    if (d->d_state == DPTM03_STA_HARDON) {
		if (1 /*DBGFLG*/)
		    dbprintln("Tape went offline: \"%s\"", d->d_path);
	    }
	    d->d_state = DPTM03_STA_HARDOFF;
	}
#if 1
	/* Check for bogosity */
	if (d->mta_bot && d->mta_eof) {
	    error("dptmstat had both BOT and TM!");
	    d->mta_eof = FALSE;
	}
#endif
	dptm->dptm_wrl = d->mta_wrl;
	dptm->dptm_bot = d->mta_bot;
	dptm->dptm_eot = d->mta_eot;
	dptm->dptm_eof = d->mta_eof;
	dptm->dptm_err = d->mta_err;
	dptm->dptm_frms = d->mta_frms;
	break;
    }
}

void dptmclear(register struct devmt *d)
{
    register struct dptm03_s *dptm = d->d_tm;

    dptm->dptm_col = FALSE;
    dptm->dptm_mol = FALSE;
    dptm->dptm_wrl = FALSE;
    dptm->dptm_pip = FALSE;

    dptm->dptm_bot = FALSE;
    dptm->dptm_eot = FALSE;
    dptm->dptm_eof = FALSE;
    dptm->dptm_frms = 0;
    dptm->dptm_err = 0;
}


/* Generic device routines (null, virtual, and real)
**	Open, Close, Read, Write, Write-EOF, Write-EOT.
*/

/* MOUNT - must already have set up
**	d_buff, d_len.
**
**	Assumes devclose() has already been applied to unmount existing
**	tape if necessary.
*/
int devmount(register struct devmt *d,
	     register struct vmtattrs *ta)
{
    int typ;
    char *opath = ta->vmta_path;
    int wrtf;
    char *path = NULL;

    /* Determine basic type type here.  For now, anything that
       sets vmta_dev is assumed to be a "hard" device.
       Nothing yet depends on exactly what kind of device.
    */
    if (ta->vmta_mask & VMTA_DEV) {
	typ = MTYP_HALF;
	wrtf = VMT_MODE_UPDATE;
    } else {
	typ = MTYP_VIRT;
	wrtf = (ta->vmta_mask & VMTA_MODE)
	    ? ta->vmta_mode : VMT_MODE_RDONLY;
    }

    /* Check out path argument and copy it */
    if (typ != MTYP_NONE) {
	if (!opath || !*opath) {
	    error("Null mount path");
	    return FALSE;
	}
	path = malloc(strlen(opath)+1);
	strcpy(path, opath);
    }

    /* Open the necessary files */
    switch (typ) {

    case MTYP_NONE:
	return TRUE;

    case MTYP_VIRT:
	/* Mount & open virtual tape.
	*/
	if (!vmt_attrmount(&(d->d_vmt), ta)) {
	    error("Couldn't mount virtual tape for %s: %s",
		  ( (wrtf==VMT_MODE_RDONLY) ? "reading"
		  : (wrtf==VMT_MODE_CREATE) ? "create"
		  : (wrtf==VMT_MODE_UPDATE) ? "update" : "unknown-op"),
		  path);
	    free(path);
	    return FALSE;
	}
	/* Check buffer length */
	if (d->d_vmt.mt_tdr.tdmaxrsiz > DPTM_MAXRECSIZ) {
	    error("Tapedir contains record larger than max (%ld > max %ld)",
		  (long)d->d_vmt.mt_tdr.tdmaxrsiz, (long)DPTM_MAXRECSIZ);
	}
	d->d_state = DPTM03_STA_SOFTON;
	if (DBGFLG)
	    dbprintln("Mounted virtual %s tape \"%s\"",
		      (wrtf ? "R/W" : "RO"), path);

	break;

    default:
	/* Check out path to see if it's a real tape device or not.
	** Do this by opening in non-blocking mode, so that we don't hang up.
	** NOTE however: Even if it succeeded and the state is "online",
	** it's still closed immediately because non-blocking open also implies
	** non-blocking I/O, which is NOT what we want. 
	*/
	if (!os_mtopen(d, path, OS_MTOF_READONLY
			       |OS_MTOF_NONBLOCK|OS_MTOF_NRCHECK)) {
	    syserr(-1, "Cannot mount device \"%s\"", path);
	    free(path);
	    return FALSE;
	}

	/* Won, we think it's a tape device.
	** Close it right away and set state to re-try with a blocking open.
	*/
	os_mtclose(d);
	d->d_state = DPTM03_STA_HARDOFF;
	d->d_openretry = d->d_tm->dptm_blkopen;
	if (DBGFLG)
	    dbprintln("Mounted tape device \"%s\"", path);
	break;
    }

    /* Set up VMT vars, with optional tape-info if specified.
    ** Note no need to set up virtual tape stuff, vmt_attrmount
    ** already did that.
    */
    d->d_istape = typ;
    d->d_path = path;

    return TRUE;
}

/* Special frob only called for mounted hard-device tapes
** while waiting for them to go online.  State will be DPTM03_STA_HARDOFF.
*/
int devmolwait(struct devmt *d)
{
    int res;
    sigset_t mask, omask;
    int mtof = 0;			/* Start with R/W open */

    sigfillset(&mask);

  retry:
    if (DBGFLG)
	dbprintln("devmolwait try %ld", (long)d->d_openretry);
    sigprocmask(SIG_UNBLOCK, &mask, &omask);	/* Allow sigs to break out */
    res = os_mtopen(d, d->d_path, mtof); 	/* Do blocking open */
    sigprocmask(SIG_SETMASK, &omask, (sigset_t *)NULL);	/* Restore mask */

    if (!res) {
	/* If failed, check out error.  On AXP OSF, block times out after
	** a mere 45 seconds!
	*/
	if ((d->mta_err == EIO)		/* OSF/1 timed out? */
	    || (d->mta_err == 0)) {	/* or generic EINTR or EWOULDBLOCK? */
	  recheck:
	    if (--(d->d_openretry) > 0)	/* yeah, bump retry count */
		return FALSE;		/* Still OK to stay blocked */

	    /* Retry count expired; attempt a non-blocking open to verify
	    ** that drive still exists and is powered on.
	    */
	    if (!os_mtopen(d, d->d_path, OS_MTOF_READONLY|OS_MTOF_NONBLOCK)) {
		syserr(-1, "Device \"%s\" no longer openable", d->d_path);
		devclose(d);
		return FALSE;
	    }
	    /* Still there, so (after closing FD) reset blocking opens */
	    os_mtclose(d);
	    d->d_openretry = d->d_tm->dptm_blkopen;
	    if (DBGFLG)
		dbprintln("Device \"%s\" still openable", d->d_path);
	    return FALSE;

	} else if ((mtof == 0) &&
		   ((d->mta_err == EACCES)
		    || (d->mta_err == EROFS))) {
	    /* Attempting R/W open and got an error that indicates
	       tape is write-locked.
	       OSF/1 gives "Permission denied" - see Note of 3/10/97
	       re hideous lossage by OSF/1 V4.0 reel magtape driver.
	       Linux st.c (v.20010812) gives "Read-only file system".
	     */
	    mtof = OS_MTOF_READONLY;		/* Attempt R/O open instead */
	    d->mta_err = 0;
	    if (DBGFLG)
		dbprintln("Device \"%s\" R/W access denied; trying R/O",
			  d->d_path);
	    goto retry;
	}

	/* Some other kind of fatal error,
	** give up and forget about this device; "unmount" it.
	*/
	syserr(-1, "Device \"%s\" cannot be opened", d->d_path);
	devclose(d);
	return FALSE;		/* Do something more drastic? */
    }

    /* Opened successfully -- but must check to make sure there
       really is something in the drive.  os_mtopen has already set state.
    */
    if (!d->mta_mol) {
	/* No medium on-line???  Foo.
	   This shouldn't happen on OSF/1 which blocks until MOL, but
	   *does* happen on Linux.
	   On Linux it is useless to recheck state since the MOL bit
	   will never change regardless of actual tape status (and
	   regardless of executing MTNOPs), so we have to re-try a
	   full open later, after sleeping a bit to give other
	   stuff a chance to run.
	 */
	if (DBGFLG)
	    dbprintln("Device \"%s\" opened but no MOL, sleeping", d->d_path);
	os_mtclose(d);
	sleep(10);		/* 10 sec for now, later make param */
	return FALSE;		/* Give caller a chance to check stuff */
    }

    /* Opened successfully!  Do final consistency check to avoid
	possible screw case where we blocked attempting to open it read-only
	and by the time it succeeded the tape was actually writable.  If
	this happens, try again using read/write mode.
	This has the potential to be an infinite loop if the O/S is really
	screwing us over, so at least give caller a chance to do other
	stuff by going to "recheck" instead of "retry".
    */
    if (mtof && !(d->mta_wrl)) {
	/* Opened read-only, but drive isn't write-locked after all!! */
	if (DBGFLG)
	    dbprintln("Device \"%s\" opened RO but not write-locked, retrying",
		      d->d_path);
	os_mtclose(d);
	goto recheck;
    }

    /* OK, finally opened completely successfully */
    dptmstat(d);			/* Won, set external state! */

    if (DBGFLG)
	dbprintln("Device \"%s\" opened for %s",
		  d->d_path, (mtof ? "R/O" : "R/W"));
    return res;
}



int devclose(struct devmt *d)
{
    int res;

    if (DBGFLG && d->d_path)
	dbprintln("Closing \"%s\"", d->d_path);

    switch (d->d_istape) {
    case MTYP_NONE:
	res = TRUE;
	break;
    case MTYP_VIRT:
	res = vmt_unmount(&d->d_vmt);	/* Close virtual tape */
	break;
    default:
	res = os_mtclose(d);		/* Close real tape */
	break;
    }

    /* Force us to forget about it even if above stuff failed */
    d->d_state = DPTM03_STA_SOFTOFF;
    d->d_istape = MTYP_NONE;
    if (d->d_path) {
	free(d->d_path);
	d->d_path = NULL;
    }
    return res;
}


int devunload(struct devmt *d)
{
    if (DBGFLG)
	dbprintln("Unload");

    switch (d->d_istape) {

    case MTYP_NONE:
	d->d_vmt.mt_err = TRUE;		/* Error, can't do anything */
	return FALSE;

    case MTYP_VIRT:
	/* Virtual tape, unmount it (updates vmt params itself) */
	return vmt_unmount(&(d->d_vmt));

    default:
	/* Real tape, attempt physical unload */
	if (!os_mtunload(d)) {
	    syserr(-1, "Unload error on %s", d->d_path);
	    /* Ignore failure, assume tape no longer there */
	}
	os_mtclose(d);		/* Close it, take offline */

	if (DBGFLG)
	    dbprintln("Tape forced offline: \"%s\"", d->d_path);
	return TRUE;
    }
}

/* Read one data unit (record, tapemark, etc) from device
**	Returns 1 if read something
**	Returns 0 if read nothing, but no error
**	Returns -1 if error of some kind
** For now, note assumes d_buff and d_blen are args.
*/

int devread(struct devmt *d)
{
    switch (d->d_istape) {

    case MTYP_NONE:
	d->d_vmt.mt_frames = 0;		/* No data read */
	d->d_vmt.mt_err = TRUE;		/* Error, can't do anything */
	return FALSE;

    case MTYP_VIRT:
	/* Virtual tape, read in (updates vmt params itself) */
	return vmt_rget(&(d->d_vmt), d->d_buff, d->d_blen);

    default:
	/* Real tape, attempt physical read */
	if (!os_mtread(d)) {		/* Attempt read from device */
	    syserr(-1, "Read error on %s", d->d_path);
	    return FALSE;
	}
	return TRUE;
    }
}

/* Write one record to device.
*/
int devwrite(register struct devmt *d, unsigned char *buff, size_t len)
{
    if (DBGFLG)
	dbprintln("Write %ld", (long)len);

    switch (d->d_istape) {

    case MTYP_NONE:
	d->d_vmt.mt_frames = 0;		/* No data written */
	d->d_vmt.mt_err = TRUE;		/* Error, can't do anything */
	return FALSE;

    case MTYP_VIRT:
	/* Virtual tape, write out (updates vmt params itself) */
	return vmt_rput(&(d->d_vmt), buff, len);

    default:
	/* Real tape, attempt physical write */
	if (!os_mtwrite(d, buff, len)) {
	    syserr(-1, "Write error on %s", d->d_path);
	    return FALSE;
	}
	return TRUE;
    }
}

int devweof(register struct devmt *d)
{
    switch (d->d_istape) {

    case MTYP_NONE:
	d->d_vmt.mt_err = TRUE;		/* Error, can't do anything */
	return FALSE;

    case MTYP_VIRT:
	/* Virtual tape, write EOF (updates vmt params itself) */
	return vmt_eof(&(d->d_vmt));

    default:
	/* Real tape, attempt physical tapemark write */
	if (!os_mtweof(d)) {
	    syserr(-1, "TM write error on %s", d->d_path);
	    return FALSE;
	}
	return TRUE;
    }
}

int devrewind(register struct devmt *d)
{
    if (DBGFLG)
	dbprintln("Rewind");

    switch (d->d_istape) {

    case MTYP_NONE:
	d->d_vmt.mt_err = TRUE;		/* Error, can't do anything */
	return FALSE;

    case MTYP_VIRT:
	/* Virtual tape, rewind (updates vmt params itself) */
	return vmt_rewind(&(d->d_vmt));

    default:
	/* Real tape, attempt physical rewind */
	if (!os_mtrewind(d)) {
	    syserr(-1, "Rewind error on %s", d->d_path);
	    return FALSE;
	}
	return TRUE;
    }
}

int devrspace(register struct devmt *d, long unsigned int cnt, int revf)
{
    unsigned long res;

    if (DBGFLG)
	dbprintln("RecSpace %ld%s", cnt, (revf ? " reverse" : ""));

    switch (d->d_istape) {

    case MTYP_NONE:
	d->d_vmt.mt_err = TRUE;		/* Error, can't do anything */
	return FALSE;

    case MTYP_VIRT:
	/* Virtual tape, returns in mt_frames the # records spaced back */
	if (!vmt_rspace(&(d->d_vmt), revf, cnt)) {
	    d->d_vmt.mt_err = TRUE;
	    return FALSE;
	}
	return TRUE;

    default:
	/* Real tape, attempt physical reverse space */
	if (!os_mtspace(d, cnt, revf)) {
	    syserr(-1, "Rspace error on %s", d->d_path);
	    return FALSE;
	}
	return TRUE;
    }
}

int devfspace(register struct devmt *d, long unsigned int cnt, int revf)
{
    unsigned long res;

    if (DBGFLG)
	dbprintln("FileSpace %ld%s", cnt, (revf ? " reverse" : ""));

    switch (d->d_istape) {

    case MTYP_NONE:
	d->d_vmt.mt_err = TRUE;		/* Error, can't do anything */
	return FALSE;

    case MTYP_VIRT:
	/* Virtual tape, space forward (updates mt_frames params itself) */
	if (!(res = vmt_fspace(&(d->d_vmt), revf, cnt))) {
	    d->d_vmt.mt_err = TRUE;
	}
	return res;

    default:
	/* Real tape, attempt physical space */
	if (!os_mtfspace(d, cnt, revf)) {
	    syserr(-1, "Fspace error on %s", d->d_path);
	    return FALSE;
	}
	return TRUE;
    }
}


static void
devstatus(register struct devmt *d, char *label)
{
    char errstr[40];

    if (d->mta_err)
	sprintf(errstr, " ERR=%d", d->mta_err);
    else errstr[0] = '\0';
    dbprintln("%s = %s%s%s%s%s%s%s f=%ld b=%ld",
	      label,
	      d->mta_eof ? " EOF" : "",
	      d->mta_bot ? " BOT" : "",
	      d->mta_eot ? " EOT" : "",
	      d->mta_mol ? " MOL" : "",
	      d->mta_wrl ? " WRL" : "",
	      d->mta_rew ? " REW" : "",
	      errstr,
	      d->mta_fileno, d->mta_blkno);
}

/* OS Hardware Magtape handling routines
*/

/* Note here whether OS is one that provides good status info or
 * not.  Unfortunately most don't.  This flag helps avoid constant
 * complaints about bad state by suppressing them unless OS really
 * is expected to have done the right thing.
 */
#if CENV_SYS_DECOSF
# define OS_GOODMTSTATE 1
#else
# define OS_GOODMTSTATE 0
#endif

/* Initialize flags
*/
void
os_mtflaginit(register struct devmt *d)
{
    os_mtmoveinit(d);
    d->mta_mol = d->mta_wrl = 0;
    d->mta_erreg = 0;
    d->mta_fileno = d->mta_prevfileno = 0;
    d->mta_blkno  = d->mta_prevblkno  = 0;
}

/* Initialize flags for tape movement
*/
void
os_mtmoveinit(register struct devmt *d)
{
    d->mta_bot = d->mta_eot = d->mta_eof = 0;
    d->mta_rew = 0;
    d->mta_err = 0;
    d->mta_frms = 0;
}

#if CENV_SYS_UNIX
/* General-purpose ioctl operation invocation
*/
int
osux_mtop(register struct devmt *d, int op, int cnt, char *name)
{
    struct mtop mtcmd;

    if (!name) name = "unknown";
    mtcmd.mt_op = op;
    mtcmd.mt_count = cnt;
    if (DBGFLG)
	dbprintln("ioctl(%s, %ld)", name, (long)cnt);
    if (ioctl(d->d_fd, MTIOCTOP, &mtcmd) < 0) {
	d->mta_err = errno;
	if (DBGFLG)
	    syserr(-1, "%s ioctl failed", name);
	return FALSE;
    }
    return TRUE;
}

/* Get physical magtape state.
 * Unfortunately there is no general way to do this.
 *
 * The "op" arg is positive if called after a successful operation,
 * negative if it failed.  This serves as a hint to help guess what the
 * correct status likely is, if the OS doesn't provide enough information.
 */
int
os_mtstate(register struct devmt *d,
	   int op)		/* Hint if needed; a DPTM_xxx op for now */
{
    int res = FALSE;

#if CENV_SYS_DECOSF
    /* XXX: should upgrade to MTIOCGET now that it returns same info */

    struct devget dg;	/* Mostly use stat and category_stat */
    struct mtget mt;

    if (ioctl(d->d_fd, DEVIOCGET, &dg) < 0) {
	syserr(-1, "os_mtstate DEVIOCGET failed");
	return FALSE;
    }
    if (ioctl(d->d_fd, MTIOCGET, &mt) < 0) {
	syserr(-1, "os_mtstate MTIOCGET failed");
	return FALSE;
    }

    /* Supposed to be the same, but check anyway */
    if (dg.stat != mt.mt_dsreg) {
	error("Tru64 DEVIOCGET stat 0x%lx != 0x%lx MTIOCGET mt_dsreg",
	      (long)dg.stat, (long)mt.mt_dsreg);
    }

    d->mta_eof = ((dg.category_stat & DEV_TPMARK) != 0);
    d->mta_rew = ((dg.category_stat & DEV_RWDING) != 0);

    d->mta_bot = ((dg.stat & DEV_BOM) != 0);
    d->mta_eot = ((dg.stat & DEV_EOM) != 0);
    d->mta_mol = ((dg.stat & DEV_OFFLINE) == 0);	/* Invert sense */
    d->mta_wrl = ((dg.stat & DEV_WRTLCK) != 0);
    /*
    d->mta_err = ((dg.stat & (DEV_SOFTERR|DEV_HARDERR)) != 0);
    */
    d->mta_prevfileno = d->mta_fileno;
    d->mta_prevblkno = d->mta_blkno;
    d->mta_fileno = mt.mt_fileno;
    d->mta_blkno = mt.mt_blkno;

    res = TRUE; 

#elif CENV_SYS_LINUX
    struct mtget mt;	/* Mostly use mt_gstat */

    if (ioctl(d->d_fd, MTIOCGET, &mt) < 0) {
	syserr(-1, "os_mtstate MTIOCGET failed");
	return FALSE;
    }
    d->mta_rew = 0;
# if 0
    d->mta_err = mt.mt_erreg;	/* Only has soft errors; we want hard ones */
# endif

    d->mta_eof = (GMT_EOF(mt.mt_gstat) != 0);
    d->mta_bot = (GMT_BOT(mt.mt_gstat) != 0);
    d->mta_eot = (GMT_EOT(mt.mt_gstat) != 0);
    d->mta_mol = (GMT_ONLINE(mt.mt_gstat) != 0);
    d->mta_wrl = (GMT_WR_PROT(mt.mt_gstat) != 0);

    if (GMT_DR_OPEN(mt.mt_gstat) && d->mta_mol) {
	error("mt_gstat: 0x%lx both ONLINE and DR_OPEN set!",
	      (long)mt.mt_gstat);
    }

    d->mta_prevfileno = d->mta_fileno;
    d->mta_prevblkno = d->mta_blkno;
    d->mta_fileno = mt.mt_fileno;
    d->mta_blkno = mt.mt_blkno;

    res = TRUE; 

#elif CENV_SYS_SUN || CENV_SYS_SOLARIS || CENV_SYS_FREEBSD
    struct mtget mt;

    /* Status is pretty useless on these systems.
     * In fact it's so useless, perhaps we are better off returning FALSE.
     */
    if (ioctl(d->d_fd, MTIOCGET, (char *)&mt) < 0) {
	syserr(-1, "os_mtstate MTIOCGET failed");
	return FALSE;
    }
    /* Do the pitiful best we can */

    d->mta_prevfileno = d->mta_fileno;
    d->mta_prevblkno = d->mta_blkno;
    d->mta_fileno = mt.mt_fileno;
    d->mta_blkno = mt.mt_blkno;

#if 0
    d->mta_err = (mt.mt_erreg != 0);
#endif
    d->mta_bot = ((mt.mt_fileno == 0) && (mt.mt_blkno == 0));
    d->mta_eof = ((mt.mt_fileno != 0) && (mt.mt_blkno == 0));

#if 0
    d->mta_rew = 0;	/* Pretend never rewinding, barf */
    d->mta_eot = 0;	/* Pretend never at EOT, barf */
    d->mta_mol = 1;	/* Pretend always online, barf */
    d->mta_wrl = 0;	/* Pretend never write-locked, barf */
#endif
    res = FALSE;

#else
    if (DBGFLG)
	dbprint("os_mtstate has no OS state");
    res = FALSE;
#endif

    /* Do heuristics if necessary; make best guess based on op that
       was just completed.
    */
    if (!res) switch (op) {
    case DPTM_MOUNT:		/* R/W open won */
    case DPTM_ROMNT:		/* R/O open won */
	d->mta_mol = TRUE;
	d->mta_bot = TRUE;
	d->mta_wrl = (op == DPTM_ROMNT);
	break;
    case DPTM_RDF:		/* Read forward won but returned 0 */
	d->mta_eof = TRUE;	/* Best guess */
	break;
    case -DPTM_RDF:		/* Read forward failed, ENOSPC */
    case -DPTM_WRT:		/* Write failed, ENOSPC */
	d->mta_eot = TRUE;	/* Best guess */
	break;
    case DPTM_REW:		/* Rewind won */
	d->mta_rew = FALSE;	/* Best guess */
	d->mta_bot = TRUE;
	break;
    case -DPTM_SPF:		/* Space Forward failed */
    case -DPTM_SPR:		/* Space Reverse failed */
	/* Can't distinguish real error from hitting tapemark, so
	** just assume tapemark (or BOT/EOT)
	*/
	if (!d->mta_bot && !d->mta_eot)	/* Unless OS knows we hit BOT/EOT */
	    d->mta_eof = TRUE;		/* Best guess */
	break;

    case DPTM_SFF:		/* Space File Forward */
    case DPTM_SFR:		/* Space File Reverse */
	d->mta_eof = TRUE;	/* Best guess */
	break;

    case -DPTM_SFF:		/* Space File Forward failed */
	/* Can't distinguish error from hitting EOT; assume latter. */
	d->mta_eot = TRUE;
	break;

    case -DPTM_SFR:		/* Space File Reverse failed */
	/* Can't distinguish error from hitting BOT; assume latter. */
	d->mta_bot = TRUE;
	break;

    }

    if (DBGFLG) {
	devstatus(d, (res ? "os_mtstate TRUE" : "os_mtstate FALSE"));
    }
    return res;
}
#endif /* CENV_SYS_UNIX */

/* Called when an unexpected error is hit -- attempts to determine
 * whether the tape is still there or if it's a real data error.
 *
 * Unfortunately this process is atrociously OS-dependent.
 *
 * Called in these situations:
 *	read failed, and wasn't ENOSPC.
 *		Very unusual -- may be data error, or drive offline.
 *		Unfortunately no way to determine if it's latter,
 *		except possibly by doing special re-open and re-check.
 *	Write failed, and wasn't ENOSPC.
 *		Also very unusual; attempt re-open.  
 *	Tapemark write failed
 *		Also very unusual, tho allow for ENOSPC?  Attempt re-open.
 *	Rewind failed
 *		Should not happen, almost certainly tape is gone.
 *	file spacing failed (EIO)
 *		This *should* mean hit BOT or EOT.  If neither is set,
 *		drive may have gone offline.
 *
 *	record spacing failed (EIO)
 *		This is typical for hitting a tapemark, so this should
 *		be assumed.  mt_fileno should have changed appropriately.
 *		May also have hit BOT or EOT; check mt_fileno.  If
 *		doesn't change going backward (and no BOT), or no change
 *		going backward (and no EOT), drive may have gone offline.
 *
 * When in any doubt, for nearly all operations it is OK to simply fail;
 * the resulting TM03 device error will give pause to the PDP-10 OS.
 * The exceptions are for file and record spacing operations, where
 * failure can be normal.  For these two:
 *
 *	File spacing: OK to return success as long as one of BOT or EOT
 *	is set -- this is easy to do and guarantees that the PDP-10 OS will
 *	not get stuck in a loop.  So, this is what os_mtfspace does
 *	without calling the heavy-duty os_mterrchk.
 *
 *	Record spacing: this is the hard one.  If we are always failing due
 *	to something like tape going offline, without sufficient error status
 *	to detect this, then we never see BOT or EOT and simply assuming
 *	EOF can allow the PDP-10 OS to loop infinitely.  Somehow we have
 *	to ensure that this case forces either a tape offline or BOT/EOT
 *	condition.
 *	Solution for now: see whether mt_fileno and mt_blkno have changed
 *	as a result of the call.  If not, then force either BOT/EOT
 *	depending on direction, and complain loudly.
 */
void
os_mterrchk(register struct devmt *d,
	   int op)		/* Hint if needed; a DPTM_xxx op for now */
{
    int saverr = d->mta_err;

    if (DBGFLG)
	dbprintln("Checking out error for possible offline transition");

    os_mtshow(d, stderr);	/* First show full status */

#if DPTM03_LINUX_HACK
    /* For Linux in particular this is hideous; the only way to tell if the
     * tape is still there is to re-open it!  But this would require
     * always using the no-rewind device, otherwise this has the
     * horrible side effect of forcing a rewind on a perfectly good tape.
     * Can check at open time -- bit 0x80 (128) of the device minor #
     * is set if it's a no-rewind device.
     */
    if (d->d_isnorewind) {
	int wrl = d->mta_wrl;

	if (DBGFLG)
	    dbprintln("Attempting horrible Linux recovery");
	(void) os_mtclose(d);
	if (!os_mtopen(d, d->d_path, (wrl ? OS_MTOF_READONLY : 0))) {
	    syserr(errno, "Cannot re-open %s", d->d_path);
	    devclose(d);
	    return;
	}
	/* OK, should have good state bits now */
    }
#else
    /* Error may indicate tape is no longer present or drive is gone.
       Try to distinguish this error from a real data I/O error
       by performing a NOP.
    */
    if (!osux_mtop(d, MTNOP, 1, "MTNOP")) {
	saverr = d->mta_err;
	error("Assuming tape gone, closing drive");
	(void) os_mtclose(d);	/* This clears state flags including MOL */
	d->mta_err = saverr;
	return;
    }
    /* Drive appears to still be OK */
    if (DBGFLG)
	dbprintln("Checkout NOP succeeded");
#endif

    (void) os_mtstate(d, op);	/* Attempt to pass on OS state */
    d->mta_err = saverr;	/* Preserving original error */
}

int
os_mtopen(struct devmt *d, char *path, int mtof)
{

    os_mtflaginit(d);	/* Clear all state flags */

#if CENV_SYS_UNIX
  {
    int fd;
    int mode = ((mtof & OS_MTOF_READONLY) ? O_RDONLY   : O_RDWR)
	     | ((mtof & OS_MTOF_NONBLOCK) ? O_NONBLOCK : 0);
    int res;

    if (DBGFLG)
	dbprintln("OS opening %s, mode %#o", path, mode); 
    d->d_fd = fd = open(path, mode, 0600);
    if (fd < 0) {
	if (DBGFLG)
	    syserr(errno, "OS open failed"); 

	/* Check out failures.  Only two are allowed (ie do not set
	   mta_err)
	*/
	switch (errno) {
	    case EWOULDBLOCK:
	    case EINTR:		/* EINTR is specifically OK */
		break;
	    default:
		d->mta_err = errno;
		break;
	}
	return FALSE;
    }
    if (DBGFLG) {
	dbprintln("OS open won, fd %d", fd); 
    }

    /* Find current state if possible */
    res = os_mtstate(d, (mtof & OS_MTOF_READONLY) ? DPTM_ROMNT : DPTM_MOUNT);
    if (mtof & OS_MTOF_NRCHECK) {
	/* This is the only place where this var is reset */
	d->d_isnorewind = FALSE;
	if (!res) {
	    error("WARNING - %s may not be a tape device.\n", path);
	} else {
#if DPTM03_LINUX_HACK
	    /* On Linux we want to re-open the tape whenever we hit an
	       error as that's the only way to determine if the tape is
	       still present, so we really want the no-rewind device!
	    */
	    struct stat st;
	    if (fstat(fd, &st) != 0) {
		syserr(errno, "OS fstat failed"); 
		return FALSE;
	    }
	    if (st.st_rdev & 0x80)	/* Minor dev # has +128 for NR */
		d->d_isnorewind = TRUE;
	    if (!d->d_isnorewind) {
		error("WARNING - %s is not a no-rewind tape!", path);
	    }
#endif
	}
    }
  }
    return TRUE;
#else
    return FALSE;
#endif
}

int
os_mtclose(struct devmt *d)
{
    int res;

    os_mtflaginit(d);	/* Clear state flags, including MOL! */

#if CENV_SYS_UNIX
    res = close(d->d_fd);
    d->d_fd = -1;
    return (res == 0);
#else
    return TRUE;
#endif
}

int
os_mtread(register struct devmt *d)
{
#if CENV_SYS_UNIX
    /* Don't try to support retries here.  If OS doesn't do it, there isn't
    ** a whole lot we can do better.  (But then again, it's Un*x, so...)
    */
    unsigned int res;

    os_mtmoveinit(d);	/* Clear movement flags */

    for (;;)
    {
	switch (res = read(d->d_fd, d->d_buff, d->d_blen)) {
	case 0:				/* Tapemark */
	    (void) os_mtstate(d, DPTM_RDF);
	    if (!d->mta_eof && !d->mta_eot) {	/* Sanity check */
		if (DBGFLG || OS_GOODMTSTATE)
		    error("Read 0 but EOF/EOT not set");
		d->mta_eof = TRUE;
	    }
	    return TRUE;

	default:			/* Assume record read and all's well */
	    d->mta_frms = res;
	    return TRUE;

	case -1:			/* Error */
	    if (errno == EINTR)
		continue;
	    if (errno == ENOSPC) {	/* OSF/1 returns this for EOT */
		(void) os_mtstate(d, -DPTM_RDF);	/* Set fail state */
		if (!d->mta_eot) {		/* Sanity check */
		    if (DBGFLG || OS_GOODMTSTATE)
			error("Read ENOSPC but EOT not set");
		    d->mta_eot = TRUE;
		}
		return TRUE;
	    }
	    d->mta_err = errno;		/* Always indicate error */
	    syserr(-1, "Tape read error");
	    os_mterrchk(d, -DPTM_RDF);	/* Check out error more carefully */
	    return FALSE;
	}
    }
#else	/* CENV_SYS_UNIX */
    return FALSE;
#endif
}

int
os_mtwrite(register struct devmt *d,
	   register unsigned char *buff,
	   size_t len)
{
#if CENV_SYS_UNIX

    unsigned int ret;

    os_mtmoveinit(d);	/* Clear movement flags */

    for (;;)
    {
	if ((ret = write(d->d_fd, buff, len)) == len) {
	    d->mta_frms = ret;		/* Won!  Assume all's well */
	    return TRUE;
	}

	/* Error of some kind */
	if (ret == -1) {
	    int saverr = errno;

	    if (errno == EINTR)
		continue;
	    syserr(errno, "Write error");
	    if (saverr == ENOSPC) {	/* OSF/1, Linux return this for EOT */
		(void) os_mtstate(d, -DPTM_WRT);	/* Set fail state */
		if (!d->mta_eot) {		/* Sanity check */
		    if (DBGFLG || OS_GOODMTSTATE)
			error("Write ENOSPC but EOT not set");
		    d->mta_eot = TRUE;
		}
		return FALSE;
	    }

	    d->mta_err = saverr;	/* Always indicate error */
	    os_mterrchk(d, -DPTM_WRT);	/* Check out error carefully */
	    return FALSE;
	}

	error("Write trunc: %ld => %d", (long)len, ret);
	(void) os_mtstate(d, DPTM_WRT);	/* Try to get general OS status */
	d->mta_frms = ret;		/* Return # frames written */
	return TRUE;	/* Some kind of error, but let caller figure out */
    }
#else	/* CENV_SYS_UNIX */
    return FALSE;
#endif
}

int
os_mtweof(struct devmt *d)		/* Write a tapemark */
{
    os_mtmoveinit(d);	/* Clear movement flags */

#if CENV_SYS_UNIX
    if (!osux_mtop(d, MTWEOF, 1, "MTWEOF")) {
	syserr(-1, "Tapemark write error");
	os_mterrchk(d, -DPTM_WTM);	 /* Try to recover state from OS */
	return FALSE;
    }
    /* Assume all's well, change status ourselves.
     * Although the tech manual is ambiguous, it appears from the T10
     * driver that the real TM03 set "tapemark detected" when writing
     * a tapemark, so we do the same.
     */
    d->mta_eof = TRUE;
    d->mta_fileno += 1;
    d->mta_blkno = 0;
    return TRUE;
#else
    return FALSE;
#endif
}

int
os_mtunload(struct devmt *d)		/* Try to unload tape */
{
    d->mta_mol = FALSE;		/* Say no longer online */
    os_mtmoveinit(d);		/* Clear movement flags */
#if CENV_SYS_UNIX
    lseek(d->d_fd, (long)0, 0);		/* Barf - See DECOSF "man mtio" */

    /* Do MTUNLOAD.  Known to be defined by DECOSF and LINUX; other
       systems must use MTOFFL as closest equivalent.
    */
# ifndef MTUNLOAD
#  define MTUNLOAD MTOFFL	/* For SUN/SOLARIS/XBSD, closest equiv. */
# endif
    return osux_mtop(d, MTUNLOAD, 1, "MTUNLOAD");
#else
    return FALSE;
#endif
}

/* Rewind.
 * Assumption is that the OS will not return from the ioctl until
 * the rewind is complete, which means this can block for a potentially
 * long time.
 */
int
os_mtrewind(struct devmt *d)		/* Rewind tape */
{
    os_mtmoveinit(d);		/* Clear movement flags */
    d->mta_rew = TRUE;		/* Now rewinding */

#if CENV_SYS_UNIX
    lseek(d->d_fd, (long)0, 0);		/* Barf - See DECOSF "man mtio" */
    if (!osux_mtop(d, MTREW, 1, "MTREW")) {
	syserr(-1, "Tape rewind error");
	os_mterrchk(d, -DPTM_REW);	 /* Try to recover state from OS */
	return FALSE;
    }
    d->mta_rew = FALSE;
    (void) os_mtstate(d, DPTM_REW);	/* Won, get general OS status */
    if (!d->mta_bot) {			/* Sanity check */
	if (DBGFLG || OS_GOODMTSTATE)
	    error("Rewind done but BOT not set");
	d->mta_bot = TRUE;
    }
    return TRUE;
#else
    return FALSE;
#endif
}


/* Space Record
 *	The Linux "st" driver returns EIO if it hits a tapemark
 * before spacing over all N records.  Also, if this happens while
 * spacing backward, its blkno position is set -1 (indeterminate).
 *	The FreeBSD "sa" driver may or may not do the same thing.
 *
 * In general one cannot depend on the residual count being available from
 * the OS; mt_resid may or may not be supported.
 * mt_fileno and mt_blkno appear to usually be present, but upon
 * hitting a tapemark mt_blkno is reset so it's impossible to tell from
 * its value how many records were actually spaced prior to the TM.
 *
 * Fortunately no PDP-10 OS depends on the exact residual value.  It
 * is sufficient to set mta_frms to the exact count if everything succeeded
 * (thus returning 0 in the TM03 FC reg) and some lesser value if not.
 *
 * Thus we only need to do the first space specially, in order to
 * distinguish the none-skipped case from the at-least-one-skipped case.
 * Otherwise, we'd have to always do record-by-record spacing.
 *
 * NOTE: The TM03 does NOT change the frame count when it hits a tapemark.
 */
int
os_mtspace(struct devmt *d, long unsigned int cnt, int revf)
{
#if CENV_SYS_UNIX
    int op = (revf ? MTBSR : MTFSR);
    int dir = (revf ? -1 : 1);
    int prevfileno;
    long unsigned n;

    /* Find current position prior to space attempt */
    (void) os_mtstate(d, 0);
    prevfileno = d->mta_fileno;

    os_mtmoveinit(d);		/* Clear movement flags */

    /* First pass do 1 record, next pass do all the rest.
     * (If want to revert to one-at-a-time accuracy, take out "n = cnt")
     */
    for (n = 1; cnt; cnt -= n, n = cnt) {
	if (!osux_mtop(d, op, n, (revf ? "MTBSR" : "MTFSR")))
	    break;
	d->mta_frms += n;	/* Success! */
	if (d->mta_blkno >= 0)
	    d->mta_blkno += (dir * n);
    }

    /* Analyze results */
    if (cnt) {
	/* Ugh, early termination.  Find out if tape still there. */
#if 0
	os_mterrchk(d, (revf ? -DPTM_SPR : -DPTM_SPF));
#else
	/* Don't do heavy check for now, see comments in os_mterrchk() */
	(void) os_mtstate(d, (revf ? -DPTM_SPR : -DPTM_SPF));
#endif
	if (!d->mta_mol)
	    return FALSE;

	/* If tape still there, must have hit either EOF or BOT/EOT.
	 * We can generally detect BOT/EOT but EOF is problematical,
	 * so if neither BOT/EOT then assume EOF, unless there seems
	 * reason to believe tape went offline (see comments in
	 * os_mterrchk for more).
	 */
	if (!d->mta_bot && !d->mta_eot && !d->mta_eof) {
	    if (DBGFLG || OS_GOODMTSTATE)
		error("Spacing %s %ld failed with no BOT/EOT/EOF!",
		      (revf ? "backward" : "forward"), (long)cnt);
	    /* If we moved at all, or detect some change in mt_fileno
	       or mt_blkno, assume we hit EOF.  Otherwise, say BOT/EOT
	       in order to avoid infinite PDP-10 OS loops.
	    */
	    if (d->mta_frms || (d->mta_fileno != prevfileno)
		|| (d->mta_blkno != d->mta_prevblkno)) {
		if (DBGFLG || OS_GOODMTSTATE) {
		    dbprintln(
		     "Assuming EOF: frms %ld, fileno %ld->%ld, blkno %ld->ld",
		     d->mta_frms, prevfileno, d->mta_fileno,
		     d->mta_prevblkno, d->mta_blkno);
		}
		d->mta_eof = TRUE;
	    } else {
		if (DBGFLG || OS_GOODMTSTATE) {
		    error(
		      "Assuming %s: frms %ld, fileno %ld->%ld, blkno %ld->%ld",
		      (revf ? "BOT" : "EOT"),
		      d->mta_frms, prevfileno, d->mta_fileno,
		      d->mta_prevblkno, d->mta_blkno);
		}
		if (revf)
		    d->mta_bot = TRUE;
		else
		    d->mta_eot = TRUE;
	    }
	}
    }
    /* If succeeded, assume no need to fetch new state */
    if (DBGFLG) {
	dbprintln("RecSpaced %s: %ld",
		  (revf ? "backward" : "forward"), d->mta_frms);
	devstatus(d, "os_mtspace");
    }
    return TRUE;

#else
    return FALSE;
#endif
}

/* Space File
 *	The Linux "st" driver returns EIO if it hits BOT/EOT
 * before spacing over all N tapemarks, but its mt_fileno and mt_blkno
 * position should be accurate regardless of success/failure.
 *
 * Similar residual count considerations as for os_mtspace().
 *
 * Note that the TM03 has no file-space command; the only time
 * this code is used is when dvtm03 substitutes a "fspace 1" for
 * a max record space command.  Thus this is never actually invoked
 * with a count other than 1.
 *
 * But this will change if the TM78 is ever supported!
 */
int
os_mtfspace(struct devmt *d, long unsigned int cnt, int revf)
{
#if CENV_SYS_UNIX
    int op = (revf ? MTBSF : MTFSF);
    int dir = (revf ? -1 : 1);
    int prevfileno;
    long unsigned n;

    /* Find current position prior to space attempt */
    (void) os_mtstate(d, 0);
    prevfileno = d->mta_fileno;

    os_mtmoveinit(d);		/* Clear movement flags */

    /* First pass do 1 file, next pass do all the rest.
     * (If want to revert to one-at-a-time accuracy, take out "n = cnt")
     */
    for (n = 1; cnt; cnt -= n, n = cnt) {
	if (!osux_mtop(d, op, n, (revf ? "MTBSF" : "MTFSF")))
	    break;
	d->mta_frms += n;	/* Success! */
	if (d->mta_fileno >= 0)
	    d->mta_fileno += (dir * n);
	d->mta_blkno = (revf ? -1 : 0);
    }

    /* Analyze results */
    if (cnt) {
	/* Ugh, early termination.  Find out if tape still there. */
#if 0
	os_mterrchk(d, (revf ? -DPTM_SFR : -DPTM_SFF));
#else
	/* Don't do heavy check for now, see comments in os_mterrchk() */
	(void) os_mtstate(d, (revf ? -DPTM_SFR : -DPTM_SFF));
#endif
	if (!d->mta_mol)
	    return FALSE;

	/* Verify that we are either at BOT or EOT */
	if (!d->mta_bot && !d->mta_eot) {
	    if (DBGFLG || OS_GOODMTSTATE) {
		error("File Spacing %s %ld failed with no BOT/EOT!",
		      (revf ? "backward" : "forward"), (long)cnt);
	    }
	    /* Recover by assuming we hit limit in one direction or another */
	    if (revf)
		d->mta_bot = TRUE;
	    else
		d->mta_eot = TRUE;
	}
    } else {
	/* If succeeded, assume no need to fetch new state.  On full
	 * success, neither BOT nor EOT should be set, but EOF should be.
	 */
	d->mta_eof = TRUE;
    }

    if (DBGFLG) {
	dbprintln("FileSpaced %s: %ld",
		  (revf ? "backward" : "forward"), d->mta_frms);
	devstatus(d, "os_mtfspace");
    }
    return TRUE;

#else
    return FALSE;
#endif
}


int
os_mtclrerr(struct devmt *d)
{
    /* Do MTCSE.  Known to be defined only by DECOSF? */
#if CENV_SYS_UNIX
# if CENV_SYS_SUN || CENV_SYS_SOLARIS || CENV_SYS_BSD || CENV_SYS_LINUX
#  ifndef MTCSE			/* No equiv, try NOP instead */
#   define MTCSE MTNOP
#  endif
# endif
    if (!osux_mtop(d, MTCSE, 1, "MTCSE")) {
	return FALSE;
    }
    d->mta_err = 0;
    return TRUE;
#else
    return FALSE
#endif
}

/* Status printing
 */

#if 0 /* DECOSF */
struct  mtget   {
        DEV_EEI_STATUS eei;             /* Extended Error Information */
};
#endif /* DECOSF */

#if 0 /* FreeBSD */
struct mtget {
        daddr_t mt_blksiz;      /* presently operating blocksize */
        daddr_t mt_density;     /* presently operating density */
        u_int32_t mt_comp;      /* presently operating compression */
        daddr_t mt_blksiz0;     /* blocksize for mode 0 */
        daddr_t mt_blksiz1;     /* blocksize for mode 1 */
        daddr_t mt_blksiz2;     /* blocksize for mode 2 */
        daddr_t mt_blksiz3;     /* blocksize for mode 3 */
        daddr_t mt_density0;    /* density for mode 0 */
        daddr_t mt_density1;    /* density for mode 1 */
        daddr_t mt_density2;    /* density for mode 2 */
        daddr_t mt_density3;    /* density for mode 3 */
};
#endif /* FreeBSD */


void
os_mtshow(struct devmt *d, FILE *f)
{
#if CENV_SYS_UNIX
    struct mtget mt;

    if (ioctl(d->d_fd, MTIOCGET, (char *)&mt) < 0) {
	fprintf(f, "; Couldn't get status for %s; %s\r\n",
				d->d_path, dp_strerror(-1));
	return;
    }
    fprintf(f, "; Status for magtape %s:\r\n", d->d_path);

    /* First try to do generic stuff.
     * Problem: different OSes use these in different ways!
     */
    fprintf(f, "; mt_type:  0x%lx\r\n", (long)mt.mt_type);
    fprintf(f, "; mt_dsreg: 0x%lx\r\n", (long)mt.mt_dsreg);
    fprintf(f, "; mt_erreg: 0x%lx\r\n", (long)mt.mt_erreg);
    fprintf(f, "; mt_resid:  %ld\r\n", (long)mt.mt_resid);
    fprintf(f, "; mt_fileno: %ld\r\n", (long)mt.mt_fileno);
    fprintf(f, "; mt_blkno:  %ld\r\n", (long)mt.mt_blkno);

    /* Now do strictly OSD stuff */

# if CENV_SYS_SUN || CENV_SYS_SOLARIS
    fprintf(f, "; mt_flags: 0x%lx ->", (long)mt.mt_flags);
    if (mt.mt_flags & MTF_SCSI) fprintf(f, " SCSI");
    if (mt.mt_flags & MTF_REEL) fprintf(f, " REEL");
    if (mt.mt_flags & MTF_ASF)  fprintf(f, " ABSPOS");
#  ifdef MTF_TAPE_HEAD_DIRTY
    if (mt.mt_flags & MTF_TAPE_HEAD_DIRTY) fprintf(f, " DIRTY");
#  endif
#  ifdef MTF_TAPE_CLN_SUPPORTED
    if (mt.mt_flags & MTF_TAPE_CLN_SUPPORTED) fprintf(f, " CLNSUP");
#  endif
    fprintf(f, "\r\n");
    fprintf(f, ";   mt_bf: %ld\r\n", (long)mt.mt_bf);
# endif /* SUN || SOLARIS */

# if CENV_SYS_LINUX
    fprintf(f, "; mt_gstat: 0x%lx\r\n", (long)mt.mt_gstat);
# endif /* LINUX */

#endif	/* CENV_SYS_UNIX */
}
