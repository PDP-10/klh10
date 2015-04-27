/* VDKFMT.C - Utility to copy or format virtual disks.
*/
/* $Id: vdkfmt.c,v 2.6 2002/05/21 09:51:26 klh Exp $
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
 * $Log: vdkfmt.c,v $
 * Revision 2.6  2002/05/21 09:51:26  klh
 * Change protos for vdk_read, vdk_write
 *
 * Revision 2.5  2001/11/19 10:51:43  klh
 * Bugfix: was freeing d_path which is now static.
 *
 * Revision 2.4  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*	VDKFMT is mainly used to copy or format virtual disks.
**	later it may become a general-purpose utility for managing
**	KLH10 disk packs.
**
*/

#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>		/* exit() */
#include <stdarg.h>
#include <errno.h>
#include <sys/file.h>		/* For open() flags */

#include "rcsid.h"
#include "cenv.h"
#include "osdsup.h"
#include "vdisk.h"		/* Include virtual disk stuff */

#if CENV_SYS_UNIX
# include <unistd.h>		/* Basic Unix syscalls */
# include <sys/types.h>
# include <sys/ioctl.h>
# include <sys/mtio.h>
# define NULLDEV "/dev/null"
# define FD_STDIN 0
# define FD_STDOUT 1
#endif

#define FNAMSIZ 200

#define TRUE 1
#define FALSE 0

#ifdef RCSID
 RCSID(vdkfmt_c,"$Id: vdkfmt.c,v 2.6 2002/05/21 09:51:26 klh Exp $")
#endif

/* Disk type configuration params.
**	All of these numbers assume drives using 18-bit formatting.
**	(16-bit formatting has more sectors per track)
*/
static struct diskconf {
	char dcf_name[8];	/* Drive type name (RP06, etc) */
	int dcf_type;		/* Type bits for type register */
	int dcf_nwds;		/* Words/Sector */
	int dcf_nsec;		/* Sectors/Track */
	int dcf_ntrk;		/* Tracks/Cylinder */
	int dcf_ncyl;		/* Cylinders/Drive */
} diskconfs[] = {
	/*	 wrd  sec trk cyl       Cyl+maint totsec totmaintsec */
    { "RP02", 0, 128, 10, 20, 203 },	/* 200+3  40000	 40,600 */
    { "RP03", 0, 128, 10, 20, 403 },	/* 400+3  80000	 80,600 */

    { "RP04", 0, 128, 20, 19, 411 },	/* 406+5 154280 156,180 */
    { "RP05", 0, 128, 20, 19, 411 },	/*   "      "	  "     */
    { "RP06", 0, 128, 20, 19, 815 },	/* 810+5 307800 309,700 */
    { "RP07", 0, 128, 43, 32, 630 },	/* 629+1 865504 866,880 */
    { "RM03", 0, 128, 30,  5, 823 },	/* 821+2 123150 123,450 */
    { "RM02", 0, 128, 30,  5, 823 },	/*    "     "      "    */
    { "RM05", 0, 128, 30, 19, 823 },	/* 821+2 467970	469,110 */
    { "RM80", 0, 128, 30, 14, 561 },	/* 559+2 234780 235,620 */

    { "" }
};
/* RP07 doc p.6-23 says addressing params are:
**	Mode		Cyl Head/Track	Sector(18) Sector(16)
**	Functional:	630	32	43	    50
**	Diagnostic:	632	32	43	    50
*/

/* In ITS:
	RP06 was 812+3
	RP07 was 627+3
	RM03 was 821+2
	RM05 was 820
	RM80 was 556+3
*/

/* VDKFMT parameters */

char nusage[] = "\
Usage: vdkfmt <params>\n\
  ip=<path>	Input disk device/file:\n\
  op=<path>	Output disk device\n\
  ifmt=<fmt>	format of input pack data\n\
  ofmt=<fmt>	format of output pack data\n\
  dt=<type>	Type of drive (RP06, etc)\n\
  log=<path> 	Log filespec (optional, defaults to stderr)\n\
  verbose	Verbose (optional)\n\
";



/* Switch parameters */
int sw_tdtest;
int sw_peot;
int sw_verbose;
int sw_maxsec;
int sw_maxfile;
char *sw_logpath;
FILE *logf;

#define DBGFLG sw_verbose

