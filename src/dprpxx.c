/* DPRPXX.C - Device sub-Process for KLH10 RPxx Disk
*/
/* $Id: dprpxx.c,v 2.5 2003/02/23 18:15:36 klh Exp $
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
 * $Log: dprpxx.c,v $
 * Revision 2.5  2003/02/23 18:15:36  klh
 * Tweak cast to avoid warning on NetBSD/Alpha.
 *
 * Revision 2.4  2002/05/21 09:52:19  klh
 * Change protos for vdk_read, vdk_write
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*

	This subprocess is intended to handle either actual hardware
disk drives, or virtual disk files.

	A disk is mounted by passing it a string path argument and
setting other parameters in the shared memory area to describe format,
size, and configuration.

*/

#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>	/* For malloc */
#include <stdarg.h>

#include "klh10.h"	/* For config params */
#include "word10.h"
#include "dpsup.h"		/* General DP defs */
#include "dprpxx.h"		/* RPXX specific defs */
#include "vdisk.h"		/* Virtual disk hackery */

#ifdef RCSID
 RCSID(dprpxx_c,"$Id: dprpxx.c,v 2.5 2003/02/23 18:15:36 klh Exp $")
#endif

#if CENV_SYS_UNIX
# include <unistd.h>		/* Include standard unix syscalls */
# include <sys/ioctl.h>
#endif

struct devdk {
	struct dp_s d_dp;	/* Point to shared memory area */
	struct dprpxx_s *d_rp;	/* Shorthand ptr to DPRPXX struct in area */
	w10_t *d_10mem;		/* Ptr to 10 memory, if DMA allowed */
	unsigned long d_10siz;	/* Size of 10 memory, in words */

	int d_mntreq;		/* TRUE if mount request pending */
	int d_state;
#define DPRPXX_STA_OFF 0
#define DPRPXX_STA_ON  1

	int d_isdisk;		/* Disk type, MTYP_xxx */
	char *d_path;		/* M Disk drive path spec */
#if CENV_SYS_UNIX
	int d_fd;
#endif
	unsigned char *d_buff;	/* Ptr to buffer loc */
	size_t d_blen;		/* Actual buffer length */

	struct vdk_unit d_vdk;	/* Virtual disk info */
} devdk;


void rptoten(struct devdk *);
void tentorp(struct devdk *);

void dprpclear(struct devdk *);
#if 0
void dprpstat(struct devdk *);
#endif

int devmount(struct devdk *d, char *opath, int wrtf);
int devclose(struct devdk *);
int devread(struct devdk *);
int devwrite(struct devdk *);
int dmaread(struct devdk *);
int dmawrite(struct devdk *);

void sscattn(struct devdk *d);
void chkmntreq(struct devdk *d);

#if 0
int os_dkopen(), os_dkread(), os_dkwrite(), os_dkclrerr();
int os_dkfsr(), os_dkshow();
#endif


/* For now, include VDISK source directly, so as to avoid compile-time
** switch conflicts (eg with KLH10).
*/
#include "vdisk.c"

#define DBGFLG (devdk.d_rp->dprp_dpc.dpc_debug)


/* Low-level support */


static void efatal(int num, char *fmt, ...)
{
    fprintf(stderr, "\n%s: ", "dprpxx");
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    putc('\n', stderr);

    dp_exit(&devdk.d_dp, num);
}

static void esfatal(int num, char *fmt, ...)
{
    fprintf(stderr, "\n%s: ", "dprpxx");
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fprintf(stderr, " - %s\n", dp_strerror(errno));

    dp_exit(&devdk.d_dp, num);
}

static void error(char *fmt, ...)
{
    fprintf(stderr, "\n%s: ", "dprpxx");
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
}

static void syserr(int num, char *fmt, ...)
{
    fprintf(stderr, "\n%s: ", "dprpxx");
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fprintf(stderr, " - %s\n", dp_strerror(num));
}

