/* WFCONV.C - 36-bit word file format conversion filter
*/
/* $Id: wfconv.c,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: wfconv.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*
	NOTE: due to the fact that PDP-10 words are used as the canonical
	input conversion target (and output conversion source), input is
	always effectively rounded up to a word boundary.

	This can cause the addition of a few trailing zero bytes when
	converting from Text-Newline or Ansi-ASCII format, so WFCONV is
	not a general-purpose text-to-text conversion program.  For that,
	one can use:

		# To insert CR (must use ^V or equiv to quote the ^M)
		$ sed 's/$/\^M/' temp.nls > temp.crlfs

		# To delete CR (must use ^V or equiv to quote the ^M)
		$ sed 's/\^M$//' temp.crlfs > temp.nls

*/

#include <stdio.h>
#include <stdlib.h>	/* Malloc and friends */
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "rcsid.h"
#include "word10.h"
#include "wfio.h"

#ifdef RCSID
 RCSID(wfconv_c,"$Id: wfconv.c,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

#define WFIO_DEBUG 0

#include "wfio.c"	/* Inclusion, so that WFIO_DEBUG works */

static int wtypesw(int ch);
static char usage[] = "\
Usage: wfconv -io <infile >outfile\n\
  where 'i' and 'o' are chars specifying the input and output formats:\n\
    c   - Core-dump  (std tape format, 4 8-bit, 1 4-bit bytes = 36 bits)\n\
    h   - High-density (FTP 36-bit image, 9 8-bit bytes = 72 bits)\n\
    a,7 - Ansi-Ascii (4 7-bit, 1 8-bit byte = 36 bits)\n\
    s,6 - Sixbit     (6 6-bit bytes = 36 bits)\n\
    u   - Unixified  (Alan Bawden format, various = 36 bits)\n\
    t   - Text-Newline (CRLF-NL conversion; 5 7-bit bytes = 35 bits ONLY)\n\
    d   - Debug (output only - show word values)\n\
    D   - Debug (like -d with offsets)\n\
  Note: EOF on input always zero-pads up to a PDP-10 word boundary.\n\
";

static int dbgout = 0;	/* 1 for d, 2 for D (offsets) */

int
main(int argc, char **argv)
{
    char *typestr;
    int from, to;
    struct wfile wfrom, wto;
    w10_t w;

    if (argc != 2 || strlen(typestr = argv[1]) != 3) {
	fprintf(stderr, usage);
	exit(1);
    }
    if (typestr[0] != '-'
      || (from = wtypesw(typestr[1])) < 0) {
	fprintf(stderr, "%s: unknown input conversion '%c'\n%s",
					argv[0], typestr[1], usage);
	exit(1);
    }
    if (typestr[2] == 'd') {
	dbgout = 1;
    } else if (typestr[2] == 'D') {
	dbgout = 2;
    } else if ((to = wtypesw(typestr[2])) < 0) {
	fprintf(stderr, "%s: unknown output conversion '%c'\n%s",
					argv[0], typestr[2], usage);
	exit(1);
    }

    wf_init(&wfrom, from, stdin);

    if (dbgout) {			/* Debug output, just show value */
	long wloc = 0;
	while (wf_get(&wfrom, &w) > 0) {
	    if (dbgout == 2)
		fprintf(stdout, "%#lo: ", wloc++);
	    fprintf(stdout, "%6lo,,%6lo\n", (long)LHGET(w), (long)RHGET(w));
	}
    } else {
	wf_init(&wto,   to,   stdout);

	while (wf_get(&wfrom, &w) > 0)
	    if (wf_put(&wto, w) <= 0) {
		fprintf(stderr, "Output error, aborting\n");
		break;
	    }
	wf_flush(&wto);
    }

    fclose(stdout);
    exit(0);
}

static int wtypesw(int ch)
{
    char cbuf[4];
    char *cp = cbuf;

    strcpy(cp, "x36");		/* Set up to let wf_type do the work */
    switch (ch) {		/* Handle any special synonyms */
	default:  *cp = ch;  break;
	case 't':
	case 'T': cp = "tnl"; break;
    }
    return wf_type(cp);
}

