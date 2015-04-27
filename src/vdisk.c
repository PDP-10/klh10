/* VDISK.C - Virtual Disk support routines
*/
/* $Id: vdisk.c,v 2.5 2002/05/21 09:47:06 klh Exp $
*/
/*  Copyright © 1993, 2001 Kenneth L. Harrenstien
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
 * $Log: vdisk.c,v $
 * Revision 2.5  2002/05/21 09:47:06  klh
 * Second pass at LFS: make sure internal computations are big enough
 *
 * Revision 2.4  2002/03/28 16:52:52  klh
 * First pass at using LFS (Large File Support)
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include <stdio.h>
#include <stdlib.h>	/* For malloc */
#include <stdarg.h>
#include <string.h>

#include "rcsid.h"
#include "cenv.h"
#include "word10.h"
#include "osdsup.h"
#include "vdisk.h"

#ifdef RCSID
 RCSID(vdisk_c,"$Id: vdisk.c,v 2.5 2002/05/21 09:47:06 klh Exp $")
#endif

/* Define stuff for format conversions */

# define vdk_fmt(i,n,c,s,fr,to) \
	fr(w10_t *, int, unsigned char *), \
	to(unsigned char *, w10_t *, int)

static void VDK_FORMATS;	/* Automate function predecls */
# undef vdk_fmt

static struct {
	char *fmt_name;		/* Short name of format */
	int fmt_siz;		/* # bytes in a double-word */
	void (*fmt_fr)(		/* Conversion routine FROM format to words */
		w10_t *, int, unsigned char *);
	void (*fmt_to)(		/* Conversion routine TO format from words */
		unsigned char *, w10_t *, int);
} vdkfmttab[] = {
# define vdk_fmt(i,n,c,s,f,t) { n, s, f, t }
	VDK_FORMATS
# undef vdk_fmt
};

/* Error reporting for all VDISK code.
** Currently uses a fixed-size error string buffer.  This is dumb but
** simple; all calling routines must make some effort to keep length
** within bounds.
** Specifically, never use plain "%s" -- always limit it.
*/
static void
vdkerror(register struct vdk_unit *d, char *fmt, ...)
{
    char ebuf[512];
    char *devnam = &d->dk_devname[0];

    if (*devnam == 0)
	devnam = "<\?\?\?>";
    {
	va_list ap;
	va_start(ap, fmt);
	vsprintf(ebuf, fmt, ap);
	va_end(ap);
    }

    if (d->dk_errhan == NULL)
	fprintf(stderr, "%s: %s\n", devnam, ebuf);
    else
	(*(d->dk_errhan))(d, ebuf);
}


int
vdk_init(register struct vdk_unit *d,
	 void (*errhdlr)(struct vdk_unit *, char *),
	 char *arg)
{
    memset((char *)d, 0, sizeof(*d));	/* For now */
    d->dk_errhan = errhdlr;
    d->dk_errarg = arg;
    return TRUE;
}

