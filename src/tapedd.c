/* TAPEDD.C - Utility to copy tapes
*/
/* $Id: tapedd.c,v 2.5 2002/03/28 16:52:31 klh Exp $
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
 * $Log: tapedd.c,v $
 * Revision 2.5  2002/03/28 16:52:31  klh
 * First pass at using LFS (Large File Support)
 *
 * Revision 2.4  2002/03/10 22:17:02  klh
 * Fix LOGTAPE macro for virtual format case
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*	TAPEDD is mainly used to copy tapes onto disk and make
**	further tape copies, either directly from the original tapes
**	or from the disk version.  It was previously called TDCOPY;
**	TAPEDD is its new, portable incarnation AND HAS NOT BEEN
**	TESTED very much yet.  Be careful!
**
** In order to represent the disk version accurately, a "tape directory"
** is used to describe the format of the raw data, including record
** lengths and tapemarks.  This permits anything that understands the
** tape directory to read the raw-data file as if it were an actual tape.
**
** In the absence of properly implemented pseudo-devices on Unix,
** the next step would be to provide a library-type interface such that
** the routines can be applied either to on-disk files or actual tapes,
** without needing to know which is which.  VMTAPE.C is a step towards
** this; OSDTAP.C could complete it.
*/

#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>		/* exit() */
#include <errno.h>
#include <stdarg.h>		/* For error-reporting functions */
#include <sys/file.h>		/* For open() flags */

#include "rcsid.h"
#include "cenv.h"
#include "vmtape.h"		/* Include virtual magtape stuff */

#if CENV_SYS_T20
# include <jsys.h>
# include <macsym.h>		/* FLD macros */
# define char8 _KCCtype_char8
# define CENV_SYSF_STRERROR 1
# define NULLDEV "NUL:"
# define FD_STDIN 0
# define FD_STDOUT 1

#elif CENV_SYS_UNIX
# include <unistd.h>		/* Basic Unix syscalls */ 
# include <sys/types.h>
# include <sys/ioctl.h>
# include <sys/mtio.h>
# define char8 unsigned char
# define O_BSIZE_8 0
# define NULLDEV "/dev/null"
# define FD_STDIN 0
# define FD_STDOUT 1
# define strCMP strcmp		/* Temporary compat hack */
#endif


#define MAXRECSIZE (1L<<16)	/* was ((15*518*5)+512) */
#define FNAMSIZ 200

#define TRUE 1
#define FALSE 0

#ifdef RCSID
 RCSID(tapedd_c,"$Id: tapedd.c,v 2.5 2002/03/28 16:52:31 klh Exp $")
#endif

/* For now, must include VMT source directly, so as to avoid compile-time
** switch conflicts (eg with KLH10).
*/
#include "vmtape.c"

/* New TAPEDD parameters */

char nusage[] = "\
Usage: tapedd <params>\n\
  itX=<path>	(Required) Input Tape device, where 'X' is optional\n\
		drive spec: (defaults to 'h')\n\
		     h - Half-inch magtape drive (default)\n\
		     q - QIC (quarter-inch cartridge) drive\n\
		     8 - 8mm drive\n\
		     4 - 4mm DAT drive\n\
		    vF - Virtual tape & drive, where 'F' is optional\n\
			format spec: (defaults based on file extension)\n\
			   r - Raw format, paired with control file\n\
			   s - TPS format\n\
			   e - TPE format\n\
			   c - TPC format\n"
#if VMTAPE_ITSDUMP
"			   i - (read-only) ITS DUMP tapedir\n"
#endif
#if VMTAPE_T20DUMP
"			   6 - (read-only) TOPS-10/20 DUMPER V6 tapedir\n"
#endif
"\
  otX=<path>	(Required) Output Tape device, X as above\n\
  {i,o}c=<path>	alternate tape Control file (old id=,od=)\n\
  {i,o}f=<path>	alternate raw data file\n\
  {i,o,}bs=<n>	Block size (record length)\n\
  log=<path> 	Log filespec (defaults to stderr)\n\
  rskip=<#>	Skip # input records\n\
  fskip=<#>	Skip # input files/tapemarks\n\
  rcnt=<#>	Max # records to write\n\
  fcnt=<#>	Max # files/tapemarks to write\n\
  peot		Use physical EOT, ignore logical EOT (double tapemark)\n\
  test		Parse control file, output result to stdout\n\
  verbose	Verbose\n\
";


/* Elaboration on new TAPEDD parameter switches:

--REQUIRED--
	{i,o}t*=<path>	Tape path spec (implies both control & data)
--OPTIONAL--
	{i,o}f=<path>		Tape data spec
	{i,o}c=<path>		Tape control (description) spec

Defaults:
	If a device is specified (one of 4/8/q/h), no control file is used
		unless one is specified with {i/o}d.
		An {i/o}f spec is redundant but will change the path without
		changing the device type.
	If a virtual tape is specified (v), both the control file and
		the data file pathnames are derived from that spec.
		Either derivation may be overriden with the appropriate
		additional spec ({i/o}f or {i/o}d).
Possible special cases:
	If no ot*= spec is given, data output is assumed to be the null device.
		This is mainly useful when generating a tape-desc file from
		the input; od= can specify where it goes.
		of= is ignored.
	If no it*= spec is given, no data is input, although a tape-desc
		file can be given with id=.
		if= is ignored.

	Thus, the equivalent of "test" is:
		tapedd id=foo od=

Whenever a null filename is specified (eg with "of="), it is understood
	to mean standard input or output (depending on whether the spec
	was for input or output).

More special cases (the user is not expected to know this, but in case
any of them are tried, this table is an attempt to figure out what
would be logical behavior).  Note "itD=" means "it{h,q,8,4}" as opposed
to "itv=".

COMMAND-LINE	 DEV
   SPECS	STRUCT	INTERPRETATION
it*= id= if=	p d  r
 -    -   -	- -  -	Error: No input of any kind (null tape)
 -    -   x	- -  -	Error, ambiguous (later can test or make assumptions)
 -    tdr -	- d  -	Error unless also have od= (testing tapedesc parse)
 -    tdr x	- -  -	Error, ambiguous (later can test or make assumptions)
itD=T -   -	T -  T	OK: Reads device: type D, dev T.
itD=T -   xdv	T -  x	OK: Reads device: type D, dev x. (T ignored)
itD=T tdr -	T d  T	OK: Reads device: type D, dev T, checks vs dir d [*]
itD=T tdr xdv	T d  x	OK: Reads device: type D, dev x, checks vs dir d [*]
itv=V -   -	V Vd Vr	OK: Reads virtual: dir Vd, raw Vr.
itv=V -   raw	V Vd r	OK: Reads virtual: dir Vd, raw r.
itv=V tdr -	V d  Vr	OK: Reads virtual: dir  d, raw Vr.
itv=V tdr raw	V d  r	OK: Reads virtual: dir  d, raw r.

			[*] = Reads records of sizes and numbers
				given by tapedir; warns if any clashes?
				Or scan normally, cross-check vs tapedir.

ot*= od= of=	p d  r
 -    -   -	- -  -	Error: No output of any kind (useless)
 -    -   x	- -  -	Error, ambiguous? (or test or assume 1/2"?)
 -    tdr -	- d  -	OK: just outputs tapedir.
 -    tdr x	- -  -	Error, ambiguous? (or test or assume 1/2"?)
otD=T -   -	T -  T	OK: Writes device: type D, dev T.
otD=T -   xdv	T -  x	OK: Writes device: type D, dev x. (T ignored)
otD=T tdr -	T d  T	OK: Writes device: type D, dev T, plus tapedir d
otD=T tdr xdv	T d  x	OK: Writes device: type D, dev x, plus tapedir d
otv=V -   -	V Vd Vr	OK: Writes virtual: dir Vd, raw Vr.
otv=V -   raw	V Vd r	OK: Writes virtual: dir Vd, raw r.
otv=V tdr -	V d  Vr	OK: Writes virtual: dir  d, raw Vr.
otv=V tdr raw	V d  r	OK: Writes virtual: dir  d, raw r.


*/
#if 0  /* Old TDCOPY switches, for posterity */
char usage[] = "\
Usage: tdcopy <sws> <tapedir filespec>\n\
	-r        Read tape only (make tapedir, no copy)\n\
	-w        Write tape (from tapedir)\n\
	-c        Copy tapes directly\n\
	-t        Test tapedir (parse tapedir, rewrite to stdout)\n\
	-i <path> Input filespec (- is stdin)\n\
	-o <path> Output filespec (- is stdout)\n\
	-l <path> Log filespec (required if -i or -o use -)\n\
	-q        Previous -i or -o filespec is QIC drive\n\
	-h        Previous -i or -o filespec is 1/2\" drive\n\
	-8        Previous -i or -o filespec is 8mm drive\n\
	-b <#>    Block (record) size to use (esp for QIC)\n\
	-v        Verbose\n\
	-n <#>    # of records to do\n\
	-m <#>    # of files (tapemarks) to do\n\
	-p        Use physical EOT (ignore logical)\n\
";
#endif /* 0 */


