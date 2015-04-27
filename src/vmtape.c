/* VMTAPE.C - Virtual Magnetic-Tape support routines
*/
/* $Id: vmtape.c,v 2.5 2001/11/19 10:41:28 klh Exp $
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
 * $Log: vmtape.c,v $
 * Revision 2.5  2001/11/19 10:41:28  klh
 * Fix TPS format WEOF.
 *
 * Revision 2.4  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*

	The purpose of VMTAPE is primarily to invent the contents of a
virtual magtape on the fly, as it is being read, by following the
directions of the currently mounted tape-file.  This tape-file may
variously be referred to as a "data file", "control file" or even
"tape directory" since it may specify many different files which are
to be sucked up to serve as the contents of the magtape.

vmt_attrmount() is called when the user wants to "mount" a magtape, either
for reading or writing.  For reading, the specified tape-file is read
in and parsed into an internal list that will be referenced when data
is requested from the drive.  Any errors during the tape-file parse
will cause vmt_attrmount() to fail.  Errors during the reading of
subsidiary files simply skip over those files.  Both generate error
messages to the stdio stream provided.

For writing, no attempt is made to understand the contents of the data
being written, and only raw-format or TPS/TPE output is supported.  A data
file is generated, called <tape-file>.tap or <tape-file>.tps, and the
control file itself (if one is required) is not output until the tape
is rewound or unmounted.

See vmtape.h for more on formats.

*/

/* To-do Notes and stuff:

	- Long standing annoyance: Overhaul all refs to use a
consistent terminology such as "tape control file" as opposed to "tape
directory file" to avoid confusing overload of "d" in names.  (data,
directory, description, ...)  Then "tapefile" refers to either an OS
filespec for hardware, or a pair of control/data disk files, or a
single file with embedded structure?

	- Tied in with above, establish conventions for naming the
various formats, especially filename extensions.  Main problem is that
everyone likes to use .TAP!  One idea is to require that all tape
files use an extension of .TPx, such as .tpc, .tps, .tpr.

	- A personal desiderata is to finally settle on some extension
for the control/tapedir file that sorted AHEAD of its raw data file;
.ctl is good, but is not a .TPx.  .TPC is "taken".  .TPD?  That letter
"D" again.  Sigh!

	- Add "oneway" flag to better support unzip pipes or TPC-like
formats.  Could apply to any data format.

	- Implement "inmem" feature, initially only for RO tapes.
What internal format to use?  Convert to use TDR, or TPS?  In order
to accomodate R/W would have to either extend TDR a bit or snarf in
the SBSTR package (latter probably gross overkill).

	- Allow using TDR information with formats other than RAW.  This
would allow TPC to work better, as well as INMEM.

	- Flush REOF even for raw mode?  Or extend it to one-way
formats so they can backspace over one tapemark at least.

	- Provide vmt_attrget()?

	- Someday move all OS_ stuff of dptm03/tapedd into a shared
osdtap.c.  Then can also consider optionally extending vmtape.c to
hardware devices so as to finally provide a consistent interface.

*/


#include <errno.h>
#include <string.h>
#include <stdlib.h>	/* For malloc */
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>	/* For error-reporting functions */

#include "klh10.h"
#include "osdsup.h"
#include "word10.h"
#include "wfio.h"	/* For word-based file i/o */
#include "vmtape.h"

#ifdef RCSID
 RCSID(vmtape_c,"$Id: vmtape.c,v 2.5 2001/11/19 10:41:28 klh Exp $")
#endif

/* External OS-dependent definitions needed, acquired from file that
   included us.
*/
#if 0
/* OS_MAXPATHLEN */
extern char *os_strerror(int);
#endif

/* Internal OSD stuff we currently include (later move into osdtap.c?)
 */ 
#if VMTAPE_ITSDUMP
static int os_fullpath(char *loc, char *dir, char *file, int maxlen);
static int os_fmtime(FILE *f, register struct tm *tm);
static int os_net2tm(long unsigned int ntm, register struct tm *tm);
#endif /* VMTAPE_ITSDUMP */


/* Data & misc defs */
struct vmtfmt_s {
    int tf_fmt;
    char *tf_name;
    char *tf_ext;
    int tf_flags;
};
static struct vmtfmt_s vmtfmttab[] = {
#define vmtfdef(en,str,ext,flgs) { en, str, ext, flgs }
    VMTFORMATS
#undef vmtfdef
};

/* Predeclarations */
static int  datf_open(struct vmtape *t, char *fname, char *mode);
static void datf_close(struct vmtape *t);
static int  tdr_scan(struct vmtape *t, FILE *f, char *fname);
static char *dupcstr(char *s);
static void tdr_reset(struct vmtape *t);
static void td_init(struct vmttdrdef *td);
static void td_reset(struct vmttdrdef *td);
static void td_fout(struct vmttdrdef *td, FILE *f, char *fnam);
static void td_trunc(struct vmttdrdef *td);
static struct vmtrecdef *td_recapp(struct vmttdrdef *td,
				   long unsigned int len, int cnt, int err);

#if 0
# define vmtseterr(t) (((t)->mt_state = TS_ERR), (t)->mt_err++)
#else
# define vmtseterr(t) ((t)->mt_err++)
#endif

#if VMTAPE_ITSDUMP

/* Definitions for ITSDUMP format
 */

/* Format of ITS DUMP tapes, from SYSENG;DUMP >

THBLK:  -LTHBLK,,0      ;TAPE-HEADER-BLOCK
THTPN:  -1      ;TAPE NUMBER,,NUMBER OF REEL IN THIS DUMP, -1==> WE DON'T YET KNOW THIS PARAMETER
THDATE: 0       ;TAPE CREATION DATE (SIXBIT)
THTYPE: 0       ;0 RANDOM, >0 FULL DUMP, <0 INCREMENTAL DUMP
LTHBLK==.-THBLK
LTHTPN: -1      ;LAST THTPN
 
HBLK:   -LHBLK,,0               ;FILE-HEADER-BLOCK
HSNM:   0       ;SYS NAME
HFN1:   0       ;FN1
HFN2:   0       ;FN2
LHBLKZ==.-HBLK                  ; Must be at least this long
HPKN:   0       ;LINK FLAG,,PACK NUMBER (SAME INFO AS 3RD VALUE FROM
                ; FILBLK, BUT IN A DIFFERENT FORMAT)
HDATE:  0       ;CREATION DATE AND TIME OF FILE ON DISK (DISK-FORMAT) (4TH
                ; VALUE FROM FILBLK)
                                ; Next two added 7/14/89 by Alan
HRDATE: 0       ;REFERENCE DATE, AUTHOR INDEX, BYTE SIZE AND BYTE COUNT
                ; (5TH VALUE FROM FILBLK)
HLEN:   0       ;LENGTH OF FILE IN WORDS (FROM FILLEN)
LHBLK==.-HBLK                   ; Must not be longer than this

Note that if the file is actually a link, its length is assumed to be 3
words, and the contents are (in order) the FN1, FN2, and DIR of the file
pointed to.

A tapemark separates each file from the next.  The exact record length
does not matter, except that the last record of each file is only as
long as needed to include the last words of the file.  Conceivably
each file could be a single long record of varying size, but in
practice ITS DUMP seems to use a standard buffer size of 1024 words
(5120 frames in core-dump format) for all records except the last one.

Double tapemark is logical EOT, as usual.

Standard data mode for ITSDUMP is core-dump; nothing else makes sense.

*/

#define VMT_ITS_FPW    (5)			/* Frames per word  */
#define VMT_ITS_DEFRECLEN (1024*VMT_ITS_FPW)	/* Default record length */

enum {	TH_CNT=0,	/* File-block header, -4,,0 to -8,,0 */
	TH_DIR,		/* Dir */
	TH_FN1,		/* FN1 */
	TH_FN2,		/* FN2 */
	TH_LKF,		/* <linkf>,,<pack #> */
	TH_CRD,		/* <creation date & time> */
	TH_RFD,		/* <refdate>,,<author idx><bytesize&bytecnt> */
			/*	-1,,777000 if unknown */
	TH_FLN,		/* <file length in wds> */
			/*	-1 if unknown */
	TH_LFN1,	/* Link target FN1, FN2, DIR */
	TH_LFN2,
	TH_LDIR
};

enum efdfmt { FDFMT_WFT=1, FDFMT_LINK };


struct vmtfdits {
	int linkf;
	w10_t dir, fn1, fn2;
	w10_t xdir, xfn1, xfn2;
	w10_t crdatim;
	w10_t rfdatetc;
	w10_t wdlen;
	w10_t auth;		/* Not used as yet, but maybe someday... */
};


struct vmtfildef {			/* Always malloced */
	struct vmtfildef *fdprev;
	struct vmtfildef *fdnext;
	char *fdfname;		/* Source filename on host system */

	enum efdfmt fdfmt;
	enum wftypes fdwft;
	struct vmtfdits fdits;	/* ITS filespec */
};

static w10_t itsdumphead[4] = {		/* Tape header */
	W10XWD(0777774, 0),		/* <-# wds (-4)>,,0 */
	W10XWD(1, 0),			/* <tape #>,,<reel #> */
	W10XWD(0312220, 0262125),	/* YYMMDD in sixbit */
	W10XWD(0, 0)			/* <tape type> 0 = random */
};

static w10_t sixbit(char *str);
static w10_t itsdate(FILE *f);
static w10_t itsnet2qdat(uint32 ntm);
static w10_t itsfillen(uint32 bsz, uint32 bcnt, w10_t *rfd);

static int its_wget(struct vmtape *t, w10_t *wp);


#endif /* VMTAPE_ITSDUMP */

/* Definitions for RAW format
 */

#if VMTAPE_RAW

/*
**	A tape is considered to consist of an arbitrary sequence of data
** records and tapemarks.  The logical position whenever idle (not in the
** middle of an IO transfer) is always at a point in between two such
** records or tapemarks (or BOT or EOT).
**
**	For RAW format, the raw tape data is exactly that -- a file of
** nothing but data bytes with no internal structuring to indicate record
** lengths or tapemarks.  Structure is imposed on this data with a "tape
** directory" that internally consists of a linked list of "vmtrecdef"
** structures, and externally is represented by an ASCII "control" or
** "tapedir" file.
**	The reasons for using this technique rather than something
** with internal structure (eg TPS) are largely historical -- in order 
** to save tapes with the hardware I could access, they had to be copied
** directly onto media such as QIC tapes which had no concept of record
** length, and it wasn't clear whether anything better would come along.
**
** RAW tape state info:
**
** When idle:
**    tdcur may be:
**	NULL -- tape is at BOT or EOT depending on whether tdrncur is 0 or 1.
**	Else points to LAST record def transferred.
**		tdrncur likewise indicates last record # transferred; this
**		will be a number from 1 to N.  If 0, none of the records
**		in the current definition have been transferred yet.
**		If N or N+1, all of the current record definition has been
**		transferred.
**	Note that a tapemark (EOF) is considered a record of itself, with
**		a length & count of 0.  If tdcur points at a tapemark, tdrncur
**		will be 0 to indicate a logical position before the tapemark,
**		else >= 1 to indicate a position after it.
**
** The function vmt_iobeg() is used to initialize the tape state from
** the above intermediate state to one of the states below:
**
** During read IO transfer (state TS_RDATA):
**	tdcur -> the current record definition structure,
**	tdrncur = (nonzero) indicates which of the possibly N records
**		defined is actually about to be read by the next IO transfer.
** During read IO transfer (state TS_REOF):
**	tdcur -> the current tapemark record
**	tdrncur = 0
** During read IO transfer (state TS_EOT):
**	tdcur = NULL
**	tdrncur = 1
**
** During write IO transfer (state TS_WRITE):
**		tdcur = NULL 
**		tdrncur = 1 to indicate tape is at EOT.
**	The tape directory info has been munged to forget about all
**	previous info after the current write point.
**	When the IO transfer completes, the record or tapemark written
**	will be appended to the end of the tape directory.
*/

struct vmtrecdef {	/* Always malloced */
    struct vmtrecdef *rdnext;	/* Next record */
    struct vmtrecdef *rdprev;	/* Prev record */
    unsigned long rdloc;	/* Record loc */
    unsigned long rdlen;	/* Record length (can be 0 if tapemark/err) */
    unsigned int  rdcnt;	/* # of recs of this length (0 iff tapemark) */
    int           rderr;	/* If non-zero, error reading this record */
};

#endif /* VMTAPE_RAW */

/* Definitions for Bob Supnik's SIMH magtape format
   (also attributed to John Wilson's E11 tape format, although E11
   format differs in NOT having a padding byte!)

"TPS" FILE FORMAT (.TAP, .TPS, .TPE)
====================================

	This is the format used by Bob Supnik's SIMH emulators.  It is
almost the same as John Wilson's E11 format, which it was intended to
resemble.

A TPS file is assumed to consist of 8-bit bytes.

Each record starts with a 4-byte header in little-endian order.
The high bit of this 32-bit header is set to indicate an error of
some kind (similar to the 'E' flag in a RAW-format control file) and
the remaining 31 bits contain the record length N.

This header is followed by N data bytes plus, if N is odd, a padding
byte (of undefined value) at the end.  Following this is a copy of the
4-byte record header.

Tapemarks are represented by a single header with N=0.

The reason for having the record length both before and after the
record data is so tape motion in the reverse direction can be more
easily simulated.

The E11 (.TPE) format is identical except there are no padding bytes.

 */

/* No need to maintain record structure information as it can be
   determined by reading from the data file.
   Perhaps later maintain I/O pointer in case it eliminates some seeks,
   but because of stdio buffering this doesn't matter a great deal.

  Tape file will be opened on t->mt_datf as a binary stream.
  SEEK position will always point to the start of the next record
  or tapemark (assuming going forward).

  Thus, to read a record/mark: read the header to find the length,
  read the data if present, and read the trailer header to confirm it
  matches, thus leaving the ptr at the start of the next record.

  To write: write out the header, then the data, then the trailer.

  Must do so portably, in little-endian order.
 */

#define VMT_TPS_ERRF (1UL<<31)	/* Error - high order bit of record header */
#define VMT_TPS_CNTF (VMT_TPS_ERRF-1)
#define VMT_TPS_COUNT(ucp) (\
		(((uint32)(ucp)[3])<<24) | \
		(((uint32)(ucp)[2])<<16) | \
			 ((ucp)[1] << 8) | \
			 ((ucp)[0]))

/*

"TPC" FILE FORMAT (.TPC)
========================

	This format is not as flexible as the others but was
apparently quite commonly used for DECUS tape images, and Tim Shoppa
has a "couple thousand" TPC images stashed away.

A TPC file is assumed to consist of 8-bit bytes.

Each record starts with a 2-byte header in little-endian order,
containing 16 bits of record length N.

This header is followed by N data bytes plus, if N is odd, a padding
byte (of undefined value) at the end.  Unlike TPS format, there is no
trailing header.

Tapemarks are represented by a single header with N=0.

Obviously it is difficult to use this format directly when reverse
tape motion is desired; an internal representation must be built.

 */
#define VMT_TPC_COUNT(ucp) (((ucp)[1]<<8)|((ucp)[0]))



void
vmt_init(register struct vmtape *t,
	 char *devnam)
{
#if 1
    memset((char *)t, 0, sizeof(*t));	/* For now */
#else
    td_init(&t->mt_tdr);
    t->mt_state = TS_CLOSED;
#endif
    t->mt_devname = devnam;
    t->mt_errhan = NULL;
}


/* Error reporting for all VMTAPE code.
** Currently uses a fixed-size error string buffer.  This is dumb but
** simple; all calling routines must make some effort to keep length
** within bounds.
** Specifically, never use plain "%s" -- always limit it.
*/
static void
vmterror(register struct vmtape *t, char *fmt, ...)
{
    char ebuf[512];

    if (t->mt_devname == NULL)
	t->mt_devname = "<\?\?\?>";
    {
	va_list ap;
	va_start(ap, fmt);
	vsprintf(ebuf, fmt, ap);
	va_end(ap);
    }

    if (t->mt_errhan == NULL)
	fprintf(stderr, "[%s: %s]\n", t->mt_devname, ebuf);
    else
	(*(t->mt_errhan))(t->mt_errarg, t, ebuf);
}

/* Useful variant for reporting tape-dir parsing errors
*/
static void
tdrwarn(register struct vmtape *t, char *fmt, ...)
{
    char ebuf[512];

    if (t->mt_devname == NULL)
	t->mt_devname = "<\?\?\?>";
    sprintf(ebuf, "Tapedir error, file \"%.128s\" line %d: ",
		t->mt_filename ? t->mt_filename : "<null>", t->mt_lineno);
    {
	va_list ap;
	va_start(ap, fmt);
	vsprintf(ebuf+strlen(ebuf), fmt, ap);
	va_end(ap);
    }

    if (t->mt_errhan == NULL)
	fprintf(stderr, "[%s: %s]\n", t->mt_devname, ebuf);
    else
	(*(t->mt_errhan))(t->mt_errarg, t, ebuf);
}