int
vdk_mount(register struct vdk_unit *d,
	  char *path,
	  int wrtf)
{
    size_t cvtsiz;

    if (!d->dk_devname[0]) {
	d->dk_err = EINVAL;	/* Invalid arg */
	return FALSE;		/* Not initialized */
    }

    /* Prepare some things that depend on format.
    ** If doing conversion, the buffer pointed to by dk_buf needs to be big
    ** enough to contain at least one sector of the largest possible format.
    ** For now I'll default it to 1024 words as that's the largest OS page
    ** unit (ITS) and should contain enough bytes to support even a bizarro
    ** future sector format.
    */

    if ((0 <= d->dk_format) && (d->dk_format < VDK_FMT_N)) {
	cvtsiz = 1024 * sizeof(w10_t);			/* Default bufsiz */
	d->dk_bytesec = (VDK_NWDS(d) * vdkfmttab[d->dk_format].fmt_siz) / 2;

	if ((d->dk_fmt2wds = vdkfmttab[d->dk_format].fmt_fr) == cvtfr_raw)
	    d->dk_fmt2wds = NULL;
	if ((d->dk_wds2fmt = vdkfmttab[d->dk_format].fmt_to) == cvtto_raw)
	    d->dk_wds2fmt = NULL;

    } else {
	vdkerror(d, "vdk_mount: Unknown disk format %d", d->dk_format);
	d->dk_err = EINVAL;
	return FALSE;
    }

    /* If doing any kind of conversions, will need buffer */
    if (d->dk_fmt2wds || d->dk_wds2fmt) {
	if (!d->dk_buf || (d->dk_bufsiz < cvtsiz)) {
	    if (d->dk_buf) {
		free(d->dk_buf);
		d->dk_buf = NULL;
	    }
	    if (!(d->dk_buf = (unsigned char *)malloc(cvtsiz))) {
		vdkerror(d, "vdk_mount: Cannot alloc cvt buffer of size %ld",
				(long)cvtsiz);
		d->dk_err = errno;
		return FALSE;
	    }
	    d->dk_bufsiz = cvtsiz;
	}
	d->dk_bufsecs = d->dk_bufsiz / d->dk_bytesec;	/* # sectors in buff */
    }


#if VDK_DISKMAP
    if (d->dk_ismap) {
	memset((char *)d->dk_dfh, 0, sizeof(struct vdk_header));
	d->dk_blkalign = -1;	/* Round down, ignore extra sectors */
	d->dk_secblk = 1;	/* Blocking factor: # sectors per block */

	d->dk_wdsblk = d->dk_secblk * d->dk_nwds;	/* # words per block */
	d->dk_byteblk = d->dk_secblk * d->dk_bytesec;	/* # bytes per block */
	d->dk_blkcyl = (uint32)d->dk_nsecs * d->dk_ntrks;

	/* Temporarily blkcyl is # sectors per cylinder */
	if (d->dk_blkalign < 0) {
	    d->dk_blkcyl /= d->dk_secblk;		/* Round down */
	    d->dk_nblks = d->dk_blkcyl * d->dk_ncyls;
	} else if (d->dk_blkalign > 0) {		/* Round up */
	    d->dk_blkcyl = (d->dk_blkcyl + d->dk_secblk - 1) / d->dk_secblk;
	    d->dk_nblks = d->dk_blkcyl * d->dk_ncyls;
	} else {					/* Don't round */
	    d->dk_nblks = (d->dk_blkcyl * d->dk_ncyls) / d->dk_secblk;
	    d->dk_blkcyl = 0;	/* Shouldn't need this any more */
	}

	d->dk_map = NULL;
	d->dk_freep = 0;
    }
#endif	/* VDK_DISKMAP */


    /* Actually open the real device! */
    d->dk_iswrite = wrtf;			/* See if writing */
    if (!os_fdopen(&d->dk_fd, path, (wrtf ? "+b" : "rb"))) {
	/* Diskfile doesn't appear to exist, try to create it */
	fprintf(stderr, "[Creating %s disk file \"%s\"]\r\n",
			d->dk_devname, path);
	if (!wrtf || !os_fdopen(&d->dk_fd, path, "+bc")) {
	    vdkerror(d, "vdk_mount: Cannot create %s disk file \"%s\"",
			d->dk_devname, path);
	    d->dk_err = errno;
	    return FALSE;
	}
#if VDK_DISKMAP
	if (d->dk_ismap && !vdk_mapcreate(d)) {
	    vdkerror(d, "vdk_mount: Cannot create disk map");
	    /* mapcreate should set dkerr */
	    return FALSE;
	}
#endif
    }
#if VDK_DISKMAP
      else if (d->dk_ismap) {
	if (!vdk_mapload(d)) {
	    vdkerror(d, "vdk_mount: Cannot load disk map");
	    /* mapload should set dkerr */
	    return FALSE;
	}
    }
#endif

    /* Success, remember the filename */
    if (!(d->dk_filename = (char *)malloc(strlen(path)+1))) {
	vdkerror(d, "vdk_mount: Cannot malloc pathname \"%s\"", path);
	d->dk_err = errno;
	os_fdclose(d->dk_fd);
	return FALSE;
    }
    strcpy(d->dk_filename, path);

    return TRUE;
}

int
vdk_unmount(register struct vdk_unit *d)
{
    if (d->dk_filename) {
#if VDK_DISKMAP
	if (d->dk_ismap) {
	    if (!vdk_unmap(d))
		return 0;
	}
#endif
	if (!os_fdclose(d->dk_fd))
	    return 0;
	free(d->dk_filename);
	d->dk_filename = NULL;
    }
    return 1;
}