typedef unsigned long rsiz_t;	/* Type to hold size of record */

/* Switch parameters */
int  sw_tdtest = FALSE;
int  sw_peot = FALSE;
int  sw_verbose = FALSE;
long sw_maxrec = 0;
long sw_maxfile = 0;
long sw_recskip = 0;
long sw_fileskip = 0;
rsiz_t sw_bothsiz = 0;
char *sw_logpath = NULL;
FILE *logf = NULL;

struct dev {
	char *d_pname;	/* Print name, "In" or "Out" */
	char *d_path;	/* Tape drive path spec */
	char *d_cpath;	/* Control (descriptor) path spec */
	char *d_rpath;	/* Raw data path spec */
	int d_istape;		/* Tape device (hardware or virtual) */
	int d_vfmt;		/* If virtual, tape format to use */
	rsiz_t d_recsiz;	/* Block (record) size to use */
	char8 *d_buff, *d_iop;	/* Ptr to buffer loc, ptr into buffer */
	rsiz_t d_blen, d_buse;	/* Actual buffer length, # chars used */
	int d_bisdyn;		/* True if buff dynamically allocated */
	struct vmtape d_vmt;	/* Virtual magtape info */

	/* Hardware tape info */
	int d_fd;
	long	mta_herr,	/* Hard errors (unrecoverable) */
		mta_serr,	/* Soft errors (includes retries) */
		mta_frms;	/* # frames (bytes) read in record */
	int	mta_bot,	/* TRUE if BOT seen */
		mta_eot,	/* TRUE if EOT seen */
		mta_eof,	/* TRUE if EOF (tapemark) seen */
		mta_retry;	/* # times to retry a failing op */

	vmtpos_t d_tloc;	/* Location in bytes read/written from BOT */
	long d_recs,		/* # recs in tape so far */
		d_frecs,	/* # recs in current file so far */
		d_files,	/* # files (tapemarks) seen so far */
		d_herrs,	/* Hard errors (unrecoverable) */
		d_serrs;	/* Soft errors (includes retries) */
};
struct dev dvi = { "In" };
struct dev dvo = { "Out" };

/* Values for d_istape.  Those for d_vfmt come from vmtape.h */
#define MTYP_NULL	0	/* Null device */
#define MTYP_VIRT	1	/* Virtual magtape, d_vfmt has format */
#define MTYP_HALF	2	/* Half-inch reel magtape */
#define MTYP_QIC	3	/* Quarter-inch cartridge magtape */
#define MTYP_8MM	4	/* 8mm cartridge magtape */
#define MTYP_4MM	5	/* 4mm DDS/DAT cartridge magtape */
char *mtypstr[] = {
	"nulldev",
	"virtual",
	"1/2\"",
	"QIC",
	"8mm",
	"4mm"
};


int cmdsget(int ac, char **av);
int docopy(void);

int devbuffer(struct dev *d, char *buffp, rsiz_t blen);
int devopen(struct dev *d, int wrtf);
int devclose(struct dev *d);
int devread(struct dev *d);
int devwrite(struct dev *d, unsigned char *buff, rsiz_t len);

int devwerr(struct dev *d, int err);
int devweof(struct dev *d);
int devweot(struct dev *d);

int  os_mtopen(struct dev *dp, int wrtf);
int  os_mtread(struct dev *dp);
int  os_mtfsr(struct dev *dp);
void os_mtstatus(struct dev *dp, FILE *f);
int  os_mtclose(struct dev *dp);
int  os_mtwrite(struct dev *dp);
int  os_mtweof(struct dev *dp);
void os_mtclrerr(int fd);

void errhan(void *arg, struct vmtape *t, char *s);
void efatal(char *errmsg);
void swerror(char *fmt, ...);

#if CENV_SYS_T20
void t20status(struct dev *d, FILE *f, int swd, int cnt);
#endif

#if 0
static char *dupcstr();		/* Use the one from vmtape.c for now */
#endif

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

void errhan(void *arg, struct vmtape *t, char *s)
{
    fprintf(logf, "; %s: %s\n", t->mt_devname, s);
}


void efatal(char *errmsg)	/* print error message and exit */
             		/* error message string */
{
    fflush(stdout);
    fprintf(logf, "\n?%s\n",errmsg);
    exit(1);
}


static int do_tdtest(void);

