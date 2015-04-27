/* VMTAPE.H - Virtual Magnetic-Tape support definitions
*/
/* $Id: vmtape.h,v 2.4 2002/03/28 16:57:39 klh Exp $
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
 * $Log: vmtape.h,v $
 * Revision 2.4  2002/03/28 16:57:39  klh
 * First pass at using LFS (Large File Support)
 * RAW format code revised
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#ifndef VMTAPE_INCLUDED		/* Only include once */
#define VMTAPE_INCLUDED 1

#ifdef RCSID
 RCSID(vmtape_h,"$Id: vmtape.h,v 2.4 2002/03/28 16:57:39 klh Exp $")
#endif

#include "cenv.h"	/* Ensure have OSD target stuff */

#ifndef  VMTAPE_ARGSTR	/* Include mount arg string parsing? */
# define VMTAPE_ARGSTR 1
#endif
#ifndef  VMTAPE_POPEN	/* Include pipe input and decompress? */
# define VMTAPE_POPEN (CENV_SYS_UNIX)
#endif
#ifndef VMTAPE_ITSDUMP	/* Include ITS DUMP tape-creation code? */
# ifdef KLH10_SYS_ITS
#  define VMTAPE_ITSDUMP KLH10_SYS_ITS
# else
#  define VMTAPE_ITSDUMP 0
# endif
#endif

#ifndef  VMTAPE_T20DUMP	/* Include T20 DUMPER tape-creation code? */
# define VMTAPE_T20DUMP 0	/* Not implemented yet */
#endif
#ifndef  VMTAPE_RAW	/* Include RAW format code? */
# define VMTAPE_RAW 1	/* Always; conditional is mainly to clarify code */
#endif

#if VMTAPE_ITSDUMP || VMTAPE_T20DUMP	/* Need this if doing word I/O */
# include "word10.h"
# include "wfio.h"
#endif

/* Attempt to determine max size and type of tape position information.
 * This is most important when running on platforms that only support
 * 64-bit integers with a non-long data type.
 */
#ifndef VMTAPE_POS_T
# if CENV_SYSF_LFS == 0
#  define VMTAPE_POS_T long
# else
#  define VMTAPE_POS_T off_t
# endif
# if CENV_SYSF_FSEEKO
#  define VMTAPE_POS_FSEEK(f,pos) fseeko((f), (off_t)(pos), SEEK_SET)
# else
#  define VMTAPE_POS_FSEEK(f,pos) fseek((f), (long)(pos), SEEK_SET)
# endif
# define VMTAPE_POS_FMT CENV_SYSF_LFS_FMT
#endif

typedef VMTAPE_POS_T vmtpos_t;