struct devdk {
	char *d_pname;	/* Print name, "In" or "Out" */
	char *d_path;	/* Disk drive path spec */
	int d_isdisk;		/* NZ if hardware device, else virtual */
	int d_fmt;		/* Format to use */
	long d_totsec;		/* Total # sectors */
	struct vdk_unit d_vdk;	/* Virtual disk info */
	struct diskconf d_dcf;
};
struct devdk dvi = { "In" };
struct devdk dvo = { "Out" };

/* Values for d_isdisk */
#define MTYP_NULL	0	/* Null device */
#define MTYP_VIRT	1	/* Virtual disk */
#define MTYP_HARD	2	/* Hard raw disk partition */

char *mtypstr[] = {
	"nulldev",
	"virtual",
	"hard"
};

int cmdsget(int ac, char **av);
int docopy(void);
int zerosector(w10_t *wp, int nwds);

int devopen(struct devdk *d, int wrtf);
int devclose(struct devdk *d);
int devread(struct devdk *d, long int daddr, w10_t *buff);
int devwrite(struct devdk *d, long int daddr, w10_t *buff);

void swerror(char *fmt, ...);
void efatal(char *errmsg);
void errhan(struct vdk_unit *t, char *s);

/* For now, must include VDISK source directly, so as to avoid compile-time
** switch conflicts (eg with KLH10).
*/
#include "vdisk.c"


static int partyp(char *cp, struct diskconf *dcf)
{
    register struct diskconf *dc;

    for (dc = diskconfs; dc->dcf_name[0]; ++dc) {
	if (strcasecmp(cp, dc->dcf_name) == 0) {
	    *dcf = *dc;			/* Found match, won */
	    return TRUE;
	}
    }
    return FALSE;			/* Unknown disk type */
}

/* Parse disk format -- something that VDISK understands.
*/
static char *fmttab[] = {
# define vdk_fmt(i,n,c,s,f,t) n
	VDK_FORMATS
# undef vdk_fmt
};

static int parfmt(char *cp, int *afmt)
{
    register int i;

    for (i = 0; i < VDK_FMT_N; ++i) {
	if (strcasecmp(cp, fmttab[i]) == 0) {
	    *afmt = i;			/* Found match, won */
	    return TRUE;
	}
    }
    return FALSE;			/* Unknown disk format */
}

/* Error handling */

/* Copied from OSDSUP.C */

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
#if !CENV_SYS_XBSD
	extern int sys_nerr;
	extern char *sys_errlist[];
#endif
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

void errhan(struct vdk_unit *t, char *s)
{
    fprintf(logf, "; %s: %s\n", t->dk_devname, s);
}


void efatal(char *errmsg)	/* print error message and exit */
             		/* error message string */
{
    fflush(stdout);
    fprintf(logf, "\n?%s\n",errmsg);
    exit(1);
}


int
main(int argc, char **argv)
{
    int ret;

    logf = stderr;
    signal(SIGINT, exit);	/* Allow int to terminate log files etc */

    dvi.d_fmt = dvo.d_fmt = -1;

    if (ret = cmdsget(argc, argv))	/* Parse and handle command line */
	exit(ret);


    if (!sw_logpath)
	logf = stderr;
    else {
	if ((logf = fopen(sw_logpath, "w")) == NULL) {
	    logf = stderr;
	    fprintf(logf, "; Cannot open log file \"%s\", using stderr.\n",
				sw_logpath);
	}
    }

    /* Set up defaults for devices, and log all params if requested */
    dvi.d_totsec = dvo.d_totsec =
		dvi.d_dcf.dcf_nsec *
		dvi.d_dcf.dcf_ntrk *
		dvi.d_dcf.dcf_ncyl;
    if (sw_verbose) {
	fprintf(logf, ";  Input disk spec \"%s\" (Type: %s) Format: %s\n",
				dvi.d_path, mtypstr[dvi.d_isdisk],
				fmttab[dvi.d_fmt]);
	fprintf(logf, "; Output disk spec \"%s\" (Type: %s) Format: %s\n",
				dvo.d_path, mtypstr[dvo.d_isdisk],
				fmttab[dvo.d_fmt]);

	/* Show config info here */
	fprintf(logf, "; Drive type: %s\n", dvi.d_dcf.dcf_name);
	fprintf(logf, ";   %ld sectors, %d wds/sec\n", dvi.d_totsec,
					dvi.d_dcf.dcf_nwds);
	fprintf(logf, ";   (%d secs, %d trks, %d cyls\n",
		dvi.d_dcf.dcf_nsec,
		dvi.d_dcf.dcf_ntrk,
		dvi.d_dcf.dcf_ncyl);
	if (sw_logpath)
	    fprintf(logf, "; Using logging path %s\n", sw_logpath);
    }

    /* Open I/O files as appropriate */
    if (!devopen(&dvi, FALSE))	/* Open for reading */
	exit(1);
    if (!devopen(&dvo, TRUE))	/* Open for writing */
	exit(1);

    /* Do it! */
    fprintf(logf, "; Copying from \"%s\" to \"%s\"...\n", dvi.d_path,
		dvo.d_path ? dvo.d_path : NULLDEV);
    if (ret = docopy())
	ret = devclose(&dvo);
    else (void) devclose(&dvo);

    if (!ret) fprintf(logf, "; Stopped unexpectedly.\n");

#if 0
  {
    register struct devdk *d;
    for (d = &dvi; d; d = (d == &dvi) ? &dvo : NULL) {
	if (d->d_isdisk)
	    fprintf(logf, "; %s:  %d+%d errs, %d secs, %ld bytes\n",
			d->d_pname, d->mta_herr, d->mta_serr,
			d->d_secs, d->d_tloc);
    }
  }
#endif

    fclose(logf);
    exit(ret ? 0 : 1);
}