static int vmt_crmount(struct vmtape *t, struct vmtattrs *ta);
static int vmt_rdmount(struct vmtape *t, struct vmtattrs *ta);
static int fmtexamine(struct vmtape *t, FILE *f, int fmt);
static char *os_pathext(char *path);
static char *genpath(struct vmtape *t, char *path, char *oldext, char *newext);

int
vmt_unmount(register struct vmtape *t)
{
    int res = TRUE;

    /* vmt_unmount must be callable from any state, even intermediate ones
    ** where tape was incompletely opened.  So always check everything.
    */
    if (t->mt_datf) {		/* Have an open data file? */
	/* If still any output to deliver, ensure it's out by doing rewind. */
	res = vmt_rewind(t);	/* Remember if output finalization fails */
    }

    datf_close(t);		/* Close either input or output data */
    if (t->mt_ctlf) {
	fclose(t->mt_ctlf);
	t->mt_ctlf = NULL;
    }
    tdr_reset(t);		/* Flush TDR and FDF stuff */

    if (t->mt_datpath) {
	free(t->mt_datpath);
	t->mt_datpath = NULL;
    }
    if (t->mt_filename) {
	free(t->mt_filename);
	t->mt_filename = NULL;
    }

    t->mt_state = TS_CLOSED;		/* Must be 0 */
    t->mt_writable = FALSE;
    t->mt_format = VMT_FMT_UNK;
    t->mt_fmtp = &vmtfmttab[t->mt_format];
    t->mt_frames = t->mt_err = 0;
    t->mt_iowrt = t->mt_eof = t->mt_eot = t->mt_bot = FALSE;
    return res;
}

int
vmt_pathmount(struct vmtape *t, char *path, char *args)
{
    struct vmtattrs v;

    if (!path)		/* If just wanted unmount, that's all! */
	return vmt_unmount(t);

    /* Set up initial path */
    if (strlen(path) >= sizeof(v.vmta_path)) {
	vmterror(t, "mount path too long (%d max)", (int)sizeof(v.vmta_path));
	return FALSE;
    }
    strcpy(v.vmta_path, path);	/* Set path */
    v.vmta_mask = VMTA_PATH;	/* Initialize attribs */

    /* Parse args if any, adding to attribs */
    if (args && !vmt_attrparse(t, &v, args)) {
	/* Already reported any errors */
	return FALSE;
    }
    return vmt_attrmount(t, &v);
}

int
vmt_attrmount(struct vmtape *t, struct vmtattrs *ta)
{
    char *pext;
    enum vmtfmt efmt = 0, fmt;
    char *efn = NULL;

    vmt_unmount(t);		/* Always unmount old tape if any */

    if (!(ta->vmta_mask & VMTA_PATH) || !ta->vmta_path[0]) {
	vmterror(t, "no pathname given to mount");
	return FALSE;
    }

    /* Set defaults for unspecified stuff */
    if (!(ta->vmta_mask & VMTA_CTLPATH))
	ta->vmta_ctlpath[0] = '\0';
    if (!(ta->vmta_mask & VMTA_DATPATH))
	ta->vmta_datpath[0] = '\0';
    if (!(ta->vmta_mask & VMTA_MODE))
	ta->vmta_mode = VMT_MODE_RDONLY;	/* Default mode is RO */
    if (!(ta->vmta_mask & VMTA_UNZIP))
	ta->vmta_unzip = FALSE;

    /* See if extension implies a format.  May not need this, but
       to simplify coding always do it upfront.
    */
    pext = os_pathext(ta->vmta_path);		/* Find file ext if any */
    if (pext) {
	efmt = vmt_exttofmt(pext);
    }

    if (ta->vmta_mask & VMTA_FMTREQ)
	fmt = ta->vmta_fmtreq;		/* Explicit request  */
    else if (efmt)
	fmt = efmt;				/* Filename extension spec */
    else if (ta->vmta_mask & VMTA_FMTDFLT)
	fmt = ta->vmta_fmtdflt;
    else
	fmt = 0;

    if (ta->vmta_mode & VMT_MODE_CREATE) {
	/* Create new tapefile, must not exist.
	   Format is determined first by explicit request, then by
	   filename extension, then by user default, then coded default.
	   CTL is turned into coded default (RAW).
	   Only RAW and TPS/TPE are currently supported for output.
	 */
	ta->vmta_fmtreq = fmt ? fmt : VMTAPE_FMT_DEFAULT;
	return vmt_crmount(t, ta);

    } else if (ta->vmta_mode & VMT_MODE_UPDATE) {
	vmterror(t, "Update of existing tapefile not yet supported");
	return FALSE;

    } else if (ta->vmta_mode & VMT_MODE_RDONLY) {
	/* Tapefile must exist; determine format.
	   Determined first by explicit request (& verified)
	   then filename ext (& verified), then by user default
	   (& verified), finally by examination.
	   If cannot determine, give up and complain.
	 */
	ta->vmta_fmtreq = fmt ? fmt : VMT_FMT_UNK;

	return vmt_rdmount(t, ta);
    }
}

/* VMT_CRMOUNT - Create and mount a new virtual tape for
**	writing.
*/
static int
vmt_crmount(register struct vmtape *t,
	    register struct vmtattrs *ta)
{
    FILE *cf = NULL, *df;
    char *cfn = NULL, *dfn = NULL;

    enum vmtfmt fmt = ta->vmta_fmtreq;
    struct vmtfmt_s *fmtp = &vmtfmttab[fmt];
    char *pext;

    /* Check format.  This weeds out CTL, for example.
     * Currently only supports RAW, TPE, TPS.
     */
    if (!(fmtp->tf_flags & VMTFF_WR)) {
	vmterror(t, "Unwritable tape format %s",
		 fmtp->tf_name);
	return FALSE;
    }
    pext = os_pathext(ta->vmta_path);	/* Will probably want this */

    /* First verify necessary filenames are present */
    if (fmtp->tf_flags & (VMTFF_CTL | VMTFF_XCTL)) {
	/* If explicit ctlpath given, use it without question */
	if ((cfn = ta->vmta_ctlpath) && *cfn)
	    cfn = dupcstr(cfn);
	else if ((cfn = ta->vmta_path) && *cfn) {
	    /* Normal path,  Add CTL extension if none present */
	    cfn = pext ? dupcstr(cfn)
		       : genpath(t, cfn, NULL, vmtfmttab[VMT_FMT_CTL].tf_ext);
	} else {
	    vmterror(t, "No tape control filename");
	    return FALSE;
	}
	if (!cfn) {
	    vmterror(t, "No mem for tape control filename");
	    return FALSE;
	}
    }
    /* From here on must fail by going to badret, since cfn must be freed */

    /* Ditto for output data filename, which must always be present */
    if ((dfn = ta->vmta_datpath) && *dfn)
	dfn = dupcstr(dfn);
    else if ((dfn = ta->vmta_path) && *dfn) {
	/* Use normal path.  A bit tricky to ensure right extension.
	   If no extension given, or path was for the CTL file,
	   	ensure it has a new extension identifying the format.
	   Otherwise (has ext and fmt doesn't use CTL file), use the
	   given path as-is.
	*/
	if (!pext || cfn)
	    dfn = genpath(t, dfn, NULL, fmtp->tf_ext);
	else
	    dfn = dupcstr(dfn);
    }
    if (!dfn) {
	vmterror(t, "No mem for tape data filename");
	goto badret;
    }

    /* Now verify filenames don't already exist */
    if (cfn && (cf = fopen(cfn, "r"))) {
	fclose(cf);
	vmterror(t, "Tape control file \"%.256s\" already exists", cfn);
	goto badret;
    }

    if (df = fopen(dfn, "rb")) {
	fclose(df);
	vmterror(t, "Tape data file \"%.256s\" already exists", dfn);
	goto badret;
    }

    /* OK, now open for writing! */
    if (cfn && !(cf = fopen(cfn, "w"))) {
	vmterror(t, "Cannot create tape control file \"%.256s\": %.80s",
		 cfn, os_strerror(errno));
	goto badret;
    }
    if (!(df = fopen(dfn, "w+b"))) {
	fclose(df);
	if (cfn) fclose(cf);
	vmterror(t, "Cannot create tape data file \"%.256s\": %.80s",
		 dfn, os_strerror(errno));
	goto badret;
    }

    /* All's well!  Finalize state for writing */
    t->mt_filename = cfn;
    t->mt_datpath = dfn;
    t->mt_ctlf = cf;
    t->mt_datf = df;
    t->mt_ispipe = FALSE;
    t->mt_writable = TRUE;
    t->mt_format = fmt;
    t->mt_fmtp = fmtp;
    t->mt_state = TS_RWRITE;
#if 0
    switch (fmt) {
    case VMT_FMT_RAW:
	break;
    case VMT_FMT_TPS:
	break;
    }
#endif
    return TRUE;

    /* If come here, error of some kind.  Clean up... */
 badret:
    if (cf) fclose(cf);
    if (df) fclose(df);
    if (cfn) free(cfn);
    if (dfn) free(dfn);
    vmt_unmount(t);		/* Ensure everything reset */

    return FALSE;
}

/* Mount virtual tape for reading only
   Assumes any previous tape is unmounted.
 */
static int
vmt_rdmount(register struct vmtape *t,
	    register struct vmtattrs *ta)
{
    FILE *cf = NULL, *df = NULL;
    char *cfn = NULL, *dfn = NULL;

    char *cfname = ta->vmta_ctlpath;
    char *dfname = ta->vmta_datpath;
    enum vmtfmt fmt = ta->vmta_fmtreq;
    struct vmtfmt_s *fmtp = &vmtfmttab[fmt];

    if (t->mt_debug)
	vmterror(t, "vmt_rdmount %d=%s p=%s c=%s d=%s",
		 fmt, vmt_fmtname(fmt), ta->vmta_path, cfname, dfname);

    /* General plan:
	If no fmt: examine pathT, then pathB, then fail.
		If found, go to verify apparent format.
	If fmt is ctl-type, check pathT, ctlpathT, then fail.
		If found, verify fileT is ctl.  Find true fmt.
	If fmt is dat-type, check pathB, datpathB, then fail.
		If found, verify fileB is of given fmt.
    */
    t->mt_ctlf = NULL;		/* Paranoia, shd already be clear */
    t->mt_datf = NULL;
    t->mt_filename = NULL;
    t->mt_datpath = NULL;

    if (fmt == VMT_FMT_UNK) {
	/* Unknown at this point implies no explicit format spec AND
	   no recognizable extension.
	*/
	if (cf = fopen(ta->vmta_path, "r")) {
	    cfn = ta->vmta_path;
	} else {
	    /* Non-ex file.  If no ext in path, try all possible ctl
	       extensions to see if a ctl file exists, and if so, assume ctl.
	    */
	    register struct vmtfmt_s *p;

	    if (os_pathext(ta->vmta_path)) {
		/* Has ext, so cannot try anything with it */
		vmterror(t, "Cannot open tape file \"%s\"", ta->vmta_path);
		return FALSE;
	    }

	    for (p = vmtfmttab; p->tf_name; ++p) {
		int flags = (p->tf_flags & VMTFF_ALIAS)
		    ? (vmtfmttab[p->tf_flags & VMTFF_FMT].tf_flags)
		    : p->tf_flags;
		if ((flags & VMTFF_CTL) && p->tf_ext) {
		    if (!(cfn = genpath(t, ta->vmta_path, NULL, p->tf_ext))) {
			vmterror(t, "malloc failed for tape pathname");
			return FALSE;
		    }
		    if (cf = fopen(cfn, "r"))
			break;
		    free(cfn);
		}
	    }
	    if (!cf) {
		vmterror(t, "Cannot find tape file \"%s\"", ta->vmta_path);
		return FALSE;
	    }
	    fmt = VMT_FMT_CTL;	/* Open, now insist on this format */
	    goto havefmt;
	}
	/* Opened cfn with stream f, now verify it specially so if
	   it doesn't look like a control file we can try something else.
	*/
	if (fmt = fmtexamine(t, cf, VMT_FMT_CTL)) {
	    /* Looks like control file! */
	    goto havectl;
	}
	/* Oops, doesn't look like a control file.  Try data. */
	fclose(cf);
	if (cfn != ta->vmta_path)
	    free(cfn);
	cfn = NULL;

	if (!(df = fopen(ta->vmta_path, "rb"))) {
	    vmterror(t, "Cannot open binary tape file \"%s\"", ta->vmta_path);
	    return FALSE;
	}
	if (!(fmt = fmtexamine(t, df, VMT_FMT_RAW))) {	/* Look for bin fmt */
	    fclose(df);
	    vmterror(t, "Cannot determine format of tape file \"%s\"",
		     ta->vmta_path);
	    return FALSE;
	}
	dfn = ta->vmta_path;
    }
 havefmt:
    if (vmtfmttab[fmt].tf_flags & (VMTFF_CTL | VMTFF_XCTL)) {
	/* Control file should exist, open it if not already open */
	if (!cf) {
	    if (!(cf = fopen(ta->vmta_path, "r"))) {
		vmterror(t, "Cannot open tape control file \"%s\"", ta->vmta_path);
		goto badret;
	    }
	    cfn = ta->vmta_path;
	}
	if (fmtexamine(t, cf, VMT_FMT_CTL) != VMT_FMT_CTL) {
	    vmterror(t, "Non-text tape control file \"%s\"", cfn);
	    goto badret;
	}
    havectl:
	/* Looks like a control file, suck it all in */
	if (!tdr_scan(t, cf, cfn)) {	/* Read and parse the tapefile */
	    vmterror(t, "Error reading tape control file");
	    goto badret;		/* Failed for some reason */
	}
#if VMTAPE_ITSDUMP
	t->mt_fitsdate = itsdate(cf);	/* Remember creation date in case */
#endif
	fclose(cf);			/* Don't need tapefile open anymore */
	cf = NULL;
	if (cfn == ta->vmta_path)	/* Remember ctl filename */
	    cfn = dupcstr(cfn);
	t->mt_filename = cfn;

	fmt = t->mt_format;		/* Get real format from parsed file */
    }

    /* Format known and control file snarfed if needed,
       now do format-specific open of data file
    */
    t->mt_format = fmt;
    t->mt_fmtp = &vmtfmttab[t->mt_format];
    switch (t->mt_format) {
	char *basefn;

    case VMT_FMT_RAW:
    case VMT_FMT_TPE:
    case VMT_FMT_TPS:
    case VMT_FMT_TPC:
	basefn = NULL;
	if (dfn) {		/* If already open, use that path */
	    if (dfn == ta->vmta_path)
		if (!(dfn = dupcstr(dfn))) {
		    vmterror(t, "malloc failed for tape data pathname");
		    goto badret;
		}
	} else if (ta->vmta_datpath[0]) {	/* Use explicit data path? */
	    if (!(dfn = dupcstr(ta->vmta_datpath))) {
		vmterror(t, "malloc failed for tape data pathname");
		goto badret;
	    }
	} else {
	    /* No explicit data path.  Attempt to figure one out either from
	       the opened control file (if one; should always exist for
	       RAW) or from the base path.
	    */
	    if (!(basefn = cfn) && ta->vmta_path[0]) {
		/* Try general path */
		if (!(dfn = dupcstr(ta->vmta_path))) {
		    vmterror(t, "malloc failed for tape data pathname");
		    goto badret;
		}
		basefn = dfn;		/* Try others if this one fails */
	    }
	}
	if (!dfn && !basefn) {
	    vmterror(t, "No tape data pathname given");
	    goto badret;
	}

	if (dfn && datf_open(t, dfn, (ta->vmta_unzip ? "zrb" : "rb"))) {
	    ;			/* Success */
	} else if (basefn) {
	    /* No data file spec, make one up from base filename if any */
	    register struct vmtfmt_s *p;

	    for (p = vmtfmttab; p->tf_name; ++p) {
		if ((t->mt_format == p->tf_fmt)
		    || ((p->tf_flags & VMTFF_ALIAS)
			&& (t->mt_format == (p->tf_flags & VMTFF_FMT))
			&& p->tf_ext)) {
		    if (!(dfn = genpath(t, basefn, NULL, p->tf_ext))) {
			vmterror(t, "malloc failed for tape data pathname");
			goto badret;
		    }
		    if (datf_open(t, dfn, (ta->vmta_unzip ? "zrb" : "rb")))
			break;
		    free(dfn);		/* This name failed, free it up */
		    dfn = NULL;
		}
	    }
	}
	if (!t->mt_datf) {
	    vmterror(t, "Cannot open tape data file \"%.256s\": %.80s",
		     (dfn ? dfn : basefn), os_strerror(errno));
	    goto badret;
	}
	t->mt_datpath = dfn;
	t->mt_writable = FALSE;
	t->mt_state = TS_RDATA;
	break;

#if VMTAPE_ITSDUMP
    case VMT_FMT_ITS:
	/* Set up state for reading ITS dump file */
	t->mt_ctlf = NULL;
	t->mt_datf = NULL;
	t->mt_ispipe = FALSE;
	t->mt_writable = FALSE;
	t->mt_state = TS_THEAD;	/* For now */
	break;
#endif

    default:
	if ((unsigned)(t->mt_format) < VMT_FMT_N)
	    vmterror(t, "Unsupported tape format: %s",
		     vmt_fmtname(t->mt_format));
	else
	    vmterror(t, "Bad tape format spec: %d", (int)t->mt_format);
	goto badret;
    }

    /* One last little hack - if requested, skip over first N files */
    if ((ta->vmta_mask & VMTA_FSKIP) && ta->vmta_fskip)
	(void) vmt_fspace(t, 0, (long) ta->vmta_fskip);

    return TRUE;

    /* If come here, error of some kind.  Clean up... */
 badret:
    if (cf) fclose(cf);
    if (df) fclose(df);
    if (cfn
      && (cfn != ta->vmta_path)
      && (cfn != t->mt_filename))	/* Avoid multiple free() */
	free(cfn);
    if (dfn
      && (dfn != ta->vmta_path)
      && (dfn != t->mt_datpath))	/* Avoid multiple free() */
	free(dfn);
    vmt_unmount(t);			/* Ensure everything reset */

    return FALSE;
}