int
main(int argc, char **argv)
{
    register struct dev *d;
    int ret;

    logf = stderr;
    signal(SIGINT, exit);	/* Allow int to terminate log files etc */

    vmt_init(&dvi.d_vmt, "TapeIn");
    dvi.d_vmt.mt_errhan = errhan;
    dvi.d_vmt.mt_errarg = &dvi.d_vmt;
    vmt_init(&dvo.d_vmt, "TapeOut");
    dvo.d_vmt.mt_errhan = errhan;
    dvo.d_vmt.mt_errarg = &dvo.d_vmt;

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

    /* Special test? */
    if (sw_tdtest) {
	exit(do_tdtest());
    }

    /* Set up defaults for devices, and log all params if requested */

    /* bs=, if specified, becomes default for both in and out */
    if (!dvi.d_recsiz) dvi.d_recsiz = sw_bothsiz;
    if (!dvo.d_recsiz) dvo.d_recsiz = sw_bothsiz;
    if (sw_verbose) {
#define LOGTAPE(d,name) \
	fprintf(logf, "; %s tape spec \"%s\" (Type: %s", \
			name, (d).d_path, mtypstr[(d).d_istape]); \
	if ((d).d_istape == MTYP_VIRT) \
	    fprintf(logf, " format: %s", vmt_fmtname((d).d_vfmt)); \
	fprintf(logf, ")\n")

	LOGTAPE(dvi, " Input");
	LOGTAPE(dvo, "Output");
#undef LOGTAPE

	if (dvi.d_recsiz)
	    fprintf(logf, ";  Input record size %ld\n", (long)dvi.d_recsiz);
	if (dvo.d_recsiz)
	    fprintf(logf, "; Output record size %ld\n", (long)dvo.d_recsiz);
	if (sw_maxfile)
	    fprintf(logf, "; Max tapemarks (files) to process: %ld\n", sw_maxfile);
	if (sw_maxrec)
	    fprintf(logf, "; Max records to process: %ld\n", sw_maxrec);
	if (sw_logpath)
	    fprintf(logf, "; Using logging path %s\n", sw_logpath);
    }

    /* Open I/O files as appropriate */
    if (!devopen(&dvi, FALSE))	/* Open for reading */
	exit(1);
    if (!devopen(&dvo, TRUE))	/* Open for writing */
	exit(1);

    /* Set up buffering.  This is somewhat tricky due to all the
    ** possible situations; future parameters may complicate it by specifying
    ** record truncation, padding, or conversion of some kind.
    ** For now, the primary intent is to ensure we have a buffer large
    ** enough to handle the maximum expected record size, while still
    ** retaining the capability of asking for less than the maximum
    ** possible (64K) in case memory usage is a concern (less and less likely
    ** these days).
    **
    ** Since there are no funny parameters, currently just one buffer
    ** is used, whichever of input or output is larger.
    */
    if (dvi.d_blen < dvo.d_blen)
	dvi.d_blen = dvo.d_blen;
    else if (dvi.d_blen > dvo.d_blen)
	dvo.d_blen = dvi.d_blen;

    if (!devbuffer(&dvi, (char *)NULL, dvi.d_blen)
      || !devbuffer(&dvo, (char *)dvi.d_buff, dvi.d_blen)) {
	exit(1);
    }


    /* Do it! */
    fprintf(logf, "; Copying from \"%s\" to \"%s\"...\n", dvi.d_path,
		dvo.d_path ? dvo.d_path : NULLDEV);
    if (!docopy()) {
	fprintf(logf, "; Stopped unexpectedly.\n");
	ret = FALSE;
    }
    if (!devclose(&dvo)) {
	fprintf(logf, "; Error closing output.\n");
	ret = FALSE;
    }
    for (d = &dvi; d; d = (d == &dvi) ? &dvo : NULL) {
	if (d->d_istape)
	    fprintf(logf, "; %3s: %ld+%ld errs, %ld files, %ld recs, %"
		    VMTAPE_POS_FMT "d bytes\n",
			d->d_pname, d->mta_herr, d->mta_serr,
			d->d_files, d->d_recs, d->d_tloc);
    }

    fclose(logf);
    exit(ret ? 0 : 1);
}


int docopy(void)
{
    int err, ret = TRUE;
    int done = FALSE;

    /* If skipping input, do that first */
    if (sw_fileskip) {
	/* Skip files */
	while ((err = devread(&dvi)) >= 0) {
	    if (vmt_isateof(&dvi.d_vmt)) {	/* Hit tapemark? */
		dvi.d_files++;		/* Bump count of files */
		dvi.d_frecs = 0;
		if (dvi.d_files >= sw_fileskip)
		    break;
	    }
	    if (vmt_isateot(&dvi.d_vmt))
		break;
	}
    }
    if (sw_recskip && (err >= 0) && !vmt_isateot(&dvi.d_vmt)) {
	/* Skip records (after skipping files) */
	while ((err = devread(&dvi)) >= 0) {
	    if (vmt_framecnt(&dvi.d_vmt) && (dvi.d_frecs >= sw_recskip))
		break;
	    if (vmt_isateof(&dvi.d_vmt)) {	/* Hit tapemark? */
		dvi.d_files++;		/* Bump count of files */
		dvi.d_frecs = 0;
		break;
	    }
	    if (vmt_isateot(&dvi.d_vmt))
		break;
	}
    }
    if (err >= 0 && !vmt_isateot(&dvi.d_vmt)) while (!done) {
	/* Get a record/tapemark */
	if ((err = devread(&dvi)) < 0)
	    break;

	/* Now copy results to output device */
	if (vmt_framecnt(&dvi.d_vmt)) {
	    if (!devwrite(&dvo, dvi.d_buff,
			  (rsiz_t)vmt_framecnt(&dvi.d_vmt))) {
		fprintf(logf, "; Stopped due to output write error: %s\n",
			os_strerror(-1));
		ret = FALSE;
		break;
	    }
	    if (sw_maxrec && dvo.d_recs >= sw_maxrec)
		done = TRUE;		/* Stop after all checks done */
	}

#if 0
/* XXX This needs more thought/work so it isn't always called each time
   an EOF is encountered by devread() of virtual tape.
*/
	if (err == FALSE)
	    devwerr(&dvo, err);
#endif

	if (vmt_isateof(&dvi.d_vmt)) {	/* Hit tapemark? */
	    long ofrecs = dvi.d_frecs;	/* Remember # recs in current file */
	    int wwon;

	    dvi.d_files++;		/* Bump count of files */
	    dvi.d_frecs = 0;
	    wwon = devweof(&dvo);

	    if (ofrecs == 0 && !sw_peot) {	/* Double tapemark? */
		--dvi.d_files;		/* Don't count as another file */
		if (wwon) --dvo.d_files;
		break;			/* Double tapemark == stop */
	    }
	    if (sw_maxfile && dvo.d_files >= sw_maxfile)
		break;
	}

	if (vmt_isateot(&dvi.d_vmt))	/* If input tape has reached EOT, */
	    break;			/* quit loop, we're done! */
    }

    if (err < 0) {
	fprintf(logf, "; Stopped due to input read error: %s\n",
		os_strerror(-1));
	ret = FALSE;
    }

    /* Copy either done or aborted.  Finalize output.  This call
    ** ensures that all output buffers are flushed and the tape dir, if
    ** one, is written out.
    */
    devweot(&dvo);			/* Write "EOT" */

    return ret;
}