/* Read from disk.
**	Return # sectors read.
**
**	Raw (no-conversion) case reads directly to word buffer.
**	Conversion cases will use intermediate buffer.
**	    Too dangerous to do conversion-in-place as CPU and disk are
**	    now independent threads/processes and CPU shouldn't ever see
**	    the intermediate forms!
*/
int
vdk_read(register struct vdk_unit *d,
	 w10_t *wp,		/* Word buffer to read data */
	 uint32 secaddr,	/* Sector addr on disk */
	 int nsec)		/* # sectors - Never more than 16 bits */
{
#if VDK_DISKMAP
    register osdaddr_t dwaddr;

    if (d->dk_format != VDK_FMT_RAW) {
	vdkerror(d, "vdk_read: Can't map format %d", d->dk_format);
	return 0;
    }
    dwaddr = ((osdaddr_t)secaddr) * VDK_NWDS(d);
    if (dwaddr % d->dk_wdsblk) {
	vdkerror(d, "vdk_read: Non-sector disk address %" OSDADDR_FMT "d",
		 dwaddr);
	return 0;		/* Later do something better */
    }
    return vdk_mapio(d, 0, dwaddr, wp, (int)(nsec * VDK_NWDS(d)))

#else
    register osdaddr_t daddr;
    register size_t bcnt;
    size_t ndone = 0;
    int secleft;

    d->dk_err = 0;

    if (d->dk_fmt2wds == NULL) {	/* RAW input?  (No conversion) */

	daddr = ((osdaddr_t)secaddr) * VDK_NWDS(d) * sizeof(w10_t);
	bcnt = nsec * VDK_NWDS(d) * sizeof(w10_t);

	if (!os_fdseek(d->dk_fd, daddr)) {
	    d->dk_err = errno;		/* OS DEP!! */
	    vdkerror(d, "vdk_read: seek failed for %"
		     OSDADDR_FMT "d, errno = %d", daddr, errno);
	    return 0;			/* Later do something better? */
	}

	if (!os_fdread(d->dk_fd, (char *)wp, bcnt, &ndone)) {
	    d->dk_err = errno;		/* OS DEP!! */
	    vdkerror(d, "vdk_read: failed: cnt %ld, ret %ld, errno = %d",
		     (long)bcnt, (long)ndone, errno);
	    return ndone / (VDK_NWDS(d) * sizeof(w10_t));
	}

	if (ndone < bcnt) {	/* If incomplete read, sector may not exist */
	    /* SPECIAL HACK FOR SPARSE NORMAL FILES
	    ** Fill out unread words with zeros, assuming sectors haven't yet
	    ** been created on disk and we ran into EOF.
	    */
	    memset(((char *)wp)+ndone, 0, bcnt - ndone);
	}
	return nsec;
    }

    /* Any other kind of format comes here -- requires using another buffer.
    ** Speed is not of the essence for the following cases.
    ** An inner loop is used because the buffer may not be large enough to
    ** hold the entire transfer requested.  It will always, however, be large
    ** enough to hold at least one sector at a time.
    */

    /* Set up for OS I/O */
    daddr = ((osdaddr_t)secaddr) * d->dk_bytesec; /* Disk addr in bytes */
    if (!os_fdseek(d->dk_fd, daddr)) {
	d->dk_err = errno;		/* OS DEP!! */
	vdkerror(d, "vdk_read: seek failed for %" OSDADDR_FMT "d, errno = %d",
			daddr, errno);
	return 0;
    }

    secleft = nsec;
    while (secleft > 0) {
	int err;
	int secwant, secdone;

	secwant = (secleft <= d->dk_bufsecs) ? secleft : d->dk_bufsecs;
	bcnt = secwant * d->dk_bytesec;		/* # bytes to read */

	err = !os_fdread(d->dk_fd, (char *) d->dk_buf, bcnt, &ndone);

	/* Find # sectors read in (ie need conversion) */
	secdone = (ndone == bcnt) ? secwant : (ndone / d->dk_bytesec);
	if (secdone) {
	    (*d->dk_fmt2wds)(wp, (int)(secdone * VDK_NWDS(d)), d->dk_buf);
	    secleft -= secdone;
	    wp += secdone * VDK_NWDS(d);
	}

	if (err) {
	    d->dk_err = errno;		/* OS DEP!! */
	    vdkerror(d, "vdk_read: failed, cnt %ld, ret %ld, errno = %d",
			    (long)bcnt, (long)ndone, errno);
	    break;
	}

	if (secdone < secwant) {
	    /* SPECIAL HACK FOR SPARSE NORMAL FILES
	    ** Fill out unread words with zeros, assuming sectors haven't yet
	    ** been created on disk and we ran into EOF.
	    */
	    memset((char *)wp, 0, secleft * VDK_NWDS(d) * sizeof(w10_t));
	    /* wp += secleft * VDK_NWDS(d); */
	    secleft = 0;
	    break;
	}
    }

    return nsec - secleft;
#endif /* !VDK_DISKMAP */
}