#if CENV_SYS_UNIX
# define OSD_PATH_EXTSEPCHAR '.'
# define OSD_PATH_DIRSEPCHAR '/'
#elif CENV_SYS_MAC
# define OSD_PATH_EXTSEPCHAR '.'
# define OSD_PATH_DIRSEPCHAR ':'
#else
# error "OSD code - must define pathname chars"
#endif

/* Return pointer to extension component of a pathname.
   Assumes separated from base component by a single char; needs to be
   recoded if that's not the case.
 */
static char *
os_pathext(char *path)
{
    register char *s;

    /* Find last occ of possible extension separator, and win if
       it's not part of a directory component.
    */
    if ((s = strrchr(path, OSD_PATH_EXTSEPCHAR))
	&& (strchr(s, OSD_PATH_DIRSEPCHAR) == NULL))
	return s+1;		/* Skip over separator char */
    return NULL;
}

/* Generate newly allocated pathname from a base name using given extension.
   May return same pathname if base already has that extension.
   If "oldext" given, old ext must match or new ext is simply appended.
 */
static char *
genpath(struct vmtape *t,
	char *base,
	char *oldext,
	char *newext)
{
    register char *fn, *ext, *s;
    register int blen;

    if ((s = os_pathext(base))		/* If base has extension */
	&& (!oldext			/* and no criteria */
	    || (strcmp(s, oldext)==0)))	/* or meets criteria */
	blen = (s - base) - 1;		/* then replace sep-char & ext! */
    else blen = strlen(base);		/* No ext, append to whole name */

    if (fn = malloc(blen+1+strlen(newext)+1)) {
	memcpy(fn, base, blen);
	s = fn + blen;
	*s++ = OSD_PATH_EXTSEPCHAR;
	strcpy(s, newext);
    }

    if (t->mt_debug)
	vmterror(t, "genpath b=%s o=%s e=%s -> %s",
		 base, oldext ? oldext : "(null)", newext, fn);

    return fn;
}


/* Examine file contents for clues to its format.
   Returns -1 if hits an I/O error.
   Otherwise returns its guess as to file's format, which will be 0
   (i.e. VMT_FMT_UNK) if it can't tell or was asked to give up early.
   The "fmt" arg allows a little control:
	VMT_FMT_UNK (0) to try all known formats,
	VMT_FMT_RAW to check for all binary formats,
	VMT_FMT_XXX to check for that specific format (currently only 
			VMT_FMT_CTL, VMT_FMT_TPS, VMT_FMT_TPE will work).

   Note that VMT_FMT_CTL is only guaranteed to work with a text stream, and
   VMT_FMT_RAW with a binary stream.  VMT_FMT_UNK may fail to work correctly on
   a system where text and binary streams are different (such as TOPS-20!).
   If that becomes necessary, the fix is to try the file in both modes.

   In general VMT_FMT_TPC cannot be reliably distinguished from other formats,
   although it is possible.
 */
static int
fmtexamine(struct vmtape *t, FILE *f, int fmt)
{
    register int i, n;
    uint32 cnt, cnt1;
    unsigned char buff[10];	/* Read first N chars */
    unsigned char buf2[4];

    memset((void *)buff, 0, sizeof(buff));
    n = fread((void *)buff, 1, sizeof(buff), f);
    if (n == -1) {
	vmterror(t, "Error determining tape file format: %s",
		 os_strerror(errno));
	return -1;
    }
    rewind(f);

    /* Only possibilities for all sizes less than 10 are:
       0 = anything
       1 = -
       2 = one TPC tapemark
       3 = -
       4 = one TPS/TPE tapemark, two TPC tapemarks, or one TPC record
       5 = -
       6 = 3 TPC tapemarks, one TPC tapemark & record, or one TPC record
       7 = -
       8 = two TPS/TPE tapemarks, or some combos of TPC records/tapemarks
       9 = one TPE record
     */
    if (n != sizeof(buff)) {
	switch (n) {
	case 2:		/* Drop thru for TPC check */
	    break;
	case 4:
	    if (VMT_TPS_COUNT(&buff[0]) == 0) {
		/* File is single TPS/TPE or double TPC tapemark */
		if (   (fmt == VMT_FMT_UNK)
		    || (fmt == VMT_FMT_RAW))
		    return VMT_FMT_TPS;
		if (   (fmt == VMT_FMT_TPE)
		    || (fmt == VMT_FMT_TPS)
		    || (fmt == VMT_FMT_TPC))
		    return fmt;
		return VMT_FMT_UNK;	/* Not what we wanted */
	    }
	    break;
	case 6:		/* Drop thru for TPC check */
	    break;
	case 8:
	    if ((VMT_TPS_COUNT(&buff[0]) == 0)
		&& (VMT_TPS_COUNT(&buff[4]) == 0)) {
		/* File is double TPS/TPE tapemark (or quad TPC) */
		if (   (fmt == VMT_FMT_UNK)
		    || (fmt == VMT_FMT_RAW))
		    return VMT_FMT_TPS;
		if (   (fmt == VMT_FMT_TPE)
		    || (fmt == VMT_FMT_TPS)
		    || (fmt == VMT_FMT_TPC))
		    return fmt;
		return VMT_FMT_UNK;	/* Not what we wanted */
	    }
	    break;
	case 9:	/* I'm sorry I even wrote this */
	    if ((VMT_TPS_COUNT(&buff[0]) == 1)
		&& (VMT_TPS_COUNT(&buff[5]) == 1)) {
		/* File is TPE 1-byte record */
		if (   (fmt == VMT_FMT_UNK)
		    || (fmt == VMT_FMT_RAW)
		    || (fmt == VMT_FMT_TPE))
		    return VMT_FMT_TPE;
		return VMT_FMT_UNK;	/* Not what we wanted */
	    }
	    break;
	default:	/* 0, 1, 3, 5, 7 */
	    return VMT_FMT_UNK;
	}
	/* Come here if length is even and TPS/TPE didn't work  */
	if (   (fmt == VMT_FMT_UNK)
	    || (fmt == VMT_FMT_RAW)
	    || (fmt == VMT_FMT_TPC)) {
	    /* Check for TPC */
	    for (i = 0; i < n; i += 2) {
		if (cnt = VMT_TPC_COUNT(&buff[i]))
		    i += (cnt&01) ? (cnt+1) : cnt;
	    }
	    if (i == n)		/* Verify no overrun */
		return VMT_FMT_TPC;
	}
	return VMT_FMT_UNK;
    }

    /* Check for ASCII control file */
    for (i = 0; i < n; ++i)
	if (!isspace(buff[i]) && !isprint(buff[i]))
	    break;
    if (i == n) {
	/* Looks like a control file */
	if (   (fmt == VMT_FMT_UNK)
	    || (fmt == VMT_FMT_CTL))
	    return VMT_FMT_CTL;
	return VMT_FMT_UNK;	/* Not what we wanted */
    }

    if ((fmt == VMT_FMT_UNK) || (fmt != VMT_FMT_CTL)) {
	/* Not ASCII, check for TPS.  32-bit count must be replicated at end
	   of record, and impose 16-bit length sanity check.
	   XXX: If count is odd, we have an opportunity to distinguish between
	   TPS (which pads) and TPE (which doesn't).
	 */
	cnt = cnt1 = VMT_TPS_COUNT(&buff[0]) & VMT_TPS_CNTF;
	cnt1 += (cnt1 & 01);		/* Round up to 2-byte boundary */
	if ((cnt1 <= (1L<<16))		/* Limit to 16 bits of record length */
	    && (fseek(f, (long)cnt1+4, SEEK_SET) == 0)
	    && (fread((void *)buf2, 1, sizeof(buf2), f) == sizeof(buf2))
	    && (memcmp((void *)buff, (void *)buf2, sizeof(buf2)) == 0)) {
	    /* Success */
	    rewind(f);
	    
	    if (   (fmt == VMT_FMT_UNK)
		|| (fmt == VMT_FMT_RAW))
		return VMT_FMT_TPS;
	    if (   (fmt == VMT_FMT_TPE)
		|| (fmt == VMT_FMT_TPS))
		return fmt;
	    return VMT_FMT_UNK;	/* Not what we wanted */
	}
    }
    
    /* Give up.  No test for VMT_FMT_TPC as it could look like
       almost anything.
    */
    rewind(f);
    return VMT_FMT_UNK;
}


enum vmtfmt
vmt_strtofmt(char *str)
{
    register int i;

    for (i = 0; i < VMT_FMT_N; ++i) {
	if (vmtfmttab[i].tf_name
	  && (strcasecmp(str, vmtfmttab[i].tf_name)==0)) {
	    if (vmtfmttab[i].tf_flags & VMTFF_ALIAS)
		return vmtfmttab[i].tf_flags & VMTFF_FMT;
	    return i;
	}
    }
    return 0;
}

enum vmtfmt
vmt_exttofmt(char *ext)
{
    register int i;

    for (i = 0; i < VMT_FMT_N; ++i) {
	if (vmtfmttab[i].tf_ext
	  && (strcasecmp(ext, vmtfmttab[i].tf_ext)==0)) {
	    if (vmtfmttab[i].tf_flags & VMTFF_ALIAS)
		return vmtfmttab[i].tf_flags & VMTFF_FMT;
	    return i;
	}
    }
    return 0;
}

#if VMTAPE_ARGSTR	/* Include optional mount arg string parsing */ 

#include "prmstr.h"

#define VMTMNT_PARAMS \
    prmdef(VMTP_HARD,  "hard"),  /* Device, not virtual tape */\
    prmdef(VMTP_DEBUG, "debug"), /* Virtual: TRUE for debug output */\
    prmdef(VMTP_FMT,   "fmt"),   /* Virtual: format= */\
    prmdef(VMTP_MODE,  "mode"),  /* Virtual: mode= */\
    prmdef(VMTP_PATH,  "path"),  /* Virtual: path= */\
    prmdef(VMTP_CPATH, "cpath"), /* Virtual: cpath= */\
    prmdef(VMTP_RPATH, "rpath"), /* Virtual: rpath= */\
    prmdef(VMTP_UNZIP, "unzip"), /* Virtual: TRUE to allow uncompression */\
    prmdef(VMTP_INMEM, "inmem"), /* Virtual: TRUE to suck into memory */\
    prmdef(VMTP_FSKIP, "fskip"), /* Virtual: fskip=# files to skip */\
    prmdef(VMTP_RO,    "ro"),	 /* Same as mode=read */\
    prmdef(VMTP_RW,    "rw"),	 /* Same as mode=create */\
    prmdef(VMTP_READ,  "read"),  /* Same as mode=read */\
    prmdef(VMTP_CREATE,"create"),/* Same as mode=create */\
    prmdef(VMTP_UPDATE,"update") /* Same as mode=update */

enum {
# define prmdef(i,s) i
	VMTMNT_PARAMS
# undef prmdef
};

static char *vmtprmtab[] = {
# define prmdef(i,s) s
	VMTMNT_PARAMS
# undef prmdef
	, NULL
};

static int
vmtparmode(char *cp, int *mode)
{
    if      (strcasecmp(cp, "read"  )==0) *mode = VMT_MODE_RDONLY;
    else if (strcasecmp(cp, "create")==0) *mode = VMT_MODE_CREATE;
    else if (strcasecmp(cp, "update")==0) *mode = VMT_MODE_UPDATE;
    else return FALSE;
    return TRUE;
}

static int
vmtparfmt(char *cp, enum vmtfmt *fmt)
{
    return (*fmt = vmt_strtofmt(cp)) ? TRUE : FALSE;
}


int
vmt_attrparse(register struct vmtape *t,
	      register struct vmtattrs *ta,
	      char *argstr)
{
    int i, ret = TRUE;
    struct prmstate_s prm;
    char buff[200];
    long lval;

    if (!argstr)
	return TRUE;
    prm_init(&prm, buff, sizeof(buff),
		argstr, strlen(argstr),
		vmtprmtab, sizeof(vmtprmtab[0]));
    while ((i = prm_next(&prm)) != PRMK_DONE) {
	switch (i) {
	case PRMK_NONE:
	    vmterror(t,"Unknown mount parameter \"%s\"", prm.prm_name);
	    ret = FALSE;
	    continue;
	case PRMK_AMBI:
	    vmterror(t,"Ambiguous mount parameter \"%s\"", prm.prm_name);
	    ret = FALSE;
	    continue;
	default:	/* Handle matches not supported */
	    vmterror(t,"Unsupported mount parameter \"%s\"", prm.prm_name);
	    ret = FALSE;
	    continue;

	/* --------- specific args ----------- */

	case VMTP_FMT:	/* Virtual: format= */
	    if (!prm.prm_val || !vmtparfmt(prm.prm_val, &ta->vmta_fmtreq))
		break;
	    ta->vmta_mask |= VMTA_FMTREQ;
	    continue;

	case VMTP_MODE:	/* Virtual: mode= */
	    if (!prm.prm_val || !vmtparmode(prm.prm_val, &ta->vmta_mode))
		break;
	    ta->vmta_mask |= VMTA_MODE;
	    continue;

	case VMTP_FSKIP: /* Virtual: fskip= */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    ta->vmta_fskip = lval;
	    ta->vmta_mask |= VMTA_FSKIP;
	    continue;

#define VMTPMODE(v) \
	if (prm.prm_val) break; /* No arg allowed */\
	ta->vmta_mode = (v); \
	ta->vmta_mask |= VMTA_MODE

	case VMTP_RO:	/* Same as mode=read */
	case VMTP_READ:	/* Same as mode=read */
	    VMTPMODE(VMT_MODE_RDONLY);
	    continue;

	case VMTP_RW:		/* Compatibility: Create, not Update */
	case VMTP_CREATE:	/* Same as mode=create */
	    VMTPMODE(VMT_MODE_CREATE);
	    continue;

	case VMTP_UPDATE:	/* Same as mode=update */
	    VMTPMODE(VMT_MODE_UPDATE);
	    continue;
#undef VMTPMODE

#define VMTPBOOL(v, default) \
	(prm.prm_val ? s_tobool(prm.prm_val, &(v)) \
		     : (((v) = (default)), TRUE))

	case VMTP_UNZIP:	/* Virtual: TRUE to allow uncompression */
	    if (!VMTPBOOL(ta->vmta_unzip, TRUE))
		break;
	    ta->vmta_mask |= VMTA_UNZIP;
	    continue;

	case VMTP_INMEM:	/* Virtual: TRUE to suck into memory */
	    if (!VMTPBOOL(ta->vmta_inmem, TRUE))
		break;
	    ta->vmta_mask |= VMTA_INMEM;
	    continue;

	case VMTP_DEBUG:	/* Virtual: TRUE for debug output */
	    /* Note this parameter operates immediately on the vmtape
	       structure itself, not the vmtattrs struct!
	    */
	    if (!VMTPBOOL(t->mt_debug, TRUE))
		break;
	    ta->vmta_mask |= VMTA_INMEM;
	    continue;
#undef VMTPBOOL

#define VMTPSTR(v,siz,attr) \
    if (!prm.prm_val) break; \
    if (strlen(prm.prm_val) >= siz) { \
	vmterror(t, "mount param \"%s\" too long (max %d)", \
		 prm.prm_name, (int)siz); \
	ret = FALSE; \
	continue; \
    } else strcpy((v), prm.prm_val); \
    ta->vmta_mask |= (attr);

	case VMTP_HARD:		/* Device, not actually virtual tape */
	    if (!prm.prm_val)		/* No arg => default  */
		prm.prm_val = "*";
	    VMTPSTR(ta->vmta_dev, sizeof(ta->vmta_dev), VMTA_DEV);
	    continue;

	case VMTP_PATH:	/* Virtual: path= */
	    VMTPSTR(ta->vmta_path, sizeof(ta->vmta_path), VMTA_PATH);
	    continue;

	case VMTP_CPATH:	/* Virtual: cpath= */
	    VMTPSTR(ta->vmta_ctlpath, sizeof(ta->vmta_ctlpath), VMTA_CTLPATH);
	    continue;

	case VMTP_RPATH:	/* Virtual: rpath= */
	    VMTPSTR(ta->vmta_datpath, sizeof(ta->vmta_datpath), VMTA_DATPATH);
	    continue;
#undef VMTPSTR
	}

	/* Break out to here for generic arg complaints */
	ret = FALSE;
	if (prm.prm_val)
	    vmterror(t,"mount param \"%s\" has bad value: \"%s\"",
		     prm.prm_name, prm.prm_val);
	else
	    vmterror(t,"mount param \"%s\" has missing value",
		     prm.prm_name);
    }

    /* Param string all done, do followup checks or cleanup */

    return ret;
}



