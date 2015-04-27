/* WFIO.C - 36-bit Word File I/O facilities
*/
/* $Id: wfio.c,v 2.4 2002/03/28 16:53:54 klh Exp $
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
 * $Log: wfio.c,v $
 * Revision 2.4  2002/03/28 16:53:54  klh
 * First pass at using LFS (Large File Support)
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*
File formats supported:

	u36 - Alan Bawden's unixified stuff
	h36 - High-density mode - same as FTP 36-bit Image.
		All bits used in all bytes.
		byte 0 - high 8 bits of word
		byte 5 - low 4 bits of word in high end, high 4 bits of
				next word in low end.
		Thus 9 bytes for every 2 words.
	c36 - Core-Dump mode - standard tape format.
	a36 - ANSI-ASCII mode - 5 bytes, 7 bits per byte (high bit clear)
		except in last byte, where low bit (35) of word is high bit
		of byte.
	s36 - Sixbit mode - 6 bytes, 6 bits/byte in low bits.  Was mainly
		for 7-track tapes.
	nlt - Newline Text.  On input, converts NL to CR-LF; on output,
		converts CR-LF to NL.

    TAPE FORMATS:

	S36: Tape_Sixbit (6)
		 0  0 B0  1  2  3  4  5
		 0  0  6  7  8  9 10 11
		 0  0 12 13 14 15 16 17
		 0  0 18 19 20 21 22 23
		 0  0 24 25 26 27 28 29
		 0  0 30 31 32 33 34 35

	C36: Tape_Coredump (5)
		B0  1  2  3  4  5  6  7
		 8  9 10 11 12 13 14 15
		16 17 18 19 20 21 22 23
		24 25 26 27 28 29 30 31
		 0  0  0  0 32 33 34 35

	A36: Tape_Ascii (5)
		 0 B0  1  2  3  4  5  6
		 0  7  8  9 10 11 12 13
		 0 14 15 16 17 18 19 20
		 0 21 22 23 24 25 26 27
		35 28 29 30 31 32 33 34

	H36: Tape_Hidens, Tape_FTP (9/2)
		B0  1  2  3  4  5  6  7
		 8  9 10 11 12 13 14 15
		16 17 18 19 20 21 22 23
		24 25 26 27 28 29 30 31
		32 33 34 35 B0  1  2  3
		 4  5  6  7  8  9 10 11
		12 13 14 15 16 17 18 19
		20 21 22 23 24 25 26 27
		28 29 30 31 32 33 34 35

    DISK FORMATS:

	dbd9: Disk_BigEnd_Double (9/2) - Same as H36 (Tape_Hidens)

	dld9: Disk_LittleEnd_Double (9/2)
		28 29 30 31 32 33 34 35
		20 21 22 23 24 25 26 27
		12 13 14 15 16 17 18 19
		 4  5  6  7  8  9 10 11
		32 33 34 35 B0  1  2  3
		24 25 26 27 28 29 30 31
		16 17 18 19 20 21 22 23
		 8  9 10 11 12 13 14 15
		B0  1  2  3  4  5  6  7

	dbh4: Disk_BigEnd_Halfword (8)
		 0  0  0  0  0  0  0  0
		 0  0  0  0  0  0 B0  1
		 2  3  4  5  6  7  8  9
		10 11 12 13 14 15 16 17
		 0  0  0  0  0  0  0  0
		 0  0  0  0  0  0 18 19
		20 21 22 23 24 25 26 27
		28 29 30 31 32 33 34 35

	dlh4: Disk_LittleEnd_Halfword (8)
		28 29 30 31 32 33 34 35
		20 21 22 23 24 25 26 27
		 0  0  0  0  0  0 18 19
		 0  0  0  0  0  0  0  0
		10 11 12 13 14 15 16 17
		 2  3  4  5  6  7  8  9
		 0  0  0  0  0  0 B0  1
		 0  0  0  0  0  0  0  0

	dbw8: Disk_BigEnd_Word (8)
		 0  0  0  0  0  0  0  0
		 0  0  0  0  0  0  0  0
		 0  0  0  0  0  0  0  0
		 0  0  0  0 B0  1  2  3
		 4  5  6  7  8  9 10 11
		12 13 14 15 16 17 18 19
		20 21 22 23 24 25 26 27
		28 29 30 31 32 33 34 35

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

#include "cenv.h"

#include <stdio.h>
#include <stdlib.h>	/* Malloc and friends */
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "rcsid.h"
#include "word10.h"
#include "wfio.h"