/* Write to disk.
**	Return # sectors written.
**
**	Raw (no-conversion) case writes directly from word buffer.
**	Conversion cases use intermediate buffer.
*/
int
vdk_write(register struct vdk_unit *d,
	  w10_t *wp,		/* Word buffer to read data */
	  uint32 secaddr,	/* Sector addr on disk */
	  int nsec)		/* # sectors - Never more than 16 bits */
{
#if VDK_DISKMAP
    register osdaddr_t dwaddr;

    if (d->dk_format != VDK_FMT_RAW) {
	vdkerror(d, "vdk_write: Can't map format %d", d->dk_format);
	return 0;
    }
    dwaddr = ((osdaddr_t)secaddr) * VDK_NWDS(d);
    if (dwaddr % d->dk_wdsblk) {
	vdkerror(d, "vdk_write: Non-sector disk address %" OSDADDR_FMT "d",
		 dwaddr);
	return 0;		/* Later do something better */
    }
    return vdk_mapio(d, TRUE, dwaddr, wp, (int)(nsec * VDK_NWDS(d)))

#else
    register osdaddr_t daddr;
    register size_t bcnt;
    size_t ndone = 0;
    int secleft;

    d->dk_err = 0;

    if (d->dk_wds2fmt == NULL) {	/* RAW output?  (No conversion) */

	daddr = ((osdaddr_t)secaddr) * VDK_NWDS(d) * sizeof(w10_t);
	bcnt = nsec * VDK_NWDS(d) * sizeof(w10_t);

	if (!os_fdseek(d->dk_fd, daddr)) {
	    vdkerror(d, "vdk_write: seek failed for %" OSDADDR_FMT
		     "d, errno = %d", daddr, errno);
	    d->dk_err = errno;		/* OS DEP!! */
	    return 0;			/* Later do something better? */
	}

	if (!os_fdwrite(d->dk_fd, (char *)wp, bcnt, &ndone)) {
	    vdkerror(d, "vdk_write: failed: cnt %ld, ret %ld, errno = %d",
			    (long)bcnt, (long)ndone, errno);
	    d->dk_err = errno;		/* OS DEP!! */
	    return ndone / (VDK_NWDS(d) * sizeof(w10_t));
	}
	return nsec;
    }

    /* Any other kind of format comes here -- may require using another buffer.
    ** Speed is not of the essence for the following cases.
    ** An inner loop is used because the buffer may not be large enough to
    ** hold the entire transfer requested.  It will always, however, be large
    ** enough to hold at least one sector at a time.
    */

    /* Set up for OS I/O */
    daddr = ((osdaddr_t)secaddr) * d->dk_bytesec; /* Disk addr in bytes */
    if (!os_fdseek(d->dk_fd, daddr)) {
	vdkerror(d, "vdk_read: seek failed for %" OSDADDR_FMT "d, errno = %d",
			daddr, errno);
	d->dk_err = errno;		/* OS DEP!! */
	return 0;
    }

    secleft = nsec;
    while (secleft > 0) {
	int err;
	int secwant, secdone;

	secwant = (secleft <= d->dk_bufsecs) ? secleft : d->dk_bufsecs;

	/* Convert words into buffer */
	(*d->dk_wds2fmt)(d->dk_buf, wp, (int)(secwant * VDK_NWDS(d)));

	bcnt = secwant * d->dk_bytesec;		/* # bytes to write */

	err = !os_fdwrite(d->dk_fd, (char *) d->dk_buf, bcnt, &ndone);

	/* Find # sectors written */
	secdone = (ndone == bcnt) ? secwant : (ndone / d->dk_bytesec);
	if (secdone) {
	    secleft -= secdone;
	    wp += secdone * VDK_NWDS(d);
	}

	if (err || (secdone != secwant)) {
	    d->dk_err = errno;			/* OS DEP!!! */
	    vdkerror(d, "vdk_write: failed, cnt %ld, ret %ld, errno = %d",
			    (long)bcnt, (long)ndone, errno);
	    break;
	}
    }

    return nsec - secleft;
#endif /* !VDK_DISKMAP */
}

/* Format conversion routines */

/*
	dbd9: Disk_BigEnd_Double (9/2) - Same as H36 (Tape_Hidens)
		B0  1  2  3  4  5  6  7
		 8  9 10 11 12 13 14 15		[Word 0]
		16 17 18 19 20 21 22 23
		24 25 26 27 28 29 30 31
		32 33 34 35 B0  1  2  3
		 4  5  6  7  8  9 10 11		[Word 1]
		12 13 14 15 16 17 18 19
		20 21 22 23 24 25 26 27
		28 29 30 31 32 33 34 35

*/
static void
cvtfr_dbd9(register w10_t *wp,
	   register int wcnt,
	   register unsigned char *ucp)
{
    register w10_t w;
    register int dwcnt = wcnt >> 1;

    for (; --dwcnt >= 0; ucp += 9) {
	LRHSET(w,
	    (((ucp[0]&0377)<<10) | ((ucp[1]&0377)<<2) | ((ucp[2]>>6)&03)),
	    (((ucp[2]&077)<<12)  | ((ucp[3]&0377)<<4) | ((ucp[4]>>4)&017)));
	*wp++ = w;
	LRHSET(w,
	    (((ucp[4]&017)<<14) | ((ucp[5]&0377)<<6) | ((ucp[6]>>2)&077)),
	    (((ucp[6]&03)<<16)  | ((ucp[7]&0377)<<8) | (ucp[8]&0377)));
	*wp++ = w;
    }

    /* Ugh, allow gobbling an odd word for generality */
    if (wcnt & 01) {
	LRHSET(w,
	    (((ucp[0]&0377)<<10) | ((ucp[1]&0377)<<2) | ((ucp[2]>>6)&03)),
	    (((ucp[2]&077)<<12)  | ((ucp[3]&0377)<<4) | ((ucp[4]>>4)&017)));
	*wp++ = w;
    }
}