int
main(int argc, char **argv)
{
    register struct devdk *d = &devdk;

    /* General initialization */
    if (!dp_main(&d->d_dp, argc, argv)) {
	efatal(1, "DP init failed!");
    }
    d->d_rp = (struct dprpxx_s *)d->d_dp.dp_adr;	/* Make refs easier */

    /* After this point, can check DBGFLG */
    if (DBGFLG)
	fprintf(stderr, "[dprpxx: Started]");

    /* See if using DMA to 10 memory, and set up if so */
    d->d_10mem = NULL;
    if (d->d_rp->dprp_dma) {
	/* Attempt to attach segment into our address space */
	char *ptr = (char *)shmat(d->d_rp->dprp_shmid, (void *)0, SHM_RND);
	struct shmid_ds shmds;

	if (ptr == (char *)-1) {
	    fprintf(stderr, "[dprpxx: shmat failed for 10 mem - %s]\n",
				dp_strerror(errno));
	    d->d_rp->dprp_shmid = 0;
	    d->d_rp->dprp_dma = FALSE;
	    ptr = NULL;

	} else if (shmctl(d->d_rp->dprp_shmid, IPC_STAT, &shmds)) {
	    fprintf(stderr, "[dprpxx: shmctl failed for 10 mem - %s]\n",
				dp_strerror(errno));
	    d->d_rp->dprp_shmid = 0;
	    d->d_rp->dprp_dma = FALSE;
	    ptr = NULL;
	} else {
	    /* Set size of 10 memory we have mapped */
	    d->d_10siz = shmds.shm_segsz / sizeof(w10_t);
	}

	d->d_10mem = (w10_t *)ptr;	/* Won, set up pointer! */
	if (ptr && DBGFLG)
	    fprintf(stderr, "[dprpxx: Mapped 10 mem, %ld wds]",
					(long)d->d_10siz);
    }

    /* Find location and size of record buffer to use */
    d->d_buff = dp_xrbuff(dp_dpxto(&d->d_dp), &d->d_blen);

    /* Set up necessary event handlers for 10-DP communication.
    ** For now this is done by DPSUP.
    */

    /* Ignore TTY cruft so CTY hacking in 10 doesn't bother us */
    signal(SIGINT, SIG_IGN);	/* Ignore TTY cruft */
    signal(SIGQUIT, SIG_IGN);

    /* Open disk drive initially specified, if one; initialize stuff */
    d->d_state = DPRPXX_STA_OFF;

    /* Initialize VDK code */
    if (!vdk_init(&(d->d_vdk), NULLPROC, (char *)NULL))
	return 0;

    tentorp(d);			/* Start normal command/response process */

    return 1;			/* Never returns, but silence compiler */
}

/* RPTOTEN - Process to handle unexpected events and report them to the 10.
**	Does nothing for now; later could be responsible for listening
**	and responding to operations from a user disk-interface process.
*/

void rptoten(register struct devdk *d)
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

    for (;;) {

#if 0
	/* Make sure that buffer is free before clobbering it */
	dp_xswait(dpx);			/* Wait until buff free */

	/* OK, now do a blocking read on external command input! */
	if ((cnt = read(pffd, buff, max)) <= MINREQPKT) {

	    if (cnt < 0 && (errno == EINTR))	/* Ignore spurious signals */
		continue;

	    /* Error of some kind */
	    fprintf(stderr, "dprpxx: REQPKT read = %d, ", cnt);
	    if (cnt < 0) {
		if (--stoploop <= 0)
		    efatal(1, "Too many retries, aborting");
		fprintf(stderr, "errno %d = %s\r\n",
				errno, dp_strerror(errno));
	    } else if (cnt > 0)
		fprintf(stderr, "no REQPKT data\r\n");
	    else fprintf(stderr, "no REQPKT\r\n");

	    continue;		/* For now... */
	}

	/* Normal packet, pass to 10 via DPC */
	/* Or simply process here, then hand up results */
	dp_xsend(dpx, DPRP_RPKT, cnt);
#endif /* 0 */
    }
}

/* Slave Status Change - Signal attention.
**    Drive came online as a result of either:
**	(1) a successful soft mount request
**	(2) a hard mount came online
*/
void sscattn(register struct devdk *d)
{
    register struct dpx_s *dpx;

    dpx = dp_dpxfr(&d->d_dp);		/* Get ptr to from-DP comm rgn */

    dp_xswait(dpx);			/* Wait until receiver ready */
    dp_xsend(dpx, DPRP_MOUNT, 0);	/* Send note to 10! */

}