#endif /* VMTAPE_ARGSTR */

#if VMTAPE_POPEN
static struct vmtpipent {
    char *zext;
    int   zextlen;
    char *pcmd;
} vmtpipetab[] = {
# define UNGZIPCMD "gzip  -dc "	/* Uncompress to standard output */
# define UNBZIPCMD "bzip2 -dc "	/* Uncompress to standard output */
# define VMTPIPECMD_MAX sizeof(UNBZIPCMD)	/* Longest command */
    { ".Z",  2, UNGZIPCMD},	/* Try standard compress extension */
    { ".z",  2, UNGZIPCMD},	/* Try new GZIP convention */
    { ".gz", 3, UNGZIPCMD},	/* Try newer GZIP convention */
    { "-z",  2, UNGZIPCMD},	/* Try other GZIP convention */
    { ".bz", 3, UNBZIPCMD},	/* Try BZIP convention */
    { ".bz2",4, UNBZIPCMD},	/* Try newer BZIP2 convention */
    { NULL, 0, NULL}
};

#define VMTPIPECMDQUOT ";&()|^<>\n \t#'\"\\*?[]$"

static int
popenquote(char *str, char *qchrs, int max)
{
    register char *cp, *qp;
    register size_t i;
    char qbuf[OS_MAXPATHLEN*2+1];

    if (!(cp = strpbrk(str, qchrs)))
	return TRUE;		/* No actual quoting needed */
    i = cp - str;
    if (max > (sizeof(qbuf)-1))
	max = sizeof(qbuf)-1;

    /* Ugh, must quote. */
    qp = qbuf;
    cp = str;
    for (;;) {
	if ((max -= (i+2)) <= 0)
	    return FALSE;	/* Oops, too long */
	if (i) {		/* First copy in-between chars */
	    memcpy(qp, cp, i);
	    qp += i, cp += i;
	}
	*qp++ = '\\';		/* Insert backslash as quoter */
	*qp++ = *cp++;		/* Then copy quoted char */
	if (*cp == '\0')
	    break;
	i = strcspn(cp, qchrs);
    }
    *qp++ = '\0';
    memcpy(str, qbuf, (qp-qbuf));	/* Copy back over original string */
    return TRUE;
}
#endif /* VMTAPE_POPEN */

static int
datf_open(register struct vmtape *t,
	  char *fname, char *mode)
{
    FILE *f;
    int zmode;

    if (*mode == 'z') {
	zmode = TRUE;
	++mode;
    } else zmode = FALSE;

    /* Attempt to open filespec as given */
    t->mt_ispipe = FALSE;
    t->mt_datf = fopen(fname, mode);

    if (t->mt_debug)
	vmterror(t, "datf_open %s %s",
		 fname, (t->mt_datf ? "succeeded" : "failed"));


#if VMTAPE_POPEN
    if (zmode)
  {
    int flen = strlen(fname);
    register int i;
    register char *cp, *ecp;
    struct vmtpipent *p;
    int bzipf = FALSE;
    char cmdbuf[VMTPIPECMD_MAX+(OS_MAXPATHLEN*2)]; /* *2 to allow quoting */

    if ((flen + VMTPIPECMD_MAX+2) >= sizeof(cmdbuf)-1) {
	/* Filespec too big, cannot hack */
	return (t->mt_datf ? TRUE : FALSE);
    }
    cp = cmdbuf + VMTPIPECMD_MAX;

    /* Do checking for unzip hackery! */
    if (t->mt_datf) {
	/* See whether file just opened has an extension that indicates
	   compression.  If so, re-open it through an uncompress pipe.
	*/
	ecp = fname + flen;
	for (p = vmtpipetab; p->zext; ++p) {
	    if ((flen > p->zextlen)
		&& (strcmp(ecp - p->zextlen, p->zext)==0)) {
		/* Found it! */
		strcpy(cp, fname);
		goto dounzip;
	    }
	}
	return TRUE;
    }

    /* Furnished filespec didn't exist.  Try adding known compression
    ** extensions, and if found, invoke subprocess to uncompress it.
    */
    memcpy(cp, fname, flen);		/* Set up filename */
    ecp = cp + flen;			/* Start of added extension */

    for (p = vmtpipetab; p->zext; ++p) {
	strcpy(ecp, p->zext);
	t->mt_datf = fopen(cp, mode);
	if (t->mt_debug)
	    vmterror(t, "datf_open %s %s",
		     cp, (t->mt_datf ? "succeeded" : "failed"));

	if (t->mt_datf)
 	    goto dounzip;
    }
    return FALSE;		/* Couldn't find a match, give up */

    /* File exists!  Fire up an unzipper... */
  dounzip:
    fclose(t->mt_datf);
    t->mt_datf = NULL;

    memset(cmdbuf, ' ', VMTPIPECMD_MAX);	/* Init command with spaces */
    memcpy(cmdbuf, p->pcmd, strlen(p->pcmd));	/* Prefix command */
    if (!popenquote(cp, VMTPIPECMDQUOT, sizeof(cmdbuf)-VMTPIPECMD_MAX))
	return FALSE;			/* Couldn't quote, shouldn't happen */

    if (t->mt_datf = popen(cmdbuf, (*mode == 'r') ? "r" : "w"))
	t->mt_ispipe = TRUE;
  }
#endif /* VMTAPE_POPEN */

    return (t->mt_datf ? TRUE : FALSE);
}


static void
datf_close(register struct vmtape *t)
{
    if (t->mt_datf) {
	if (vmt_iswritable(t)) {	/* If may have output something, */
	    fflush(t->mt_datf);
	}

#if VMTAPE_POPEN
	if (t->mt_ispipe) {
	    pclose(t->mt_datf);
	} else
#endif
	    fclose(t->mt_datf);
    }
    t->mt_ispipe = FALSE;
    t->mt_datf = NULL;
}

/* Tape positioning functions */

/* Utility subroutines must return a value as follows:
*/
enum vmtfsret {
    VMT_FSRET_ERR=-1,	/* -1 = Error */
    VMT_FSRET_NONE,	/*  0 = no files spaced, at BOT or EOT */
    VMT_FSRET_ONE,	/*  1 = one file spaced, no more */
    VMT_FSRET_MORE	/*  2 = one file spaced, may be more */
};

#if VMTAPE_ITSDUMP
static enum vmtfsret its_filebwd(struct vmtape *t);
static enum vmtfsret its_filefwd(struct vmtape *t);
#endif
static int tps_recfwd(struct vmtape *t);
static int tps_recbwd(struct vmtape *t);
static int tpc_recfwd(struct vmtape *t);
static int raw_recfwd(struct vmtape *t);
static int raw_recbwd(struct vmtape *t);
static void rawposfix(struct vmtape *t);
static int td_posstate(struct vmttdrdef *td);

int
vmt_rewind(register struct vmtape *t)
{
    int res;

    if (!vmt_ismounted(t))
	return FALSE;		/* No tape mounted */

    t->mt_bot = TRUE;
    t->mt_eof = t->mt_eot = FALSE;

    switch(t->mt_format) {

    default:
	return FALSE;			/* Unknown format */
#if VMTAPE_ITSDUMP
    case VMT_FMT_ITS:
	t->mt_fcur = NULL;		/* Back to start of filedefs */
	datf_close(t);			/* Maybe check for error? */
	t->mt_cnt = 4;
	t->mt_ptr = itsdumphead;
	t->mt_state = TS_THEAD;
	break;
#endif

    case VMT_FMT_RAW:
	t->mt_tdr.tdcur = NULL;	/* Clear vmtrecdefs */
	t->mt_tdr.tdrncur = 0;
	/* Drop thru to common code */

    case VMT_FMT_TPE:
    case VMT_FMT_TPS:
    case VMT_FMT_TPC:
	res = TRUE;
	if (vmt_iswritable(t)) {    /* If open for reading/writing, */
	    res = vmt_eot(t);	/* Flush buffs, dump out any tape dir so far */
	}
	t->mt_state = TS_RDATA;
	if (t->mt_datf) {
	    rewind(t->mt_datf);
	    if (ferror(t->mt_datf) || !res)
		return FALSE;
	}
	break;
    }
    return TRUE;
}

/* Space forward or backward N records.
**	A tapemark stops the spacing without being included in the count
** (this is how the TM03 behaves).
** Sets mt_frames to the # of records spaced over.
** Returns FALSE if there was some internal error; "data" errors are ignored.
*/

int
vmt_rspace(register struct vmtape *t,
	   int revf,		/* 0 forward, else backward */
	   register unsigned long cnt)
{
    unsigned long origcnt = cnt;

    if (!vmt_ismounted(t))
	return FALSE;

    switch (t->mt_format) {
#if VMTAPE_ITSDUMP
    case VMT_FMT_ITS:
	if (revf) {	/* Can only backspace over one tapemark */
	    /* If just read a tapemark, back up over it. */
	    if (t->mt_eof) {
		t->mt_eof = FALSE;
		t->mt_state = TS_FEOF;
		t->mt_frames = 0;
		return TRUE;
	    } else if (!t->mt_bot) {
		/* Otherwise, hack it by backing up to start of current file */
		(void) its_filebwd(t);
		t->mt_frames = 1;
	    }
	    return TRUE;
	} else do {
	    w10_t w;
	    vmt_iobeg(t, FALSE);
	    while (its_wget(t, &w));		/* Space forward a record */
	    if (t->mt_frames)
		--cnt;
	    if (t->mt_eof || t->mt_eot)
		break;
	} while (cnt);
	break;
#endif	/* VMTAPE_ITSDUMP */
    case VMT_FMT_RAW:
	t->mt_eof = t->mt_eot = t->mt_bot = FALSE;
	rawposfix(t);			/* Canonicalize current position */
	if (revf) {
	    while (raw_recbwd(t) > 0
		&& !t->mt_eof && !t->mt_bot && --cnt) ;
	} else 
	    while (raw_recfwd(t) > 0
		&& !t->mt_eof && !t->mt_eot && --cnt) ;
	break;
    case VMT_FMT_TPE:
    case VMT_FMT_TPS:
	t->mt_eof = t->mt_eot = t->mt_bot = FALSE;
	if (revf) {
	    while (tps_recbwd(t) > 0
		&& !t->mt_eof && !t->mt_bot && --cnt) ;
	} else 
	    while (tps_recfwd(t) > 0
		&& !t->mt_eof && !t->mt_eot && --cnt) ;
	break;

    case VMT_FMT_TPC:
	/* Can space forward but not backward. */
	if (revf) {
	    /* XXX Attempt hackery to allow backspacing over 1 tapemark?
	    */
	    if ( 0 /* t->mt_eof */) {	/* If last thing read was an EOF */
		
	    } else {
		t->mt_frames = 0;
		return FALSE;
	    }
	} else 
	    t->mt_eof = t->mt_eot = t->mt_bot = FALSE;
	    while (tpc_recfwd(t) > 0
		&& !t->mt_eof && !t->mt_eot && --cnt) ;
	break;
    }
    t->mt_frames = origcnt - cnt;
    return TRUE;
}

/* Space forward or backward N files (tapemarks).
**  Sets mt_frames to the # of files spaced over (normally the # of tapemarks
**  seen, but will include BOT/EOT if tape moved to get there).
**  Returns FALSE if there was some internal error; "data" errors are ignored.
*/
int
vmt_fspace(register struct vmtape *t,
	   int dir,		/* 0 forward, else backward */
	   unsigned long cnt)
{
    long origcnt = cnt;
    enum vmtfsret (*foo)(struct vmtape *) = NULL;
    int (*recmove)(struct vmtape *);

    if (t->mt_debug)
	vmterror(t, "vmt_fspace %ld %s", cnt, (dir ? "rev" : "fwd"));

    t->mt_frames = 0;

    if (!vmt_ismounted(t))
	return FALSE;

    switch (t->mt_format) {
    case VMT_FMT_RAW:
	rawposfix(t);		/* Ensure in canonical state */
	recmove = dir ? raw_recbwd : raw_recfwd;
	break;
    case VMT_FMT_TPE:
    case VMT_FMT_TPS:
	recmove = dir ? tps_recbwd : tps_recfwd;
	break;
    case VMT_FMT_TPC:
	if (dir)		/* TPC cannot go backwards */
	    return FALSE;
	recmove = tpc_recfwd;
	break;
#if VMTAPE_ITSDUMP
    case VMT_FMT_ITS:
	foo = dir ? its_filebwd : its_filefwd;
	break;
#endif
    default:
	return FALSE;
    }

    /* Do general loop */
    do {
	register int fsret, res;
	if (foo)
	    fsret = (*foo)(t);
	else if (dir) {
	    /* Generic backward loop */
	    t->mt_eof = t->mt_eot = t->mt_bot = FALSE;
	    while ((res = (*recmove)(t)) > 0 && !t->mt_eof && !t->mt_bot) ;
	    if (res <= 0)
		fsret = (res < 0) ? VMT_FSRET_ERR : VMT_FSRET_NONE;
	    else
		fsret = t->mt_bot ? VMT_FSRET_ONE : VMT_FSRET_MORE;

	} else {
	    /* Generic forward loop */
	    t->mt_eof = t->mt_eot = t->mt_bot = FALSE;
	    while ((res = (*recmove)(t)) > 0 && !t->mt_eof && !t->mt_eot) ;
	    if (res <= 0)
		fsret = (res < 0) ? VMT_FSRET_ERR : VMT_FSRET_NONE;
	    else
		fsret = t->mt_eot ? VMT_FSRET_ONE : VMT_FSRET_MORE;
	}

	switch (fsret) {
	default:
	case VMT_FSRET_ERR:
	    return FALSE;
	case VMT_FSRET_NONE:
	    return TRUE;
	case VMT_FSRET_ONE:
	    t->mt_frames++;
	    return TRUE;
	case VMT_FSRET_MORE:
	    t->mt_frames++;
	    break;
	}
    } while (--cnt);
    return TRUE;
}

static int
vmtflushinp(register struct vmtape *t,
	    size_t reclen)
{
    size_t cnt, res;
    unsigned char buf[1024];

    for (; reclen > 0; reclen -= cnt) {
	cnt = (reclen < sizeof(buf) ? reclen : sizeof(buf));
	if ((res = fread(buf, 1, cnt, t->mt_datf)) != cnt) {
	    vmterror(t, "input rec error: %ld != %ld, data file \"%.256s\"",
		     res, cnt,
		     t->mt_filename);
	    vmtseterr(t);
	    return FALSE;
	}
    }
    return TRUE;
}

/* Format-specific positioning functions */

#if VMTAPE_ITSDUMP
static struct vmtfildef *fd_prevget(register struct vmtape *t);
static struct vmtfildef *fd_nextget(register struct vmtape *t);

static enum vmtfsret
its_filebwd(register struct vmtape *t)
{
    switch (t->mt_state) {
	case TS_THEAD:
	    vmt_rewind(t);
	    return VMT_FSRET_NONE;

	case TS_FHEAD:
	case TS_FDATA:
	case TS_FEOF:
	    if (fd_prevget(t))
		t->mt_state = TS_FEOF;
	    else
		vmt_rewind(t);
	    break;

	case TS_FNEXT:
	    t->mt_eof = TRUE;
	    t->mt_state = TS_FEOF;
	    break;

	case TS_EOT:
	    t->mt_eof = TRUE;
	    t->mt_state = TS_FEOF;
	    break;
	default:
	    return VMT_FSRET_ERR;
	}
    return VMT_FSRET_MORE;
}