static void
cvtto_dbd9(register unsigned char *ucp,
	   register w10_t *wp,
	   register int wcnt)
{
    register w10_t w, w2;
    register int dwcnt = wcnt >> 1;

    for (; --dwcnt >= 0; ) {
	w = *wp++;
	w2 = *wp++;
	*ucp++ = (LHGET(w) >> 10) & 0377;
	*ucp++ = (LHGET(w) >>  2) & 0377;
	*ucp++ = ((LHGET(w)&03)<<6) | ((RHGET(w) >> 12) & 077);
	*ucp++ = (RHGET(w) >>  4) & 0377;
	*ucp++ = ((RHGET(w)&017)<<4) | ((LHGET(w2) >> 14) & 017);
	*ucp++ = (LHGET(w2) >>  6) & 0377;
	*ucp++ = ((LHGET(w2)&077)<<2) | ((RHGET(w2) >> 16) & 03);
	*ucp++ = (RHGET(w2) >>  8) & 0377;
	*ucp++ =  RHGET(w2)        & 0377;
    }

    /* Ugh, allow writing an odd word for generality */
    if (wcnt & 01) {
	w = *wp++;
	*ucp++ = (LHGET(w) >> 10) & 0377;
	*ucp++ = (LHGET(w) >>  2) & 0377;
	*ucp++ = ((LHGET(w)&03)<<6) | ((RHGET(w) >> 12) & 077);
	*ucp++ = (RHGET(w) >>  4) & 0377;
	*ucp++ = ((RHGET(w)&017)<<4);
    }
}


/*
	dld9: Disk_LittleEnd_Double (9/2)
		28 29 30 31 32 33 34 35
		20 21 22 23 24 25 26 27
		12 13 14 15 16 17 18 19
		 4  5  6  7  8  9 10 11		[Word 0]
		32 33 34 35 B0  1  2  3
		24 25 26 27 28 29 30 31
		16 17 18 19 20 21 22 23
		 8  9 10 11 12 13 14 15		[Word 1]
		B0  1  2  3  4  5  6  7
*/
static void
cvtfr_dld9(register w10_t *wp,
	   register int wcnt,
	   register unsigned char *ucp)
{
    register w10_t w;
    register int dwcnt = wcnt >> 1;

    for (; --dwcnt >= 0; ucp += 9) {
	LRHSET(w,
	    (((ucp[4]&017)<<14) | ((ucp[3]&0377)<<6) | ((ucp[2]>>2)&077)),
	    (((ucp[2]&03)<<16)  | ((ucp[1]&0377)<<8) | (ucp[0]&0377)));
	*wp++ = w;
	LRHSET(w,
	    (((ucp[8]&0377)<<10) | ((ucp[7]&0377)<<2) | ((ucp[6]>>6)&03)),
	    (((ucp[6]&077)<<12)  | ((ucp[5]&0377)<<4) | ((ucp[4]>>4)&017)));
	*wp++ = w;
    }

    /* Ugh, allow gobbling an odd word for generality */
    if (wcnt & 01) {
	LRHSET(w,
	    (((ucp[4]&017)<<14) | ((ucp[3]&0377)<<6) | ((ucp[2]>>2)&077)),
	    (((ucp[2]&03)<<16)  | ((ucp[1]&0377)<<8) | (ucp[0]&0377)));
	*wp++ = w;
    }
}

static void
cvtto_dld9(register unsigned char *ucp,
	   register w10_t *wp,
	   register int wcnt)
{
    register w10_t w, w2;
    register int dwcnt = wcnt >> 1;

    for (; --dwcnt >= 0; ) {
	w = *wp++;
	w2 = *wp++;
	*ucp++ =  RHGET(w)        & 0377;
	*ucp++ = (RHGET(w) >>  8) & 0377;
	*ucp++ = ((LHGET(w)&077)<<2) | ((RHGET(w) >> 16) & 03);
	*ucp++ = (LHGET(w) >>  6) & 0377;
	*ucp++ = ((RHGET(w2)&017)<<4) | ((LHGET(w) >> 14) & 017);
	*ucp++ = (RHGET(w2) >>  4) & 0377;
	*ucp++ = ((LHGET(w2)&03)<<6) | ((RHGET(w2) >> 12) & 077);
	*ucp++ = (LHGET(w2) >>  2) & 0377;
	*ucp++ = (LHGET(w2) >> 10) & 0377;
    }

    /* Ugh, allow writing an odd word for generality */
    if (wcnt & 01) {
	w = *wp++;
	*ucp++ =  RHGET(w)        & 0377;
	*ucp++ = (RHGET(w) >>  8) & 0377;
	*ucp++ = ((LHGET(w)&077)<<2) | ((RHGET(w) >> 16) & 03);
	*ucp++ = (LHGET(w) >>  6) & 0377;
	*ucp++ = (LHGET(w) >> 14) & 017;
    }
}


/*
	dbw8: Disk_BigEnd_Word (8)
		 0  0  0  0  0  0  0  0
		 0  0  0  0  0  0  0  0
		 0  0  0  0  0  0  0  0
		 0  0  0  0 B0  1  2  3
		 4  5  6  7  8  9 10 11
		12 13 14 15 16 17 18 19
		20 21 22 23 24 25 26 27
		28 29 30 31 32 33 34 35
*/
static void
cvtfr_dbw8(register w10_t *wp,
	   register int wcnt,
	   register unsigned char *ucp)
{
    register w10_t w;

    for (; --wcnt >= 0; ucp += 8) {
	LRHSET(w,
	    (((ucp[3]&017)<<14) | ((ucp[4]&0377)<<6) | ((ucp[5]>>2)&077)),
	    (((ucp[5]&03)<<16)  | ((ucp[6]&0377)<<8) | (ucp[7]&0377)));
	*wp++ = w;
    }
}