static char pagsym[4] = { '.', '-', '=', '#'};

int docopy(void)
{
    int err;
    long nsect = 0;
    w10_t wbuff[512];
    int nwrt = 0;

    if (DBGFLG)
	fprintf(logf, "; Pages:\n");

    for (; nsect <= dvi.d_totsec;) {
	
	err = devread(&dvi, nsect, wbuff);	/* Get a sector  */
	if (!err) {
	    fprintf(logf, "; Aborting loop, last err: %s\n", os_strerror(-1));
	    return 0;
	}

	/* See whether there's any data in sector or not.
	** If none, don't write it out!
	** Later, always write if device is "hard".
	*/
	if (!zerosector(wbuff, 128)) {
	    /* Copy results to output device */
	    nwrt++;
	    err = devwrite(&dvo, nsect, wbuff);	/* Write a sector  */
	    if (!err) {
		fprintf(logf, "; Aborting loop, last err: %s\n",
					 os_strerror(-1));
		return 0;
	    }
	}

	++nsect;

	/* Hack to show nice pattern, one char per 4-sector page */
	if (DBGFLG) {
	    if ((nsect & 03) == 0) {
		putc(pagsym[nwrt&03], logf);
		nwrt = 0;
	    }
	}
    }

    if (DBGFLG)
	fprintf(logf, "\n");
    return TRUE;
}

int zerosector(register w10_t *wp, register int nwds)
{
    for (; --nwds >= 0; ++wp)
	if (LHGET(*wp) || RHGET(*wp))
	    return FALSE;
    return TRUE;
}


int swerrs = 0;

void swerror(char *fmt, ...)
{
    ++swerrs;
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    putc('\n', stderr);
}

int
cmdsget(int ac, char **av)
{
    register char *cp, *arg;
    register int plen;
    struct devdk *d;

    dvi.d_isdisk = dvo.d_isdisk = MTYP_NULL;

    while (--ac > 0 && (cp = *++av)) {
	if (arg = strchr(cp, '='))	/* If arg furnished for param, */
	    *arg++ = '\0';		/* split arg off */
	if ((plen = strlen(cp)) <= 0)
	    break;			/* Bad param */

	/* Now identify parameter.  No case folding for now, sigh */
	if (*cp == 'i' || *cp == 'o') {
	    /* Handle {i,o}{t,d,f,b} */
	    if (!arg) {
		swerror("Parameter requires arg: \"%s\"", *av);
		continue;
	    }
	    d = (*cp == 'i') ? &dvi : &dvo;
	    switch (*++cp) {
		case 'p':
		    if (strlen(cp) > 2) break;
		    if (d->d_path) {
			swerror("Param already specified: \"%s\"", *av);
			continue;
		    }
		    d->d_path = arg;
		    switch (*++cp) {
			case '\0':
			case 'v': d->d_isdisk = MTYP_VIRT; continue;
			case 'h': d->d_isdisk = MTYP_HARD; continue;
			/* default falls thru to fail */
		    }
		    break;
		case 'f':
		    if (strcmp(cp, "fmt")!=0)
			break;
		    if (d->d_fmt != -1) {
			swerror("Param already specified: \"%s\"", *av);
			continue;
		    }
		    if (!parfmt(arg, &(d->d_fmt))) {
			swerror("Unknown format: \"%s\"", arg);
			continue;
		    }
		    continue;

		/* Default just drops thru to fail */
	    }
	    swerror("Unknown parameter \"%s\"", *av);
	    continue;
	}

	if (strcmp(cp, "drive")==0 || strcmp(cp, "dt")==0) {
	    if (!partyp(arg, &(dvi.d_dcf)))
		swerror("Unknown drive type: \"%s\"", arg ? arg : "");
	    continue;

	} else if (strcmp(cp, "log")==0) {
	    if (!(sw_logpath = arg))
		swerror("Bad arg to log: \"\"");
	    continue;

	} else if (strcmp(cp, "verbose")==0 || strcmp(cp, "v")) {
	    sw_verbose = TRUE;
	    continue;
	}

	swerror("Unknown parameter \"%s\"\n%s", *av, nusage);
	return 1;
    }

    /* Clean up params */
    if (dvi.d_dcf.dcf_nwds)
	dvo.d_dcf = dvi.d_dcf;	/* Copy drive type params */
    else
	swerror("Drive type must be specified");
    if (dvi.d_fmt == -1)
	swerror("Input pack format must be specified");
    if (dvo.d_fmt == -1)
	swerror("Output pack format must be specified");

    /* Check for any parameter errors */
    if (swerrs) {
	fprintf(stderr, "%s", nusage);
	return swerrs;
    }

    return 0;
}