static enum vmtfsret
its_filefwd(register struct vmtape *t)
{
    switch (t->mt_state) {
	case TS_THEAD:
	case TS_FHEAD:
	case TS_FDATA:
	case TS_FEOF:
	    t->mt_eof = TRUE;
	    t->mt_state = TS_FNEXT;
	    break;
		
	case TS_FNEXT:
	    fd_nextget(t);		/* Move to next file, changes state */
	    break;			/* properly if no more files */

	case TS_EOT:
	    t->mt_eot = TRUE;
	    return VMT_FSRET_NONE;

	default:
	    return VMT_FSRET_ERR;
	}
    return VMT_FSRET_MORE;
}

static struct vmtfildef *
fd_prevget(register struct vmtape *t)
{
    if (t->mt_fcur) {
	if (t->mt_fcur == t->mt_fdefs) {
	    t->mt_fcur = NULL;
	} else
	    t->mt_fcur = t->mt_fcur->fdprev;
    }
    return t->mt_fcur;
}

static struct vmtfildef *
fd_nextget(register struct vmtape *t)
{
    if (!(t->mt_fcur))
	t->mt_fcur = t->mt_fdefs;
    else {
	t->mt_fcur = t->mt_fcur->fdnext;
	if (t->mt_fcur == t->mt_fdefs) {	/* Back at start? */
	    t->mt_fcur = t->mt_fcur->fdprev;	/* Yep, back up */
	    t->mt_eof = TRUE;			/* No more files */
	    t->mt_state = TS_EOT;		/* So this is really end */
	    return NULL;
	}
    }
    return t->mt_fcur;
}

#endif /* VMTAPE_ITSDUMP */


/* =================================================
	   	RAW format positioning
 */

static struct vmtrecdef *rd_prevget(struct vmtape *t);


/* Move backwards over raw tape's previous record/tapemark.
** 	rawposfix() must have been called prior to one or more calls
** of this routine.
*/
static struct vmtrecdef *
rd_prevget(register struct vmtape *t)
{
    register struct vmttdrdef *td = &t->mt_tdr;
    register struct vmtrecdef *rd;

    if (!(rd = td->tdcur)) {	/* At EOT? */
	if (!(rd = td->tdrd)) {
	    return NULL;	/* Cannot move back, still at EOT (& BOT) */
	}
	/* Back up from EOT.  rdprev will point to last record, so
	** dropping through does the right thing.
	*/
    } else {
	/* Back up from current location */
	if (--(td->tdrncur) >= 0)	/* If all goes well, */
	    return rd;			/* simply return this vmtrecdef! */

	/* Oops, must get previous vmtrecdef */
	if (rd == td->tdrd) {		/* If this vmtrecdef is already first */
	    td->tdrncur = 0;		/* stay at BOT */
	    return NULL;		/* and say couldn't move back */
	}
    }

    /* Back up to end of previous vmtrecdef */
    td->tdcur = rd = rd->rdprev;	/* Must back up to previous vmtrecdef */
    if ((td->tdrncur = rd->rdcnt) != 0)	/* Unless it's a tapemark, */
	--(td->tdrncur);		/* move over last record in it. */

    return rd;
}


/* Canonicalize raw tape position prior to doing something
**	Ensures that current position is correctly set up based on
**	values of tdcur and tdrncur.  Ignores current state but sets it
**	as if about to read next record or tapemark.
*/
static void
rawposfix(register struct vmtape *t)
{
    register struct vmttdrdef *td = &t->mt_tdr;

    if (!td->tdcur) {			/* If no current record def */
	if (!td->tdrncur) {		/* Check BOT or EOT */
	    if (!(td->tdcur = td->tdrd))	/* BOT, try to start there */
		td->tdrncur = 1;		/* Ugh, make it EOT. */
	}
    } else if (td->tdrncur && td->tdrncur >= td->tdcur->rdcnt) {
	/* Need next record def */
	td->tdrncur = 0;		/* Read 1st record of next def */
	if ((td->tdcur = td->tdcur->rdnext)
			== td->tdrd) {
	    td->tdcur = NULL;		/* Oops, reached EOT, so clobber. */
	    td->tdrncur = 1;		/* Distinguish from BOT. */
	}
    }

    /* Now set tape state appropriately. */
    t->mt_state = td_posstate(td);
}

/* Return raw tape state (a TS_ value) based on its current
** logical position.
*/
static int
td_posstate(register struct vmttdrdef *td)
{
    return
	  (td->tdcur == NULL) ? TS_EOT		/* Next read returns EOT */
	: (td->tdcur->rdlen == 0
	   || td->tdcur->rdcnt == 0) ? TS_REOF	/* Next read returns EOF */
	: TS_RDATA;				/* Next read returns data */
}

/* Space 1 record forward.
** 	rawposfix() must have been called prior to one or more calls
** of this routine.
** Returns 1 (true) on success,
** 0 (false) if cannot space forward (ie already at EOT)
** -1 if error (otherwise same as 0)
** A tapemark causes a success return with mt_eof set TRUE.
*/
static int
raw_recfwd(register struct vmtape *t)
{
    register struct vmtrecdef *rd;

    if (!(rd = t->mt_tdr.tdcur)) {	/* If at EOT */
	t->mt_eot = TRUE;
	return 0;
    }
    if (rd->rdlen == 0 || rd->rdcnt == 0)
	t->mt_eof = TRUE;
    else if (t->mt_ispipe)
	(void) vmtflushinp(t, (size_t)(rd->rdlen));

    t->mt_tdr.tdrncur++;	/* Bump to next record */
    rawposfix(t);		/* Canonicalize and set possibly new state */
    return 1;
}

static int
raw_recbwd(register struct vmtape *t)
{
    register struct vmtrecdef *rd;

    if (!(rd = rd_prevget(t))) {
	t->mt_bot = TRUE;		/* Couldn't move back, so fail */
    } else if (t->mt_tdr.tdrncur == 0) {
	if (rd->rdcnt == 0)		/* Backed up over a tapemark? */
	    t->mt_eof = TRUE;
	if (rd == t->mt_tdr.tdrd)
	    t->mt_bot = TRUE;
    }
    t->mt_state = td_posstate(&t->mt_tdr);
    return rd ? 1 : 0;
}

/* =================================================
	   	TPS format positioning
 */

/* Space 1 record forward.
** Returns 1 on success, 0 failure, -1 error.
*/
static int
tps_recfwd(register struct vmtape *t)
{
    long ioptr;
    int res;
    uint32 len;
    unsigned char header[4];

    /* Ensure set up for reading */
    if (t->mt_state != TS_RDATA)
	vmt_iobeg(t, FALSE);

    if ((res = fread(header, 1, sizeof(header), t->mt_datf))
	 != sizeof(header)) {
	if (feof(t->mt_datf)) {	/* If at EOT, ignore even partial header */
	    t->mt_eot = TRUE;
	    return 0;		/* Fail */
	} else {
	    vmterror(t, "rsp partial header: %d, data file \"%.256s\"",
		     res,
		     t->mt_filename);
	    /* What else to do??  Read/write gubbish... */
	    vmtseterr(t);
	    return -1;
	}
    }
    len = (header[3]<<24) | (header[2]<<16) | (header[1]<<8) | header[0];
    if (len & VMT_TPS_ERRF) {
	/* Error - for now, skip over it anyway */
	len &= MASK31;
    }
    if (len == 0) {
	t->mt_eof = TRUE;		/* Indicate hit tapemark */
	return 1;			/* and succeed */
    }

    /* Skip over data.  Don't bother checking post-data header, to allow
       moving over possible bad stuff.  Also ignore fseek error, next
       i/o attempt will catch it.
     */
    if ((len & 01) && (t->mt_format == VMT_FMT_TPS))
	len++;				/* TPS pads up, VMT_FMT_TPE doesn't */ 
    ioptr = len + sizeof(header);
    if (t->mt_ispipe)
	return vmtflushinp(t, ioptr) ? 1 : -1;
    else {
	if (fseek(t->mt_datf, ioptr, SEEK_CUR) != 0) {
	    vmterror(t, "rsp seek error: %d, data file \"%.256s\"",
		     errno,
		     t->mt_filename);
	    /* What else to do??  Read/write gubbish... */
	    vmtseterr(t);
	    return -1;
	}
    }
    return 1;
}

static int
tps_recbwd(register struct vmtape *t)
{
    long ioptr;
    int res;
    uint32 len, hlen;
    unsigned char header[4];

    /* Ensure set up for reading */
    if (t->mt_state != TS_RDATA)
	vmt_iobeg(t, FALSE);

    if (t->mt_ispipe) {
	vmterror(t, "Cannot backspace on pipe - data file \"%.256s\"",
		 t->mt_datpath);
	/* What else to do??  Read/write gubbish... */
	vmtseterr(t);
	return -1;
    }

    if (fseek(t->mt_datf, -sizeof(header), SEEK_CUR)) {
	if ((ioptr = ftell(t->mt_datf)) == 0) {
	    t->mt_bot = TRUE;
	    return 0;
	}
	vmterror(t, "bsp seek/tell failure: %ld, data file \"%.256s\"",
		 (long)ioptr,
		 t->mt_datpath);
	/* What else to do??  Read/write gubbish... */
	vmtseterr(t);
	return -1;
	
    }
    if ((res = fread(header, 1, sizeof(header), t->mt_datf))
	 != sizeof(header)) {
	if (feof(t->mt_datf)) {	/* If at EOT, ignore even partial header */
	    t->mt_eot = TRUE;
	    return 0;		/* Fail */
	} else {
	    vmterror(t, "rsp partial trailer: %d, data file \"%.256s\"",
		     res,
		     t->mt_datpath);
	    /* What else to do??  Read/write gubbish... */
	    vmtseterr(t);
	    return -1;
	}
    }

    len = VMT_TPS_COUNT(header);
    if (len & VMT_TPS_ERRF) {
	/* Error - for now, skip over it anyway */
	len &= MASK31;
    }
    if (len == 0) {
	t->mt_eof = TRUE;		/* Indicate hit tapemark */
	/* Must still back up over header again, drop thru */
	ioptr = -sizeof(header);
    } else {
	/* Skip back over data, position at start of record header.
	 */
	if ((len & 01) && (t->mt_format == VMT_FMT_TPS))
	    len++;			/* TPS pads up, VMT_FMT_TPE doesn't */ 
	ioptr = -(len + (sizeof(header)*2));
    }
    (void) fseek(t->mt_datf, ioptr, SEEK_CUR);

    if (len == 0)		/* If tapemark, don't bother checking header */
	return 1;

    /* Paranoia -- verify header matches trailer
     */
    if ((res = fread(header, 1, sizeof(header), t->mt_datf))
	 != sizeof(header)) {
	vmterror(t, "rsp partial header: %d, data file \"%.256s\"",
		     res,
		     t->mt_datpath);
	/* What else to do??  Read/write gubbish... */
	vmtseterr(t);
	return -1;
    }
    hlen = VMT_TPS_COUNT(header);
    if (hlen & VMT_TPS_ERRF)
	hlen &= MASK31;
    if (len != hlen) {
	vmterror(t, "rsp header-trailer mismatch: %0lX vs %0lX for \"%.256s\"",
		 (long)hlen, (long)len, t->mt_datpath);
	vmtseterr(t);
	return -1;
    }
    (void) fseek(t->mt_datf, -sizeof(header), SEEK_CUR);


    return 1;
}

/* =================================================
	   	TPC format positioning
 */

/* Space 1 record forward.
** Returns 1 on success, 0 failure, -1 error.
*/
static int
tpc_recfwd(register struct vmtape *t)
{
    long ioptr;
    int res;
    uint32 len;
    unsigned char header[2];

    /* Ensure set up for reading */
    if (t->mt_state != TS_RDATA)
	vmt_iobeg(t, FALSE);

    if ((res = fread(header, 1, sizeof(header), t->mt_datf))
	 != sizeof(header)) {
	if (feof(t->mt_datf)) {	/* If at EOT, ignore even partial header */
	    t->mt_eot = TRUE;
	    return 0;		/* Fail */
	} else {
	    vmterror(t, "rsp partial header: %d, data file \"%.256s\"",
		     res,
		     t->mt_filename);
	    /* What else to do??  Read/write gubbish... */
	    vmtseterr(t);
	    return -1;
	}
    }
    len = (header[1]<<8) | header[0];
    if (len == 0) {
	t->mt_eof = TRUE;		/* Indicate hit tapemark */
	return 1;			/* and succeed */
    }

    /* Skip over data.  Don't bother checking post-data header, to allow
       moving over possible bad stuff.  Also ignore fseek error, next
       i/o attempt will catch it.
     */
    ioptr = len + (len&01);
    if (t->mt_ispipe)
	return vmtflushinp(t, ioptr) ? 1 : -1;
    else {
	if (fseek(t->mt_datf, ioptr, SEEK_CUR) != 0) {
	    vmterror(t, "rsp seek error: %d, data file \"%.256s\"",
		     errno,
		     t->mt_filename);
	    /* What else to do??  Read/write gubbish... */
	    vmtseterr(t);
	    return -1;
	}
    }
    return 1;
}

/* Tape I/O - Start, end, EOF, EOT */

void
vmt_iobeg(register struct vmtape *t,
	  int wrtf)			/* TRUE if about to write */
{
    register struct vmttdrdef *td;
    long ioptr;

    /* Caller should always check for writability first, but do a sanity
       check anyway to catch buggy code.
    */
    if (wrtf && (!vmt_iswritable(t)
		 || !(vmtfmttab[t->mt_format].tf_flags & VMTFF_WR))) {
	vmterror(t, "write attempted to non-writable tape");
	/* Later return false value? */
	vmtseterr(t);
	return;
    }

    t->mt_frames = 0;		/* Updated by any I/O */
    t->mt_err = 0;
    t->mt_eof = t->mt_eot = t->mt_bot = FALSE;
    t->mt_iowrt = wrtf;

    switch (t->mt_format) {
    default:
	break;

    case VMT_FMT_TPC:
	if (t->mt_state != TS_RDATA) {	/* Paranoia check */
	    vmtseterr(t);
	}
	break;

    case VMT_FMT_TPE:
    case VMT_FMT_TPS:
	if ((wrtf && (t->mt_state != TS_RWRITE))
	    || (!wrtf &&  (t->mt_state != TS_RDATA))) {
	    /* Must do a seek because we're changing read/write mode.
	       (required by ANSI C).  Can skip this if already in right mode.
	    */
	    ioptr = 0;
	    if (fseek(t->mt_datf, ioptr, SEEK_CUR)) {
		vmterror(t, "%s fseek failed: data file \"%.256s\"",
			(wrtf ? "write" : "read"), t->mt_datpath);
		/* What else to do??  Read/write gubbish... */
		vmtseterr(t);
		break;
	    }
	    t->mt_state = (wrtf ? TS_RWRITE : TS_RDATA);
	}
	break;

    case VMT_FMT_RAW:
	rawposfix(t);		/* Canonicalize read position */
	td = &t->mt_tdr;
	if (wrtf) {
	    /* Caller must already have checked vmt_iswritable() to make sure
	    ** that writes are OK.
	    */
	    t->mt_state = TS_RWRITE;

	    if (td->tdcur)		/* If not at EOF, */
		td_trunc(td);	/* Truncate tape at this location! */

	    /* Now find data file IO pointer value to use, and position it.
	    ** May already be there, but must do it explicitly in case of
	    ** intervening tape reads or position commands.
	    */
	    if (td->tdrd == NULL)
		ioptr = 0;
	    else {
		/* Get pointer to last record def in list */
		register struct vmtrecdef *rd = td->tdrd->rdprev;
		ioptr = rd->rdloc + (rd->rdlen * rd->rdcnt);
	    }
	    td->tdbytes = ioptr;	/* Remember highest output so far */

	} else {

	    /* If data is forthcoming, find data file IO pointer to use. */
	    if (t->mt_state != TS_RDATA)
		return;			/* No data, nothing to do now */

	    /* Aha, set up */
	    ioptr = td->tdcur->rdloc + (td->tdcur->rdlen * td->tdrncur);
	    td->tdrfcnt = td->tdcur->rdlen;
	}

	/* Do seek on raw tape data file.
	** May already be positioned there, but must do it explicitly in case
	** of intervening tape reads or position commands.
	** (This is required by ANSI C)
	*/
	if (t->mt_ispipe) {
	    /* Do nothing and hope we're still in the right place! */
	} else if (fseek(t->mt_datf, ioptr, SEEK_SET)) {
	    vmterror(t, "%s fseek failed: data file \"%.256s\", loc %ld",
		    (wrtf ? "write" : "read"),
		    t->mt_datpath, (long)ioptr);
	    /* What else to do??  Read/write gubbish... */
	    vmtseterr(t);
	}
	break;
    }
}