/* Special test hack.
** Read in the input tape control file, if one, and output
** parsed result to stdout.
** This is used to verify that parsing works.
*/
static int
do_tdtest(void)
{
    FILE *tdif, *tdof;
    char *errstr = NULL;

    if (!dvi.d_cpath) {
	fprintf(logf, "; No input tape directory specified\n");
	return 1;
    }
    if (!dvo.d_cpath) {
	fprintf(logf, "; No output tape directory specified\n");
	return 1;
    }

    if (dvi.d_path && dvi.d_istape == MTYP_VIRT) {
	struct vmtattrs v;
	v.vmta_mask = VMTA_PATH | VMTA_FMTREQ;
	strncpy(v.vmta_path, dvi.d_path, sizeof(v.vmta_path));
	v.vmta_fmtreq = dvi.d_vfmt;
	if (!vmt_attrmount(&dvi.d_vmt, &v))	/* Try mounting RO */
	    errstr = "vmt error";
    } else {				/* Sigh, painful method */
	if (dvi.d_cpath[0] == '\0')
	    tdif = stdin;
	else if ((tdif = fopen(dvi.d_cpath, "r")) == NULL) {
	    errstr = os_strerror(-1);
	}
	if (!errstr && !tdr_scan(&dvi.d_vmt, tdif, dvi.d_cpath))
	    errstr = "parse error";
    }

    if (errstr) {
	fprintf(logf,"; Problem with tape desc file \"%s\": %s\n",
					dvi.d_cpath, errstr);
	return 1;
    }

    /* If here, parse of tape desc file won, so output it. */
    if (strcmp(dvo.d_cpath, "") == 0) {
	tdof = stdout;
    } else if ((tdof = fopen(dvo.d_cpath, "w")) == NULL) {
	fprintf(logf,"Cannot open tape desc file \"%s\": %s\n",
					dvo.d_cpath, os_strerror(-1));
	return FALSE;
    }

    td_fout(&dvi.d_vmt.mt_tdr, tdof, "<none>");	/* Just show results */
    return 0;
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
    struct dev *d;
    long ltmp;

    dvi.d_istape = dvo.d_istape = MTYP_NULL;

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
		case 't':
		    if (strlen(cp) > 3) break;
		    if (d->d_path) {
			swerror("Param already specified: \"%s\"", *av);
			continue;
		    }
		    d->d_path = arg;
		    switch (*++cp) {
		    case '\0':
		    case 'h': d->d_istape = MTYP_HALF; continue;
		    case 'q': d->d_istape = MTYP_QIC;  continue;
		    case '8': d->d_istape = MTYP_8MM;  continue;
		    case '4': d->d_istape = MTYP_4MM;  continue;
		    case 'v': d->d_istape = MTYP_VIRT;
			    switch (*++cp) {
			    case '\0':
			    case 'u': d->d_vfmt = VMT_FMT_UNK; continue;
			    case 'r': d->d_vfmt = VMT_FMT_RAW; continue;
			    case 'c': d->d_vfmt = VMT_FMT_TPC; continue;
			    case 'e': d->d_vfmt = VMT_FMT_TPE; continue;
			    case 's': d->d_vfmt = VMT_FMT_TPS; continue;
#if VMTAPE_ITSDUMP
			    case 'i': d->d_vfmt = VMT_FMT_ITS; continue;
#endif
#if VMTAPE_T20DUMP
			    case '6': d->d_vfmt = VMT_FMT_DUMPER6; continue;
#endif
			    default:
				swerror("Unknown format spec: \"%s\"", cp);
				break;	/* Drop thru to fail */
			    }
		    default:
			swerror("Unknown device spec: \"%s\"", cp);
			break;	/* Drop thru to fail */
		    }
		    break;
		case 'c':
		case 'd':
		    if (*++cp) break;		/* Ensure param name done */
		    if (d->d_cpath) {
			swerror("Param already specified: \"%s\"", *av);
			continue;
		    }
		    d->d_cpath = arg;
		    continue;
		case 'f':
		    if (*++cp) break;		/* Ensure param name done */
		    if (d->d_rpath) {
			swerror("Param already specified: \"%s\"", *av);
			continue;
		    }
		    d->d_rpath = arg;
		    continue;
		case 'b':
		    if (*++cp != 's' || *++cp)	/* Ensure param name done */
			break;
		    if (d->d_recsiz) {
			swerror("Param already specified: \"%s\"", *av);
			continue;
		    }
		    if (sscanf(arg, "%ld", &ltmp) != 1)
			swerror("Bad arg to %s: \"%s\"", *av, arg);
		    d->d_recsiz = ltmp;
		    continue;

		/* Default just drops thru to fail */
	    }
	    swerror("Unknown parameter \"%s\"", *av);
	    continue;
	}
	switch (plen) {
	case 1:
	    if (*cp == 'v') {
		sw_verbose = TRUE;
		continue;
	    }
	    break;
	case 2:
	    if (strcmp(cp, "bs")==0) {
		if (!arg || sscanf(arg, "%ld", &ltmp) != 1)
		    swerror("Bad arg to bs: \"%s\"", arg ? arg : "");
		sw_bothsiz = ltmp;
		continue;
	    }
	    break;
	case 3:
	    if (strcmp(cp, "log")==0) {
		if (!(sw_logpath = arg))
		    swerror("Bad arg to log: \"\"");
		continue;
	    }
	    break;
	case 4:
	    if (strcmp(cp, "rcnt")==0) {
		if (!arg || sscanf(arg, "%ld", &sw_maxrec) != 1)
		    swerror("Bad arg to rcnt: \"%s\"", arg ? arg : "");
		continue;
	    }
	    if (strcmp(cp, "fcnt")==0) {
		if (!arg || sscanf(arg, "%ld", &sw_maxfile) != 1) {
		    swerror("Bad arg to fcnt: \"%s\"", arg ? arg : "");
		}
		continue;
	    }
	    if (strcmp(cp, "peot")==0) {
		sw_peot = TRUE;
		continue;
	    }
	    if (strcmp(cp, "test")==0) {
		sw_tdtest = TRUE;
		continue;
	    }
	    break;
	case 5:
	    if (strcmp(cp, "rskip")==0) {
		if (!arg || sscanf(arg, "%ld", &sw_recskip) != 1)
		    swerror("Bad arg to rskip: \"%s\"", arg ? arg : "");
		continue;
	    }
	    if (strcmp(cp, "fskip")==0) {
		if (!arg || sscanf(arg, "%ld", &sw_fileskip) != 1) {
		    swerror("Bad arg to fskip: \"%s\"", arg ? arg : "");
		}
		continue;
	    }
	    break;
	case 7:
	    if (strcmp(cp, "verbose")==0) {
		sw_verbose = TRUE;
		continue;
	    }
	    break;
	}
	swerror("Unknown parameter \"%s\"\n%s", *av, nusage);
	return 1;
    }

    /* Ensure input/output specs make sense.  Must have either T or D */
    if (!dvi.d_path) {
	/* Normally an error, but allow it if id= and od= both
	** exist, without an ot= spec -- for testing tapedir scanning.
	*/
	if (dvi.d_cpath && !dvi.d_rpath
	  && !dvo.d_path && dvo.d_cpath && !dvo.d_rpath)
	    sw_tdtest = TRUE;
	else
	    swerror("Must have it= parameter");
    }
    if (!dvo.d_path && dvo.d_rpath) {
	/* Disallow of= without ot= for now, because type of output
	** device is ambiguous.
	*/
	swerror("Must have ot= to use of=");
    }

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