/*			TAPE FORMATS

There are several different classes of formats involved when dealing with
virtual (and real) tapes.

SOURCE FORMAT
-------------
	From VMTAPE's viewpoint, the first is the "source format" of
the file or files that it opens.  Such files may be text of a
particular kind, or data with a particular structure.  The source
format is normally determined by the file(s) themselves, directly by
their actual contents or indirectly by their filename extension.  At runtime
the most you can do is help VMTAPE determine this using this algorithm:

	(1) Caller mandates source format to use regardless of pathname
		and without examining any existing contents.
	(2) Otherwise, VMTAPE figures it out based on:
		Pathname extension given (or found).
		If it exists, contents of file examined.
	(3) Otherwise, VMTAPE uses a default source format.
		Currently this is CTL+RAW.

Known or possible source formats & extensions:

    CTL { ,.tpk,.tdr}	 - Control file, text specifies additional source info.
    TPC {.tpc}		 - "TaPe data with Counts"
    TPS {.tps,.e11,.tap} - "TaPe data, SIMH format" (Supnik)
    TPE (.tpe,.e11,.tap} - "TaPe data, E11 format" (John Wilson)
    RAW {.tpr,.tap}      - Raw tape data
				(Note that ".tap" is now ambiguous.)

These are tracked internally as VMT_FMT_xxx symbols.


APPLICATION FORMAT
------------------
	This is a high level description of the contents of the tape,
based on what application is ultimately expected to read it.  For
example, some common PDP-10 backup formats were ITS DUMP, Tenex BSYS,
and DEC DUMPER.  It is not necessary to know this format unless
something about the tape generation process depends on it.  This is
generally specified at the same time and in the same way as the source
format.

Known application formats:

	ITSDUMP - ITS DUMP
	DUMPER6	- TOPS DUMPER V6 (there are several earlier versions)

These could be tracked internally as VMTAF_xxx but for now they are
combined with the source format specs.


VIRTUAL FORMAT
--------------
	The virtual format is the representation that VMTAPE provides
to the caller.  This constitutes the size of a tape frame, the number
of frames in each record, and the occurrence of tape EOF or BOT/EOT
marks.

Currently this representation is always that of a 9-track tape such
that:
	- Each frame provides one 8-bit byte of data
	- Records provided as single blocks of bytes
	- Tape EOF marks indicated by zero-length blocks
	- BOT/EOT indicated by pollable status bits

This may someday change if, for example, we want to emulate 7-track
tape (6-bit frames) or DECtape (36-bit frames).  In that case, the
virtual format would be specified at runtime by the caller, and tracked
internally with VMTVF_xxx symbols.


WORD PACKING FORMAT (DATA MODE)
-------------------
	Finally for emulator purposes there is a fourth format to
consider, the way in which this virtual tape frame data is packed into
36-bit words (or words of whatever machine is being emulated).  For
the PDP-10 tape drives this is called the "data mode".  Strictly
speaking this mode is the responsibility of the caller, which will be
emulating a particular tape controller device; however, for convenience
VMTAPE includes provisions for doing word I/O in either "core-dump" or
"industry-compatible" mode.


IN PRACTICE - CTL vs NON-CTL
----------------------------
	Because there is presently only one common virtual format, and
the application formats are specified as special cases of the
"Control" source format, in effect the source format specification
almost entirely determines the processing done by VMTAPE.
	The "Control" format relies on using an auxiliary text file
to describe the exact structure of the tape; it points to other files
which contain the actual tape data.  Any source or application format
can use a control file, although not all of them require it.

	CTL required:	ITSDUMP, DUMPER, RAW
	CTL optional:	TPC, TPS, TPE

The control file is identified either by an explicit filename
extension (.tpk or the deprecated .tdr), or indirectly by no extension
at all.

 */
/*
** Currently, the following kinds of tapes can be read:
**	VMT_FMT_RAW - General-purpose raw 8-bit frames, with arbitrary tapemarks
**	VMT_FMT_TPS - General-purpose using bidirectional record lengths.
**	VMT_FMT_TPE - Ditto w/o padding
**	VMT_FMT_ITS - read-only, created on-the-fly with no record boundaries
**		except at end of each included file, when a tapemark is
**		invented.
** The following kinds of tapes can be written:
**	VMT_FMT_RAW - as above
**	VMT_FMT_TPS - as above
**	VMT_FMT_TPE - as above
*/
#define VMTFF_CTL    0x1	/* Control file (implies XCTL) */
#define VMTFF_XCTL   0x2	/* Control file must exist (else optional) */
#define VMTFF_RD     0x4	/* Readable format */
#define VMTFF_WR     0x8	/* Writable format */
#define VMTFF_ALIAS  0x100	/* Alias entry - true fmt in VMTFF_FMT */
#define VMTFF_FMT    0xff	/* Alias entry's true format */

#define VMTFORMATS \
    vmtfdef(VMT_FMT_UNK, "unknown",  NULL, 0),\
    vmtfdef(VMT_FMT_CTL,     "ctl", "tpk", VMTFF_CTL),\
    vmtfdef(VMT_FMT_ITS,     "its",  NULL, VMTFF_CTL|VMTFF_RD),\
    vmtfdef(VMT_FMT_RAW,     "raw", "tpr", VMTFF_XCTL|VMTFF_RD|VMTFF_WR),\
    vmtfdef(VMT_FMT_TPC,     "tpc", "tpc", VMTFF_RD),\
    vmtfdef(VMT_FMT_TPE,     "tpe", "tpe", VMTFF_RD|VMTFF_WR),\
    vmtfdef(VMT_FMT_TPS,     "tps", "tps", VMTFF_RD|VMTFF_WR),\
    vmtfdef(VMT_FMT_A_CTL,   "ctl", "tdr", VMTFF_ALIAS|VMT_FMT_CTL),\
    vmtfdef(VMT_FMT_A_TAP,   "any", "tap", VMTFF_ALIAS|VMTAPE_FMT_DEFAULT),\
    vmtfdef(VMT_FMT_A_TPR,   "any", "tap", VMTFF_ALIAS|VMT_FMT_RAW),\
    vmtfdef(VMT_FMT_A_ITS,"itsdump", NULL, VMTFF_ALIAS|VMT_FMT_ITS),\
    vmtfdef(VMT_FMT_N,        NULL,  NULL, 0)