void chkmntreq(register struct devdk *d)
{
    d->d_mntreq = FALSE;	/* For now */
}

/* TENTORP - Main loop for thread handling commands from the 10.
**	Reads DPC message from 10 and interprets it, returning a
**	result code (and/or data).
*/

void tentorp(register struct devdk *d)
{
    register struct dpx_s *dpx;
    register unsigned char *buff;
    size_t max;
    register int rcnt;
    int res;
    int cmd;


    if (DBGFLG)
	fprintf(stderr, "[dprpxx: in tentorp]");

    dpx = dp_dpxto(&(d->d_dp));		/* Get ptr to "To-DP" xfer stuff */
    buff = dp_xrbuff(dpx, &max);

    for (;;) {

	/* Wait until 10 has a command for us */
	dp_xrwait(dpx);

	/* Reset some stuff for every command */
	d->d_rp->dprp_err = 0;
	res = DPRP_RES_SUCC;		/* Default is successful op */

	/* Process command from 10! */
	switch (cmd = dp_xrcmd(dpx)) {

	default:
	    fprintf(stderr, "[dprpxx: Unknown cmd %o]\r\n", dp_xrcmd(dpx));
	    res = DPRP_RES_FAIL;
	    break;

	case DPRP_RESET:	/* Reset DP */
	    /* Attempt to do complete reset */
	    fprintf(stderr, "[dprpxx: Reset request]\r\n");
#if 0
	    dprp_restart(2);
#endif
	    break;

	case DPRP_MOUNT:	/* Mount disk specified by string of N bytes */
	  {
	    unsigned char *tmpbuf = buff+1;
	    int wrtf;

	    dprpclear(d);
	    switch (buff[0]) {		/* Check first char */
	    case 'R':	wrtf = FALSE;	break;
	    case '*':
	    case 'W':	wrtf = TRUE;	break;
	    default:
		fprintf(stderr, "[dprpxx: Unknown mount type \'%c\']\r\n",
					buff[0]);
		res = DPRP_RES_FAIL;
		break;
	    }
	    if (res != DPRP_RES_FAIL) {
		if (!devmount(d, (char *)tmpbuf, wrtf)) {
		    res = DPRP_RES_FAIL;
		}
	    }
	  }
	    break;

	case DPRP_SEEK:
	case DPRP_NOP:		/* No operation */
	    break;

	case DPRP_UNL:		/* Unload??  (Eject?)  */
	    if (!devclose(d)) {		/* Same as close for now */
		res = DPRP_RES_FAIL;
	    }
	    break;

	case DPRP_WRITE:	/* Write N words */
	    rcnt = dp_xrcnt(dpx);		/* Get length to write */
	    if (!devwrite(d)) {
		/* Handle write error of some kind */
		res = DPRP_RES_FAIL;
	    }
	    break;

	case DPRP_READ:		/* Read N words */
	    if (!devread(d)) {
		/* Handle read error of some kind? */
		res = DPRP_RES_FAIL;
	    }
	    break;

	case DPRP_WRDMA:	/* Write N sectors into mem */
	    if (!dmawrite(d)) {
		/* Handle write error of some kind */
		res = DPRP_RES_FAIL;
	    }
	    break;

	case DPRP_RDDMA:	/* Read N sectors into mem */
	    if (!dmaread(d)) {
		/* Handle read error of some kind? */
		res = DPRP_RES_FAIL;
	    }
	    break;

	}
#if 0
	dprpstat(d);		/* Update most status vars */
#endif

	/* Command done, return result and tell 10 we're done */
	dp_xrdoack(dpx, res);
    }
}