int devopen(register struct dev *d, int wrtf)
{
    char *cpath, *rpath;

    /* Generate default filenames in consistent fashion and open them.
    ** See table in comments near switch descriptions.
    */
    if (d->d_istape == MTYP_VIRT) {
#if 1	/* New regime */
	struct vmtattrs v;

	v.vmta_mask = VMTA_FMTREQ | VMTA_MODE | VMTA_PATH | VMTA_UNZIP;
	v.vmta_fmtreq = d->d_vfmt;
	v.vmta_mode = wrtf ? VMT_MODE_CREATE : VMT_MODE_RDONLY;
	v.vmta_unzip = TRUE;
	strncpy(v.vmta_path, d->d_path, sizeof(v.vmta_path));
	if (d->d_cpath) {
	    v.vmta_mask |= VMTA_CTLPATH;
	    strncpy(v.vmta_ctlpath, d->d_cpath, sizeof(v.vmta_ctlpath));
	}
	if (d->d_rpath) {
	    v.vmta_mask |= VMTA_DATPATH;
	    strncpy(v.vmta_datpath, d->d_rpath, sizeof(v.vmta_datpath));
	}
	if (!vmt_attrmount(&d->d_vmt, &v)) {	/* Try mounting it */
	    fprintf(logf, "; Couldn't mount %sput virtual tape: %s\n",
				d->d_pname, d->d_path);
	    return FALSE;
	}

#else	/* Old regime */
	/* Use path as cpath; generate rawdata filename */
	if (!(cpath = d->d_cpath)) {
	    /* Default cpath to path */
	    cpath = d->d_path;
	}
	if (!(rpath = d->d_rpath)) {
	    /* Default rpath to path.tap, unless path ends with .tdr
	    ** in which case replace the .tdr with .tap.
	    */
	    char *s;
	    rpath = malloc(strlen(d->d_path)+4+1);
	    if (!rpath) {
		fprintf(logf, "; Cannot malloc data filename\n");
		return FALSE;
	    }
	    strcpy(rpath, d->d_path);
	    if (!((s = strrchr(rpath, '.')) && (strcmp(s, ".tdr")==0)))
		    s = rpath + strlen(rpath);
	    strcpy(s, ".tap");
	}

	/* Defaults all set up, store them back in structure */
	d->d_cpath = cpath;
	d->d_rpath = rpath;

	/* Open up virtual files.  Need special hackery if either
	** the tapedesc or rawdata path was explicitly given.
	*/
	if (!vmt_xmount(&(d->d_vmt), d->d_cpath, d->d_rpath, wrtf)) {
	    fprintf(logf, "; Couldn't mount %sput virtual tape: %s\n",
				d->d_pname, d->d_path);
	    return FALSE;
	}
#endif


    } else {
	/* Default filenames for real device */
	cpath = d->d_cpath;		/* Desc file may not be wanted */
	if (!(rpath = d->d_rpath))
	    rpath = d->d_path;		/* Data file defaults to tapefile */

	/* Defaults all set up, store them back in structure */
	d->d_cpath = cpath;
	d->d_rpath = rpath;

	if (d->d_rpath) {
	    /* Open real tape device */
	    if (strcmp(d->d_rpath, "") == 0) {
		d->d_fd = wrtf ? FD_STDOUT : FD_STDIN;
	    } else if (!os_mtopen(d, wrtf)) {
		fprintf(logf, "; Cannot open %sput: %s\n",
				    d->d_pname, os_strerror(-1));
		return FALSE;
	    }
	}

	/* Set up VMT vars, with optional tape-info if specified.
	** Note virtual tape isn't set here; vmt_mount already did that.
	*/
	switch (d->d_istape) {
	    case MTYP_NULL:	d->d_vmt.mt_devname = "NUL";	break;
	    case MTYP_HALF:	d->d_vmt.mt_devname = "MTA";	break;
	    case MTYP_QIC:	d->d_vmt.mt_devname = "QIC";	break;
	    case MTYP_8MM:	d->d_vmt.mt_devname = "8MM";	break;
	    case MTYP_4MM:	d->d_vmt.mt_devname = "4MM";	break;
	}


	/* See if optional tapedesc for non-virtual drive.  If so,
	** fake out virtual tape code to think it has a tapedesc.
	** This is ugly and fragile.
	*/
	if (d->d_cpath) {

	    if (!(d->d_vmt.mt_filename = dupcstr(d->d_cpath))) {
		fprintf(logf, "; malloc failed for cpath dupcstr\n");
		return FALSE;
	    }
	    if (strcmp(d->d_cpath, "") == 0) {
		d->d_vmt.mt_ctlf = (wrtf ? stdout : stdin);
	    } else if ((d->d_vmt.mt_ctlf =
		    fopen(d->d_cpath, (wrtf ? "w" : "r"))) == NULL) {
		fprintf(logf,"Cannot open tape desc file \"%s\": %s\n",
					    d->d_cpath, os_strerror(-1));
		return FALSE;
	    }

	    /* Tape-desc file open, now parse it if reading */
	    if (!wrtf && !tdr_scan(&(d->d_vmt), d->d_vmt.mt_ctlf, d->d_cpath)) {
		fprintf(logf, "; Cannot parse tape desc\n");
		return FALSE;
	    }
	    /* Do final setup if writing */
	    if (wrtf && d->d_rpath) {
		if (!(d->d_vmt.mt_datpath = dupcstr(d->d_rpath))) {
		    fprintf(logf, "; malloc failed for rpath dupcstr\n");
		    return FALSE;
		}
	    }
	}

    }


    /* Set default buffer length */
    if (d->d_blen = d->d_vmt.mt_tdr.tdmaxrsiz) {
	if (d->d_recsiz) {		/* Explicit record size spec? */
	    if (d->d_blen <= d->d_recsiz)	/* If it's bigger, */
		d->d_blen = d->d_recsiz;	/* adjust quietly */
	    else				/* else warn user */
		fprintf(logf, "; Tapedir forcing larger %sput record size (%ld instead of %ld)\n",
			d->d_pname, (long)d->d_blen, (long)d->d_recsiz);

	}
    } else				/* No tapedir, use spec if any */
	d->d_blen = d->d_recsiz ? d->d_recsiz : MAXRECSIZE;

    return TRUE;
}