/* First alias entry */
#define VMT_FMT_ALIASES VMT_FMT_A_CTL

#if 0 /* Possible future formats */
    vmtfdef(VMT_FMT_DUMPER6, "dumper6", NULL, VMTFF_CTL),
#endif

#define vmtfdef(en, str, ext, flgs) en
enum vmtfmt { VMTFORMATS };			/* mt_format values */
#undef vmtfdef

/* Default format.  Used when creating a tape, or reading one with a ".tap"
** extension, and none was explicitly specified.
*/
#ifndef  VMTAPE_FMT_DEFAULT
# define VMTAPE_FMT_DEFAULT VMT_FMT_TPS
#endif


/* CTL tape directory def */
struct vmttdrdef {
    int tdfmt;			/* Format of data in tape data file */
				/* (unused; assumes one byte per frame) */
    char *tdfname;		/* M Name of tape data file */
    struct vmtrecdef *tdrd;	/* M Start of circ list of record/mark defs */
    struct vmtrecdef *tdcur;	/*   Current record def */
    int tdrncur;		/*   Current # of record in *tdcur */
    long tdrfcnt;		/*   # frames left to read in current record */

    /* Stats */
    unsigned long tdfils;	/* Total # files */
    unsigned long tdrecs;	/* Total # records */
    unsigned long tdmaxrsiz;	/* # bytes in longest record known */
    vmtpos_t tdbytes;		/* Total # bytes (frames) */
};

/* Tape spec and status block.
**	Members marked with 'M' are malloced dynamically.
*/
struct vmtape {
	char *mt_devname;	/*   Device name, for error output */
	void (*mt_errhan)	/*   Error handling routine */
			(void *, struct vmtape *, char *);
	void *mt_errarg;	/*   1st arg provided to errhan */
	int mt_debug;		/*   TRUE to get debugging output */
	char *mt_filename;	/* M Pathname of tapefile to use */
	char *mt_datpath;	/* M Pathname of data file */
	FILE *mt_ctlf;		/*   Cntl file I/O handle */
	FILE *mt_datf;		/*   Data file I/O handle */
	int mt_ispipe;		/*   TRUE if datf is a popen'd pipe */
	int mt_writable;	/*   TRUE if tape can be written */
	enum vmtfmt mt_format;	/*   VMT_FMT_xxx format specifier */
	struct vmtfmt_s *mt_fmtp; /*   Pointer to format table entry */
	int mt_state;		/*   TS_xxx internal state */

	/* Tape status - following are updated after each tape op */
	int mt_iowrt;		/* TRUE if current IO is write */
	long mt_frames;		/* # frames in record just read */
	int mt_err;		/* NZ if error in last operation */
	int mt_eof;		/* TRUE if just read EOF */
	int mt_eot;		/* TRUE if at physical EOT */
	int mt_bot;		/* TRUE if at physical BOT */

	/* CTL (tapedir) file stuff */
	char *mt_contents;	/* M Pointer to snarfed ctl file contents */
	int mt_lineno;		/*   Line number while parsing ctl file */
	int mt_ntdrerrs;	/*   # of errors parsing ctl file */
	struct vmttdrdef mt_tdr;	/*   Tape directory info */

#if VMTAPE_ITSDUMP
	struct vmtfildef *mt_fdefs;	/* M Tape files info */
	struct vmtfildef *mt_fcur;	/*   Current file */
	struct wfile mt_wf;	/* For word data i/o on datf */
	w10_t mt_fitsdate;	/* ITS date of tapefile creation */
	w10_t *mt_ptr;		/* Pointer to tape data */
	int mt_cnt;		/* # words still pointed to */
	w10_t mt_wbuf[12];	/* Small temporary buffer */
#endif
};