int
vmt_ioend(register struct vmtape *t)
{
    switch (t->mt_format) {
    default:
	return TRUE;

    case VMT_FMT_TPE:
    case VMT_FMT_TPS:		/* No cleanup necessary */
	break;

    case VMT_FMT_RAW:		/* Handle raw tape */
	/* Check to see if any I/O actually happened (record data or tapemark).
	** It's possible for some errors to prevent I/O from happening, thus
	** the logical position hasn't changed.
	*/
	if (!t->mt_frames && !t->mt_eof)
	    return TRUE;

	if (t->mt_iowrt) {			/* If writing, */
	    if (t->mt_frames) {		/* force out any data */
		fflush(t->mt_datf);
		if (ferror(t->mt_datf))
		    return FALSE;
		if (!td_recapp(&t->mt_tdr,	/* append new record def */
			    t->mt_frames, 1, 0)) {
		    vmterror(t, "vmt_ioend: td_recapp malloc failed");
		    return FALSE;
		}
	    }
	} else {			/* If reading, */
	    t->mt_tdr.tdrncur++;	/* move past this record/tapemark */
	}
	break;
    }
    return TRUE;
}

/* Write EOF (tapemark, filemark)
**	Caller has already checked for ability to write.
**	Returns FALSE if couldn't do it.
*/
int
vmt_eof(register struct vmtape *t)
{
    if (!vmt_iswritable(t)) {
	/* Read-only tape or format, cannot write */
	t->mt_err++;
	return FALSE;
    }
    vmt_iobeg(t, TRUE);			/* Set up for write */

    switch (t->mt_format) {
    default:
	break;			/* Wrong format */

    case VMT_FMT_TPE:
    case VMT_FMT_TPS:
	/* Just write zero length "record" */
	return vmt_rput(t, (unsigned char *)NULL, (size_t)0);

    case VMT_FMT_RAW:
	/* Instead of calling vmt_ioend(), invoke a slightly different
	** operation and leave it at that.
	** Failure return means malloc failed.
	*/
	if (!td_recapp(&t->mt_tdr, (long)0, 0, 0)) {	/* Append EOF */
	    vmterror(t, "vmt_eof: td_recapp malloc failed");
	    return FALSE;
	}
	return TRUE;
    }
    return FALSE;
}

/* Write what amounts to EOT, when tape being unmounted or rewound.
**	Finalize output data, write tape directory file.
*/
int
vmt_eot(register struct vmtape *t)
{			/* But don't close file yet, let caller decide */
    int errs = 0;

    if (!vmt_iswritable(t)) {
	/* Read-only tape or format, cannot write */
	t->mt_err++;
	return FALSE;
    }

    if (t->mt_datf) {
	if ((fflush(t->mt_datf) != 0)	/* Force out any remaining raw data */
	    || ferror(t->mt_datf))
	    errs++;
    }
    if (t->mt_ctlf) {		/* If control file open */
	/* Write out tape directory */
	rewind(t->mt_ctlf);	/* Rewind in case not the first update */
	td_fout(&t->mt_tdr, t->mt_ctlf, t->mt_datpath);
	if ((fflush(t->mt_ctlf) != 0)
	    || ferror(t->mt_ctlf))	/* Check to see if any errors */
	    errs++;
    }
    if (errs) t->mt_err = errs;
    return (errs ? FALSE : TRUE);
}


/* Tape I/O - Actual data! */

#if VMTAPE_ITSDUMP

/* ITS_WGET(t, wp)
**	Returns TRUE if a word was read, FALSE otherwise.
**	For a FALSE return, mt_err must be checked to determine
**	whether the failure was due to an error.
*/	

int
its_wget(register struct vmtape *t,
	 register w10_t *wp)
{
    switch (t->mt_state) {
    case TS_THEAD:
	if (--(t->mt_cnt) >= 0) {
	    *wp = *(t->mt_ptr)++;
	    t->mt_frames += VMT_ITS_FPW;
	    return TRUE;
	}
	t->mt_state = TS_FNEXT;
	return its_wget(t, wp);

    case TS_FHEAD:
	if (--(t->mt_cnt) >= 0) {
	    *wp = *(t->mt_ptr)++;
	    t->mt_frames += VMT_ITS_FPW;
	    return TRUE;
	}
	if (t->mt_fcur->fdfmt == FDFMT_LINK) {
	    t->mt_state = TS_FEOF;	/* When done with link block, no more */
	    return t->mt_frames	/* If anything already read, */
		? FALSE			/* return end of record, */
		: its_wget(t, wp);	/* else return tapemark (EOF) */
	}
	t->mt_state = TS_FDATA;
	/* Fall through to return data */

    case TS_FDATA:
	switch (wf_get(&t->mt_wf, wp)) {
	    case 1:
		t->mt_frames += VMT_ITS_FPW;
		return TRUE;
	    case 0: break;
	    case -1: t->mt_err++;
	}
	datf_close(t);
	t->mt_state = TS_FEOF;	/* No more data, so return EOF next time */
	return (t->mt_frames || t->mt_err)	/* If anything already read, */
		? FALSE			/* return end of record, */
		: its_wget(t, wp);	/* else return tapemark (EOF) */

    case TS_FNEXT:
    {
	register struct vmtfildef *fd;

	if (!(fd = fd_nextget(t)))	/* If no more files, */
	    return FALSE;		/* at EOT, state has been set */

	if (fd->fdfmt == FDFMT_WFT) {
	    char fnbuf[400];
	    if (os_fullpath(fnbuf, t->mt_filename, fd->fdfname, sizeof(fnbuf))
				> sizeof(fnbuf)) {
		vmterror(t, "its_wget: path too big for %.128s & %.128s",
			t->mt_filename, fd->fdfname);
		return its_wget(t, wp);	/* Try next one */
	    }
		
	    if (!datf_open(t, fnbuf, "zrb")) {
		vmterror(t, "its_wget: Can't open: %.256s", fnbuf);
		return its_wget(t, wp);	/* Try next one */
	    }
	    wf_init(&t->mt_wf, fd->fdwft, t->mt_datf);

	    /* Put together a file block */
	    LRHSET(t->mt_wbuf[TH_LKF], 0, 0);		/* Say a file */
	    t->mt_cnt = TH_FLN+1;

	    /* Set creation time from source file, if not already set */
	    if (!LHGET(fd->fdits.crdatim) && !RHGET(fd->fdits.crdatim))
		fd->fdits.crdatim = itsdate(t->mt_datf);

	} else if (fd->fdfmt == FDFMT_LINK) {
	    /* Put together a link block */

	    /* Set creation time from tape dir file, if not already set */
	    if (!LHGET(fd->fdits.crdatim) && !RHGET(fd->fdits.crdatim))
		fd->fdits.crdatim = t->mt_fitsdate;

	    LRHSET(t->mt_wbuf[TH_LKF], 1, 0);		/* Say a link */

	    t->mt_wbuf[TH_LFN1] = fd->fdits.xfn1;	/* Set file data */
	    t->mt_wbuf[TH_LFN2] = fd->fdits.xfn2;
	    t->mt_wbuf[TH_LDIR] = fd->fdits.xdir;
	    t->mt_cnt = TH_LDIR+1;
	} else {
	    vmterror(t, "its_wget: Bad file type %d.", fd->fdfmt);
	    return its_wget(t, wp);
	}

	/* Common to all */
	LRHSET(t->mt_wbuf[TH_CNT], (-(TH_FLN+1))&H10MASK, 0);
	t->mt_wbuf[TH_DIR] = fd->fdits.dir;
	t->mt_wbuf[TH_FN1] = fd->fdits.fn1;
	t->mt_wbuf[TH_FN2] = fd->fdits.fn2;
	t->mt_wbuf[TH_CRD] = fd->fdits.crdatim;
	if (LHGET(fd->fdits.rfdatetc) || RHGET(fd->fdits.rfdatetc))
	    t->mt_wbuf[TH_RFD] = fd->fdits.rfdatetc;
	else						/* Unknown info */
	    LRHSET(t->mt_wbuf[TH_RFD], H10MASK, 0777000);
	LRHSET(t->mt_wbuf[TH_FLN], H10MASK, H10MASK);	/* Unknown len */
	t->mt_ptr = t->mt_wbuf;
	t->mt_state = TS_FHEAD;
 	return its_wget(t, wp);
    }

    case TS_FEOF:
	t->mt_eof = TRUE;
	t->mt_state = TS_FNEXT;
	return FALSE;

    case TS_EOT:
	t->mt_eot = TRUE;
	return FALSE;

    case TS_ERR:
    case TS_CLOSED:
    default:
	t->mt_err++;
	return FALSE;
    }
}

#endif /* VMTAPE_ITSDUMP */

/* Do I/O on complete records (raw frame bytes) */

/* VMT_RGET - Read a raw record.
**	Returns TRUE if some data was read; FALSE if not.
**	FALSE could mean either an EOF, or EOT, or error; data structure
**	must be checked with vmt_*() macros.
*/
int
vmt_rget(register struct vmtape *t,
	 register unsigned char *buff,
	 size_t len)
{
    register size_t nget;
    int res = FALSE;

    vmt_iobeg(t, FALSE);	/* Set up for reading */

    switch (t->mt_format) {
    default:
	t->mt_err++;
	return FALSE;

#if VMTAPE_ITSDUMP
    case VMT_FMT_ITS:
      {
	w10_t w;
	register unsigned char *ucp = buff;
	register int wlen;

	/* ITSDUMP source input has no record length boundaries, so each
	   file could be a single huge record.  In tne interest of sanity,
	   break things up into a manageable 1K words that corresponds to
	   the normal buffer size used by DUMP (or ITS).
	   If the caller wants a smaller record, just do that, with the
	   proviso that we will never return part of a word.
	*/
	if (len > VMT_ITS_DEFRECLEN)
	    len = VMT_ITS_DEFRECLEN;
	wlen = len / VMT_ITS_FPW;
	if (wlen == 0) {	/* Buffer too small? */
	    vmterror(t, "Unsupportable buffer size: %ld", (long)len);
	    t->mt_err++;
	    return FALSE;
	}
	for (; wlen > 0; --wlen, t->mt_frames += VMT_ITS_FPW) {
	    if (!its_wget(t, &w)) {
		if (t->mt_frames)
		    return TRUE;
		/* Tapemark, BOT/EOT, or some kind of error, just pass along */
		return FALSE;
	    }
	    /* Got word, convert to bytes and put in buffer.
	       Assumes core-dump mode; ITSDUMP meaningless otherwise.
	    */
	    *ucp++ = (LHGET(w)>>10) & 0377;
	    *ucp++ = (LHGET(w)>> 2) & 0377;
	    *ucp++ = ((LHGET(w)&03)<<6) | ((RHGET(w)>>12)&077);
	    *ucp++ = (RHGET(w)>>4) & 0377;
	    *ucp++ = RHGET(w) & 017;
	}
	/* No need to call vmt_ioend() */
	return TRUE;
      }
#endif /* VMTAPE_ITSDUMP */

    case VMT_FMT_RAW:
	switch (t->mt_state) {
	case TS_EOT:
	    t->mt_eot = TRUE;
	    return FALSE;		/* Don't call vmt_ioend */

	case TS_REOF:
	    t->mt_eof = TRUE;
	    t->mt_state = TS_RDATA;
	    break;

	case TS_RDATA:
	    if (len > t->mt_tdr.tdrfcnt) {
		len = t->mt_tdr.tdrfcnt;	/* Truncate our request */
		if (len <= 0) 
		    break;			/* Nothing in record??? */
	    }

	    if ((nget = fread(buff, 1, len, t->mt_datf)) != len) {
		/* Handle premature EOF on tape data file */
		vmterror(t, "Premature EOF on tape data file \"%.256s\" (read %ld of %ld)",
			 t->mt_datpath,
			 (long)nget, (long)len);
		len = nget;
		t->mt_eot = TRUE;
		t->mt_state = TS_EOT;
	    }
	    t->mt_frames += nget;
	    t->mt_tdr.tdrfcnt -= nget;
	    res = TRUE;
	    break;

	default:		/* Bad state?? */
	    /* Should return error indicator, but also try to get back into
	       a good state.
	    */
	    t->mt_err++;
	    t->mt_state = TS_RDATA;	/* Wild guess */
	    res = FALSE;
	    break;
	}

	(void) vmt_ioend(t);
	break;

    case VMT_FMT_TPE:
    case VMT_FMT_TPS:
    {
	long ioptr;
	uint32 reclen;
	unsigned char header[4];
	unsigned char trailer[8];

	if (t->mt_state != TS_RDATA) {
	    /* Bad state?? */
	    t->mt_err++;
	    t->mt_state = TS_RDATA;
	    return FALSE;
	}
	if ((res = fread(header, 1, sizeof(header), t->mt_datf))
	     != sizeof(header)) {
	    if (feof(t->mt_datf)) {	/* If at EOT, ignore even partial header */
		t->mt_eot = TRUE;
		return FALSE;		/* Fail */
	    } else {
		vmterror(t, "rget partial header: %d, data file \"%.256s\"",
			 res,
			 t->mt_datpath);
		/* What else to do??  Read/write gubbish... */
		vmtseterr(t);
		return FALSE;
	    }
	}
	reclen = (header[3]<<24)|(header[2]<<16) | (header[1]<<8) | header[0];
	if (reclen & VMT_TPS_ERRF) {
	    /* Error - for now, go for it anyway */
	    reclen &= MASK31;
	}
	if (reclen == 0) {
	    t->mt_eof = TRUE;		/* Indicate hit tapemark */
	    return FALSE;		/* No data read */
	}
	if (len > reclen)
	    len = reclen;	/* Truncate our request */
	else if (reclen > len) {
	    /* Record length longer than our buffer.  This should
	       never happen, but nothing stops someone from creating
	       a bogus tape.  Satisfy request but complain.
	     */
	    vmterror(t, "warning: reclen > buflen (%ld > %ld) for \"%.256s\"",
		     (long)reclen, (long)len, t->mt_datpath);
	}

	/* Read data! */
	if ((nget = fread(buff, 1, len, t->mt_datf))
	     != len) {
	    vmterror(t, "rget partial header: %ld, data file \"%.256s\"",
		     (long)nget,
		     t->mt_datpath);
	    /* What else to do??  Read/write gubbish... */
	    vmtseterr(t);
	    return FALSE;
	}
	t->mt_frames += nget;
	if (reclen > len) {
	    /* Skip over unread portion of record */
	    if (t->mt_ispipe)
		(void) vmtflushinp(t, (size_t)(reclen-len));
	    else
		(void) fseek(t->mt_datf, (long)(reclen-len), SEEK_CUR);
	}

	/* Read trailer and verify it.
	 */
	memset(trailer, 0, sizeof(trailer));
	if ((reclen & 01) && (t->mt_format == VMT_FMT_TPE))
	    reclen = 0;			/* Pad unless VMT_FMT_TPE */
	(void) fread(trailer+4-(reclen&01), 1, 4+(reclen&01), t->mt_datf);
	if (memcmp(header, trailer+4, sizeof(header))) {
	    uint32 headv = (header[3]<<24) | (header[2]<<16)
			   | (header[1]<<8) | header[0];
	    uint32 trailv = (trailer[7]<<24) | (trailer[6]<<16)
			   | (trailer[5]<<8) | trailer[4];
	    vmterror(t,
"warning: header-trailer mismatch: %0lX vs %0lX for \"%.256s\"",
		     (long)headv, (long)trailv,
		     t->mt_datpath);
	}

	(void) vmt_ioend(t);
	break;
	}

    case VMT_FMT_TPC:
    {
	long ioptr;
	uint32 reclen;
	unsigned char header[2];

	if (t->mt_state != TS_RDATA) {
	    /* Bad state?? */
	    t->mt_err++;
	    t->mt_state = TS_RDATA;
	    return FALSE;
	}
	if ((res = fread(header, 1, sizeof(header), t->mt_datf))
	     != sizeof(header)) {
	    if (feof(t->mt_datf)) {	/* If at EOT, ignore even partial header */
		t->mt_eot = TRUE;
		return FALSE;		/* Fail */
	    } else {
		vmterror(t, "rget partial header: %d, data file \"%.256s\"",
			 res,
			 t->mt_datpath);
		/* What else to do??  Read/write gubbish... */
		vmtseterr(t);
		return FALSE;
	    }
	}
	reclen = (header[1]<<8) | header[0];
	if (reclen == 0) {
	    t->mt_eof = TRUE;		/* Indicate hit tapemark */
	    return FALSE;		/* No data read */
	}
	if (len > reclen)
	    len = reclen;	/* Truncate our request */
	else if (reclen > len) {
	    /* Record length longer than our buffer.  This should
	       never happen, but nothing stops someone from creating
	       a bogus tape.  Satisfy request but complain.
	     */
	    vmterror(t, "warning: reclen > buflen (%ld > %ld) for \"%.256s\"",
		     (long)reclen, (long)len, t->mt_datpath);
	}

	/* Read data! */
	if ((nget = fread(buff, 1, len, t->mt_datf))
	     != len) {
	    vmterror(t, "rget partial header: %ld, data file \"%.256s\"",
		     (long)nget,
		     t->mt_datpath);
	    /* What else to do??  Read/write gubbish... */
	    vmtseterr(t);
	    return FALSE;
	}
	t->mt_frames += nget;
	if (reclen > len) {
	    /* Skip over unread portion of record */
	    if (t->mt_ispipe)
		(void) vmtflushinp(t, (size_t)(reclen-len));
	    else
		(void) fseek(t->mt_datf, (long)(reclen-len), SEEK_CUR);
	}
	/* TPC has no trailer, but may align up?? */
	if (reclen & 01)
	    (void) fgetc(t->mt_datf);

	(void) vmt_ioend(t);
	break;
    }

    }
    return res;
}