/* Set up device buffering.
**	Must be called after device already opened.
**	Probably will lose if called after I/O done.
** If buffp set, will use that with blen and not free it when closed.
** If buffp NULL, will allocate buffer of size blen, unless blen is 0
**	in which case it figures out a default.
*/
int devbuffer(struct dev *d, char *buffp, rsiz_t blen)
{
    if (blen <= 0) {
	blen = MAXRECSIZE;	/* No size spec, use a safe default */
	buffp = NULL;		/* Ensure any pointer arg is ignored */
    }

    d->d_bisdyn = FALSE;
    if (buffp == NULL) {
	/* Always round actual buffer size upwards to be modulo-512 in case
	** either input or output has to deal with QIC drives, otherwise the
	** unix I/O call will fail without even trying to do anything!
	*/
	blen = (blen + 511) & ~511;	/* Relies on fact 512 is power of 2 */
	if ((buffp = malloc(blen)) == NULL) {
	    fprintf(logf, "; Cannot malloc %sput buffer (size %ld)\n",
			d->d_pname, blen);
	    return FALSE;
	}
	d->d_bisdyn = TRUE;
    }
    d->d_blen = blen;
    d->d_buff = (char8 *)buffp;
    d->d_iop = d->d_buff;
    d->d_buse = 0;

    return TRUE;
}

int devclose(struct dev *d)
{
    int ret;

    if (d->d_istape == MTYP_VIRT) {
	ret = vmt_unmount(&d->d_vmt);	/* Close virtual tape */
    } else {
	ret = TRUE;
	if (d->d_rpath)
	    ret = os_mtclose(d);		/* Close real tape */
	if (d->d_cpath) {
	    struct vmtape *t = &d->d_vmt;	/* Close tapedesc */

	    rewind(t->mt_ctlf);	/* Rewind in case not the first update */
	    td_fout(&t->mt_tdr, t->mt_ctlf, t->mt_datpath);
	    fflush(t->mt_ctlf);
	    if (ferror(t->mt_ctlf))	/* Check to see if any errors */
		ret = FALSE;
	}
    }

    if (d->d_bisdyn && d->d_buff) {
	free((char *)(d->d_buff));
	d->d_buff = NULL;
    }
    return ret;
}


/* Read one data unit (record, tapemark, etc) from device
**	Returns 1 if read something
**	Returns 0 if read nothing, but no error
**	Returns -1 if error of some kind
*/

int devread(struct dev *d)
{
    int ret;

    if (d->d_istape == MTYP_VIRT) {
	/* Virtual tape, read in and change vmt params directly */
	ret = vmt_rget(&(d->d_vmt), d->d_buff, (size_t)d->d_blen);
    } else if (d->d_rpath) {
	/* Real tape, attempt physical read */

	/* Still to do?  Handle dir-only case, plus dir+dev case? */
	ret = os_mtread(d);		/* Attempt read from device */
	if (ret < 0) {
	    fprintf(logf, "; %sput device read error: %s\n",
		d->d_pname, os_strerror(-1));
	    return ret;
	}

	if (d->d_vmt.mt_frames = d->mta_frms) {		/* Read any data? */
	    d->d_buse = d->mta_frms;	/* Say this much of buffer used */
	    d->d_iop = d->d_buff;
	}
	d->d_vmt.mt_eof = d->mta_eof;
	d->d_vmt.mt_eot = d->mta_eot;
	d->d_vmt.mt_bot = d->mta_bot;

    } else {
	/* Null device, read nothing */
	d->d_vmt.mt_frames = 0;		/* No data read */
	d->d_vmt.mt_eof = TRUE;		/* At BOF, EOF, and EOT! */
	d->d_vmt.mt_eot = TRUE;
	d->d_vmt.mt_bot = TRUE;
	ret = TRUE;
    }


    if (d->d_vmt.mt_frames) {
	d->d_tloc += d->d_vmt.mt_frames;
	d->d_recs++;			/* Got a record */
	d->d_frecs++;			/* Bump rec cnt for current file */
    }
    return ret;
}

/* Write one record to device.
**	For now, note mtawrite assumes d_buff and d_buse are args.
*/
int devwrite(struct dev *d, unsigned char *buff, rsiz_t len)
{
    int ret = TRUE;

    if (len) {
	if (d->d_istape == MTYP_VIRT) {
	    if (ret = vmt_rput(&(d->d_vmt), buff, (size_t)len)) {
		d->d_tloc += len;
		d->d_recs++;
		d->d_frecs++;
	    }
	} else {
	    if (d->d_cpath)		/* Add to tapedir if one */
		td_recapp(&(d->d_vmt.mt_tdr), len, 1, 0);
	    if (d->d_rpath) {		/* Output to real device if one */
		d->d_recsiz = d->d_buse = len;
		ret = os_mtwrite(d);
		if (d->mta_frms) {
		    d->d_tloc += d->mta_frms;
		    d->d_recs++;
		    d->d_frecs++;
		}
	    }
	}
    }

    return ret;
}

int devwerr(struct dev *d, int err)
{
    if (d->d_istape == MTYP_VIRT)
	return vmt_eput(&(d->d_vmt), err);

    if (d->d_cpath) {		/* Have tapedir? */
	if (!td_recapp(&d->d_vmt.mt_tdr, (long)0, 0, err)) {
	    fprintf(logf, "devwerr: td_recapp malloc failed");
	    return FALSE;
	}

    }
    return TRUE;
}

int devweof(struct dev *d)
{
    int ret = TRUE;

    if (d->d_istape == MTYP_VIRT)
	ret = vmt_eof(&(d->d_vmt));
    else {
	if (d->d_cpath) {		/* Have tapedir? */
	    if (!td_recapp(&d->d_vmt.mt_tdr, (long)0, 1, 0)) {
		fprintf(logf, "devweof: td_recapp malloc failed");
		ret = FALSE;
	    }
	}
	if (d->d_rpath)
	    ret = os_mtweof(d) ? ret : FALSE;	/* Keep F if already F */
    }
    if (ret) {
	d->d_files++;		/* Bump count of files written */
	d->d_frecs = 0;
    }
    return ret;
}

int devweot(struct dev *d)
{
    if (d->d_istape == MTYP_VIRT)
	return vmt_eot(&(d->d_vmt));

    /* Otherwise devclose() will do all that's needed */

    return TRUE;
}

/* Magtape handling routines
*/

#if CENV_SYS_T20

/* MAGTAPE DEVICE STATUS BITS */

#define MT_ILW monsym("MT%ILW")	/* ILLEGAL WRITE */
#define MT_DVE monsym("MT%DVE")	/* DEVICE ERROR */
#define MT_DAE monsym("MT%DAE")	/* DATA ERROR */
#define MT_SER monsym("MT%SER")	/* SUPPRESS ERROR RECOVERY PROCEDURES */
#define MT_EOF monsym("MT%EOF")	/* EOF (FILE MARK) */
#define MT_IRL monsym("MT%IRL")	/* INCORRECT RECORD LENGTH */
#define MT_BOT monsym("MT%BOT")	/* BEGINNING OF TAPE */
#define MT_EOT monsym("MT%EOT")	/* END OF TAPE */
#define MT_EVP monsym("MT%EVP")	/* EVEN PARITY */
#define MT_DEN monsym("MT%DEN")	/* DENSITY (0 IS 'NORMAL') */
#define MT_CCT monsym("MT%CCT")	/* CHARACTER COUNTER */
#define MT_NSH monsym("MT%NSH")	/* DATA MODE OR DENS NOT SUPPORTED BY HDW */