#if 0
/* DPRPSTAT - Set shared status values
*/
void dprpstat(d)
register struct devdk *d;
{
    register struct dprpxx_s *dprp = d->d_rp;
    register struct vdk_unit *t = &d->d_vdk;

    switch (d->d_isdisk) {
    case MTYP_NONE:
	dprp->dprp_mol = FALSE;		/* Medium online */
	dprp->dprp_wrl = FALSE;	/* Write-locked */
	break;

    case MTYP_VIRT:
	dprp->dprp_mol = vmt_ismounted(t);		/* Medium online */
	dprp->dprp_wrl = !vmt_iswritable(t);	/* Write-locked */
	dprp->dprp_err = vmt_errors(t);
	break;

    default:
	if (dprp->dprp_mol = d->mta_mol) {
	    if (d->d_state == DPRPXX_STA_OFF) {
		if (DBGFLG)
		    fprintf(stderr, "[dprpxx: Disk came online: \"%s\"]\r\n",
					d->d_path);
		sscattn(d);			/* Signal 10 re change */
	    }
	    d->d_state = DPRPXX_STA_ON;
	} else {
	    if (d->d_state == DPRPXX_STA_ON) {
		if (DBGFLG)
		    fprintf(stderr, "[dprpxx: Disk went offline: \"%s\"]\r\n",
					d->d_path);
	    }
	    d->d_state = DPRPXX_STA_OFF;
	}
	dprp->dprp_wrl = d->mta_wrl;
	dprp->dprp_err = d->mta_err;
	break;
    }
}
#endif

void dprpclear(register struct devdk *d)
{
    register struct dprpxx_s *dprp = d->d_rp;

    dprp->dprp_mol = FALSE;
    dprp->dprp_wrl = FALSE;
    dprp->dprp_err = 0;
}


/* Generic device routines (null, virtual, and real)
**	Open, Close, Read, Write, Write-EOF, Write-EOT.
*/

/* MOUNT - must already have set up
**	d_buff, d_len.
*/
int devmount(register struct devdk *d, char *opath, int wrtf)
{
    register struct dprpxx_s *dprp = d->d_rp;
    char *path;

    /* Unmount & close existing drive if any */
    if (d->d_state == DPRPXX_STA_ON)
	devclose(d);

    /* Check out path argument and copy it */

    if (!opath || !*opath) {
	fprintf(stderr, "[dprpxx: Null mount path]\r\n");
	return FALSE;
    }
    path = malloc(strlen(opath)+1);
    strcpy(path, opath);


    /* Open the necessary files */

    /* All these should have been set up beforehand in shared mem */
    d->d_vdk.dk_format = dprp->dprp_fmt;
    strcpy(d->d_vdk.dk_devname, dprp->dprp_devname);
    d->d_vdk.dk_ncyls = dprp->dprp_ncyl;
    d->d_vdk.dk_ntrks = dprp->dprp_ntrk;
    d->d_vdk.dk_nsecs = dprp->dprp_nsec;
    d->d_vdk.dk_nwds = dprp->dprp_nwds;
    if (!vdk_mount(&d->d_vdk, path, wrtf)) {
	fprintf(stderr, "[dprpxx: Cannot mount device \"%s\": %s]\r\n", 
			    path, dp_strerror(d->d_vdk.dk_err));
	free(path);
	return FALSE;
    }

    d->d_state = DPRPXX_STA_ON;
    if (DBGFLG)
	fprintf(stderr, "[dprpxx: Mounted disk device \"%s\"]\r\n", path);

    d->d_path = path;
    d->d_isdisk = TRUE;
    dprp->dprp_mol = TRUE;
    dprp->dprp_wrl = !wrtf;

    return TRUE;
}


int devclose(struct devdk *d)
{
    int res = TRUE;

    if (DBGFLG && d->d_path)
	fprintf(stderr, "[dprpxx: Closing \"%s\"]\r\n", d->d_path);

    if (d->d_isdisk) {
	res = vdk_unmount(&d->d_vdk);		/* Close real disk */
    }

    /* Force us to forget about it even if above stuff failed */
    d->d_state = DPRPXX_STA_OFF;
    d->d_isdisk = FALSE;
    if (d->d_path) {
	free(d->d_path);
	d->d_path = NULL;
    }
    return res;
}

/* Read from device
**	Returns 1 if read something
**	Returns 0 if read nothing or error
*/