/* Generic device routines (null, virtual, and real)
**	Open, Close, Read, Write, Write-EOF, Write-EOT.
*/

int devopen(register struct devdk *d, int wrtf)
{
    char *opath, *path;

    if (!(opath = d->d_path) || !*opath) {
	fprintf(logf, "; Null mount path\n");
	return FALSE;
    }
    path = malloc(strlen(opath)+1);
    strcpy(path, opath);

    /* Set up config vars */
    vdk_init(&(d->d_vdk), errhan, NULL);

    d->d_vdk.dk_format = d->d_fmt;
    strcpy(d->d_vdk.dk_devname, d->d_dcf.dcf_name);
    d->d_vdk.dk_ncyls = d->d_dcf.dcf_ncyl;
    d->d_vdk.dk_ntrks = d->d_dcf.dcf_ntrk;
    d->d_vdk.dk_nsecs = d->d_dcf.dcf_nsec;
    d->d_vdk.dk_nwds  = d->d_dcf.dcf_nwds;

    if (!vdk_mount(&(d->d_vdk), path, wrtf)) {
	fprintf(logf, "; Cannot mount device \"%s\": %s\n",
		    path, os_strerror(d->d_vdk.dk_err));
	free(path);
	return FALSE;
    }
    free(path);

    return TRUE;
}

int devclose(struct devdk *d)
{
    int res = TRUE;

    if (DBGFLG && d->d_path)
	fprintf(logf, "; Closing \"%s\"\n", d->d_path);

    if (d->d_isdisk) {
	res = vdk_unmount(&(d->d_vdk));		/* Close real disk */
    }

    /* Force us to forget about it even if above stuff failed */
    d->d_isdisk = FALSE;
    return res;
}


/* Read from device
**	Returns 1 if read something
**	Returns 0 if read nothing or error
*/

int devread(struct devdk *d, long int daddr, w10_t *buff)
{
    int nsec;

#if 0
    if (DBGFLG)
	fprintf(logf, "; read daddr=%ld\n", daddr);
#endif

    nsec = vdk_read(&d->d_vdk, buff, (uint32)daddr, 1);

    if (d->d_vdk.dk_err
      || (nsec != 1)) {
	fprintf(logf, "; read error on %s: %s\n",
		    d->d_vdk.dk_filename, os_strerror(d->d_vdk.dk_err));
	return FALSE;
    }
    return TRUE;
}

/* Write to device.
*/
int devwrite(struct devdk *d, long int daddr, w10_t *buff)
{
    int nsec;

#if 0
    if (DBGFLG)
	fprintf(logf, "; write daddr=%ld\n", daddr);
#endif

    nsec = vdk_write(&d->d_vdk, buff, (uint32)daddr, 1);

    if (d->d_vdk.dk_err
      || (nsec != 1)) {
	fprintf(logf, "; write error on %s: %s\n",
		    d->d_vdk.dk_filename, os_strerror(d->d_vdk.dk_err));
	return FALSE;
    }

    return TRUE;
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