/* State notes:

    Not all states are used by all formats.  The comments document which
    are used by which.

    TPS/TPE and RAW share RDATA and RWRITE.  It's important for stdio output
    to remember whether we're reading or writing since a fseek is required
    whenever this changes.
*/
enum {	TS_CLOSED=0,	/* All: No tape open */
	TS_THEAD,	/* ITSDUMP: Reading tape header */
	TS_FNEXT,	/* ITSDUMP: Start reading next file */
	TS_FHEAD,	/* ITSDUMP: Reading file header */
	TS_FDATA,	/* ITSDUMP: Reading file data */
	TS_FEOF,	/* ITSDUMP: Reading tapemark (file EOF) */
	TS_EOT,		/* ITSDUMP: Reading EOT */
	TS_RDATA,	/* Raw, TPS/TPE: Idle or Reading data */
	TS_RWRITE,	/* Raw, TPS/TPE: Writing tape */
	TS_ERR		/* All: Error, try to re-synch on next I/O */
};


/* Attributes for vmt_attrparse(), vmt_attrmount() and possibly
   other future calls.
 */
struct vmtattrs {
    int vmta_mask;
# define VMTA_FMTREQ	0x1
# define VMTA_FMTDFLT	0x2
# define VMTA_MODE	0x4
# define VMTA_STREAM	0x8
# define VMTA_PATH	0x10
# define VMTA_CTLPATH	0x20
# define VMTA_DATPATH	0x40
# define VMTA_DEV	0x80
# define VMTA_UNZIP	0x100
# define VMTA_INMEM	0x200
# define VMTA_FSKIP	0x400
    enum vmtfmt vmta_fmtreq;	/* Explicitly requested format */
    enum vmtfmt vmta_fmtdflt;	/* User-specified default format */
    int vmta_mode;
# define VMT_MODE_RDONLY 0x1	/* Read-only; files must exist */
# define VMT_MODE_UPDATE 0x2	/* Read/Write; files must exist */
# define VMT_MODE_CREATE 0x4	/* Read/Write; files created, must not exist */
    FILE *vmta_stream;		/* File actually this stream */
    int vmta_unzip;		/* TRUE to attempt unzips */
    int vmta_inmem;		/* TRUE to attempt in-memory operation */
    int vmta_fskip;		/* # of files to skip on mount */

    char vmta_dev[8];		/* Device type (for non-virtual) */
    char vmta_path[128];
    char vmta_ctlpath[128];
    char vmta_datpath[128];
};



/* External facilities */

extern void vmt_init(struct vmtape *, char *);
#if VMTAPE_ARGSTR
extern int  vmt_pathmount(struct vmtape *t, char *path, char *args);
extern int  vmt_attrparse(struct vmtape *t, struct vmtattrs *ta, char *args);
#endif
extern int  vmt_attrmount(struct vmtape *t, struct vmtattrs *ta);
extern int  vmt_unmount(struct vmtape *);
extern int  vmt_rewind(struct vmtape *);

extern int  vmt_rget(struct vmtape *, unsigned char *, size_t);
extern int  vmt_rput(struct vmtape *, unsigned char *, size_t);
extern int  vmt_eput(struct vmtape *, int);

extern int vmt_eof(struct vmtape *);
extern int vmt_eot(struct vmtape *);
extern int vmt_rspace(struct vmtape *, int, unsigned long);
extern int vmt_fspace(struct vmtape *, int, unsigned long);

enum vmtfmt vmt_strtofmt(char *);
enum vmtfmt vmt_exttofmt(char *);
#define vmt_tapepath(t) ((t)->mt_filename ? (t)->mt_filename : (t)->mt_datpath)
#define vmt_format(t) ((t)->mt_format)
#define vmt_fmtname(fmt) ((((unsigned)(fmt)) < VMT_FMT_N) ? \
			vmtfmttab[fmt].tf_name : "unknownfmt")

#define vmt_ismounted(t) ((t)->mt_state != 0)
#define vmt_iswritable(t) ((t)->mt_writable)
#define vmt_isatbot(t) ((t)->mt_bot)
#define vmt_isateof(t) ((t)->mt_eof)
#define vmt_isateot(t) ((t)->mt_eot)
#define vmt_errors(t)	((t)->mt_err)
#define vmt_framecnt(t) ((t)->mt_frames)
#define vmt_frstowds(t,f) ((f)/(t)->mt_fpw)
#define vmt_wdstofrs(t,w) ((w)*(t)->mt_fpw)

#endif /* ifndef VMTAPE_INCLUDED */