int devread(struct devdk *d)
{
    register struct dprpxx_s *dprp = d->d_rp;
    int nsec;

    if (! d->d_isdisk) {
	dprp->dprp_err = 1;
	dprp->dprp_scnt = 0;
	return FALSE;
    }
    nsec = dprp->dprp_scnt;
    if (DBGFLG)
	fprintf(stderr,
	    "[dprpxx: read daddr=%ld, buff=0x%lx, wc=%d, nsec=%d]\r\n",
	    (long)dprp->dprp_daddr, (long)d->d_buff,
	    (int)(nsec * dprp->dprp_nwds), nsec);

    dprp->dprp_scnt = vdk_read(&d->d_vdk, 
	    (w10_t *)d->d_buff,		/* Word buffer loc */
	    (uint32) dprp->dprp_daddr,	/* Disk addr (sectors) */
	    nsec);			/* # sectors */
    if ((dprp->dprp_err = d->d_vdk.dk_err)
      || (dprp->dprp_scnt != nsec)) {
	fprintf(stderr, "[dprpxx: read error on %s: %s]\r\n",
		    d->d_vdk.dk_filename, dp_strerror(d->d_vdk.dk_err));
	return FALSE;
    }
    return TRUE;
}

/* Write to device.
*/
int devwrite(struct devdk *d)
{
    register struct dprpxx_s *dprp = d->d_rp;
    int nsec;

    if (! d->d_isdisk) {
	dprp->dprp_err = 1;
	dprp->dprp_scnt = 0;
	return FALSE;
    }
    nsec = dprp->dprp_scnt;
    if (DBGFLG)
	fprintf(stderr,
	    "[dprpxx: write daddr=%ld, buff=0x%lx, wc=%d, nsec=%d]\r\n",
	    (long)dprp->dprp_daddr, (long)d->d_buff,
	    (int)(nsec * dprp->dprp_nwds), nsec);

    dprp->dprp_scnt = vdk_write(&d->d_vdk, 
	    (w10_t *)d->d_buff,		/* Word buffer loc */
	    (uint32) dprp->dprp_daddr,	/* Disk addr (sectors) */
	    nsec);			/* # sectors */
    if ((dprp->dprp_err = d->d_vdk.dk_err)
      || (dprp->dprp_scnt != nsec)) {
	fprintf(stderr, "[dprpxx: write error on %s: %s]\r\n",
		    d->d_vdk.dk_filename, dp_strerror(d->d_vdk.dk_err));
	return FALSE;
    }
    return TRUE;
}

/* Read DMA sectors from device
**	Returns 1 if read something
**	Returns 0 if read nothing or error
*/

int dmaread(register struct devdk *d)
{
    register struct dprpxx_s *dprp = d->d_rp;
    int nsec;
    int res;

    if (!d->d_isdisk) {
	dprp->dprp_err = 1;
	dprp->dprp_scnt = 0;
	return FALSE;
    }
    if (!d->d_10mem) {
	fprintf(stderr, "[dprpxx: Read DMA unsupported!]\r\n");
	dprp->dprp_err = 1;
	dprp->dprp_scnt = 0;
	return FALSE;
    }
    if (dprp->dprp_phyadr >= d->d_10siz) {
	fprintf(stderr, "[dprpxx: Non-ex phys addr %#lo]\r\n",
				(long)dprp->dprp_phyadr);
	dprp->dprp_err = 1;
	dprp->dprp_scnt = 0;
	return FALSE;
    }

    nsec = dprp->dprp_scnt;

    if (DBGFLG)
	fprintf(stderr,
	    "[dprpxx: read daddr=%ld, mem=%#lo, wc=%d, nsec=%d]\r\n",
		(long)dprp->dprp_daddr, (long)dprp->dprp_phyadr,
		(int)(nsec * dprp->dprp_nwds), nsec);

    res = vdk_read(&d->d_vdk, 
	    d->d_10mem +
		dprp->dprp_phyadr,	/* Word buffer loc */
	    (uint32) dprp->dprp_daddr,	/* Disk addr (sectors) */
	    nsec);			/* # sectors */

    dprp->dprp_scnt = res;
    dprp->dprp_err = d->d_vdk.dk_err;

    if (res == nsec && !dprp->dprp_err)
	return TRUE;

    fprintf(stderr, "[dprpxx: read error on %s: %s]\r\n",
		    d->d_vdk.dk_filename, dp_strerror(d->d_vdk.dk_err));
    return FALSE;
}