static void
cvtto_dbw8(register unsigned char *ucp,
	   register w10_t *wp,
	   register int wcnt)
{
    register w10_t w;

    for (; --wcnt >= 0; ) {
	w = *wp++;
	*ucp++ = 0;
	*ucp++ = 0;
	*ucp++ = 0;
	*ucp++ = (LHGET(w) >> 14) & 017;
	*ucp++ = (LHGET(w) >>  6) & 0377;
	*ucp++ = ((LHGET(w)&077)<<2) | ((RHGET(w) >> 16) & 03);
	*ucp++ = (RHGET(w) >>  8) & 0377;
	*ucp++ =  RHGET(w)        & 0377;
    }
}

/*
	dlw8: Disk_LittleEnd_Word (8)
		28 29 30 31 32 33 34 35
		20 21 22 23 24 25 26 27
		12 13 14 15 16 17 18 19
		 4  5  6  7  8  9 10 11
		 0  0  0  0 B0  1  2  3
		 0  0  0  0  0  0  0  0
		 0  0  0  0  0  0  0  0
		 0  0  0  0  0  0  0  0
*/
static void
cvtfr_dlw8(register w10_t *wp,
	   register int wcnt,
	   register unsigned char *ucp)
{
    register w10_t w;

    for (; --wcnt >= 0; ucp += 8) {
	LRHSET(w,
	    (((ucp[4]&017)<<14) | ((ucp[3]&0377)<<6) | ((ucp[2]>>2)&077)),
	    (((ucp[2]&03)<<16)  | ((ucp[1]&0377)<<8) | (ucp[0]&0377)));
	*wp++ = w;
    }
}

static void
cvtto_dlw8(register unsigned char *ucp,
	   register w10_t *wp,
	   register int wcnt)
{
    register w10_t w;

    for (; --wcnt >= 0; ) {
	w = *wp++;
	*ucp++ =  RHGET(w)        & 0377;
	*ucp++ = (RHGET(w) >>  8) & 0377;
	*ucp++ = ((LHGET(w)&077)<<2) | ((RHGET(w) >> 16) & 03);
	*ucp++ = (LHGET(w) >>  6) & 0377;
	*ucp++ = (LHGET(w) >> 14) & 017;
	*ucp++ = 0;
	*ucp++ = 0;
	*ucp++ = 0;
    }
}

/*
	dbh4: Disk_BigEnd_Halfword (8)
		 0  0  0  0  0  0  0  0
		 0  0  0  0  0  0 B0  1
		 2  3  4  5  6  7  8  9
		10 11 12 13 14 15 16 17
		 0  0  0  0  0  0  0  0
		 0  0  0  0  0  0 18 19
		20 21 22 23 24 25 26 27
		28 29 30 31 32 33 34 35
*/
static void
cvtfr_dbh4(register w10_t *wp,
	   register int wcnt,
	   register unsigned char *ucp)
{
    register w10_t w;

    for (; --wcnt >= 0; ucp += 8) {
	LRHSET(w, (((ucp[1]&03)<<16) | ((ucp[2]&0377)<<8) | (ucp[3]&0377)),
		  (((ucp[5]&03)<<16) | ((ucp[6]&0377)<<8) | (ucp[7]&0377)));
	*wp++ = w;
    }
}

static void
cvtto_dbh4(register unsigned char *ucp,
	   register w10_t *wp,
	   register int wcnt)
{
    register w10_t w;

    for (; --wcnt >= 0; ) {
	w = *wp++;
	*ucp++ = 0;
	*ucp++ = (LHGET(w) >> 16) & 03;
	*ucp++ = (LHGET(w) >>  8) & 0377;
	*ucp++ =  LHGET(w)        & 0377;
	*ucp++ = 0;
	*ucp++ = (RHGET(w) >> 16) & 03;
	*ucp++ = (RHGET(w) >>  8) & 0377;
	*ucp++ =  RHGET(w)        & 0377;
    }
}

/*
	dlh4: Disk_LittleEnd_Halfword (8)
		28 29 30 31 32 33 34 35
		20 21 22 23 24 25 26 27
		 0  0  0  0  0  0 18 19
		 0  0  0  0  0  0  0  0
		10 11 12 13 14 15 16 17
		 2  3  4  5  6  7  8  9
		 0  0  0  0  0  0 B0  1
		 0  0  0  0  0  0  0  0

*/
static void
cvtfr_dlh4(register w10_t *wp,
	   register int wcnt,
	   register unsigned char *ucp)
{
    register w10_t w;

    for (; --wcnt >= 0; ucp += 8) {
	LRHSET(w, (((ucp[6]&03)<<16) | ((ucp[5]&0377)<<8) | (ucp[4]&0377)),
		  (((ucp[2]&03)<<16) | ((ucp[1]&0377)<<8) | (ucp[0]&0377)));
	*wp++ = w;
    }
}