/* Write a record
*/
int
vmt_rput(register struct vmtape *t,
	 register unsigned char *buff,
	 size_t len)
{
    size_t nput;

    if (!(t->mt_fmtp->tf_flags & VMTFF_WR)) {
	/* Read-only format, cannot write */
	t->mt_err++;
	return FALSE;
    }
    vmt_iobeg(t, TRUE);

    if (t->mt_state != TS_RWRITE) {
	t->mt_err++;
	return FALSE;
    }

    switch (t->mt_format) {
	int pad;
	unsigned char header[8];	/* For best alignment */

    case VMT_FMT_TPS:
	if (pad = (len & 01)) {		/* Must pad? */
	    header[3] = 0;
	} else {
    case VMT_FMT_TPE:
	    pad = 0;
	}
	header[4] = len & 0377;
	header[5] = (len >>  8) & 0377;
	header[6] = (len >> 16) & 0377;
	header[7] = (len >> 24) & 0377;

	if ((fwrite(header+4, 1, 4, t->mt_datf) == -1)
	    || (len && ((nput = fwrite(buff, 1, len, t->mt_datf)) == -1))
	    || (len && (fwrite(header+4-pad, 1, 4+pad, t->mt_datf)) == -1) ) {
	    vmterror(t, "Output error on tape data file \"%.256s\"",
		     t->mt_datpath);
	    t->mt_err++;
	    return FALSE;
	}
	t->mt_frames = nput;
	break;

    case VMT_FMT_RAW:
	nput = fwrite(buff, 1, len, t->mt_datf);
	if (nput != -1)
	    t->mt_frames = nput;
	else {
	    vmterror(t, "Output error on tape data file \"%.256s\"",
			t->mt_datpath);
	    t->mt_err++;
	    return FALSE;
	}
	break;
    }
    return vmt_ioend(t);
}

/* Add error indication onto last record.
**	Only intended for use by tdcopy program.
**	If uselast is TRUE, uses last record (right after a vmt_ioend!)
**	otherwise creates a new, null record to hold the error.
*/
int
vmt_eput(register struct vmtape *t,
	 int err, int uselast)
{
    switch (t->mt_format) {
    case VMT_FMT_RAW:
	if (!uselast || !t->mt_tdr.tdrd || !t->mt_tdr.tdcur) {
	    vmt_iobeg(t, TRUE);		/* Set up for write */
	    if (!td_recapp(&t->mt_tdr, (long)0, 1, err)) {
		vmterror(t, "vmt_eput: td_recapp malloc failed");
		return FALSE;
	    }
	    return TRUE;
	}
	t->mt_tdr.tdcur->rderr = err;
	break;
    case VMT_FMT_TPE:
    case VMT_FMT_TPS:
	/* Possible but not implemented yet */
	break;
    }
    return TRUE;
}

/*
		Description of Control Tape file format
		(formerly "Tape Directory format")

	A tape directory is a text file that describes the structure of
the associated tape data file.  The data file is simply a continuous
stream of 8-bit bytes corresponding to the logical stream of 8-bit tape data
frames.
	The directory is formatted as a set of logical text lines.
Each line begins with a keyword optionally followed by data.  A
logical line may be continued over several physical lines.  Whitespace
is ignored.  Comments are introduced by the character ';' and continue
to the end of that physical line.

Keywords:
	TF-Format: <fmtword> [<filename>]
				; Tape datafile format and optional filename
	<#>: <filedesc>		; File/Record descriptors
	<indentation> <filedesc>	; optional continuation of above
	EOT:			; Phys End of Tape, remaining text ignored.

<fmtword> - describes how the tape file data is stored on disk, one of:
	"raw" - 8-bit bytes, one per frame.  (T20: bytesize 8)
	"packed" - T20 only.

<filename> - name of tape data file.  If none, name is built using
	".tap" as an extension to the name of the tape directory file.
	This extension replaces ".tdr" if it exists, otherwise ".tap" is
	simply appended.

<#> - location in tapefile of 1st record beginning next file.  For "raw"
	format this corresponds to the logical frame # on tape as well, but
	for other formats this may serve a realignment purpose.
	The string "BOT" is considered equivalent to "0".

<filedesc> - a list of <recdesc> record descriptors composing the file,
	separated by whitespace.  The list may be empty, as is normally
	the case for two consecutive tapemarks that signal a logical EOT.

<recdesc> - A record descriptor, with the format
		<len>[*<cnt>][E[<type>]]
	where:
	<len> - decimal record length, in frames (8-bit bytes)
	<cnt> - # times this record length recurs.
	E - indicates error when reading this record (or the last of a
		series).  <type> is an optional # and specifies additional
		information, if any, as to nature of the error.

	The string "EOF" is a special <recdesc> that signals a tape mark.

NOTE: may want to make EOF a special <len> instead, mainly so an error
indication can be bound to it.
*/

/* Grovel over tape directory -- a bunch of lines, each line of format:
**
** ITSFILE:	dir fn1 fn2 <type> <filename-or-linkname>
**    <type> is one of:
**		"->" dir fn1 fn2	; an ITS link
**		<wft> filename		; a data file of 36-bit words,
**					; in format indicated by <wft>,
**					; anything that wfio.h knows about.
**					; Usually "u36".
**
** ITSDIR: dir <dirpathname>
**	where dirpathname is a directory containing u36-type files,
**	all of which are read in.  Later, may interpret DIR.LIST too.
*/

static int fileread(FILE *f, char **acp, size_t *asz);
static char *wdscan(char **acp);
static int numscan(char *cp, char **acp, long int *aval);
static void filscan(struct vmtape *t, struct vmttdrdef *td, char *cp);
#if VMTAPE_ITSDUMP
static struct vmtfildef *itsparse(struct vmtape *t, char **acp);
#endif

#define TDRERR(t,args) ((t)->mt_ntdrerrs++, tdrwarn args, 0)

static void
tdr_reset(struct vmtape *t)
{
    td_reset(&t->mt_tdr);	/* Flush raw tape stuff */

#if VMTAPE_ITSDUMP
    if (t->mt_fdefs) {		/* Flush non-raw FDF stuff */
	register struct vmtfildef *fd = t->mt_fdefs, *next;
	do {
	    next = fd->fdnext;
	    free((char *)fd);
	} while ((fd = next) != t->mt_fdefs);
	t->mt_fdefs = NULL;
    }
    t->mt_fcur = NULL;
#endif /* VMTAPE_ITSDUMP */

    if (t->mt_contents) {	/* Flush tapefile buffer */
	free(t->mt_contents);
	t->mt_contents = NULL;
    }
}

static int
tdr_scan(struct vmtape *t,
	 FILE *f,
	 char *fname)
{
    struct vmttdrdef *td = &t->mt_tdr;
    int indent;
    char *cp;
    char *key, *num;
    long tloc = 0;
    size_t fsize = 0;
    char *line, *nline;
    char *savetpath = t->mt_filename;	/* Save & restore this */

    t->mt_filename = fname;		/* Use this for TDRERR msgs */

    tdr_reset(t);

    t->mt_ntdrerrs = 0;
    t->mt_format = 0;
    t->mt_fmtp = &vmtfmttab[t->mt_format];

    /* Set up initial file to read */
    if (fileread(f, &(t->mt_contents), &fsize) <= 0) {
	TDRERR(t, (t,"Tape-file readin failed: %ld bytes", fsize));
    } else
      for (nline = t->mt_contents; cp = nline; t->mt_lineno++) {
	/* Trim off a line */
	if (nline = strchr(nline, '\n'))
	    *nline++ = '\0';	/* Truncate line, point past EOL */
	line = cp;		/* Remember start of line */

	/* Parse line */
	indent = (*cp == ' ') || (*cp == '\t');	/* Remember if indentation */
	while (isspace(*cp)) ++cp;	/* Flush initial whitespace */
	if (key = strchr(cp, ';'))	/* Strip off comments here */
	    *key = '\0';
	if (!*cp) continue;		/* Ignore blank lines */

	/* If indented, assume it's a continuation of a raw record format
	** specification
	*/
	if (indent) {
	    if (!td->tdrd) {
		TDRERR(t,(t, "Indented record defs without active tape/file def"));
		continue;
	    }
	    filscan(t, td, cp);
	    continue;
	}

	/* Not indented, look for regular keyword of form "foo:" */
	if (!(cp = strpbrk(key = cp, ": \t")) || *cp != ':') {
	    TDRERR(t,(t, "No keyword: \"%s\"", key));
	    continue;
	}
	*cp++ = 0;			/* Zap colon, move over */
	if (numscan(key, &num, &tloc) && !*num) {	/* All digits? */
	    /* Assume it's a record definition */
	    if (tloc == 0 && td->tdrd) {
		TDRERR(t,(t, "Record descs seen before BOT"));
	    } else if (tloc != td->tdbytes) {
		if (tloc < td->tdbytes)
		    TDRERR(t,(t, "Tape overlap?  Loc now %ld, TD file says %ld",
					tloc, td->tdbytes));
		else	/* Warning, not true error */
		    tdrwarn(t, "Tape gap?  Loc now %ld, TD file says %ld",
					tloc, td->tdbytes);
	    }
	    td->tdbytes = tloc;		/* Set file location as given */
	    filscan(t, td, cp);		/* Scan line and link recs into list */

	} else if (strcasecmp(key, "BOT")==0) {
	    if (td->tdrd || td->tdbytes) {
		TDRERR(t,(t, "BOT already seen"));
		continue;
	    }
	    filscan(t, td, cp);		/* Scan line and link recs into list */

	} else if (strcasecmp(key, "EOT")==0) {

	} else if (strcasecmp(key, "TF-Format")==0) {
	    char *fmtstr;

	    if (t->mt_format) {
		TDRERR(t,(t, "TF-Format already seen"));
		continue;
	    }
	    if (!(t->mt_format = vmt_strtofmt(fmtstr = wdscan(&cp)))) {
		TDRERR(t,(t,"Unknown TF-Format: %s", fmtstr));
		continue;
	    }
	    t->mt_fmtp = &vmtfmttab[t->mt_format];
	    cp = wdscan(&cp);		/* Get optional data filename */
	    if (cp) {
		if (!(td->tdfname = dupcstr(cp))) {
		    TDRERR(t,(t, "malloc failed for tdfname"));
		    continue;
		}
	    } else
		td->tdfname = NULL;
#if VMTAPE_ITSDUMP
	} else if (strcasecmp(key, "ITSFILE")==0) {
	    struct vmtfildef *fd;

	    if (t->mt_format == 0) {
		t->mt_format = VMT_FMT_ITS;
		t->mt_fmtp = &vmtfmttab[t->mt_format];
	    }
	    fd = itsparse(t, &cp);
	    if (fd) {
		/* Link in to circular list */
		if (t->mt_fdefs) {
		    fd->fdnext = t->mt_fdefs;
		    fd->fdprev = t->mt_fdefs->fdprev;
		    fd->fdprev->fdnext = fd;
		    t->mt_fdefs->fdprev = fd;
		} else {
		    t->mt_fdefs = fd->fdnext = fd->fdprev = fd;
		}
	    }
	    continue;
	} else if (strcasecmp(key, "ITSDIR")==0) {
	    /* Insert hack code here */
#endif /* VMTAPE_ITSDUMP */
	    

	} else
	    TDRERR(t,(t, "Unknown keyword \"%s\"", key));
    }

    if (!feof(f))
	TDRERR(t,(t, "Input I/O error"));

    t->mt_filename = savetpath;		/* Restore saved tapefile path */
    if (t->mt_ntdrerrs) {
	/* Scan failed, clean up */
	tdr_reset(t);
	t->mt_format = 0;
	t->mt_fmtp = &vmtfmttab[t->mt_format];
	return FALSE;
    }
    return TRUE;
}


static int
numscan(char *cp, char **acp, long *aval)
{
    register char *s = cp;
    long val = 0;
    while (isdigit(*s)) {
	val = (val * 10) + (*s - '0');
	++s;
    }
    *aval = val;
    *acp = s;
    return (s == cp) ? 0 : 1;
}



/* Read in file into a single memory chunk */
static int
fileread(FILE *f,
	 char **acp,
	 size_t *asz)
{
    register size_t size = 0;
    register char *ptr = NULL, *cp, *op;
    register int res;

# define CHUNKSIZE 1024
    for (;;) {
	ptr = (op = ptr) ? realloc(ptr, size+CHUNKSIZE+1)
			: malloc(CHUNKSIZE+1);
	if (!ptr) {
	    *acp = "Out of memory for file";
	    if (op)
		free(op);
	    return 0;
	}
	cp = ptr + size;
	res = fread(cp, 1, CHUNKSIZE, f);
	if (res > 0)
	    size += res;
	if (res < CHUNKSIZE) {
	    if (!feof(f) || ferror(f)) {
		free(ptr);
		*acp = "Error during file input";
		return -1;
	    }
	    if (!(ptr = realloc((op = ptr), size+1))) {
		free(op);
		*acp = "File memory trim error";
		return 0;
	    }
	    ptr[size] = '\0';	/* Ensure nul-terminated just in case */
	    *acp = ptr;
	    *asz = size;
	    return 1;
	}
    }
}

/* Scan a list of records constituting a tape file.
*/

static void
filscan(struct vmtape *t,
	struct vmttdrdef *td,
	char *cp)
{
    char *rs;
    long len, cnt, err;

    while (rs = wdscan(&cp)) {
	char *cs, *es;

	if (!numscan(rs, &cs, &len)) {
	    if (strcasecmp(rs, "EOF") == 0) {	/* Tapemark? */
		if (td_recapp(td, (long)0, 0, 0) == NULL)	/* Yep, add one here */
		    TDRERR(t,(t, "malloc failed while adding tapemark"));
	    } else
		TDRERR(t,(t, "Bad reclen syntax"));
	    continue;
	}
	if (*cs == '*') {
	    if (!numscan(++cs, &cs, &cnt)) {
		TDRERR(t,(t, "Bad reccnt syntax"));
		continue;
	    }
	} else cnt = 1;
	if (*cs) {
	    if (*cs != 'e' || *cs != 'E') {
		TDRERR(t,(t, "Bad record syntax"));
		continue;
	    }
	    if (!numscan(++cs, &es, &err) || *es) {
		TDRERR(t,(t, "Bad recerr syntax"));
		continue;
	    }
	} else err = 0;

	/* Add record to list */
	if (td_recapp(td, len, (int)cnt, (int)err) == NULL)
	    TDRERR(t,(t, "malloc failed while adding record"));
    }
}


static char *
wdscan(char **acp)
{
    register char *cp = *acp, *s;

    if (isspace(*cp))		/* Skip to word */
        while (isspace(*++cp));
    s = cp;
    switch (*s) {
	case 0:	return NULL;		/* Nothing left */
	default:			/* Normal word */
	    while (!isspace(*++s) && *s);
	    if (*s) *s++ = 0;		/* Tie off word */
	    break;
    }
    *acp = s;
    return cp;
}

static char *
dupcstr(char *s)
{
    char *cp;

    cp = malloc(strlen(s)+1);
    if (!cp)
	return NULL;
    return strcpy(cp, s);
}

/* ITSDUMP tape directory parsing and misc auxiliaries */

#if VMTAPE_ITSDUMP