/* Write DMA sectors to device.
*/
int dmawrite(register struct devdk *d)
{
    register struct dprpxx_s *dprp = d->d_rp;
    int nsec;
    int res;

    if (!d->d_isdisk) {
	dprp->dprp_err = 1;
	dprp->dprp_scnt = 0;
	return FALSE;
    }
    if (!d->d_10mem) {
	fprintf(stderr, "[dprpxx: Write DMA unsupported!]\r\n");
	dprp->dprp_err = 1;
	dprp->dprp_scnt = 0;
	return FALSE;
    }
    if (dprp->dprp_phyadr >= d->d_10siz) {
	fprintf(stderr, "[dprpxx: Non-ex phys addr %#lo]\r\n",
				(long)dprp->dprp_phyadr);
	dprp->dprp_err = 1;
	dprp->dprp_scnt = 0;
	return FALSE;
    }

    nsec = dprp->dprp_scnt;

    if (DBGFLG)
	fprintf(stderr,
	    "[dprpxx: write daddr=%ld, mem=%#lo, wc=%d, nsec=%d]\r\n",
		(long)dprp->dprp_daddr, (long)dprp->dprp_phyadr,
		(int)(nsec * dprp->dprp_nwds), nsec);

    res = vdk_write(&d->d_vdk, 
	    d->d_10mem +
		dprp->dprp_phyadr,	/* Word buffer loc */
	    (uint32) dprp->dprp_daddr,	/* Disk addr (sectors) */
	    nsec);			/* # sectors */

    dprp->dprp_scnt = res;
    dprp->dprp_err = d->d_vdk.dk_err;

    if (res == nsec && !dprp->dprp_err)
	return TRUE;

    fprintf(stderr, "[dprpxx: write error on %s: %s]\r\n",
		    d->d_vdk.dk_filename, dp_strerror(d->d_vdk.dk_err));
    return FALSE;
}

#if 0

/* Disk handling routines
*/


int os_dkopen(d, path, wrtf)
struct devdk *d;
char *path;
{

    d->d_rp->dprp_mol = 0;	/* Plus general state */
    d->d_rp->dprp_wrl = 0;

#if CENV_SYS_UNIX
  {
    int fd;

    fd = open(path, (wrtf ? O_RDWR : O_RDONLY), 0600);
    if (fd < 0) {

	/* Check out failures.  Maybe later allow ENOENT or EINTR? */
	switch (errno) {
	    default:
		d->d_rp->dprp_err = errno;
		break;
	}
	return FALSE;
    }
    d->d_fd = fd;
    d->d_rp->dprp_mol = TRUE;
    d->d_rp->dprp_wrl = !wrtf;
  }
    return TRUE;
#else
    return FALSE;
#endif
}

int os_dkclose(d)
struct devdk *d;
{
    d->d_rp->dprp_mol = 0;
    d->d_rp->dprp_wrl = 0;

#if CENV_SYS_UNIX
    return close(d->d_fd);
#else
    return TRUE;
#endif
}

os_dkread(d)
register struct devdk *d;
{
#if CENV_SYS_UNIX
    /* Don't try to support retries here.  If OS doesn't do it, there isn't
    ** a whole lot we can do better.  (But then again, it's Un*x, so...)
    */
    unsigned int res;

    for (;;) {
	switch (res = read(d->d_fd, d->d_buff, d->d_blen)) {
	case 0:				/* Diskmark */
	    d->d_rp->dprp_eof = TRUE;
	    return TRUE;

	default:			/* Assume record read */
	    d->d_rp->dprp_frms = res;
	    return TRUE;

	case -1:			/* Error */
	    if (errno = EINTR)
		continue;
	    if (errno = ENOSPC) {	/* OSF/1 returns this for EOT */
		d->d_rp->dprp_eot = TRUE;
		return TRUE;
	    }
	    d->d_rp->dprp_err++;
	    fprintf(stderr, "[dprpxx: Disk read error: %s\r\n",
				dp_strerror(errno));
	    os_dkshow(d, stderr);	/* Show full status */
	    fprintf(stderr, "]\r\n");
	    return FALSE;
	}
    }
    return FALSE;
#endif	/* CENV_SYS_UNIX */

    return 0;		/* For now */
}