static void
cvtto_dlh4(register unsigned char *ucp,
	   register w10_t *wp,
	   register int wcnt)
{
    register w10_t w;

    for (; --wcnt >= 0; ) {
	w = *wp++;
	*ucp++ =  RHGET(w)        & 0377;
	*ucp++ = (RHGET(w) >>  8) & 0377;
	*ucp++ = (RHGET(w) >> 16) & 03;
	*ucp++ = 0;
	*ucp++ =  LHGET(w)        & 0377;
	*ucp++ = (LHGET(w) >>  8) & 0377;
	*ucp++ = (LHGET(w) >> 16) & 03;
	*ucp++ = 0;
    }
}


/* "Rare" semi-raw conversions - no byte shuffling, but data is masked.
*/
static void
cvtfr_rare(register w10_t *wp,
	   register int wcnt,
	   register unsigned char *ucp)
{
    register w10_t w, *frwp = (w10_t *)ucp;

    while (--wcnt >= 0) {
	XWDSET(w, LHPGET(frwp)&H10MASK, RHPGET(frwp)&H10MASK);
	++frwp;
	*wp++ = w;
    }
}

static void
cvtto_rare(register unsigned char *ucp,
	   register w10_t *wp,
	   register int wcnt)
{
    register w10_t w, *towp = (w10_t *)ucp;

    while (--wcnt >= 0) {
	XWDSET(w, LHPGET(wp)&H10MASK, RHPGET(wp)&H10MASK);
	++wp;
	*towp++ = w;
    }
}


/* Raw no-op conversions.
**	Mainly here so conversion table can be filled out with meaningful
**	vectors for no-conversion cases.  Shouldn't actually be called.
*/
static void
cvtfr_raw(register w10_t *wp,
	  register int wcnt,
	  register unsigned char *ucp)
{
    memcpy((char *)wp, (char *)ucp, wcnt * sizeof(w10_t));
}

static void
cvtto_raw(register unsigned char *ucp,
	  register w10_t *wp,
	  register int wcnt)
{
    memcpy((char *)ucp, (char *)wp, wcnt * sizeof(w10_t));
}

/* Disk-map specific support */

#if VDK_DISKMAP

static int
vdk_mapcreate(register struct vdk_unit *d)
{
    register osdaddr_t da;
    register size_t mapsiz;
    size_t err;

    /* Derive map config params from blocking factor */
#define MAPINIT \
    d->dk_byteblk = (uint32) d->dk_secblk * d->dk_bytesec; /* bytes/blk */\
    d->dk_nblks = (((uint32) d->dk_nsecs ) * d-<dk_ntrks)		\
	/* Later, may subtract leftover sectors here?  (- SKIPSECS) */	\
			* d->dk_ncyls;					\
    d->dk_nblks /= d->dk_secblk;	/* Find # blocks this disk */

    mapsiz = d->dk_nblks * sizeof(osdaddr_t);
    d->dk_map = (osdaddr_t *)calloc((size_t)d->dk_nblks, sizeof(osdaddr_t));
    if (!d->dk_map) {
	vdkerror(d, "vdk_mapcreate: Malloc failed for map of %ld entries",
					(long) d->dk_nblks);
	return 0;
    }

    /* Compute first free disk addr after header and map written.
    ** Round up to first block.
    */
    da = sizeof(struct vdk_header) + mapsiz;
    da = ((da + d->dk_byteblk-1) / d->dk_byteblk) * d->dk_byteblk;

    /* Fill out initial disk header */
    d->dk_dfh.dh_freep = da;	/* First free realdisk address */

    /* Now initialize the diskfile.  Assume just created, so at start. */
    if (!os_fdwrite(d->dk_fd, (char *) &(d->dk_dfh), sizeof(struct vdk_header), &err)
      || err != sizeof(struct vdk_header)) {
	vdkerror(d, "vdk_mapcreate: write failed for cnt %ld, errno = %d",
			(long)sizeof(struct vdk_header), errno);
	return 0;
    }
    if (!os_fdwrite(d->dk_fd, (char *) d->dk_map, mapsiz, &err)
      || err != mapsiz) {
	vdkerror(d, "vdk_mapcreate: write failed for cnt %ld, errno = %d",
				(long)mapsiz, errno);
	return 0;
    }
    return 1;		/* Won! */
}