static struct vmtfildef *
itsparse(struct vmtape *t, char **acp)
{
    struct vmtfildef *fd;
    int islink;
    int wft;
    char *dir, *fn1, *fn2, *type, *xdir, *xfn1, *xfn2;
    char *screat, *sref, *sbsz, *slen, *sauth;
    long creat, ref, bsz, len;

    dir = wdscan(acp);
    fn1 = wdscan(acp);
    fn2 = wdscan(acp);
    type = wdscan(acp);

    screat = sref = sbsz = slen = sauth = NULL;
    creat = ref = bsz = len = 0;
    if (type && !strcmp(type, "{")) {	/* Special extra file info? */
	for (;;) {			/* Fake loop so break can work */
	    if ((type = wdscan(acp))
		&& strcmp(type, "}")) screat = type; else break;
	    if ((type = wdscan(acp))
		&& strcmp(type, "}")) sref = type; else break;
	    if ((type = wdscan(acp))
		&& strcmp(type, "}")) sbsz = type; else break;
	    if ((type = wdscan(acp))
		&& strcmp(type, "}")) slen = type; else break;
	    if ((type = wdscan(acp))
		&& strcmp(type, "}")) sauth = type; else break;
	    type = wdscan(acp);
	    break;
	}
	if (!type || strcmp(type, "}")) {
	    TDRERR(t,(t, "Bad ITSfile spec, missing close-brace"));
	    return NULL;
	}
	type = wdscan(acp);	/* OK, next thing should really be type */
    }
    xdir = wdscan(acp);
    xfn1 = wdscan(acp);
    xfn2 = wdscan(acp);
    if (!type || !xdir) {
	TDRERR(t,(t, "Bad ITSfile spec"));
	return NULL;
    }

    /* Check out file type */
    if (!strcmp(type, "->")) islink = TRUE;
    else {
	islink = FALSE;
	if ((wft = wf_type(type)) < 0) {
	    TDRERR(t,(t, "Unknown ITSfile format: \"%s\"", type));
	    return NULL;
	}
    }

    /* Check out any numbers */
    if (screat && !sscanf(screat, "%li", &creat)) {
	TDRERR(t,(t, "Bad ITSfile creation date: \"%s\"", screat));
	return NULL;
    }
    if (sref && !sscanf(sref, "%li", &ref)) {
	TDRERR(t,(t, "Bad ITSfile reference date: \"%s\"", sref));
	return NULL;
    }
    if (sbsz && !sscanf(sbsz, "%li", &bsz)) {
	TDRERR(t,(t, "Bad ITSfile bytesize: \"%s\"", sbsz));
	return NULL;
    }
    if (slen && !sscanf(slen, "%li", &len)) {
	TDRERR(t,(t, "Bad ITSfile length: \"%s\"", slen));
	return NULL;
    }

    /* Allocate structure for this ITSfile spec.  Note all data is
    ** automatically cleared, so stuff like crdatim needn't be set.
    */
    if (!(fd = (struct vmtfildef *)calloc(1, sizeof(*fd)))) {
	TDRERR(t,(t, "Out of memory for filespec"));
	return NULL;
    }
    fd->fdnext = NULL;

    fd->fdits.dir = sixbit(dir);
    fd->fdits.fn1 = sixbit(fn1);
    fd->fdits.fn2 = sixbit(fn2);
    if (islink) {
	fd->fdfmt = FDFMT_LINK;
	fd->fdits.linkf = 1;
	fd->fdfname = NULL;
	fd->fdits.xdir = sixbit(xdir);
	fd->fdits.xfn1 = sixbit(xfn1);
	fd->fdits.xfn2 = sixbit(xfn2);
    } else {
	fd->fdfmt = FDFMT_WFT;
	fd->fdwft = wft;
	fd->fdits.linkf = 0;
	fd->fdfname = xdir;
    }

    /* Now do extra info, which has peculiar formats */
    if (screat)
	fd->fdits.crdatim = itsnet2qdat(creat);
    if (sref)
	fd->fdits.rfdatetc = itsnet2qdat(ref);
    if (slen)
	fd->fdits.wdlen = itsfillen(bsz, len, &(fd->fdits.rfdatetc));
    if (sauth)
	fd->fdits.auth = sixbit(sauth);

    return fd;
}

/* This assumes we're using ASCII character set! */
#define tosixbit(c) ((((c) & 0100) ? (c)|040 : (c)&~040)&077)

static w10_t
sixbit(register char *str)
{
    register int i = 6*6;
    register unsigned c;
    register h10_t lh = 0, rh = 0;
    w10_t w;

    if (str) {
      while (i > 0 && (c = *str++)) {
	if (c == '~')	/* Translation hack so spaces can be embedded */
	    c = ' ';
	if (i > 18)
	    lh |= (h10_t)tosixbit(c) << ((i -= 6)-18);
	else
	    rh |= (h10_t)tosixbit(c) << (i -= 6);
      }
    }
    LRHSET(w, lh, rh);
    return w;
}

#include <time.h>	/* For struct tm */

static w10_t
itsdate(FILE *f)
{
    register w10_t w;
    struct tm tm;

    LRHSET(w, 0, 0);
    if (os_fmtime(f, &tm)) {	/* Get last modified time of open file */
	/* Now put together an ITS time word */
	LHSET(w, ((h10_t)(tm.tm_year % 100)<<9) | ((tm.tm_mon+1)<<5) | tm.tm_mday);
	RHSET(w, ((((h10_t)tm.tm_hour * 60) + tm.tm_min)*60 + tm.tm_sec) << 1);
    }
    return w;
}

/* Convert from Network 32-bit standard time (RFC 738) to ITS
** disk-format date/time
*/

static w10_t
itsnet2qdat(uint32 ntm)
{
    register w10_t w;
    struct tm tm;

    LRHSET(w, 0, 0);

    /* Special hack - assume time is coming from a real ITS dump, and
    ** compensate for timezone so dates don't change.  ITS always assumed
    ** EST/EDT (-5 hrs from GMT).  In order for local time breakdown to
    ** provide us with the original EST values, we need to add in the offset
    ** between the local timezone and EST.
    ** Yes, this is very site-dependent and should be an OS dependent routine.
    ** Current assumption for testing/debugging is local zone of PST.
    */
#define NTM_ITSTZOFF ((8-5)*60*60)
    ntm += NTM_ITSTZOFF;

    if (os_net2tm(ntm, &tm)) {	/* Get time breakdown for Net time value */
	/* Now put together an ITS time word */
	LHSET(w, ((h10_t)(tm.tm_year % 100)<<9) | ((tm.tm_mon+1)<<5) | tm.tm_mday);
	RHSET(w, ((((h10_t)tm.tm_hour * 60) + tm.tm_min)*60 + tm.tm_sec) << 1);
    }
    return w;
}

/* Given bytesize and bytecount, find length in words and
** set funny bits in RH of reference date to indicate actual EOF.
*/
static w10_t
itsfillen(register uint32 bsz,
	  register uint32 bcnt,
	  w10_t *rfd)
{
    register uint32 bpw, wlen;
    register w10_t w;

    if (bsz == 0) {		/* Leave refdate field 0, assume S=36 */
	LRHSET(w, bcnt >> H10BITS, bcnt & H10MASK);
	return w;
    }
    if (bsz > W10BITS)
	bsz = W10BITS;
    bpw = W10BITS / bsz;	/* Find # bytes per word */
    wlen = bcnt / bpw;		/* Find # words */
    bcnt %= bpw;		/* Find remainder */
    if (bcnt)
	++wlen;
    LRHSET(w, wlen >> H10BITS, wlen & H10MASK);

    /* Now compute funny bits in RH of ref date.  These are the low 9 bits,
    ** the meanings of which are documented in SYSTEM;FSDEFS >.
    */
/*
	;LET S=BITS PER BYTE, C=COUNT OF UNUSED BYTES IN LAST WD
	;400+100xS+C	S=1 TO 3	C=0 TO 35.
	;200+20xS+C	S=4 TO 7	C=0 TO 8
	;44+4xS+C	S=8 TO 18.	C=0 TO 3
	;44-S		S=19. TO 36.	C=0
	;NOTE THAT OLD FILES HAVE UNBYTE=0 => S=36.
*/
    bcnt = bpw - bcnt;		/* Turn # bytes left into # bytes unused */
    if (bsz < 4)
	bsz = 0400 + (0100 * bsz) + bcnt;
    else if (bsz < 8)
	bsz = 0200 + (020 * bsz) + bcnt;
    else if (bsz < 19)
	bsz = 044 + (04 * bsz) + bcnt;
    else
	bsz = 044 - bsz;

    RHSET(*rfd, RHGET(*rfd) | (bsz & 0777));
    return w;
}
#endif /* VMTAPE_ITSDUMP */

/* Tape Directory functions for raw magtape files */

static void td_rdadd(struct vmttdrdef *td, struct vmtrecdef *rd);
static void td_rddel(struct vmttdrdef *td, struct vmtrecdef *rd);

static void
td_init(struct vmttdrdef *td)
{
    td->tdfmt = 0;
    td->tdfils = td->tdrecs = td->tdbytes = 0;
    td->tdrd = td->tdcur = NULL;
    td->tdfname = NULL;
    td->tdmaxrsiz = 0;
}

static void
td_reset(register struct vmttdrdef *td)
{
    if (td->tdfname)
	free(td->tdfname);
    while (td->tdrd)
	td_rddel(td, td->tdrd->rdprev);
    td_init(td);
}


/* Output ASCII version of tape directory */
static void
td_fout(struct vmttdrdef *td,
	FILE *f,
	char *fnam)
{
    struct vmtrecdef *rd;
    int bol = TRUE;

    fprintf(f, "\
; Tape directory for %s\n\
; Bytes: %ld, Records: %d, Files(EOF marks): %d\n\
TF-Format: raw %s\n",
		fnam ? fnam : "<none>",
		td->tdbytes, td->tdrecs, td->tdfils,
		fnam ? fnam : "");
    if (rd = td->tdrd) {
	do {
	    if (bol) {
		fprintf(f, "%lu:", rd->rdloc);
		bol = FALSE;
	    }
	    if (rd->rdcnt == 0) {
		fprintf(f, " EOF\n");
		bol = TRUE;
	    } else if (rd->rdcnt == 1) fprintf(f, " %lu", rd->rdlen);
	    else fprintf(f, " %lu*%u", rd->rdlen, rd->rdcnt);
	} while ((rd = rd->rdnext) != td->tdrd);
    }
    if (!bol) fprintf(f, "\n");
    fprintf(f, "EOT:\n");
}

/* Append a record to end of tape directory.
**	Count 0 means a tapemark (EOF).
**	Returns NULL if needs to allocate new record def and out of space.
*/

static struct vmtrecdef *
td_recapp(register struct vmttdrdef *td,
	  unsigned long len,
	  int cnt, int err)
{
    register struct vmtrecdef *rd;

    /* Try for fast merge with previous record */
    if (td->tdrd) {
	rd = td->tdrd->rdprev;	/* Get prev record */
	if (!err && !rd->rderr && cnt && rd->rdcnt && len == rd->rdlen) {
	    rd->rdcnt += cnt;
	    td->tdrecs += cnt;
	    td->tdbytes += (len * cnt);
	    return rd;
	}
    }

    /* Can't merge, create new rec */
    if (!(rd = (struct vmtrecdef *)malloc(sizeof(struct vmtrecdef)))) {
	/* Up to caller to report errors... */
	return NULL;
    }
    rd->rdloc = td->tdbytes;
    rd->rdlen = len;
    rd->rdcnt = cnt;
    rd->rderr = err;
    td_rdadd(td, rd);		/* Add to circ list */
    if (cnt) {			/* Normal data record */
	td->tdrecs += cnt;
	td->tdbytes += (len * cnt);
	if (td->tdmaxrsiz < len)	/* Update max rec size seen */
	    td->tdmaxrsiz = len;
    } else {			/* Tapemark (EOF) */
	td->tdfils++;
    }
    return rd;
}

/* Link into overall list */

static void
td_rdadd(struct vmttdrdef *td,
	 struct vmtrecdef *rd)
{
    if (!td->tdrd) {
	td->tdrd = rd->rdprev = rd->rdnext = rd;
	return;
    }
    rd->rdnext = td->tdrd;
    rd->rdprev = td->tdrd->rdprev;
    rd->rdprev->rdnext = rd;
    td->tdrd->rdprev = rd;
}

/* Unlink from overall list */

static void
td_rddel(register struct vmttdrdef *td,
	 register struct vmtrecdef *rd)
{
    register struct vmtrecdef *rdn = rd->rdnext;

    if (rd == rdn)		/* Only thing on list? */
	td->tdrd = NULL;
    else {
	if (rd == td->tdrd)	/* Ugh, flushing start of multiple list?! */
	    td->tdrd = rdn;	/* Dumb solution, make next node be start */

	rd->rdprev->rdnext = rdn;	/* Link previous forward to next */
	rdn->rdprev = rd->rdprev;	/* Link next back to previous */
    }
    free((char *)rd);
}

/* Truncate the list after the current canonicalized logical position.
**	This is used when about to write to the tape.
*/
static void
td_trunc(register struct vmttdrdef *td)
{
    register struct vmtrecdef *rd = td->tdcur;

    if (rd == NULL)		/* If already at EOT */
	return;

    if (td->tdrncur > 0) {	/* Pos is inside a vmtrecdef? */
	rd->rdcnt = td->tdrncur;	/* Chop off remaining count */
	if ((rd = rd->rdnext) == td->tdrd)	/* Get next, check it */
	    rd = NULL;			/* None left! */
    }
    /* Flush all vmtrecdefs from rd to end, inclusive */
    if (rd) {
	register struct vmtrecdef *pd;
	do {
	    /* Flush vmtrecdef from end, remember its address */
	    pd = td->tdrd->rdprev;
	    td_rddel(td, pd);
	} while (rd != pd);	/* Keep going until we flushed rd */
    }

    /* Now set up EOT state */
    td->tdcur = NULL;
    td->tdrncur = 1;
}

/* OS-Dependent code
   Perhaps someday move into an osdtap.c ?
 */

#if VMTAPE_ITSDUMP

#include <time.h>

#if CENV_SYS_UNIX
# include <sys/stat.h>
#endif

/* OS_FULLPATH - Generate full pathname given path of current directory
**	plus filename relative to that directory.
**	Returns # chars the string would need, including terminating NUL byte.
**	If this number is <= maxlen, the string is copied into loc.
**
**	Only needed for magtape DUMP creation hacking.
*/
static int
os_fullpath(char *loc, char *dir, char *file, int maxlen)
{
    register int cnt;
    int dlen, flen;

    dlen = strlen(dir);
    flen = strlen(file);
#if CENV_SYS_UNIX
    if (file[0] == '/') {
	dlen = 0;
    } else {
	char *cp = strrchr(dir, '/');
	dlen = (cp ? 1+(cp-dir) : 0);
    }
#else	/* Unknown, assume simple append */
#endif
    cnt = dlen+flen+1;
    if (cnt <= maxlen) {
	if (dlen) strncpy(loc, dir, dlen);
	strcpy(loc+dlen, file);
    }
    return cnt;
}

/* OS_FMTIME - Find last modified time of file open on stream.
**	Only needed for ITS DUMP tape hacking.
*/
static int
os_fmtime(FILE *f, register struct tm *tm)
{
#if CENV_SYS_UNIX
    struct stat sbuf;
    struct tm *stm;

    if ((fstat(fileno(f), &sbuf) >= 0)
      && (stm = localtime(&sbuf.st_mtime))) {
	*tm = *stm;
	return TRUE;
    }
#elif CENV_SYS_MAC
    FileParam pb;
    int err;
    struct tm *stm;

    pb.ioFRefNum = f->handle;
    pb.ioFDirIndex = 0;
    pb.ioVRefNum = 0;
    pb.ioNamePtr = NULL;
    err = PBGetFInfoSync((ParmBlkPtr)(&pb));
    if ( (err == 0) 
      && (stm = localtime(&pb.ioFlMdDat))) {
	*tm = *stm;
	return TRUE;
    }

#else
# error "Unimplemented OS routine os_fmtime()"
#endif
    return FALSE;
}

/* OS_NET2TM - Convert 32-bit Network Time (RFC738) into a TM struct.
**	Only needed for magtape DUMP creation hacking.
*/
static int
os_net2tm(unsigned long ntm,
	  register struct tm *tm)
{
    time_t utime;
    register struct tm *stm;
#if CENV_SYS_UNIX
	/* Temporary crock cuz I'm too tired. */
#define NTM_1_1_76 2398291200UL	/* Known ntm value for 1/1/76 (leap year) */
 	/* ntm value of Unix epoch (1/1/70) */
#define NTM_UEPOCH (NTM_1_1_76 - (((365*6)+1) * (24*60*60)))

    if (ntm < NTM_UEPOCH)
	return FALSE;
    utime = ntm - NTM_UEPOCH;
#endif /* UNIX */
    if (stm = localtime(&utime)) {
	*tm = *stm;
	return TRUE;
    }
    return FALSE;
}
#endif /* VMTAPE_ITSDUMP */