#ifdef RCSID
 RCSID(wfio_c,"$Id: wfio.c,v 2.4 2002/03/28 16:53:54 klh Exp $")
#endif

#ifndef WFIO_DEBUG	/* Include debug output capability if 1 */
#  define WFIO_DEBUG 0
#endif

static int wfu_get(struct wfile *, w10_t *),
	wfu_gasc(struct wfile *, w10_t *, int);

static char *wftnames[] = {
#  define wtdef(i,n) n
	WF_TYPENAMDEFS
#  undef wtdef
};
#define WF_NTYPES (sizeof(wftnames)/sizeof(wftnames[0]))

void
wf_init(register struct wfile *wf, int type, FILE *f)
{
    wf->wff = f;
    wf->wfsiop = ftell(f);
    wf->wfloc = 0;
    wf->wflastch = WFC_NONE;
    wf->wferrlen = wf->wferrfmt = 0;
    wf->wftype = type;
    if (0 <= wf->wftype && wf->wftype < WF_NTYPES)
	wf->wftypnam = wftnames[wf->wftype];
    else {		/* No way to indicate error yet */
	wf->wftypnam = "???";
    }
}

int
wf_type(char *nam)
{
    if (strlen(nam) != 3)
	return -1;		/* Not a name we know about yet */
    if (nam[1] == '3' && nam[2] == '6')
	switch (nam[0]) {
	    case 'u':    case 'U':	return WFT_U36;
	    case 'h':    case 'H':	return WFT_H36;
	    case 'c':    case 'C':	return WFT_C36;
	    case '7':
	    case 'a':    case 'A':	return WFT_A36;
	    case '6':
	    case 's':    case 'S':	return WFT_S36;
	    default:
		return -1;
	}
    if (strcasecmp(nam, "tnl") == 0)
	return WFT_TNL;
    return -1;
}

/* WFILE seeking.

This is tricky primarily for two cases: U36 and H36.

	U36 cannot reliably implement wf_seek for reading, because we have
no idea how many bytes are being used to represent any particular words
(without actually reading them in).  Writing is OK because we always
use the simple-minded tactic of using binary format, 5 bytes per word.

	H36 does know where to go, but has a problem because the two
words of each double-word share bits from a single byte.  This is excerbated
by the fact that we don't know ahead of time whether the next operation
will be a read or a write; in addition to this, ANSI C requires that a
fflush() or fseek() occur between sequences of reads and writes.
	To resolve this, the wflastch variable is used as a state
indicator.  If it is set to:
	WFC_NONE - the I/O pointer is at the start of a word pair,
		either reading or writing.
	WFC_PREREAD - the I/O pointer is logically at the start of the
		2nd word of a pair, but actually is pointing to the byte
		shared by the 2 words, and must read that byte before
		either reading or writing the next word.
	> WFC_NONE - as above, but wflastch already contains the needed 4
		bits, right-justified.  It will have the bit WFC_WVAL set
		if the last operation was a wf_put as opposed to a wf_get.

One other fine detail: in order to implement arbitrary seeks and writes into
existing data for H36 format, wf_flush() must be called before closing the
stream, and it must take care of forcing out the last 4 bits of the
1st word of a pair, so that the bottom 4 bits of existing file data for that
byte aren't clobbered to 0.

*/


#ifndef SEEK_SET
#  define SEEK_SET 0	/* Usual pre-ANSI value */
#endif
#ifndef SEEK_CUR
#  define SEEK_CUR 1	/* Usual pre-ANSI value */
#endif

int
wf_rewind(register struct wfile *wf)
{
    return wf_seek(wf, (wfoff_t)0);
}

