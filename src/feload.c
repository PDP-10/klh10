/* FELOAD.C - PDP-10 boot loader routines
*/
/* $Id: feload.c,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: feload.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*	Loads executables into physical memory.
*/

#include <stdio.h>
#include <stdlib.h>	/* Malloc and friends */
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "klh10.h"	/* For overall config defs */
#include "word10.h"
#include "kn10ops.h"
#include "wfio.h"
#include "feload.h"

#ifdef RCSID
 RCSID(feload_c,"$Id: feload.c,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* Commonly needed vector value */
#define LH_JRST ((h10_t)I_JRST<<9)

/* DEC sharable save format - block IDs */
#define DECSSF_DIR 01776
#define DECSSF_EV  01775
#define DECSSF_PDV 01774
#define DECSSF_END 01777

static int load_typefind(WFILE *, struct loadinfo *);
static int load_sblk(WFILE *, struct loadinfo *);
static int dump_dsblk(WFILE *, struct loadinfo *, int);
static int load_decsav(WFILE *, struct loadinfo *);
static int load_decexe(WFILE *, struct loadinfo *);

int
fe_load(register WFILE *wf,
	register struct loadinfo *lp)
{
    /* Initialize result variables of loadinfo struct */
    lp->ldi_allerr = 0;
    lp->ldi_loaddr = (uint18)1 << 18;
    lp->ldi_hiaddr = 0;
    LRHSET(lp->ldi_startwd, 0, 0);
    lp->ldi_ndata = 0;
    lp->ldi_nsyms = 0;
    lp->ldi_cksumerr = 0;
    lp->ldi_aibgot = 0;
    lp->ldi_evlen = -1;
    lp->ldi_evloc = 0;

    /* See if load format is already known.  If not, try to determine
    ** from initial contents of file.
    */
    if (lp->ldi_type == LOADT_UNKNOWN) {
	lp->ldi_type = load_typefind(wf, lp);
	wf_rewind(wf);				/* Back up to start */
    }

    switch (lp->ldi_type) {
    case LOADT_UNKNOWN:
	lp->ldi_typname = "Unknown";
	break;
    case LOADT_PDUMP:
	lp->ldi_typname = "ITS-PDUMP";
	fprintf(stderr, "Loading aborted, unable to load ITS PDUMP format.\n");
	break;
    case LOADT_SBLK:
	lp->ldi_typname = "ITS-SBLK";
	return load_sblk(wf, lp);
    case LOADT_DECSAV:
	lp->ldi_typname = "DEC-CSAV";
	return load_decsav(wf, lp);
    case LOADT_DECEXE:
	lp->ldi_typname = "DEC-PEXE";
	return load_decexe(wf, lp);
    }
    return 0;
}

/* LDVM_MAP - Do loader-specific mapping of addresses to physical memory
**	locations.  For now, assumes only phys mapping in effect, no virtual
**	funnies.
**	Also, permit addresses larger than 18 bits, so caller must be sure
**	arg is OK!
*/
static
vmptr_t ldvm_map(paddr_t pa)
{
    if (pa <= AC_17)
	return &cpu.acblk.cur[pa];	/* Use currently active AC block */

    return vm_physmap(pa);
}

/* FE_DUMP - Dump out memory in a loadable format.
**	The loadinfo struct must be properly set up beforehand:
**		ldi_type = desired dump format
**		ldi_loaddr, ldi_hiaddr = range to dump
**		ldi_startwd = start address in RH if any
**	(other members may become significant with other formats)
*/
int
fe_dump(register WFILE *wf, register struct loadinfo *lp)
{
    int res;

    /* Initialize result variables of loadinfo struct */
    lp->ldi_allerr = 0;
    lp->ldi_ndata = 0;
    lp->ldi_nsyms = 0;
    lp->ldi_cksumerr = 0;

    /* See if load format is already known.  If not, try to determine
    ** from current system type.
    */
    if (lp->ldi_type == LOADT_UNKNOWN) {
#if KLH10_SYS_ITS
	lp->ldi_type = LOADT_SBLK;
#else
	lp->ldi_type = LOADT_DECSAV;
#endif
    }

    switch (lp->ldi_type) {
    case LOADT_SBLK:
    case LOADT_DECSAV:
	lp->ldi_typname = (lp->ldi_type == LOADT_SBLK)
				? "ITS-SBLK" : "DEC-CSAV";
	res = dump_dsblk(wf, lp, lp->ldi_type);
	break;

    default:
	fprintf(stderr, "fe_dump: unknown format %d\n", lp->ldi_type);
	lp->ldi_allerr++;
	return 0;
    }

    wf_flush(wf);
    return res;
}


static int
load_typefind(register WFILE *wf,
	      struct loadinfo *lp)
{
    w10_t w, w2;

    /* Read first word (or two) to help determine format type */
    if (wf_get(wf, &w) <= 0) {
	fprintf(stderr, "Loading aborted, initial read failed\n");
	return LOADT_UNKNOWN;			/* Ugh return */
    }

    if (op10m_skipge(w)) {
	/* First word is positive */
	if (op10m_skipe(w))		/* If 1st word is 0 */
	    return LOADT_PDUMP;		/* assume ITS PDUMP */
	if (LHGET(w) == DECSSF_DIR)	/* If special EXE value */
	    return LOADT_DECEXE;	/* assume DEC sharable */
	return LOADT_SBLK;		/* Else must be SBLK with no RIM */
    }

    /* First word is negative, check for special values */
    if (RHGET(w))			/* If RH non-zero, */
	return LOADT_DECSAV;		/* can't be any of the others */

    if (LHGET(w) != 0710440		/* If not DATAI PTR,0 (RIM) */
      && LHGET(w) != 0777761)		/* or -17,,0 (RIM10) */
	return LOADT_DECSAV;		/* then also can't be RIM/SBLK */
    
    /* We might be looking at a RIM or RIM10 preface to a SBLK file.
    ** Check the second word to be doubly sure.  If it isn't a CONO PTR,60
    ** then give up and default to DEC unsharable format.
    */
    if (wf_get(wf, &w2) <= 0) {
	fprintf(stderr, "Loading aborted, read of 2nd word failed\n");
	return LOADT_UNKNOWN;			/* Ugh return */
    }
    if (LHGET(w2) == 0710600 && RHGET(w2) == 060)	/* CONO PTR,60 */
	return LOADT_SBLK;
    return LOADT_DECSAV;
}

/* ITS SBLK format loading code */

/*
SBLK format:
	<RIM loader block>
	<simple blocks>
	<start address>
	 [<optional symtab blocks>]
	<start address>

    <RIM loader block>:		
	This block contains 1 to 16 (RIM10) or more (RIM) words, where the
	first word must be non-zero and the last word (which may be the
	first word, and which marks the end of the block) is JRST 1.
	    The RIM (Read-In Mode) loader starts with:
		0: 710440,,     0	DATAI PTR,
		1: 710600,,    60	CONO PTR,60
	    The RIM10 loader starts with:
		0: -017,,0
		1: CONO PTR,60

    <simple block>:
	-<# wds>,,<addr>
	<block of that many words>
	<checksum word>

    <start address>:
	A non-negative word seen while looking for a simple block is
	interpreted as the start address.  (normally, a JRST <addr>).
	This may be 0 if there is no start address.

    <symtab block>:
	Exactly like simple blocks, but with zero <addr>s.  A duplicate
	start-address indicates there are no more symtab blocks.
*/

#define DDTLOC 0774000	/* Loc of ITS Exec DDT in phys mem */


static int ld_bget(WFILE *, struct loadinfo *, w10_t *);

static int
load_sblk(register WFILE *wf,
	  register struct loadinfo *lp)
{
    register int i;
    register paddr_t addr;
    register w10_t *wp;
#define WBUFLEN 16000+2		/* Make room for big blocks (ITS symtab) */
    register w10_t *wbuf;

    /* Get a buffer for reading blocks into. This is done with malloc
    ** instead of using the stack because at least one platform (Mac MPW C)
    ** silently generates incorrect code for stack frames > 32Kb.
    */
    wbuf = (w10_t *)malloc(sizeof(w10_t) * WBUFLEN);
    if (!wbuf) {
	fprintf(stderr, "Loader couldn't allocate buffer - malloc failed.\n");
	return 0;
    }

    /* First skip over RIM10 loader */
    for (;;) {
	if (wf_get(wf, wbuf) <= 0) {
	    fprintf(stderr, "SBLK load aborted before data blocks\n");
	    free(wbuf);
	    return 0;			/* Ugh return */
	}
	if ((LHGET(wbuf[0]) == LH_JRST) && (RHGET(wbuf[0]) == 1))
	    break;
    }
    if (lp->ldi_debug)
	printf("  RIM10 Loader skipped.\n");


    /* Now read simple data blocks and store them in
    ** PDP-10 physical memory.  This could be done with a memcpy if
    ** phys mem is identical to the buffer format.
    */
    while ((i = ld_bget(wf, lp, wbuf)) > 1) {
	addr = RHGET(wbuf[0]);		/* Find addr of first word */
	if (addr < lp->ldi_loaddr) lp->ldi_loaddr = addr;
	lp->ldi_ndata += (i -= 2);
	for (wp = wbuf+1; --i >= 0; ++wp, ++addr)
	    vm_pset(ldvm_map(addr & H10MASK), *wp);
	if (addr > lp->ldi_hiaddr) lp->ldi_hiaddr = addr-1;
    }
    if (i <= 0) {
	fprintf(stderr, "SBLK aborted before first start address\n");
	free(wbuf);
	return 0;
    }

    /* Now have a positive word in wbuf, assume it's the start addr */
    lp->ldi_startwd = wbuf[0];		/* Remember it */
    if (lp->ldi_debug)
	printf("  Start address = %lo\n", (long)RHGET(wbuf[0]));

    /* Now gobble up symtab blocks. */
    while ((i = ld_bget(wf, lp, wbuf)) > 1) {
	register w10_t sptr;
	int nsym = i - 2;
	lp->ldi_nsyms += nsym;
	addr = RHGET(wbuf[0]);		/* Find addr of first word */
	if (addr) {
	    int j;

	    if (addr != 3) {	/* Understand Misc Info blocks */
		fprintf(stderr, "Symtab block with nonzero RH: %#lo\n",
					(long)addr);
		continue;			/* Ignore it */
	    }
	    if (i <= 0)
		continue;

	    /* We only understand Type 1 subblocks (Assembly Info) */
	    if (LHGET(wbuf[1]) != ((-6)&H10MASK) || RHGET(wbuf[1]) != 1) {
		fprintf(stderr, "Unknown subblock in MiscInfo blk: %lo,,%lo\n",
				(long)LHGET(wbuf[1]), (long)RHGET(wbuf[1]));
		continue;			/* Ignore it */
	    }
	    if (--i <= 0) continue;

	    /* Have at least one word of data for info.
	    ** Copy them into array... unspecified words remain zero.
	    */
	    lp->ldi_aibgot = i = ((i > 6) ? 6 : i);
	    for (j = 0; j < i; ++j)
		lp->ldi_asminf[j] = wbuf[j+2];
	    continue;		/* And that's all for now */
	}
	if (nsym <= 0) continue;

	/* Add this batch of syms to Exec DDT's symtab.
	** Perhaps add a flag to make this optional.
	*/
	sptr = vm_pget(ldvm_map((paddr_t)DDTLOC-1));
	if (RHGET(sptr) != DDTLOC-2) {	/* Check for DDT */
	    printf("No EXEC DDT?  Ignoring block of %d syms.\n", nsym);
	    continue;			/* Not there */
	}
	sptr = vm_pget(ldvm_map((paddr_t)DDTLOC-2));
	LHSET(sptr, (LHGET(sptr)-nsym)&H10MASK);
	RHSET(sptr, (RHGET(sptr)-nsym)&H10MASK);
	addr = RHGET(sptr);		/* Copy sym data to this loc */
	for (wp = wbuf+1; --i >= 0; ++wp, ++addr)
	    vm_pset(ldvm_map(addr & H10MASK), *wp);

	/* Now tell DDT about it */
	vm_pset(ldvm_map((paddr_t)DDTLOC-2), sptr);	/* Store new symtab ptr */
	vm_psetlh(ldvm_map((paddr_t)DDTLOC-1), H10MASK);	/* Tell DDT symtab munged */
	vm_pset(ldvm_map((paddr_t)DDTLOC-4), lp->ldi_startwd);	/* Set start addr */
	printf("Added %d syms to DDT, total %ld\n", nsym,
					(long) -(LHGET(sptr)|~MASK18));
    }

    if (i <= 0) {
	fprintf(stderr, "SBLK aborted before final start address\n");
	free(wbuf);
	return 0;
    }

    /* Last check -- should be duplicate of first start word */
    if (LHGET(wbuf[0]) != LHGET(lp->ldi_startwd)
      || RHGET(wbuf[0]) != RHGET(lp->ldi_startwd)) {
	fprintf(stderr, "SBLK start address mismatch: %#lo,,%#lo != %#lo,,%#lo\n",
		(long) LHGET(lp->ldi_startwd), (long) RHGET(lp->ldi_startwd),
		(long) LHGET(wbuf[0]), (long) RHGET(wbuf[0]));
	free(wbuf);
	return 0;
    }
    free(wbuf);
    return 1;		/* Won! */
}

/* Read in simple block of up to WBUFLEN words, including header & checksum.
**	Returns positive # words read, if all's well.
**	Returns # <= 0 if error, where # gives words read so far.
*/
static int
ld_bget(register WFILE *wf,
	register struct loadinfo *lp,
	register w10_t *aw)
{
    register int i, cnt;

    /* Get first word */
    if (wf_get(wf, aw) <= 0)
	return 0;

    if (op10m_skipge(*aw))
	return 1;			/* Not a simple block, stop now */

    /* Simple block, read in rest of words! */
    i = -(LHGET(*aw) | -H10SIGN);	/* Extend sign and negate to get pos */
    if (i > (WBUFLEN-2)) {
	fprintf(stderr, "Block size too large: %d (max %d)\n", i, WBUFLEN-2);
	return -1;
    }

    if (lp->ldi_debug)
	printf("  ------ Starting block of %d. words, addr=%lo ------\n",
			i, (long)RHGET(*aw));

    for (cnt = 1; --i >= 0; ++cnt) {
	if (wf_get(wf, ++aw) <= 0) {
	    fprintf(stderr, "Unexpected EOF while reading block.\n");
	    return -cnt;
	}
	/* Could compute checksum here */
    }

    /* Block data read in, now get trailing checksum */
    if (wf_get(wf, ++aw) <= 0) {
	fprintf(stderr, "Unexpected EOF while reading checksum.\n");
	return -cnt;
    }

    /* Could compare checksum here */

    return cnt+1;
}

/* ITS SBLK (and DEC SAV) dumping code
**	Per definition of SBLK, no zero words are ever dumped.
*/
static int sblk_out(WFILE *, paddr_t, int, w10_t *);
static int dsav_out(WFILE *, paddr_t, int, w10_t *);

static int
dump_dsblk(register WFILE *wf,
	   register struct loadinfo *lp,
	   int typ)			/* A LOADT_xxx type */
{
    register w10_t *wp;
    register paddr_t addr;
    register int blen;		/* Block length */
    paddr_t baddr;		/* Block start addr */
#define WBOLEN 128
    w10_t wbuf[WBOLEN];

    if (typ == LOADT_SBLK) {
	LRHSET(wbuf[0], LH_JRST, 1);	/* First word is JRST 1 */
	if (wf_put(wf, wbuf[0]) <= 0)
	    return 0;
    }

    addr = lp->ldi_loaddr;
    for (; addr <= lp->ldi_hiaddr;) {
	blen = 0;
	wp = wbuf;

	/* Scan for nonzero word */
	for (; addr <= lp->ldi_hiaddr; ++addr) {
	    *wp = vm_pget(ldvm_map(addr));
	    if (op10m_skipn(*wp)) {
		blen = 1;
		baddr = addr;
		break;
	    }
	}
	if (!blen) break;

	/* Have 1st wd in buffer.
	** Now scan for first zero word, or until buffer full
	*/
	for (; ++addr <= lp->ldi_hiaddr;) {
	    if (blen >= WBOLEN)
		break;
	    *++wp = vm_pget(ldvm_map(addr));
	    if (op10m_skipe(*wp))
		break;
	    ++blen;
	}

	/* Block done.  addr points to next word (may be zero),
	** blen has # of words in buffer/block.
	*/
	if (   (typ == LOADT_SBLK)   ? (sblk_out(wf, baddr, blen, wbuf) <= 0)
	    : ((typ == LOADT_DECSAV) ? (dsav_out(wf, baddr, blen, wbuf) <= 0)
	    : 1)) {
		return 0;		/* Failure of some kind */
	}

	lp->ldi_ndata += blen;		/* Remember # words */
    }

    /* Data blocks done, now add start address (or entry vector) */
    if (typ == LOADT_SBLK) {

	/* Build start address to mark end of simple blocks */
	LRHSET(wbuf[0], LH_JRST, RHGET(lp->ldi_startwd));
	wf_put(wf, wbuf[0]);

	/* Symbols should go here, if we ever remember them */

	/* Now use duplicate start address to mark end of symbols */
	LRHSET(wbuf[0], LH_JRST, RHGET(lp->ldi_startwd));
	return wf_put(wf, wbuf[0]);

    } else if (typ == LOADT_DECSAV) {

	/* Build entry vector to mark end of blocks */
	switch (lp->ldi_evlen) {
	case -1:
	case 0:
	case LH_JRST:
	    LRHSET(wbuf[0], LH_JRST, RHGET(lp->ldi_startwd));
	    break;
	default:
	    LRHSET(wbuf[0], lp->ldi_evlen, lp->ldi_evloc);
	}
	return wf_put(wf, wbuf[0]);
    }
    return 0;
}

static int
sblk_out(register WFILE *wf,
	 register paddr_t addr,
	 register int len,
	 register w10_t *wp)
{
    register w10_t w;

    LRHSET(w, (-len)&H10MASK, addr & H10MASK);	/* -<cnt>,,<addr> */
    if (wf_put(wf, w) <= 0)
	return 0;
    for (; --len >= 0; ++wp)
	if (wf_put(wf, *wp) <= 0)
	    return 0;

    LRHSET(w, 0, 0);		/* Bogus checksum for now */
    return wf_put(wf, w);
}

/* DEC SAV non-sharable SAVE format loading code */

/*
DEC nonsharable save format:
	<data blocks>
	<entry vector pointer>

    <data block>:
	-<# wds>,,<addr-1>
	<block of that many words>

    <entry vector pointer>:
	<length of vector>,,<addr of vector>

	Vector word 0 is instr to execute to start program.
	Vector word 1 is instr to execute to reenter program.
	Vector word 2 contains program version # info.

	BUT if LH is 254000 then:
		start addr = RH(120)
		reenter addr = RH(124)
		version info in 137
	Actually it appears that the RH may be the start address.
		If it's non-zero, let's use that.
*/

static int
load_decsav(register WFILE *wf,
	    register struct loadinfo *lp)
{
    register paddr_t addr;
    register int32 cnt;
    w10_t wdata;

    lp->ldi_evlen = -1;		/* Init entry vector length in case fail */

    for (;;) {
	/* Read first word of block, should be an IOWD */
	if ((cnt = wf_get(wf, &wdata)) <= 0) {
	    return cnt ? 0 : 1;
	}
	if (op10m_skipge(wdata))	/* If word is positive, done! */
	    break;

	/* Read in and load data words for one block */
	cnt = -(LHGET(wdata) | ~MASK18);	/* Get positive count */
	addr = RHGET(wdata)+1;			/* Find 1st loc to load into */
	if (lp->ldi_debug)
	    printf("  ------ Starting block of %ld. words, addr=%lo ------\n",
			(long)cnt, (long)addr);

	if (addr < lp->ldi_loaddr) lp->ldi_loaddr = addr;
	lp->ldi_ndata += cnt;
	for (; --cnt >= 0; ++addr) {
	    if (wf_get(wf, &wdata) <= 0) {
		fprintf(stderr, "Loading aborted, read failed\n");
		return 0;
	    }
	    vm_pset(ldvm_map(addr & H10MASK), wdata);
	}
	if (addr > lp->ldi_hiaddr) lp->ldi_hiaddr = addr-1;
    }

    /* Positive header word seen, assume entry vector */
    if (lp->ldi_debug)
	printf("  ------ Entry vector word %#lo,,%lo ------\n",
			(long)LHGET(wdata), (long)RHGET(wdata));
    lp->ldi_evlen = LHGET(wdata);
    lp->ldi_evloc = RHGET(wdata);

    addr = lp->ldi_evloc;		/* Set up probable start addr */
    if ((lp->ldi_evlen == LH_JRST) && !addr)
	addr = vm_pgetrh(ldvm_map((paddr_t)0120));
    LRHSET(lp->ldi_startwd, 0, addr & H10MASK);

    return 1;
}

/* DEC SAV dumping code
**	Similar to SBLK but a little simpler.
*/
static int
dsav_out(register WFILE *wf,
	 register paddr_t addr,
	 register int len,
	 register w10_t *wp)
{
    register w10_t w;

    LRHSET(w, (-len)&H10MASK, (addr-1) & H10MASK);	/* -<cnt>,,<addr-1> */
    if (wf_put(wf, w) <= 0)
	return 0;
    for (; --len >= 0; ++wp)
	if (wf_put(wf, *wp) <= 0)
	    return 0;

    /* No checksum word in DEC SAV format */
    return 1;
}

/* DEC EXE sharable SAVE format loading code */

/*
DEC sharable SAVE format:
	<directory area>:
		<directory block>
		<entry vector block>
		<optional: PDV (program data vector) block>
		<end block>
	<data area>:
		data pages

Each block has this general format:

		<id code>,,<# words in block, including this word>
		<#-1 remaining words>

    <directory block>:
		1776,,<#>
		<page group descriptors>:
			<access bits (9)> <27-bit page # in file, 0 if none>
			<9-bit repeat cnt><27-bit page # in process>

			Access bits:
				B1 - pages are sharable
				B2 - pages are writable
			Repeat count: # of pages (minus 1) in group.

    <entry vector block>:	; Optional in TOPS-10
		1775,,3
		<# words in entry vector>
		<addr of entry vector>

    <PDV block>:		; optional, basically ignore this
		1774,,<#>
		<#-1 words>

    <end block>:
		1777,,1

Entry vector contents are the same as for SAV (non-sharable) format.

TOPS-10 EXEs appear to leave out the entry vector block; in that case,
the contents are taken from 
		start addr = RH(120)
		reenter addr = RH(124)
		version info in 137
Again, just as for SAV format.

*/

#define DEC_MAXPHYSPGS ((paddr_t)1<<(PAG_PABITS-9))	/* # DEC pages on machine */

static void decld_clrpag(paddr_t);
static int decld_rdpag(struct wfile *, paddr_t, paddr_t, int);


static int
load_decexe(register WFILE *wf,
	    register struct loadinfo *lp)
{
    register paddr_t addr;
    register int32 cnt;
    register int i;
    w10_t wdata;
    register w10_t *wp;
#define DBUFLEN (1+(512*3))	/* Make room for directory area blocks */
    w10_t wbuf[DBUFLEN];	/* For loading block data */

    lp->ldi_evlen = -1;		/* Init entry vector length in case fail */

    wp = wbuf;
    if (wf_get(wf, wp) <= 0) {
	fprintf(stderr, "Loading aborted, first read failed\n");
	return 0;
    }
    if (LHGET(*wp) != DECSSF_DIR) {
	fprintf(stderr, "1st word not directory section: %#lo\n",
			(long)LHGET(*wp));
	return 0;
    }
    if ((cnt = RHGET(*wp)-1) > DBUFLEN) {
	fprintf(stderr, "Directory section too large: %ld words\n",
			(long)cnt+1);
	return 0;
    }
    if (cnt & 01)
	fprintf(stderr, "Warning, dir block has non-pair word count: %ld\n",
			 (long)cnt+1);

    /* Read in directory section */
    if (lp->ldi_debug)
	printf("  DIR section = %#lo wds\n", (long)cnt);
    for (i = cnt; --i >= 0;) {
	if (wf_get(wf, ++wp) <= 0) {
	    fprintf(stderr, "Loading aborted, read failed in dir block\n");
	    return 0;
	}
    }

    /* Gobbled directory block, now get entry vector */
    if (wf_get(wf, &wdata) <= 0) {
	fprintf(stderr, "Loading aborted, read failed after dir block\n");
	return 0;
    }

    if (LHGET(wdata) != DECSSF_EV || RHGET(wdata) != 3) {
	if (lp->ldi_debug)
	    fprintf(stderr, "No entvec block, word: %#lo,,%#lo\n",
			(long) LHGET(wdata), (long) RHGET(wdata));
	lp->ldi_evlen = 0;
	lp->ldi_evloc = 0;
    } else {
	if (wf_get(wf, &wdata) <= 0) {
	    fprintf(stderr, "Loading aborted, read failed in entvec block\n");
	    return 0;
	}
	lp->ldi_evlen = RHGET(wdata);
	if (wf_get(wf, &wdata) <= 0) {
	    fprintf(stderr, "Loading aborted, read failed in entvec block\n");
	    return 0;
	}
	lp->ldi_evloc = ((paddr_t)LHGET(wdata) << 18) | RHGET(wdata);

	if (lp->ldi_debug)
	    printf("  ENTVEC section = %#lo wds at %#lo\n",
		(long)lp->ldi_evlen, (long)lp->ldi_evloc);
    }

    /* At this point we could also scan for the PDV and END blocks, but
    ** why bother?
    */

    /* Now grovel over the directory section, loading in all the pages it
    ** knows about.
    ** Since we're loading physical memory, the access bits are ignored.
    ** If the file page # is 0, the page group has its memory cleared; this may
    ** or may not be what the DEC bootstrap does, but seems useful.
    */
    for (wp = wbuf+1; cnt > 0; cnt -= 2, wp += 2) {
	register paddr_t fpag = RHGET(wp[0]);
	register paddr_t ppag = RHGET(wp[1]);
	i = 1 + ((LHGET(wp[1]) >> 9) & 0777);	/* High 9 bits are rpt cnt */
	if (!fpag) {
	    for (; --i >= 0; ++ppag) {
		if (lp->ldi_debug)
		    printf("  Page %#lo: clear\n", (long)ppag);
		decld_clrpag(ppag);
	    }
	} else {
	    if (lp->ldi_debug)
		printf("  Page %#lo: file page %#lo (n=%d.)\n",
			(long)fpag, (long)ppag, i);
	    decld_rdpag(wf, fpag, ppag, i);
	}
    }

    addr = lp->ldi_evloc;		/* Set up probable start addr */
    if (!addr && ( (lp->ldi_evlen == LH_JRST)	/* Old vector? */
		|| (lp->ldi_evlen == 0))) {	/* or no vector? */
	addr = vm_pgetrh(ldvm_map((paddr_t)0120));	/* Use .JBSA */
    }

    LRHSET(lp->ldi_startwd, 0, addr & H10MASK);
    return 1;
}

static void
decld_clrpag(register paddr_t pag)
{
    register int i = 512;
    register w10_t wz;

    if (pag >= DEC_MAXPHYSPGS) {
	fprintf(stderr, "Loader warning: trying to clear non-ex page %d\n",
			pag);
	    return;
    }
    pag <<= 9;
    op10m_setz(wz);
    for (; --i >= 0; ++pag)
	vm_pset(ldvm_map(pag), wz);
}

static int
decld_rdpag(register struct wfile *wf,
	    paddr_t fpag,
	    paddr_t ppag,
	    int pcnt)
{
    register paddr_t addr;
    register int i;
    w10_t w;

    if (!wf_seek(wf, (long)fpag<<9)) {
	return 0;
    }
    addr = ppag << 9;
    for (; --pcnt >= 0; ++ppag, ++fpag) {
	if (ppag >= DEC_MAXPHYSPGS) {
	    fprintf(stderr, "Loading aborted, trying to load non-ex page %d\n",
			ppag);
	    return 0;
	}
	for (i = 512; --i >= 0; ++addr) {
	    if (wf_get(wf, &w) <= 0) {
		fprintf(stderr, "Loading aborted, read failed for file page %d, proc page %d\n",
			fpag, ppag);
		return 0;
	    }
	    vm_pset(ldvm_map(addr), w);
	}
    }
    return 1;
}
