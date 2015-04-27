/* UEXBCONV.C - Convert .EXB file to .SAV
*/
/* $Id: uexbconv.c,v 2.2 2001/11/10 21:28:59 klh Exp $
*/
/*  Copyright © 2001 Kenneth L. Harrenstien
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
 * $Log: uexbconv.c,v $
 * Revision 2.2  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/* The KL10 FE is a PDP-11 that stores its PDP-10 binaries, particularly
 * the bootstraps "boot.exe" and "mtboot.exe", in a format called
 * "RSX-BINARY".  These files sometimes exist on the KL filesystem with
 * the extension .EXB.
 *
 * The KL10 program RSXFMT.EXE converts from .SAV to .EXB but does not
 * furnish the opposite conversion; hence this utility.
 */

/*
BOOT.EXB is in what RSXFMT.EXE calls "RSX-Binary" (or RSB)
format, which stores 4 8-bit bytes as two PDP-11 words, one each
halfword per code at PUTBYT:

	Word: <00><B1><B0>,,<00><B3><B2>

Actually the file is better thought of as a file of 18-bit bytes with
a PDP-11 word in each 18-bit byte.  These PDP-11 words are organized
into records like so:
	<16-bit count of 8-bit bytes>
	<bytes... bytes...>
	<1 padding byte if needed to get to 16-bit word boundary>


When transforming a SAV file into RSB format, the words are munched
and output as bytes in the following order by the code at SAVGRC:

.SAV format     => Internal  =>   Output bytes
	M Blocks of N bytes:
<-wc>,,<addr-1>	=> 0,,<addr> =>	<xxxx><B3><B2><B1><B0>	hi 4 bits dropped
<data>		=>  <data>   =>	  <B4><B3><B2><B1><B0>	hi 4 in low 4 of B4
<data>
...
	1 Block of 4 bytes:
<jrst>,,<saddr> => 0,,<addr> =>	<xxxx><B3><B2><B1><B0>	hi 4 bits dropped

*/
#include <stdio.h>

#include "word10.h"
#include "wfio.h"

#include "wfio.c"	/* To avoid linkage hassle */

int swverb = 0;
WFILE wfis, wfos;

static int getbyt(WFILE *wf);
static int getcnt(WFILE *wf);
static int getaddr(WFILE *wf, long *ap);
static int getw10(WFILE *wf, w10_t *wp);

static char usage[] = "\
Usage: %s [-v] < infile.exb > outfile.sav\n";

main(int argc, char **argv)
{
    int n;
    int wc;
    long addr;
    long saddr;
    WFILE *wf = &wfis;
    WFILE *owf = &wfos;
    w10_t w;
    char *arg;

    if (argc > 1) {
	arg = argv[1];
	if (arg[0] != '-') {
	    fprintf(stderr, usage, argv[0]);
	    exit(1);
	}
	switch (arg[1]) {
	case 'v': swverb = 1; break;
	default:
	    fprintf(stderr, usage);
	    exit(1);
	}
    }

    wf_init(wf, WFT_C36, stdin);
    wf_init(owf, WFT_C36, stdout);

    while ((n = getcnt(wf)) != EOF) {
      gotn:
	if (n < 4) {
	    fprintf(stderr, "Bad block, length %d < 4\n", n);
	    break;
	}
	if (getaddr(wf, &addr)) {
	    fprintf(stderr, "Unexpected EOF, incomplete addr\n");
	    break;
	}
	if ((n -= 4) == 0) {
	    /* Start address */
	    W10_XSET(w, 0254000 /* JRST */, addr & H10MASK);
	    wf_put(owf, w);
	    if (swverb)
		fprintf(stderr, "START = %lo,,%lo\n",
			(long)LHGET(w), (long)RHGET(w));
	    if ((n = getcnt(wf)) == EOF) {
		if (swverb)
		    fprintf(stderr, "DONE! EOF seen as expected\n");
		exit(0);
	    }
	    fprintf(stderr, "Warning - More data after start address = %d\n",
		    n);
	    goto gotn;
	}
	if ((n % 5) != 0) {
	    fprintf(stderr, "Word byte count not multiple of 5: %d\n",
		    n);
	    break;
	}
	wc = n/5;
	W10_XSET(w, (-wc) & H10MASK, (addr-1) & H10MASK);
	if (swverb)
	    fprintf(stderr, "10block = %lo,,%lo\n",
		    (long)LHGET(w), (long)RHGET(w));
	wf_put(owf, w);
	while (--wc >= 0) {
	    if (getw10(wf, &w)) {
		fprintf(stderr, "Unexpected EOF reading word, %d left\n",
			(-wc)+1);
		break;
	    }
	    wf_put(owf, w);
	}
	if (wc >= 0) break;

	if (n & 01) {
	    if (swverb)
		fprintf(stderr, "Padding input 1 byte\n");
	    (void)getbyt(wf);
	}

    }
    fprintf(stderr, "Aborted!\n");
    exit(1);
}

static int getbyt(WFILE *wf)
{
    w10_t w;
    static int bx = -1;
    static unsigned char b[4];

    switch (++bx) {
    case 0:
	if (!wf_get(wf, &w))
	    return EOF;
	if ((LHGET(w) & 0600000) || (RHGET(w) & 06000000)) {
	    fprintf(stderr, "MBZ bits set in word: %lo,,%lo\n",
		    (long)LHGET(w), (long)RHGET(w));
	    return EOF;
	}
	b[0] = LHGET(w) & 0377;
	b[1] = LHGET(w) >> 8;
	b[2] = RHGET(w) & 0377;
	b[3] = RHGET(w) >> 8;

	if (swverb)
	    fprintf(stderr, "InputWd = %lo,,%lo = %o %o %o %o\n",
		    (long)LHGET(w), (long)RHGET(w),
		    b[0], b[1], b[2], b[3]);
	return b[0];
    case 1:
	return b[1];
    case 2:
	return b[2];
    case 3:
	bx = -1;
	return b[3];
    }
    return EOF;
}

static int getcnt(WFILE *wf)
{
    int b0, b1, cnt;

    if ((b0 = getbyt(wf)) == EOF)
	return EOF;
    if ((b1 = getbyt(wf)) == EOF) {
	fprintf(stderr, "Bad EOF, 1-byte count\n");
	return EOF;
    }
    cnt = (b1<<8) | b0;
    if (swverb)
	fprintf(stderr, "Byteblock Count = %d\n", cnt);
    return cnt;
}

static int getaddr(WFILE *wf, long *ap)
{
    int i, c;
    long addr = 0;

    for (i = 0; i < 4; ++i) {
	if ((c = getbyt(wf)) == EOF)
	    return EOF;
	addr |= ((long)c) << (8*i);
    }
    *ap = addr;
    if (swverb)
	fprintf(stderr, "Addr = %lo\n", addr);
    return 0;
}

static int getw10(WFILE *wf, w10_t *wp)
{
    int i, c;
    w10_t w;
    unsigned char b[5];

    for (i = 0; i < 5; ++i) {
	if ((c = getbyt(wf)) == EOF)
	    return EOF;
	b[i] = c & 0xff;
    }
    W10_XSET(w,
	     ((b[4]&017)<<14) | (b[3]<<6) | (b[2]>>2),
	     ((b[2]&03)<<16) | (b[1]<<8) |  b[0]);
    *wp = w;
    if (swverb)
	fprintf(stderr, "Word = %lo,,%lo\n",
		(long)LHGET(w), (long)RHGET(w));
    return 0;
}