static int acs[5];		/* For any JSYS that needs it */

#endif /* CENV_SYS_T20 */

int os_mtopen(struct dev *dp, int wrtf)
{
    int fd;

#if CENV_SYS_T20
    acs[1] = monsym("GJ%NEW")|monsym("GJ%SHT");
    acs[2] = (int)(dp->d_path-1);
    if (jsys(GTJFN, acs) <= 0)
	return FALSE;
    fd = (acs[1] &= RH);
    acs[2] = FLD(monsym(".GSDMP"),monsym("OF%MOD"))
		 | monsym("OF%RD");
    if (jsys(OPENF, acs) <= 0) {
	acs[1] = fd;
	jsys(RLJFN, acs);
	return FALSE;
    }
#else
    fd = open(dp->d_path, (wrtf ? O_RDWR : O_RDONLY)|O_BSIZE_8, 0600);
    if (fd < 0)
	return FALSE;
#endif
    dp->mta_retry = 5;		/* Default for now */
    dp->d_fd = fd;
    return TRUE;
}

int os_mtclose(struct dev *dp)
{
#if CENV_SYS_T20
#else
    return close(dp->d_fd);
#endif
}

int os_mtread(struct dev *dp)
{
    int retry = dp->mta_retry;
#if CENV_SYS_T20

    static int iov[2] = {0, 0};
    int flgs;

    dp->mta_frms = dp->mta_eof = 0;
    dp->mta_bot = dp->mta_eot = 0;
    for (; retry >= 0; --retry) {
	iov[0] = XWD(-(dp->d_blen/sizeof(int)),((int)(int *)(dp->d_buff))-1);
	acs[1] = dp->d_fd;
	acs[2] = (int)&iov;
	if (jsys(DUMPI, acs) > 0) {
	    fprintf(logf, "; Warning: DUMPI won for max rec size %ld!\n",
				(long)dp->d_blen);
	    dp->mta_frms = dp->d_blen;
	    return 1;
	}
	/* Error return is normal case, since record size varies */
	switch (acs[1]) {
	case monsym("IOX4"):	/* EOF (tape mark) */
	case monsym("IOX5"):	/* Device or data error (rec length) */
	    acs[1] = dp->d_fd;
	    if (jsys(GDSTS, acs) <= 0) {
		fprintf(logf, "; Tape GDSTS%% error: %s\n", os_strerror(-1));
		os_mtclrerr(dp->d_fd);
		dp->mta_herr++;
		return 0;
	    }
	    break;
	default:
	    fprintf(logf, "; Tape DUMPI%% error: %s\n", os_strerror(-1));
	    os_mtclrerr(dp->d_fd);
	    dp->mta_herr++;
	    return 0;
	}

	/* Analyze "error".  acs[2] has flags, [3] has count in LH */
	flgs = acs[2];
	t20status(dp, logf, flgs, acs[3]);
	dp->mta_frms = ((unsigned)acs[3]) >> 18;	/* Find cnt of data */

/* Opening in industry-compatible mode apparently works to force use of
** frame count, not word count */
/*	dp->mta_frms *= sizeof(int); */
	os_mtclrerr(dp->d_fd);
	if (flgs & (MT_DVE|MT_DAE)) {
	    fprintf(logf, "; Tape error: %s\n",
			(flgs & MT_DVE) ? "Device" : "Data");
	    dp->mta_serr++;
	    continue;				/* Try again */
	}
	if (flgs & MT_EOF)
	    dp->mta_eof = TRUE;
	if (flgs & MT_EOT)
	    dp->mta_eot = TRUE;
	if (flgs & MT_BOT)
	    dp->mta_bot = TRUE;
	return 1;
    }
    dp->mta_herr++;

#else /* !CENV_SYS_T20 */

    dp->mta_frms = dp->mta_eof = 0;
    dp->mta_bot = dp->mta_eot = 0;
    for (; retry >= 0; --retry) {
	switch (dp->mta_frms = read(dp->d_fd, dp->d_buff, dp->d_blen)) {
	case 0:		/* Tapemark */
	    dp->mta_eof = TRUE;
	    return TRUE;

	default:	/* Assume record read */
	    if (dp->mta_frms <= 0) {
	case -1:	/* Error */
		dp->mta_serr++;
		fprintf(logf, "; Tape read error: %s\n", os_strerror(-1));
		os_mtstatus(dp, logf);		/* Show full status */
		if (retry <= 0) {
		    if (os_mtfsr(dp)) {		/* Try spacing over 1 rec */
			retry = dp->mta_retry;
			dp->mta_herr++;
		    } else {
			fprintf(logf, "; Cannot proceed past read error, aborting...\n");
			return -1;
		    }
		}
		continue;			/* Try again */
	    }

	    /* Special hack for QIC -- pretend we read only the record size
	    ** specified, although actual read was greater in order to
	    ** keep I/O using modulo-512 counts.
	    */
	    if (dp->d_istape == MTYP_QIC) {
		if (dp->mta_frms > dp->d_recsiz) {
		    /* Adjust so devread() gets true tapeloc */
		    dp->d_tloc += dp->mta_frms - dp->d_recsiz;
		    dp->mta_frms = dp->d_recsiz;
		}
	    } else
		if (dp->mta_frms >= dp->d_blen)
		    fprintf(logf, "; Warning: read max rec size %ld!\n",
					dp->mta_frms);


	    return TRUE;
	}
    }
    dp->mta_frms = 0;
#endif

    return 0;		/* For now */
}