int
wf_seek(register struct wfile *wf, wfoff_t loc)
{
    wfoff_t offset;

    switch (wf->wftype) {
    case WFT_U36:		/* Alan Bawden Unixified format */
    case WFT_TNL:
	/* If reading, it's hopeless.  If writing, we have a chance.
	** So assume writing, 5 bytes per word.
	*/
	wf->wflastch = WFC_NONE;	/* Reset in case reading */
	offset = loc * 5;
	break;

    case WFT_H36:		/* High-density (also FTP Image) format */
	if (!wf_flush(wf))	/* Ensure stuff forced out, reset */
	    return 0;
	offset = (loc / 2) * 9;
	if (loc & 01) {		/* If seeking to 2nd of a pair, moby hair! */
	    wf->wflastch = WFC_PREREAD;
	    offset += 4;
	} else
	    wf->wflastch = WFC_NONE;
	break;

    case WFT_C36:		/* Core-dump (aka tape) format */
    case WFT_A36:		/* Ansi-Ascii (7-bit) format */
	offset = loc * 5;
	break;

    case WFT_S36:		/* SIXBIT (7-track tape) format */
	offset = loc * 6;
	break;

    default:
	return 0;
    }

#if CENV_SYSF_FSEEKO
    if (fseeko(wf->wff, wf->wfsiop + offset, SEEK_SET)) {
#else
    if (fseek(wf->wff, (long)(wf->wfsiop + offset), SEEK_SET)) {
#endif
	return 0;
    }
    wf->wfloc = loc;
    return 1;
}

int
wf_flush(register struct wfile *wf)
{
    switch (wf->wftype) {
    case WFT_U36:		/* Alan Bawden Unixified format */
	/* Using full binary encoding by default, no funny stuff */
	break;

    case WFT_H36:		/* High-density (also FTP Image) format */
	if (wf->wflastch >= WFC_WVAL) {
	    register int ch;

	    fflush(wf->wff);		/* Permit switch to reading */
	    ch = getc(wf->wff);		/* Get existing byte */
	    if (ch != EOF) {
		ch = ((wf->wflastch & 017) << 4) | (ch & 017);
		if (fseek(wf->wff, (long)-1, SEEK_CUR) != 0)	/* Back up */
		    return 0;
	    } else ch = (wf->wflastch & 017) << 4;
	    putc(ch, wf->wff);		/* Force out last write byte */
	    wf->wflastch = WFC_NONE;
	}
	break;

    case WFT_C36:		/* Core-dump (aka tape) format */
    case WFT_A36:		/* Ansi-Ascii (7-bit) format */
    case WFT_S36:		/* SIXBIT (7-track tape) format */
	break;

    case WFT_TNL:		/* Text (7-bit) format */
	if (wf->wflastch == WFC_CR) {	/* Last char a pending CR? */
#if 1	    /* Problematic because flushing out the bare CR is only OK
		if no more words are to be output.  if any more follow
		then it's likely a LF will be present - thus the output
		of the CR would be incorrect.
		For now, assume wf_flush is only called when about to
		close the stream; this is true for WFCONV which is the
		only thing likely to be using WFT_TNL anyway.
	    */
	    putc('\r', wf->wff);
#endif
	    wf->wflastch = WFC_NONE;	/* No char saved */
	}
	break;

    default:
	return 0;
    }

    fflush(wf->wff);
    return !ferror(wf->wff);
}

int
wf_get(register struct wfile *wf, w10_t *wp)
{
    register FILE *f;
    register int i;
    uint18 cbuf[6];
    register int cix;

    if (wf->wftype == WFT_U36)	/* Alan Bawden Unixified format */
	return wfu_get(wf, wp);

    /* Check first byte; if none, no more words. */
    f = wf->wff;
    if ((i = getc(f)) == EOF) {
	/* But TNL format may still have something left over */
	if ((wf->wftype == WFT_TNL)
	  && (wf->wflastch == WFC_LF))
	    i = 0;
	else
	    return 0;
    }

    switch (wf->wftype) {
    case WFT_H36:		/* High-density (also FTP Image) format */
	cbuf[0] = i & 0377;
	cbuf[1] = ((i = getc(f)) == EOF) ? 0 : (i & 0377);
	cbuf[2] = ((i = getc(f)) == EOF) ? 0 : (i & 0377);
	cbuf[3] = ((i = getc(f)) == EOF) ? 0 : (i & 0377);
	switch (wf->wflastch) {
	case WFC_NONE:			/* First word of a pair */
	    cbuf[4] = ((i = getc(f)) == EOF) ? 0 : (i & 0377);
	    XWDPSET(wp,
		(  ((uint18)cbuf[0] << 10)
		 | (cbuf[1] << 2)
		 | (cbuf[2] >> 6)),
		(  ((uint18)(cbuf[2] & 077) << 12)
		 | (cbuf[3] << 4)
		 | (cbuf[4] >> 4))	/* High 4 bits of last byte */
		);
	    wf->wflastch = cbuf[4] & 017;	/* Remember low 4 bits */
	    break;

	case WFC_PREREAD:	/* Gross hack: 2nd word, after wf_seek */
	    cbuf[4] = ((i = getc(f)) == EOF) ? 0 : (i & 0377);
	    XWDPSET(wp,
		(  ((uint18)(cbuf[0]&017) << 14) /* Low 4 bits of prev byte */
		 | (cbuf[1] << 6)
		 | (cbuf[2] >> 2)),
		(  ((uint18)(cbuf[2] & 03) << 16)
		 | (cbuf[3] << 8)
		 | (cbuf[4]))
		);
	    wf->wflastch = WFC_NONE;
	    break;

	default:		/* Second word of a pair, after wf_get */
	    if (wf->wflastch >= WFC_WVAL)
		return -1;		/* Error, last op was a wf_put! */
	    XWDPSET(wp,
		(  ((uint18)wf->wflastch << 14)	/* Low 4 bits of prev byte */
		 | (cbuf[0] << 6)
		 | (cbuf[1] >> 2)),
		(  ((uint18)(cbuf[1] & 03) << 16)
		 | (cbuf[2] << 8)
		 | (cbuf[3]))
		);
	    wf->wflastch = WFC_NONE;
	    break;
	}
	break;

    case WFT_C36:		/* Core-dump (aka tape) format */
	cbuf[0] = i & 0377;
	cbuf[1] = ((i = getc(f)) == EOF) ? 0 : (i & 0377);
	cbuf[2] = ((i = getc(f)) == EOF) ? 0 : (i & 0377);
	cbuf[3] = ((i = getc(f)) == EOF) ? 0 : (i & 0377);
	cbuf[4] = ((i = getc(f)) == EOF) ? 0 : (i & 0377);
	XWDPSET(wp,
		(  ((uint18)cbuf[0] << 10)
		 | (cbuf[1] << 2)
		 | (cbuf[2] >> 6)),
		(  ((uint18)(cbuf[2] & 077) << 12)
		 | (cbuf[3] << 4)
		 | (cbuf[4] & 017))	/* Low 4 bits of last byte */
		);
	break;

    case WFT_A36:		/* Ansi-Ascii (7-bit) format */
	cbuf[0] = i & 0177;
	cbuf[1] = ((i = getc(f)) == EOF) ? 0 : (i & 0177);
	cbuf[2] = ((i = getc(f)) == EOF) ? 0 : (i & 0177);
	cbuf[3] = ((i = getc(f)) == EOF) ? 0 : (i & 0177);
	cbuf[4] = ((i = getc(f)) == EOF) ? 0 :
			((i & 0177) << 1) | ((i & 0200) >> 7);	/* Keep 8th */
	XWDPSET(wp,
		(  ((uint18)cbuf[0] << 11)
		 | (cbuf[1] << 4)
		 | (cbuf[2] >> 3)),
		(  ((uint18)(cbuf[2] & 07) << 15)
		 | (cbuf[3] << 8)
		 | cbuf[4] )
		);
	break;

    case WFT_TNL:		/* Unix NL text (7-bit) format */
	/* Convert all incoming NL chars to CR-LF sequence */
	cix = 0;
	if (wf->wflastch == WFC_LF) {	/* Have leftover LF from last wd? */
	    cbuf[cix++] = WFC_LF;	/* Yep, start with it */
	    wf->wflastch = WFC_NONE;	/* and reset */
	}
	/* Loop until have 5 input chars */
	for (;;) {
	    if ((i &= 0177) == '\n') {
		cbuf[cix] = WFC_CR;	/* First output PDP-10 ASCII CR */
		i = WFC_LF;		/* Ensure next is PDP-10 ASCII LF */
		if (++cix >= 5) {	/* If no room for LF after CR insert */
		    wf->wflastch = i;	/* then save for next word */
		    break;
		}
	    }
	    cbuf[cix] = i;
	    if (++cix >= 5)
		break;
	    if ((i = getc(f)) == EOF)
		i = 0;
	}
	XWDPSET(wp,
		(  ((uint18)cbuf[0] << 11)
		 | (cbuf[1] << 4)
		 | (cbuf[2] >> 3) ),
		(  ((uint18)(cbuf[2] & 07) << 15)
		 | (cbuf[3] << 8)
		 | (cbuf[4] << 1) )
		);
	break;

    case WFT_S36:		/* SIXBIT (7-track tape) format */
	cbuf[0] = i & 077;
	cbuf[1] = ((i = getc(f)) == EOF) ? 0 : (i & 077);
	cbuf[2] = ((i = getc(f)) == EOF) ? 0 : (i & 077);
	cbuf[3] = ((i = getc(f)) == EOF) ? 0 : (i & 077);
	cbuf[4] = ((i = getc(f)) == EOF) ? 0 : (i & 077);
	cbuf[5] = ((i = getc(f)) == EOF) ? 0 : (i & 077);
	XWDPSET(wp,
		( ((uint18)cbuf[0] << 12) | (cbuf[1] << 6) | cbuf[2] ),
		( ((uint18)cbuf[3] << 12) | (cbuf[4] << 6) | cbuf[5] )
		);
	break;
    }

    if (feof(f)) {
	wf->wferrlen++;		/* Error in unix length of file */
	if (wf->wfdebug)
	    fprintf(stderr, "Unexpected EOF in middle of PDP-10 word.\n");
    }
    return 1;
}

int
wf_put(register struct wfile *wf,
       register w10_t w)
{
    register FILE *f;
    register int n;
    register unsigned char *cp;
    unsigned char cbuf[10];

    switch (wf->wftype) {
    case WFT_U36:		/* Alan Bawden Unixified format */
	/* Use full binary encoding by default, no funny stuff */
	cbuf[0] = 0360 | ((LHGET(w)>>14)&017);
	cbuf[1] = (LHGET(w)>> 6)&0377;
	cbuf[2] = (((LHGET(w)&077)<<2) | ((RHGET(w)>>16)&003));
	cbuf[3] = (RHGET(w)>>8)&0377;
	cbuf[4] = RHGET(w)&0377;
	n = 5;
	break;

    case WFT_H36:		/* High-density (also FTP Image) format */
	switch (wf->wflastch) {
	case WFC_NONE:		/* First word of a pair */
	    cbuf[0] = (LHGET(w)>>10)&0377;
	    cbuf[1] = (LHGET(w)>> 2)&0377;
	    cbuf[2] = (((LHGET(w)&03)<<6) | ((RHGET(w)>>12)&077));
	    cbuf[3] = (RHGET(w)>>4)&0377;
	    n = 4;
	    wf->wflastch = (RHGET(w) & 017) | WFC_WVAL;	/* Save last 4 bits */
	    break;

	default:		/* Second word of pair, after wf_put */
	    if (wf->wflastch < WFC_WVAL)
		return -1;		/* Last op was a wf_get?! */
	    cbuf[0] = ((wf->wflastch&017)<<4) | ((LHGET(w)>>14)&017);
	    cbuf[1] = (LHGET(w)>>6)&0377;
	    cbuf[2] = (((LHGET(w)&077)<<2) | ((RHGET(w)>>16)&03));
	    cbuf[3] = (RHGET(w)>>8)&0377;
	    cbuf[4] = (RHGET(w)   )&0377;
	    n = 5;
	    wf->wflastch = WFC_NONE;
	    break;

	case WFC_PREREAD:	/* Second word of a pair, after wf_seek */
	    n = getc(wf->wff);				/* Find byte val */
	    if (n == EOF)				/* Back up one */
		n = 0;
	    if (fseek(wf->wff, (long)-1, SEEK_CUR) != 0)
		return -1;
	    cbuf[0] = (n & (017<<4))			/* Save high 4 bits */
			| ((LHGET(w)>>14)&017);
	    cbuf[1] = (LHGET(w)>>6)&0377;
	    cbuf[2] = (((LHGET(w)&077)<<2) | ((RHGET(w)>>16)&03));
	    cbuf[3] = (RHGET(w)>>8)&0377;
	    cbuf[4] = (RHGET(w)   )&0377;
	    n = 5;
	    wf->wflastch = WFC_NONE;
	    break;
	}
	break;

    case WFT_C36:		/* Core-dump (aka tape) format */
	cbuf[0] = (LHGET(w)>>10)&0377;
	cbuf[1] = (LHGET(w)>> 2)&0377;
	cbuf[2] = (((LHGET(w)&03)<<6) | ((RHGET(w)>>12)&077));
	cbuf[3] = (RHGET(w)>>4)&0377;
	cbuf[4] = RHGET(w)&017;		/* Put last 4 bits in low end */
	n = 5;
	break;

    case WFT_A36:		/* Ansi-Ascii (7-bit) format */
	cbuf[0] = (LHGET(w)>>11)&0177;
	cbuf[1] = (LHGET(w)>> 4)&0177;
	cbuf[2] = (((LHGET(w)&017)<<3) | ((RHGET(w)>>15)&07));
	cbuf[3] = (RHGET(w)>>8)&0177;
	cbuf[4] = ((RHGET(w)>>1)&0177) | ((RHGET(w)&01)<<7);
	n = 5;
	break;

    case WFT_TNL:		/* Text (7-bit) format */
	/* Convert all CR-LF sequences to just NL */
	cp = cbuf;
	if (wf->wflastch == WFC_CR) {	/* Last char a pending CR? */
	    *cp++ = WFC_CR;		/* Re-insert into our buffer */
	    wf->wflastch = WFC_NONE;	/* No char saved */
	    n = 6;
	} else n = 5;

	*cp++ = (LHGET(w)>>11)&0177;
	*cp++ = (LHGET(w)>> 4)&0177;
	*cp++ = (((LHGET(w)&017)<<3) | ((RHGET(w)>>15)&07));
	*cp++ = (RHGET(w)>>8)&0177;
	*cp   = (RHGET(w)>>1)&0177;

	f = wf->wff;
	for (cp = cbuf; --n >= 0;) {
	    if (*cp == WFC_CR) {	/* If see CR, test for LF */
		if (--n < 0) {		/* ensure a char follows */
		    wf->wflastch = WFC_CR;	/* Nope, save the CR */
		    return !ferror(f);
		}
		if (*++cp == WFC_LF)	/* CR-LF sequence? */
		    *cp = '\n';		/* Yep, replace by native NL */
		else
		    putc('\r', f);	/* Nope, let native CR pass through */
	    }
	    putc(*cp++, f);
	}
	return !ferror(f);

    case WFT_S36:		/* SIXBIT (7-track tape) format */
	cbuf[0] = (LHGET(w)>>12)&077;
	cbuf[1] = (LHGET(w)>> 6)&077;
	cbuf[2] = (LHGET(w)    )&077;
	cbuf[3] = (RHGET(w)>>12)&077;
	cbuf[4] = (RHGET(w)>> 6)&077;
	cbuf[5] = (RHGET(w)    )&077;
	n = 6;
	break;

    default:
	return -1;
    }

    f = wf->wff;
    for (cp = cbuf; --n >= 0;)
	putc(*cp++, f);
    return !ferror(f);
}

static int
wfu_get(register struct wfile *wf,
	w10_t *aw)
{
    register FILE *f = wf->wff;
    register uint18 lh, rh;
    register int i;
    unsigned int cbuf[5];	/* 16 bits is sufficient */
    register unsigned ch;
#if WFIO_DEBUG
    wf->wftp = wf->wftbuf;
#endif
    if (wf->wflastch != WFC_NONE) {
	/* Start this word using last (virtual) char.  By definition
	** that means the rest of this word uses the 7-bit encoding.
	*/
	return wfu_gasc(wf, aw, wf->wflastch);
    }
    if ((lh = getc(f)) == EOF)
	return 0;
#if WFIO_DEBUG
    *wf->wftp++ = lh;
#endif
    wf->wfloc++;

    /* First byte determines how rest of encoding will go */
    if ((lh & 0360) != 0360) {
	return wfu_gasc(wf, aw, lh);
    }
    lh = (lh&017) << (18-4); 		/* Set 4 bits 740000,,0 */

    for (i = 0; ++i <= 4; ) {
	if ((ch = getc(f)) == EOF)
	    cbuf[i] = 03;		/* ITS pads with ^C */
	else {
	    cbuf[i] = ch;
	    wf->wfloc++;
	}
#if WFIO_DEBUG
	*wf->wftp++ = ch;
#endif
    }

    /* Put binary word together.  Coded to keep int shifts within 16 bits. */
    lh |= (cbuf[1]&0377) << (18-12);	/* Set 8 bits  37700,,0 */
    rh = cbuf[2] & 0377;
    lh |= (rh >> 2) & 077;		/* Set 6 bits     77,,0 */
    rh = (rh & 03) << (18-2);		/* and 2 bits	    ,,600000 */
    rh |= (cbuf[3]&0377) << (18-10);	/* Set 8 bits	    ,,177400 */
    rh |= (cbuf[4]&0377);		/* Set 8 bits       ,,000377 */
    LRHSET(*aw, lh, rh);

#if WFIO_DEBUG
    if (wf->wfdebug) {
	register int i = wf->wftp - wf->wftbuf;
	register unsigned char *tp = wf->wftbuf;
	for (; --i >= 0; )
	    printf(" %3o", *tp++);
	printf("  BIN %6lo,,%6lo\n", (long) LHGET(*aw), (long) RHGET(*aw));
    }
#endif
    if (feof(f)) {
	wf->wferrlen++;	/* Error in unix length of file */
	if (wf->wfdebug)
	    fprintf(stderr, "Unexpected EOF in middle of PDP-10 word.\n");
    }
    return 1;			/* Won, word deposited */
}

static int
wfu_gasc(register struct wfile *wf,
	 register w10_t *aw,
	 register int ch)
{
    register int i = 0;
    int cbuf[5];


    if (ch == wf->wflastch) {
	cbuf[i++] = ch;		/* Stuff first char in immediately, no check */
	ch = getc(wf->wff);
#if WFIO_DEBUG
	*wf->wftp++ = ch;
#endif
    }

    wf->wflastch = WFC_NONE;
    for (;;) {
	switch (ch) {
	case EOF:
	    wf->wferrlen++;		/* Error in unix length of file */
	    if (wf->wfdebug)
		fprintf(stderr, "Unexpected EOF in middle of PDP-10 word.\n");
	    ch = 03;			/* ITS pads with ^C */
	    break;

	case 012:  cbuf[i++] = 015;		break;
	case 015:  ch = 012;			break;
	case 0177: cbuf[i++] = 0177; ch = 07;	break;
	case 0207: cbuf[i++] = 0177; ch = 0177;	break;
	case 0212: cbuf[i++] = 0177; ch = 015;	break;
	case 0215: cbuf[i++] = 0177; ch = 012;	break;
	case 0356: ch = 015;			break;
	case 0357: ch = 0177;			break;
	default:
	    if (ch < 0177) break;
	    if (ch < 0360) { cbuf[i++] = 0177; ch -= 0200; break; }
	    wf->wferrfmt++;	/* Error in unixify of this file */
	    fprintf(stderr, "Unexpected binary-byte in middle of word!\n");
	    fprintf(stderr, " cbuf:");
	    {	register int cnt;
		for(cnt = 0; cnt < i; ++cnt)
		    printf(" %o", cbuf[cnt]);
		printf(" Bad byte: %o   File location: %ld.\n", ch,
				wf->wfloc-1);
	    }
	    return -1;
	}
	/* Deposit char.  If it's an extra one, put in wflastch */
	if (i >= 5) {
	    wf->wflastch = ch;
	    break;
	}
	cbuf[i++] = ch;
	if (i >= 5)
	    break;
	if ((ch = getc(wf->wff)) != EOF)
	    wf->wfloc++;
#if WFIO_DEBUG
	*wf->wftp++ = ch;
#endif
    }

    /* OK, now put together word from the 5 chars we gobbled.  If there
    ** was an extra virtual char, it was left in wflastch for the next call.
    */
    LHSET(*aw, ((uint18)cbuf[0]<<(18-7)) | (cbuf[1]<<(18-14)) | (cbuf[2]>>3));
    RHSET(*aw, ((uint18)(cbuf[2]&07)<<(18-3)) | (cbuf[3]<<(18-10)) | (cbuf[4]<<1));

#if WFIO_DEBUG
    if (wf->wfdebug) {
	register int i = wf->wftp - wf->wftbuf;
	register unsigned char *cp;
	ch = 5-i;
	for (cp = wf->wftbuf; --i >= 0; )
	    printf(" %3o", *cp++);
	while (--ch >= 0)
	    printf(" -- ");
	printf("  Asc %6lo,,%6lo   [", (long) LHGET(*aw), (long) RHGET(*aw));
	for (ch = 0; ch < 5; ++ch)
	    printf(" %3o", cbuf[ch]);
	printf("]\n");
    }
#endif
    return 1;
}