int os_dkwrite(d, buff, len)
register struct devdk *d;
register char *buff;
size_t len;
{
#if CENV_SYS_UNIX

    unsigned int ret;


    for (;;) {
	if ((ret = write(d->d_fd, buff, len)) == len) {
	    d->d_rp->dprp_frms = ret;			/* Won! */
	    return TRUE;
	}

	/* Error of some kind */
	if (ret == -1) {
	    if (errno == EINTR)
		continue;
	    if (errno == ENOSPC)	/* Reached EOT? */
		d->d_rp->dprp_eot = TRUE;
	    else
		os_dkstate(d);		/* Dunno, try general status rtn */

	    fprintf(stderr, "[dprpxx: write err: %s]\r\n", dp_strerror(errno));
	    os_dkshow(d, stderr);		/* Show full status */
	    d->d_rp->dprp_err++;
	    return FALSE;
	}

	fprintf(stderr, "[dprpxx: write trunc: %ld => %d]\r\n", (long)len, ret);
	d->d_rp->dprp_frms = ret;
	return TRUE;	/* Some kind of error, but let caller figure out */
    }
#endif	/* CENV_SYS_UNIX */

    return FALSE;		/* For now */
}

#endif /* 0 */

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

#elif CENV_SYS_MOONMAC
	Boolean create = FALSE, append = FALSE;
	short refnum;
	OSErr err;
	char pascal_string[256];
	
    if (!afd) return FALSE;
    for (; modes && *modes; ++modes) switch (*modes) {
	case 'a':	append = TRUE;	break;
	case 'c':	create = TRUE;	break;
	/* Ignore read/write and unknown chars for now */
    }
	pascal_string[0] = strlen(file);
	BlockMove(file, &pascal_string[1], pascal_string[0]);
	if (create) Create(pascal_string, 0, 'KH10', 'TEXT');
	err = FSOpen(pascal_string, 0, &refnum);
	*afd = err;
	if (err) return FALSE;
	*afd = refnum;
    return TRUE;
#endif
}

int
os_fdclose(osfd_t fd)
{
#if CENV_SYS_UNIX || CENV_SYS_MAC
    return close(fd) != -1;
#else
    return 0;
#endif
}

int
os_fdseek(osfd_t fd, osdaddr_t addr)
{
#if CENV_SYS_UNIX
    return lseek(fd, addr, L_SET) != -1;
#elif CENV_SYS_MAC || CENV_SYS_MOONMAC
    return lseek(fd, addr, SEEK_SET) != -1;
#else
    return 0;
#endif
}

int
os_fdread(osfd_t fd, char *buf, size_t len, size_t *ares)
{
#if CENV_SYS_UNIX || CENV_SYS_MOONMAC
    register int res = read(fd, buf, len);
    if (res < 0) {
	if (ares) *ares = 0;
	return FALSE;
    }
#elif CENV_SYS_MAC
    /* This is actually generic code for any system supporting unix-like
    ** calls with a 16-bit integer count interface.
    */
    register size_t res = 0;
    register unsigned int scnt, sres = 0;

    while (len) {
	scnt = len > (1<<14) ? (1<<14) : len;	/* 16-bit count each whack */
	if ((sres = read(fd, buf, cnt)) != scnt) {
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
#endif
    if (ares) *ares = res;
    return TRUE;
}

int
os_fdwrite(osfd_t fd, char *buf, size_t len, size_t *ares)
{
#if CENV_SYS_UNIX || CENV_SYS_MOONMAC
    register int res = write(fd, buf, len);
    if (res < 0) {
	if (ares) *ares = 0;
	return FALSE;
    }
#elif CENV_SYS_MAC
    /* This is actually generic code for any system supporting unix-like
    ** calls with a 16-bit integer count interface.
    */
    register size_t res = 0;
    register unsigned int scnt, sres = 0;

    while (len) {
	scnt = len > (1<<14) ? (1<<14) : len;	/* 16-bit count each whack */
	if ((sres = write(fd, buf, cnt)) != scnt) {
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
#endif
    if (ares) *ares = res;
    return TRUE;
}