int os_mtwrite(struct dev *dp)
{
    int retry = dp->mta_retry;
#if CENV_SYS_T20
    static int iov[2] = {0, 0};
    int flgs;

    dp->mta_frms = dp->mta_eof = 0;
    dp->mta_bot = dp->mta_eot = 0;
    for (; retry >= 0; --retry) {
	iov[0] = XWD(-(dp->d_buse/sizeof(int)),((int)(int *)(dp->d_buff))-1);
	acs[1] = dp->d_fd;
	acs[2] = (int)&iov;
	if (jsys(DUMPO, acs) > 0) {
	    dp->mta_frms = dp->d_buse;
	    return 1;
	}
	/* Error return */
	dp->mta_serr++;
	fprintf(logf, "; Tape DUMPO%% error: %s\n", os_strerror(-1));
	os_mtclrerr(dp->d_fd);
	dp->mta_herr++;
	return 0;
    }
#elif CENV_SYS_UNIX /* !CENV_SYS_T20 */
    char8 *iop = dp->d_buff;
    int ret;
    rsiz_t full = dp->d_buse;
    rsiz_t used = full;

    dp->mta_frms = 0;

    /* Special hack for QIC -- must bump up length to modulo-512.
    */
    if (dp->d_istape == MTYP_QIC) {
	full = (used + 0777) & ~0777;	/* Round up to 512-byte boundary */
	if (used < dp->d_recsiz)
	    fprintf(logf, "; Warning: partial record padded out (%ld => %ld)\n",
			(long)used, (long)full);
	if (full > dp->d_blen) {
	    fprintf(logf, "; Internal bug: padout exceeds buffer (%ld > %ld)\n",
			(long)full, (long)dp->d_blen);
	    full = dp->d_blen;
	}
	if (full > used) {
	    memset(dp->d_buff + used, 0, full - used);	/* Zap padding */
	    dp->d_tloc += full - used;		/* Add extra so devwrite */
	}					/* knows true tapeloc */
	used = full;
    }


    do {
	if ((ret = write(dp->d_fd, iop, used)) == used) {
	    dp->mta_frms += used;			/* Won! */
	    return 1;
	}
	/* Error of some kind */
	dp->mta_serr++;
	fprintf(logf, "; (Rec %ld, try %d) ",
			dp->d_recs, dp->mta_retry - retry);
	if (ret < 0) {
	    fprintf(logf, "Tape write error: %s\n", os_strerror(-1));
	} else {
	    fprintf(logf, "Tape write truncated: %d, shd be %ld\n",
		    ret, (long)used);
	    if (ret > 0) {
		used -= ret;		/* Update to write out rest */
		iop += ret;
		dp->d_recs++;		/* Sigh, stupid crock here */
		dp->d_frecs++;
		dp->mta_frms += ret;
	    }
	}
	os_mtstatus(dp, logf);		/* Show full status */
    } while (--retry > 0);		/* Continue N times */
    dp->mta_herr++;
#endif

    return 0;		/* For now */
}

int os_mtweof(struct dev *dp)		/* Write a tapemark */
{
#if CENV_SYS_T20
    acs[1] = dp->d_fd;
    acs[2] = monsym(".MOCLE");
    jsys(MTOPR, acs);
#else
    struct mtop mtcmd;
    mtcmd.mt_op = MTWEOF;
    mtcmd.mt_count = 1;
    if (ioctl(dp->d_fd, MTIOCTOP, &mtcmd) < 0) {
	fprintf(logf, "; MTWEOF ioctl failed: %s\n", os_strerror(-1));
	dp->mta_herr++;
	return FALSE;
    }
#endif
    return TRUE;
}

int os_mtfsr(struct dev *dp)	/* Forward Space Record (to inter-record gap)*/
{
#if CENV_SYS_T20
/*    acs[1] = dp->d_fd;
    acs[2] = monsym(".MOCLE");
    jsys(MTOPR, acs);
*/
#else
    struct mtop mtcmd;
    mtcmd.mt_op = MTFSR;
    mtcmd.mt_count = 1;
    if (ioctl(dp->d_fd, MTIOCTOP, &mtcmd) < 0) {
	fprintf(logf, "; MTFSR ioctl failed: %s\n", os_strerror(-1));
	dp->mta_herr++;
	return FALSE;
    }
#endif
    return TRUE;
}

void os_mtclrerr(int fd)
{
#if CENV_SYS_T20
    acs[1] = fd;
    acs[2] = monsym(".MOCLE");
    jsys(MTOPR, acs);
#endif /* CENV_SYS_T20 */
}

#if CENV_SYS_T20
void
t20status(register struct dev *d, register FILE *f, int swd, int cnt)
{
    fprintf(f, "; Tape status: Cnt %d", ((unsigned)cnt)>>18);
    if (cnt & RH) fprintf(f, ",,%d", cnt & RH);
    fprintf(f, " Flgs: %#o", swd&RH);
    
    if (swd & MT_ILW) fprintf(f, " ILW");	/* ILLEGAL WRITE */
    if (swd & MT_DVE) fprintf(f, " DVE");	/* DEVICE ERROR */
    if (swd & MT_DAE) fprintf(f, " DAE");	/* DATA ERROR */
    if (swd & MT_SER) fprintf(f, " SER");	/* SUPPRESS ERROR RECOVERY PROCS */
    if (swd & MT_EOF) fprintf(f, " EOF");	/* EOF (FILE MARK) */
    if (swd & MT_IRL) fprintf(f, " IRL");	/* INCORRECT RECORD LENGTH */
    if (swd & MT_BOT) fprintf(f, " BOT");	/* BEGINNING OF TAPE */
    if (swd & MT_EOT) fprintf(f, " EOT");	/* END OF TAPE */
    if (swd & MT_EVP) fprintf(f, " EVP");	/* EVEN PARITY */
    if (swd & MT_DEN) fprintf(f, " DEN=%d",	/* DENSITY (0 IS 'NORMAL') */
		FLDGET(swd, MT_DEN));
    if (swd & MT_CCT) fprintf(f, " CCT=%d",	/* CHARACTER COUNTER */
		FLDGET(swd, MT_CCT));
    if (swd & MT_NSH) fprintf(f, " NSH");	/* DATA MODE OR DENS NOT SUPPORTED */
    fprintf(f, "\n");
}
#endif /* CENV_SYS_T20 */

void os_mtstatus(struct dev *dp, FILE *f)
{
#if CENV_SYS_T20
#elif CENV_SYS_UNIX
    struct mtget mtstatb;

    if (ioctl(dp->d_fd, MTIOCGET, (char *)&mtstatb) < 0) {
	fprintf(f, "; Couldn't get status for %s; %s\n",
				dp->d_path, os_strerror(-1));
	return;
    }
    fprintf(f, "; Status for magtape %s:\n", dp->d_path);
    fprintf(f, ";   Type: %#x (vals in sys/mtio.h)\n", mtstatb.mt_type);
# if CENV_SYS_SUN
    fprintf(f, ";   Flags: %#o ->", mtstatb.mt_flags);
    if (mtstatb.mt_flags & MTF_SCSI) fprintf(f, " SCSI");
    if (mtstatb.mt_flags & MTF_REEL) fprintf(f, " REEL");
    if (mtstatb.mt_flags & MTF_ASF)  fprintf(f, " ABSFILPOS");
    fprintf(f, "\n");
    fprintf(f, ";   Optim blkfact: %d\n", mtstatb.mt_bf);
# endif

    fprintf(f, ";   Drive status: %#o (dev dep)\n", mtstatb.mt_dsreg);
    fprintf(f, ";   Error status: %#o (dev dep)\n", mtstatb.mt_erreg);
    fprintf(f, ";   Err - Cnt left: %ld\n", (long)mtstatb.mt_resid);
# if CENV_SYS_SUN
    fprintf(f, ";   Err - File num: %ld\n", (long)mtstatb.mt_fileno);
    fprintf(f, ";   Err - Rec  num: %ld\n", (long)mtstatb.mt_blkno);
# endif

#endif
}