static int
vdk_mapload(register struct vdk_unit *d)
{
    register osdaddr_t da, *dp;
    register size_t mapsiz;
    size_t err;

    if (d->dk_map) {
	vdkerror(d, "vdk_mapload: Disk already mapped");
	return 0;
    }

    /* Read in diskfile header */
    if (!os_fdseek(d->dk_fd, (osdaddr_t)0)) {
	vdkerror(d, "vdk_mapload: seek failed for BOF");
	return 0;
    }
    if (!os_fdread(d->dk_fd, (char *) &(d->dk_dfh), sizeof(struct vdk_header), &err)
      || err != sizeof(struct vdk_header)) {
	vdkerror(d, "vdk_mapload: read failed for cnt %ld, errno = %d",
			(long)sizeof(struct vdk_header), errno);
	return 0;
    }

    /* Now should check veracity of header and set up remaining stuff
    ** based on the config info it contains.
    */
#if 0
	/* Nothing for now */
#endif
    MAPINIT		/* Do same config setup as mapcreate for now */

    mapsiz = d->dk_nblks * sizeof(osdaddr_t);
    d->dk_map = (osdaddr_t *)malloc(mapsiz);
    if (!d->dk_map) {
	vdkerror(d, "vdk_mapload: Malloc failed for map of %ld entries",
					(long) d->dk_nblks);
	return 0;
    }

    if (!os_fdread(d->dk_fd, (char *) d->dk_map, mapsiz, &err)
       || err != mapsiz) {
	vdkerror(d, "vdk_mapload: read failed for cnt %ld, errno = %d",
				(long)mapsiz, errno);
	return 0;
    }

    /* Now scan the map to determine the largest known block address,
    ** hence the first free one.
    ** Slogging through this at startup lets us avoid updating a
    ** disk-resident variable at every write.
    */
    da = 0;
    dp = d->dk_map;
    for (mapsiz = d->dk_nblks; mapsiz > 0; --mapsiz, ++dp) {
	if (*dp > da)
	    da = *dp;
    }
    d->dk_freep = da + d->dk_byteblk;	/* Point one block past highest */

    return 1;		/* Won! */
}


static int
vdk_mapio(struct vdk_unit *d,
	  int wrtf,
	  osdaddr_t dwaddr,
	  w10_t *wp,
	  int wcnt)
{
    register uint32 blkno;
    register osdaddr_t da;
    register int bcnt;
    register size_t bytes;
    size_t err;

    blkno = dwaddr / d->dk_wdsblk;

    if (wcnt) for (;;) {
	if ((bcnt = wcnt) > d->dk_wdsblk)
	    bcnt = d->dk_wdsblk;
	bytes = bcnt * sizeof(w10_t);
	if (blkno >= d->dk_nblks) {
	    vdkerror(d, "vdk_mapread: Non-ex block %ld", (long)blkno);
	    return 0;
	}
	d->dk_mupdate = FALSE;
	if (!(da = d->dk_map[blkno])) {	/* Block exists in map? */
	    if (!wrtf) {
		/* Nope, just pretend we read a bunch of zeros */
		memset((char *)vp, 0, bytes);
		goto rdone;
	    } else {
		d->dk_mupdate = TRUE;
		da = d->dk_freep;
	    }
	}
	if (!os_fdseek(d->dk_fd, da)) {
	    vdkerror(d, "vdk_mapread: seek failed for %" OSDADDR_FMT
		     "d, errno = %d", (long)da, errno);
	    return 0;		/* Later do something better */
	}
	if (!(wrtf
	      ? os_fdwrite(d->dk_fd, (char *)vp, bytes, &err)
	      ? os_fdread(d->dk_fd, (char *)vp, bytes, &err))) {
	    vdkerror(d, "vdk_mapio: %s failed for cnt %ld, ret %ld, errno = %d",
			wrtf ? "write" : "read",
			(long)(bytes), (long)err, errno);
	    return 0;		/* Later do something better */
	}
	if (err != bytes) {
#if 0
	    if (!wrtf && err == 0) {	/* Check for read of non-ex data */
		memset((char *)vp, 0, bytes);
		goto rdone;
	    }
#endif
	    vdkerror(d, "vdk_mapio: r/w failed for cnt %ld, ret %ld, errno = %d",
			(long)bytes, (long)err, errno);
	    return 0;		/* Later do something better */
	}
	if (d->dk_mupdate) {
	    /* Successful write, update the map */
	    if (bcnt < d->dk_wdsblk) {
		/* Fill out all of incompletely written block */
		bytes = sizeof(w10_t)*(d->dk_wdsblk - bcnt);
		memset(d->dk_blkbuf, 0, bytes);
		if (!os_fdwrite(d->dk_fd, d->dk_blkbuf, bytes, &err)) {
		    vdkerror(d, "vdk_mapio: w failed for cnt %ld, ret %ld, errno = %d",
			     (long)bytes, (long)err, errno);
		    return 0;
		}
	    }

	    da = sizeof(struct vdk_header) + (blkno * sizeof(osdaddr_t));
	    if (!os_fdseek(d->dk_fd, da)) {
		vdkerror(d, "vdk_mapio: seek failed, map update at %"
			 OSDADDR_FMT "d, errno = %d", da, errno);
		return 0;
	    }
	    if (!os_fdwrite(d->dk_fd, (char *)&(d->dk_freep),
				sizeof(osdaddr_t), &err)) {
		vdkerror(d, "vdk_mapio: w failed for cnt %ld, ret %ld, errno = %d",
			(long)sizeof(osdaddr_t), (long)err, errno);
		return 0;
	    }

	    /* Update on disk won, so now update mem */
	    d->dk_map[blkno] = d->dk_freep;
	    d->dk_freep = d->dk_byteblk;
	}

  rdone:
	if (wcnt <= bcnt)
	    break;		/* Done, leave now */
	wcnt -= bcnt;
	vp += bcnt;
	blkno++;
    }
    return 1;
}

#endif /* VDK_DISKMAP */
