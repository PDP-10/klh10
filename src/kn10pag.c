/* KN10PAG.C - Main Processor: Pager
*/
/* $Id: kn10pag.c,v 2.4 2002/04/26 05:22:21 klh Exp $
*/
/*  Copyright � 1992, 1993, 2001 Kenneth L. Harrenstien
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
 * $Log: kn10pag.c,v $
 * Revision 2.4  2002/04/26 05:22:21  klh
 * Add missing include of <string.h>
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include "klh10.h"

#include <stdio.h>
#include <string.h>

#include "osdsup.h"
#include "kn10def.h"
#include "kn10ops.h"
#if KLH10_DEV_LITES
#include "dvlites.h"
#endif
#include "klh10exp.h"
#include "kn10cpu.h"

#ifdef RCSID
 RCSID(kn10pag_c,"$Id: kn10pag.c,v 2.4 2002/04/26 05:22:21 klh Exp $")
#endif

/* Exported functions */
#include "kn10pag.h"

/* Local Pre-declarations */
static void acblk_set(unsigned, unsigned);
static void pag_enable(int);
static void pag_clear(void);
static void pag_mapclr(pment_t *);
static void pag_segclr(pment_t *);
static void pag_nxmfail(paddr_t, pment_t, char *);

/* Pager code */
/*
	As implemented here, the pager maintains its own internal map
tables which are used for all virtual memory references (non-virtual
ones go directly to physical memory).

	There are two map tables, one for EXEC mode mapping and the other
for USER mode.  There is one map entry for each possible page in the virtual
address space.

	For the ITS pager this is 256 entries, further separated
into "low" and "high" halves of 128 entries each, so that each half can
be changed independently of the other.

	When a page map is first set up, just the location of the
entries in the PDP-10 memory is noted, and the internal map entries are
cleared.  Any reference which finds an invalid (clear) entry will first
trap to the pager, which attempts to load the corresponding entry from
the PDP-10 memory.  This is called a "page refill".  If found and
valid, the entry is set internally and the reference allowed to
proceed.  If still invalid, a page fault trap happens.

	Whenever the documentation talks about an instruction
"resetting the page table", for the emulator this means that the
relevant internal map entries are cleared.  (There is no cache, thus
"clearing the cache" is a no-op.)

	Currently there is actually a third internal page map, which
merely mirrors physical memory and is used as the mapping table before
paging is turned on.  This table never changes.

*/

/* PAG_INIT - Initialize the paging system.
*/
void
pag_init(void)
{
    register int i;

    /* Initialize externally visible registers */
#if KLH10_CPU_KS
    LRHSET(cpu.mr_ebr, 0,
	(KLH10_PAG_KL ? EBR_T20 : 0));	/* Set flag if using T20 (KL) paging */
#else
    LRHSET(cpu.mr_ebr, 0,
	EBR_CCALK | EBR_CCALD);	/* KL diags expect 0?!? */
#endif

#if KLH10_CPU_KL
    LRHSET(cpu.mr_ubr, UBR_SETACB | UBR_SETPCS | UBR_SET, 0);
#elif KLH10_CPU_KS
    LRHSET(cpu.mr_ubr, UBR_SETACB | UBR_SET, 0); /* Bits 0&2 always set */
#endif

#if KLH10_JPC
    cpu.mr_ujpc = cpu.mr_ejpc = cpu.mr_jpc = 0;
#endif
#if KLH10_PAG_ITS
    cpu.pag.pr_dbr1 = cpu.pag.pr_dbr2 =		/* DBR regs */
	cpu.pag.pr_dbr3 = cpu.pag.pr_dbr4 = 0;
    cpu.pag.pr_quant =				/* Misc stuff */
	cpu.pag.pr_ujpc = cpu.pag.pr_ejpc = 0;
#elif KLH10_PAG_KL
# if KLH10_CPU_KS
    cpu.pag.pr_spb = cpu.pag.pr_csb = 0;	/* Base addresses */
# elif KLH10_CPU_KL
    op10m_setz(PAG_PR_SPB);
    op10m_setz(PAG_PR_CSB);
# endif
    op10m_setz(PAG_PR_CSTMWD);		/* Word values */
    op10m_setz(PAG_PR_PURWD);

    cpu.pag.pr_physnxm = (paddr_t)PAG_MAXPHYSPGS << PAG_BITS;
#endif	/* KLH10_PAG_KL */

#if KLH10_CPU_KL
    cpu.pag.pr_pcs = cpu.pag.pr_pcsf = 0;
    cpu.pag.pr_era = 0;
    cpu.mr_abk_pagno = -1;	/* Init address break page to never match */
#endif

    /* Do AC block initialization. */
    cpu.mr_acbcur = 0;		/* Specify current AC block */
    acblk_set(0, 0);		/* Set up cur & prev blocks */

    /* Set up physical memory "map" */
    for (i = 0; i < PAG_MAXVIRTPGS; ++i)
	pr_pmap[i] = (VMF_READ|VMF_WRITE) | i;
    pag_clear();		/* Clear Exec and User maps */

    pag_enable(0);		/* Ensure paging is off internally */
}

/* ACBLK_SET - Subroutine to set current and previous AC blocks
**	Always keeps the current block ACs in cpu.acs for faster reference.
*/
static void
acblk_set(register unsigned int new,
	  register unsigned int old)
{
    if (new >= ACBLKS_N || old >= ACBLKS_N)
	panic("acblk_set: ac blk # out of range: %d or %d", new, old);
    if (cpu.mr_acbcur != new) {
	/* Put current ACs back in shadow block, then load from new block */
	*(acblk_t *)cpu.acblks[cpu.mr_acbcur] = cpu.acs;
	cpu.acs = *(acblk_t *)cpu.acblks[new];	/* Do structure copy! */
	cpu.mr_acbcur = new;	/* Remember new AC block for next call */
    }
    acmap_set(&cpu.acs.ac[0],	/* Set up new mappings */
		((old == new) ? &cpu.acs.ac[0] : cpu.acblks[old]));
}

/* PAG_ENABLE - Enable or disable paging.
**	Callers must also clear the cache, page map, and KLH10 PC cache if any,
**	by invoking pag_clear() before or after this function.
*/
static void
pag_enable(int onf)
{

    /* Originally it wasn't clear whether the EPT, UPT, and AC block selection
    ** were in effect even when paging is off.
    ** However, this appears to be the case for the KL,
    ** and reportedly the KS as well.
    */
#if 0 /* KLH10_CPU_KS */
    if (onf) {		/* Turning paging/traps on or off? */
#else
    if (1) {		/* ALWAYS set EPT/UPT and AC block selections */
#endif
	/* Set word addr of EPT and UPT so we start using them */
	cpu.mr_ebraddr = (paddr_t)(RHGET(cpu.mr_ebr) & EBR_BASEPAG) << 9;
	cpu.mr_ubraddr =
#if KLH10_PAG_ITS
			((paddr_t)(LHGET(cpu.mr_ubr) & UBR_BASELH) << 18)
			| RHGET(cpu.mr_ubr);
#else	/* DEC */
			pag_pgtopa(RHGET(cpu.mr_ubr) & UBR_BASEPAG);
#endif
	/* Set AC block selections */
	acblk_set((LHGET(cpu.mr_ubr) & UBR_ACBCUR) >> 9,	/* Set cur */
		(LHGET(cpu.mr_ubr) & UBR_ACBPREV) >> 6);	/* and prev */
    }

#if 0 /* KLH10_CPU_KS */
    if (!onf) {		/* Turning paging/traps off? */
	/* Turn off everything that we use internally, without munging
	** the current settings of the externally visible registers.
	*/
	cpu.mr_ubraddr = cpu.mr_ebraddr = 0;	/* No EPT or UPT */
	acblk_set(0, 0);			/* Point to AC block 0 */
    }
#endif /* KS */

    if (onf) {			/* Turning paging/traps on or off? */
	if (cpu.mr_paging)	/* If already paging, can stop now */
	    return;

#if KLH10_PAG_ITS
	/* The Exec DBRs had better already be set! */
	if (!cpu.pag.pr_dbr3 || !cpu.pag.pr_dbr4)
	    panic("pag_enable: Exec DBRs not set!");
#endif

	/* Now set up mapping and context properly */

	/* Turn on paging! */
	cpu.vmap.user = cpu.pr_umap;	/* Make each mode use its own map */
	cpu.vmap.exec = cpu.pr_emap;
	vmap_set(cpu.pr_emap,		/* Set current & previous VM map */
			(PCFTEST(PCF_UIO) ? cpu.pr_umap : cpu.pr_emap));

	cpu.mr_paging = TRUE;

    } else {
	/* Turn off paging!  Do this even if think we're off, for init. */
	cpu.vmap.user = pr_pmap;	/* Make both modes use phys map */
	cpu.vmap.exec = pr_pmap;
	vmap_set(pr_pmap, pr_pmap);	/* Point to phys map */

	cpu.mr_paging = FALSE;
    }
}

/* PAG_CLEAR - Invalidate all pager maps by clearing all internal entries.
**	Perhaps optimize by keeping count of refills?  If count zero,
**	don't need to re-clear the table.
*/
static void
pag_clear(void)
{
    PCCACHE_RESET();		/* Invalidate cached PC info */
    pag_mapclr(cpu.pr_umap);
    pag_mapclr(cpu.pr_emap);
}

static void		/* Ditto but one map only */
pag_mapclr(register pment_t *p)
{
    memset((char *)p, 0, PAG_MAXVIRTPGS*sizeof(*p));
#if KLH10_CPU_KL
    cpu.mr_abk_pmflags = 0;
#endif
}

/* Not actually used for anything */
static void		/* Ditto but only half a map */
pag_segclr(register pment_t *p)
{
    memset((char *)p, 0, (PAG_MAXVIRTPGS/2)*sizeof(*p));
#if KLH10_CPU_KL
    cpu.mr_abk_pmflags = 0;
#endif
}

/* Common IO instructions that manipulate the paging system */

/* IO_RDEBR (70124 = CONI PAG,) - Read Executive Base Register
*/
ioinsdef(io_rdebr)
{
    vm_write(e, cpu.mr_ebr);
    return PCINC_1;
}

#if KLH10_CPU_KL
/* CONSZ PAG, (70130) - KL: Skips if all EBR bits in nonzero E are zero.
**	Not explicitly mentioned, but evidently works.
**	UUO on KS10.
*/
ioinsdef(io_sz_pag)
{
    return (va_insect(e) && !(va_insect(e) & RHGET(cpu.mr_ebr)))
		? PCINC_2 : PCINC_1;
}

/* CONSO PAG, (70134) - KL: Skips if any EBR bits in E are set
**	Not explicitly mentioned, but evidently works.
**	UUO on KS10.
*/
ioinsdef(io_so_pag)
{
    return (va_insect(e) & RHGET(cpu.mr_ebr))
		? PCINC_2 : PCINC_1;
}
#endif /* KL */

/* IO_WREBR (70120 = CONO PAG,)	- Write Executive Base Register
**	Set pointer to EPT, enable paging.
**	On ITS, bit EBR_T20 only affects the way MUUOs are trapped.
**	Resets (invalidates) cache and page table.
**	Note that pag_enable actually effects the EBR alterations.
*/
ioinsdef(io_wrebr)
{
    register h10_t ebits = va_insect(e);

    if (ebits & EBR_ENABLE) {
#if KLH10_PAG_ITS || KLH10_PAG_KI
	if (ebits & EBR_T20)
	    panic("io_wrebr: Cannot support TOPS-20 (KL) paging");
#elif KLH10_PAG_KL
	/* Only barf on this if KS.  KL is now always KL-paging, but
	** apparently ignores state of this bit?!  At least diags seem
	** to expect this behavior.
	*/
# if KLH10_CPU_KS
	if (!(ebits & EBR_T20))
	    panic("io_wrebr: Cannot support TOPS-10 (KI) paging");
# endif
#endif
    } else {	/* Preserve former state of bit */
	if (RHGET(cpu.mr_ebr) & EBR_T20)
	    ebits |= EBR_T20;
	else ebits &= ~EBR_T20;
    }

    /* Always set EBR, but only change paging type bit if enabled */
    RHSET(cpu.mr_ebr,
	(ebits & (EBR_CCALK|EBR_CCALD|EBR_T20|EBR_ENABLE|EBR_BASEPAG)));
    pag_enable((ebits & EBR_ENABLE)	/* Turn paging/traps on/off */
		? TRUE : FALSE);
    pag_clear();			/* Clear page tables */
#if KLH10_DEBUG
    if (cpu.mr_debug) {
	putc('[', stderr);
	pishow(stderr); pcfshow(stderr, cpu.mr_pcflags);
	fprintf(stderr,"%lo: EBR<=%lo]\r\n",
			(long) PC_30, (long) cpu.mr_ebraddr);
    }
#endif

    return PCINC_1;
}

/* IO_RDUBR (70104 = DATAI PAG,) - Read User Base Register
*/
ioinsdef(io_rdubr)
{
#if KLH10_CPU_KL    /* Stuff current PCS into UBR word. */
    op10m_tlz(cpu.mr_ubr, UBR_PCS);
    op10m_tlo(cpu.mr_ubr, pag_pcsget()&UBR_PCS);
#endif
    vm_write(e, cpu.mr_ubr);
    return PCINC_1;
}

/* IO_WRUBR (70114 = DATAO PAG,) - Write User Base Register
**	Resets (invalidates) cache and page table if UPT changes.
** The two bits UBR_SETACB and UBR_SET have already been set in cpu.mr_ubr by
** the initialization code and are never changed thereafter.
*/
ioinsdef(io_wrubr)
{
    register w10_t w;
    w = vm_read(e);		/* Get UBR word */

#if KLH10_DEBUG
    if (cpu.mr_debug) {
	putc('[', stderr);
	pishow(stderr); pcfshow(stderr, cpu.mr_pcflags);
	fprintf(stderr,"%lo: UBR<=", (long) PC_30);
    }
#endif

    /* Select AC blocks? */
    if (LHGET(w) & UBR_SETACB) {

	/* It appears that WRUBR always immediately changes
	** the AC blocks even if not paging, for both KL and KS.
	*/
	acblk_set((LHGET(w) & UBR_ACBCUR) >> 9,	/* Set cur */
		(LHGET(w) & UBR_ACBPREV) >> 6);	/* and prev */
	PCCACHE_RESET();			/* Invalidate cached PC info */

	/* Put new selections into UBR */
	LHSET(cpu.mr_ubr, (LHGET(cpu.mr_ubr) & ~(UBR_ACBCUR|UBR_ACBPREV))
			| (LHGET(w) & (UBR_ACBCUR|UBR_ACBPREV)));
#if KLH10_DEBUG
	if (cpu.mr_debug) {
	    fprintf(stderr,"(%o,%o)", (int) (LHGET(w)&UBR_ACBCUR)>>9,
				(int) (LHGET(w)&UBR_ACBPREV)>>6);
	}
#endif
    }

#if KLH10_CPU_KL
    if (LHGET(w) & UBR_SETPCS) {
	/* Doesn't matter whether paging on or not, just set pager PCS */
	pag_pcsset(LHGET(w) & UBR_PCS);
#if KLH10_DEBUG
	if (cpu.mr_debug) {
	    fprintf(stderr,"(PCS:%o)", (int)(LHGET(w) & UBR_PCS));
	}
#endif
    }
#endif	/* KL */

    /* Select new UPT? */
    if (LHGET(w) & UBR_SET) {

#if KLH10_CPU_KL
	/* Unless explicitly suppressed, setting a new user base address
	** always updates the exec/mem accounts of the previous user.
	** This must be done before mr_ubraddr is clobbered!
	*/
	if (!(RHGET(w) & UBR_DNUA))	/* Unless bit suppresses it, */
	    mtr_update();		/* update previous user accts */
#endif

	RHSET(cpu.mr_ubr, RHGET(w) & UBR_RHMASK);	/* Set UBR RH */

#if KLH10_PAG_ITS
	LHSET(cpu.mr_ubr,		/* ITS - also set UBR LH */
		(LHGET(cpu.mr_ubr) & ~UBR_BASELH) | (LHGET(w) & UBR_BASELH));
	cpu.mr_ubraddr = ((paddr_t)(LHGET(w) & UBR_BASELH) << 18) | RHGET(w);
#else	/* DEC */
	cpu.mr_ubraddr = pag_pgtopa(RHGET(w) & UBR_BASEPAG);
#endif

	/* Also must reset cache and page table.
	** KLH10 also resets its PC cache if any.
	*/
	pag_clear();		/* Or?  pag_mapclr(cpu.pr_umap); */
    }
#if KLH10_DEBUG
    if (cpu.mr_debug) {
	if (LHGET(w) & UBR_SET)
	    fprintf(stderr,"%lo]\r\n", (long) cpu.mr_ubraddr);
	else fprintf(stderr, "--]\r\n");
    }
#endif
    return PCINC_1;
}

/* IO_CLRPT (70110 = BLKO PAG,)	- Clear Page Table Entry (reffed by E)
**	Per DEC documentation (KSREF.MEM 12/78 p.2-9) this clears the
** page entry in BOTH user and exec maps.
**	ITS: Unlike the ITS ucode which only invalidates the first half-page
** (since DEC pages are half the size of ITS pages), this really does
** clear the mapping for the entire ITS page.
**
**	NOTE!!!! CLRPT must mask E to make sure the page number is within
** the supported hardware page map!  It is entirely possible for E to have
** an absurd value, and this DOES HAPPEN on TOPS-20 due to a monitor bug.
** Specifically, two places in DSKALC.MAC that call MONCLR thinking the arg is
** a page # instead of an address, giving it AC1/ 224000,,2  => page 4,,0 !!!
**	This problem is fixed by va_page() which only returns a supported
** virtual page number.
*/
ioinsdef(io_clrpt)
{
    PCCACHE_RESET();			/* Invalidate cached PC info */
    cpu.pr_umap[va_page(e)] = 0;	/* Zapo! */
    cpu.pr_emap[va_page(e)] = 0;
#if KLH10_CPU_KL
    if (va_page(e) == cpu.mr_abk_pagno)
	cpu.mr_abk_pmflags = 0;
#endif
    return PCINC_1;
}

#if KLH10_SYS_ITS

/* IO_CLRCSH (70100 = BLKI PAG,) - Clear Cache
*/
ioinsdef(io_clrcsh)
{
#if 0	/* This isn't actually necessary as the non-existent memory cache
	** has nothing to do with the KLH10 PC cache.  FYI only.
	*/
    PCCACHE_RESET();		/* Invalidate cached PC info */
#endif
    return PCINC_1;
}
#endif /* ITS */

/* Auxiliaries */

/* PFBEG, PFEND - Start and end debug info for page failure trap */
static void
pfbeg(void)
{
    fprintf(stderr, "[PFAIL(%s,%lo,%s,\"%s\") ",
		  (cpu.pag.pr_fmap == cpu.pr_umap ? "U"
		: (cpu.pag.pr_fmap == cpu.pr_emap ? "E" 
		: (cpu.pag.pr_fmap == pr_pmap     ? "P"
		: (cpu.pag.pr_fmap == NULL        ? "IO"
		: "\?\?\?")))), (long) cpu.pag.pr_fref,
			  ((cpu.pag.pr_facf&VMF_WRITE) ? "W"
			: ((cpu.pag.pr_facf&VMF_READ)  ? "R"
			: "0")),
			cpu.pag.pr_fstr ? cpu.pag.pr_fstr : "?" );
    pishow(stderr); pcfshow(stderr, cpu.mr_pcflags);
    fprintf(stderr,"%lo: => ", (long) PC_30);
}

static void
pfend(void)
{
    pishow(stderr); pcfshow(stderr, cpu.mr_pcflags);
    fprintf(stderr,"%lo:]\r\n", (long) PC_30);
}

static uint32
fetch32(register vaddr_t e)
{
    register w10_t w;
    w = vm_read(e);			/* Get word from c(E) */
    return ((uint32)LHGET(w) << 18) | RHGET(w);	/* Convert to 32-bit value */
}

static void
store32(register vaddr_t e,
	register uint32 val)
{
    register w10_t w;
    LRHSET(w, (val>>18)&H10MASK, val & H10MASK);	/* Put val into word */
    vm_write(e, w);					/* Store at c(E) */
}

#if KLH10_PAG_ITS || KLH10_PAG_KI

/* PAG_REFILL - Called when a virtual mem ref failed due to invalid
**	access bits in the page map.  Attempts a refill of the entry.
**    On success, returns a vmptr_t to the physical memory location.
**    On failure, depending on whether VMF_NOTRAP is set in the access flags,
**	Not set - calls pag_fail() to carry out a page fault trap.
**	Set - returns NULL.
*/
vmptr_t
pag_refill(register pment_t *p,	/* Page map pointer */
	   vaddr_t e,		/* Virtual address to map */
	   pment_t f)		/* Access flags */
{
    register pment_t ent;	/* Pager table entry, used by "hardware" */
    register pagno_t pag;
    register h10_t mapent;	/* Map entry set up by software */
    register vaddr_t addr;	/* Address of map entry word */

    pag = va_page(e);		/* Get virtual page number */

    /* Find address of correct page map entry from software table */

#if KLH10_PAG_ITS
    /* Find which DBR to refill from; depends on user/exec context and
    ** whether in low or high memory.
    */
    if (p == cpu.pr_umap) {		/* Assume user map (most common) */
	addr = ((pag & 0200) ? cpu.pag.pr_dbr2 : cpu.pag.pr_dbr1)
		+ ((pag & 0177) >> 1);
    } else if (p == cpu.pr_emap) {	/* Else must be exec map */
	addr = ((pag & 0200) ? cpu.pag.pr_dbr3 : cpu.pag.pr_dbr4)
		+ ((pag & 0177) >> 1);
    }
#elif KLH10_PAG_KI
    if (p == cpu.pr_umap)		/* Assume user map (most common) */
	addr = cpu.mr_ubraddr + UPT_PMU + (pag>>1);
    else if (p == cpu.pr_emap) {	/* Else must be exec map */
	if (pag & 0400)
	    addr = cpu.mr_ebraddr + EPT_PME400 + ((pag&0377)>>1);
	else {
	    if (pag >= 0340)		/* Get special seg from UPT? */
		addr = cpu.mr_ubraddr + UPT_PME340 + ((pag-0340)>>1);
	    else
		addr = cpu.mr_ebraddr + EPT_PME0 + (pag>>1);
	}
    }
#endif
      else if (p == pr_pmap) {
	/* Physical map -- turn reference into a NXM. */
	pag_nxmfail((paddr_t) va_30(e), f, "paging-off refill");
	return vm_physmap(0);		/* In case return, provide a ptr */

    } else {
	panic("pag_refill: unknown map! p=0x%lX, e=%#lo, f=%#lo",
					(long)p, (long)e, (long)f);
    }


    /* Fetch map entry from PDP-10 memory.  This is a halfword table
    ** hence we test low bit of page # to see if entry is in LH or RH.
    */
    mapent = (pag & 01)			/* Get RH or LH depending on low bit */
	? RHPGET(vm_physmap(addr)) : LHPGET(vm_physmap(addr));

    /* Transform software pagemap flags into internal "hardware" flags. */
#if KLH10_PAG_ITS
    switch (mapent & PM_ACC) {		/* Check out ITS access bits */
	case PM_ACCNON:			/* 00 - No access */
	    ent = 0;
	    break;
	case PM_ACCRO:			/* 01 - Read Only */
	case PM_ACCRWF:			/* 10 - Convert R/W/F to RO */
	    ent = (f & VMF_WRITE)	/*	Fail if writing, else win */
			? 0 : VMF_READ;
	    break;
	case PM_ACCRW:			/* 11 - Read/Write */
	    ent = VMF_READ | VMF_WRITE;	/*	Always win */
	    break;
    }
#elif KLH10_PAG_KI
    switch (mapent & (PM_ACC|PM_WRT)) {
	case 0:
	case PM_WRT:			/* No access */
	    ent = 0;
	    break;
	case PM_ACC:			/* Read-Only */
	    ent = (f & VMF_WRITE)	/*	Fail if writing, else win */
			? 0 : VMF_READ;
	    break;
	case PM_ACC|PM_WRT:		/* Read/Write */
	    ent = VMF_READ | VMF_WRITE;	/*	Always win */
	    break;
    }
#endif

    /* Now check access... */
    if (ent) {
#if KLH10_PAG_KI
	/* Should check here for getting a physical page # greater than
	** our available physical memory.  But as long as we always support
	** the max possible (512K for KS, etc) we're safe.
	*/
	if ((mapent & PM_PAG) > PAG_PAMSK) {
	    /* Ugh, fail with NXM */
	    cpu.pag.pr_fref = pag_pgtopa(mapent & PM_PAG) | va_pagoff(e);
	    cpu.pag.pr_fstr = "NXM page";
	    cpu.pag.pr_flh =
		  (p == cpu.pr_umap  ? PF_USER : 0)	/* U */
			| PMF_NXMERR			/* fail type 37 */
			| PF_VIRT;			/* V=1 */

	    cpu.aprf.aprf_set |= APRF_NXM;	/* Report as APR flag too */
	    apr_picheck();	/* Maybe set PI (will happen after pagefail) */

	} else
#endif
	{
	    p[pag] = ent | (mapent & PM_PAG);	/* Won, set hardware table */
	    return vm_physmap(pag_pgtopa(mapent & PM_PAG) | va_pagoff(e));
	}
    } else {

	/* Failed, store info so that pag_fail can trap later if caller
	** decides to invoke it.
	*/
	cpu.pag.pr_fref = va_insect(e);		/* Save virtual addr */
	cpu.pag.pr_fstr = (ent & VMF_READ) ? "W in RO page" : "NoAcc page";
#if KLH10_PAG_ITS
	cpu.pag.pr_flh = (p == cpu.pr_umap ? PF_USR : 0)
		| ((f & VMF_WRITE) ? PF_WRT : 0)
		| ((mapent & PM_29) ? PF_29 : 0)
		| ((mapent & PM_28) ? PF_28 : 0);
#elif KLH10_PAG_KI
# if KLH10_CPU_KS
	cpu.pag.pr_flh = PF_VIRT
		| (p == cpu.pr_umap ? PF_USER : 0)
		| ((f & VMF_WRITE) ? PF_WREF : 0)
		| ((mapent & PM_ACC) ? (PF_ACC
					| ((mapent & PM_SFT) ? PF_SFT : 0))
				     : 0);
# elif KLH10_CPU_KL
	cpu.pag.pr_flh =
		  (p == cpu.pr_umap  ? PF_USER : 0)	/* U */
		| ((mapent & PM_ACC) ? PF_ACC : 0)	/* A */
		| ((mapent & PM_WRT) ? PF_WRT : 0)	/* W */
		| ((mapent & PM_SFT) ? PF_SFT : 0)	/* S */
		| ((f & VMF_WRITE)   ? PF_WREF : 0)	/* T */
		| ((mapent & PM_PUB) ? PF_PUB : 0)	/* P */
		| ((mapent & PM_CSH) ? PF_CSH : 0)	/* C */
		| PF_VIRT;				/* V */
# endif
#endif
    }

    cpu.pag.pr_fmap = p;
    cpu.pag.pr_facf = f;

    if (!(f & VMF_NOTRAP))	/* If caller isn't suppressing traps, */
	pag_fail();		/* take page failure trap now! */
    return NULL;		/* Otherwise just return NULL */
}
#endif /* KLH10_PAG_ITS || KLH10_PAG_KI */


#if KLH10_CPU_KS

/* PAG_IOFAIL - Called from IO code for bad unibus address
*/
void
pag_iofail(paddr_t ioaddr,
	   int bflg)		/* True if byte-mode reference */
{
#if KLH10_PAG_ITS
    cpu.pag.pr_flh = (PF_NXI|PF_PHY|PF_IO
			| (bflg ? PF_BYT : 0)
			| (cpu.vmap.cur == cpu.pr_umap ? PF_USR : 0)
			);
#elif KLH10_PAG_KI || KLH10_PAG_KL
    cpu.pag.pr_flh = (PF_HARD|PF_VIRT|PF_IOREF
			| (bflg ? PF_IOBYTE : 0)
			| (cpu.vmap.cur == cpu.pr_umap ? PF_USER : 0)
			);
#endif
    cpu.pag.pr_fref = ioaddr;
    cpu.pag.pr_fstr = "NX IO addr";
    cpu.pag.pr_fmap = NULL;
    cpu.pag.pr_facf = 0;
    pag_fail();
}
#endif /* KLH10_CPU_KS */


/* PAG_FAIL - Instruction aborted due to page failure (mem ref).
**	Save context, set up new, then longjmp to main loop.
** NOTE: the virtual address given must be correct!  This routine assumes
**	the LH (section or controller) bits are meaningful when building
**	the page fail word.
**
** Note that ITS KS10 pager microcode differs from DEC:
**	- Words are in EPT, not UPT.
**	- Page fail vector saves PC+flags in 1 word (like T10), not 2 like T20.
**	- Instead of one vector, there are 8, one for each PI level plus
**		the non-PI case!
**
;;; In the ITS microcode the three words used to deliver a page fail are
;;; determined from the current interrupt level.  At level I, the page fail
;;; word is stored in EPTPFW+<3*I>, the old PC is stored in EPTPFO+<3*I>,
;;; and the new PC is obtained from EPTPFN+<3*I>.  If no interrupts are in
;;; progress we just use EPTPFW, EPTPFO and EPTPFN.

** DEC T10 and T20 differ mainly in that the old flags+PC are 1 word on T10,
** 2 words on T20.
**
** The Extended KL case is basically identical to KS T20.
**
** Extended KL behavior:
**	PCS is saved in the flag word ONLY if old mode was exec.
**	New flags are all cleared; PCP and PCU are not set.
**	Evidently no new PCS is set.
**
** Also, on the KL, for AR and ARX parity errors the "word with correct
**	parity" is saved in AC0, block 7.  But we never get such errors
**	except for physical NXMs.
** Also, on the KL, for IO Page Failures (ie PF during PI interrupt instr)
**	the PI function word is stored in AC2, block 7.
**	T20 monitor code says "as of KL ucode v257", 24-Feb-82.
*/
void
pag_fail(void)
{
    register w10_t w;
    register paddr_t paddr;

    /* Sanity checks */
    if (!cpu.mr_paging && cpu.mr_exsafe) {
	pfbeg();			/* Do initial barf */
	fprintf(stderr, "Paging OFF, but trapping!]\r\n");
	if (cpu.mr_exsafe >= 2)
	    apr_halt(HALT_EXSAFE);
    }
    if (cpu.mr_injrstf)			/* Internal error */
	panic("pag_fail: page fail in JRSTF!");

    /* First undo whatever we were in the middle of. */

#if KLH10_ITS_1PROC
    if (cpu.mr_in1proc) a1pr_undo();	/* Undo if in one-proceed */
#elif KLH10_CPU_KI || KLH10_CPU_KL
    if (cpu.mr_inafi) afi_undo();	/* Undo if in AFI */
#endif
    if (cpu.mr_intrap) trap_undo();	/* Undo if in trap instruction */
    if (cpu.mr_inpxct) pxct_undo();	/* Undo if PXCT operand */
					/* (This may restore correct PC!) */
    if (cpu.mr_inpi) {
	/* Page failure during interrupt instruction!
	** KS always drops dead.
	** KL does something a little more elaborate.  Since it really
	** ought not to happen, and can be hard to figure out after the fact,
	** some error output is given even though execution continues
	** to emulate the KL.
	** Note that the PI function word has already been stored in AC3 BLK7.
	** The page fail word needs to be put into AC2 BLK7.
	*/
#if KLH10_CPU_KL
	if (cpu.mr_exsafe || cpu.mr_debug) {
	    pfbeg();			/* Barf to show what's going on */
	    fprintf(stderr, "PI IO Page Fail]\r\n");
	    if (cpu.mr_exsafe >= 2)
		apr_halt(HALT_EXSAFE);
	}
	cpu.aprf.aprf_set |= APRF_IOPF;	/* Set IO Page Fail bit */
	apr_picheck();			/* Trigger new PI, we hope */
	LRHSET(w, cpu.pag.pr_flh | (cpu.pag.pr_fref >> H10BITS),
		    cpu.pag.pr_fref & H10MASK);
	cpu.acblks[7][2] = w;		/* Store page fail word here */
	cpu.mr_inpi = FALSE;		/* Say no longer in PI */
	apr_int();			/* Back to main loop */
#else	/* KA+KI+KS */
	pfbeg();
	cpu.mr_inpi = FALSE;		/* Say no longer in PI */
	panic("Illegal Instruction halt - page fail during int instr!");
#endif /* !KL */
    }

#if KLH10_DEBUG
    if (cpu.mr_debug)
	pfbeg();		/* Output page fail debugging info */
#endif

    /* Now put together a page fail word.
    ** Note that the address (pr_fref) is NOT masked here; that's left
    ** up to the refill/map code, which knows how many bits to use.
    */
    LRHSET(w, cpu.pag.pr_flh | (cpu.pag.pr_fref >> H10BITS),
		cpu.pag.pr_fref & H10MASK);

    /* Now have page fail word.  Deliver the fault... */
#if KLH10_PAG_ITS
# define XPT_PFW EPT_PFW	/* ITS uses words in EPT! */
# define XPT_PFO EPT_PFO
# define XPT_PFN EPT_PFN
    /* Find highest PIP and get number for that */
    paddr = cpu.mr_ebraddr + (3 * pilev_nums[cpu.pi.pilev_pip]);

#elif KLH10_PAG_KI || KLH10_PAG_KL
# define XPT_PFW UPT_PFW	/* T10/T20 use words in UPT! */
# define XPT_PFO UPT_PFO
# define XPT_PFN UPT_PFN
    paddr = cpu.mr_ubraddr;

#endif

    vm_pset(vm_physmap(XPT_PFW+paddr), w);	/* Store page fail word */

    /* Now save old context (PC, flags, PCS).
    ** This is *very* processor-specific and the code here
    ** doesn't account for all processor/system variations yet.
    */
#if (KLH10_CPU_KLX || KLH10_CPU_KS) && KLH10_PAG_KL	/* T20 on KS or KLX */
    /* Use 4-word page-fail block with room for big PCs */
# if KLH10_EXTADR
    if (!PCFTEST(PCF_USR))			/* Make saved flag word */
	LRHSET(w, cpu.mr_pcflags, pag_pcsget());	/* save PCS */
    else					/* Unless in user mode */
# endif
	LRHSET(w, cpu.mr_pcflags, 0);
    vm_pset(vm_physmap(UPT_PFF+paddr), w);	/* Store flag word */

# if KLH10_EXTADR
    /* NOTE: Empirical testing on a real 2065 indicates that only 23
    ** bits of PC appear to be saved, not the full 30 bits.
    ** Hence the mask by VAF_VSMSK for now (later PC or PC_SECT may be
    ** pre-cleaned, rendering explicit mask unnecessary).
    */
    LRHSET(w, PC_SECT & VAF_VSMSK, PC_INSECT);	/* Make old PC word */
# else
    LRHSET(w, 0, PC_INSECT);			/* Make old PC word */
# endif
    vm_pset(vm_physmap(XPT_PFO+paddr), w);	/* Store old PC */

    w = vm_pget(vm_physmap(XPT_PFN+paddr));	/* Get new PC word */
# if KLH10_EXTADR
    PC_SET30((((paddr_t)LHGET(w)<<18) | RHGET(w)) & MASK30);
# else
    PC_SET30(RHGET(w));				/* Set new PC */
# endif
    cpu.mr_pcflags = 0;				/* and clear flags */

#else /* KL0/T20, KLX/T10, KS/T10, ITS */

    /* Use 3-word page-fail block with old PC+flags */
    LRHSET(w, cpu.mr_pcflags, PC_INSECT);	/* Make old PC word */
    vm_pset(vm_physmap(XPT_PFO+paddr), w);	/* Store old PC */
    w = vm_pget(vm_physmap(XPT_PFN+paddr));	/* Get new PC word */
    PC_SET30(RHGET(w));				/* Set new PC */
    cpu.mr_pcflags = LHGET(w) & PCF_MASK;	/* and new flags */
#endif

    apr_pcfcheck();			/* Check flags for any changes */

#if KLH10_DEBUG
    if (cpu.mr_debug)
	pfend();			/* Finish up debug info */
#endif

    apr_int();				/* Back to main loop */
}

#if KLH10_PAG_ITS

/* ITS Page register instructions */

/* IO_LDBR1 -  (BLKI .WR.,)
** IO_LDBR2 -  (DATAI .WR.,)
** IO_LDBR3 -  (BLKO .WR.,)
** IO_LDBR4 -  (DATAO .WR.,)
*/
#define setdbr(dbr) dbr = va_insect(e); pag_clear(); return PCINC_1
ioinsdef(io_ldbr1) {	setdbr(cpu.pag.pr_dbr1);	}
ioinsdef(io_ldbr2) {	setdbr(cpu.pag.pr_dbr2);	}
ioinsdef(io_ldbr3) {	setdbr(cpu.pag.pr_dbr3);	}
ioinsdef(io_ldbr4) {	setdbr(cpu.pag.pr_dbr4);	}
#undef setdbr

/* IO_LPMR -  (CONSO .WR.,)
**	Load DBR block from memory into pager registers
*/
ioinsdef(io_lpmr)
{
    register w10_t w;
    register int i;
    vmptr_t p[5];

    /* LPMR isn't that frequent, so resolve the 5 refs with a loop */
    for (i = 0; i < 5; ++i) {
      if (i) va_inc(e);			/* Bump addr, may wrap */
      p[i] = vm_xrwmap(e, VMF_READ);	/* Ensure all locations are valid */
    }

    w = vm_pget(p[0]);		/* Get DBR1 */
    cpu.pag.pr_dbr1 = ((uint32)LHGET(w) << 18) | RHGET(w);
    w = vm_pget(p[1]);		/* Get DBR2 */
    cpu.pag.pr_dbr2 = ((uint32)LHGET(w) << 18) | RHGET(w);
    w = vm_pget(p[2]);		/* New quantum timer */
#if KLH10_CPU_KS
    /* KS quantum field is 044000 (high 32 bits; low 4 ignored) */
    cpu.pag.pr_quant = ((uint32)LHGET(w) << (18-4)) | (RHGET(w)>>4);
#else
# error "io_lpmr() not implemented for non-KS10"
#endif
    if (!cpu.pi.pilev_pip)		/* Start it going if OK */
	cpu.pag.pr_quant = quant_unfreeze(cpu.pag.pr_quant);
    w = vm_pget(p[3]);
    cpu.pag.pr_ujpc = ((uint32)LHGET(w) << 18) | RHGET(w);
    w = vm_pget(p[4]);
    cpu.pag.pr_ejpc = ((uint32)LHGET(w) << 18) | RHGET(w);
#if KLH10_ITS_JPC			/* Update JPC for mode */
    cpu.mr_jpc = (cpu.mr_usrmode ? cpu.pag.pr_ujpc : cpu.pag.pr_ejpc);
#endif

    /* Now must reset cache and page tables!! */
    pag_clear();

    return PCINC_1;
}

/* IO_SDBR1 -  (BLKI .RD.,)
** IO_SDBR2 -  (DATAI .RD.,)
** IO_SDBR3 -  (BLKO .RD.,)
** IO_SDBR4 -  (DATAO .RD.,)
*/
static pcinc_t getdbr(vaddr_t, uint32);

ioinsdef(io_sdbr1){ return getdbr(e, cpu.pag.pr_dbr1); }
ioinsdef(io_sdbr2){ return getdbr(e, cpu.pag.pr_dbr2); }
ioinsdef(io_sdbr3){ return getdbr(e, cpu.pag.pr_dbr3); }
ioinsdef(io_sdbr4){ return getdbr(e, cpu.pag.pr_dbr4); }

static pcinc_t
getdbr(vaddr_t e, uint32 dbr)
{
    register w10_t w;
    LRHSET(w, (dbr>>18)&H10MASK, dbr & H10MASK);
    vm_write(e, w);
    return PCINC_1;
}

/* IO_SPM -  (CONSO .RD.,)
**	Store DBRs etc in memory
*/
ioinsdef(io_spm)
{
    register w10_t w;
    register int32 qcnt;
    vmptr_t p[5];

    /* SPM isn't that frequent, so resolve the 5 refs with a loop */
    for (qcnt = 0; qcnt < 5; ++qcnt) {
      if (qcnt) va_inc(e);	/* Bump addr, may wrap */
      p[qcnt] = vm_wrtmap(e);	/* Ensure all locations are valid */
    }

    LRHSET(w, (cpu.pag.pr_dbr1>>18)&H10MASK, cpu.pag.pr_dbr1 & H10MASK);
    vm_pset(p[0], w);			/* Store DBR1 */
    LRHSET(w, (cpu.pag.pr_dbr2>>18)&H10MASK, cpu.pag.pr_dbr2 & H10MASK);
    vm_pset(p[1], w);			/* Store DBR2 */
    qcnt = (cpu.pi.pilev_pip ? cpu.pag.pr_quant
			     : quant_freeze(cpu.pag.pr_quant));
#if KLH10_CPU_KS
    /* KS quantum field is 044000 (high 32 bits; low 4 ignored) */
    LRHSET(w, (qcnt>>(18-4))&H10MASK, ((qcnt<<4) & H10MASK));
#else
# error "io_spm() not implemented for non-KS10"
#endif
    vm_pset(p[2], w);			/* Store Quantum timer */
#if KLH10_ITS_JPC
    if (cpu.mr_usrmode)			/* Update latest JPC for mode */
	cpu.pag.pr_ujpc = cpu.mr_jpc;
    else cpu.pag.pr_ejpc = cpu.mr_jpc;
#endif
    LRHSET(w, (cpu.pag.pr_ujpc>>18)&H10MASK, cpu.pag.pr_ujpc & H10MASK);
    vm_pset(p[3], w);
    LRHSET(w, (cpu.pag.pr_ejpc>>18)&H10MASK, cpu.pag.pr_ejpc & H10MASK);
    vm_pset(p[4], w);
    return PCINC_1;
}
#endif /* KLH10_PAG_ITS */

#if KLH10_PAG_KL	/* DEC instruction for KL (T20) paging */

/* MAP - Map an Address.
**	This is not an IO-class instruction and the PRM says nothing
**	about its legality in user mode, but it can't hurt, so no
**	user-mode checking is done.
**	HOWEVER, PRM 3-42 says on KL same IO-class restrictions do apply.
** NOTE: this code always does a full refill in order to find all of
**	the entry bits; it does not look in the "hardware" map entry
**	because we only keep a couple of the desired bits there to save
**	space and improve cacheability.
**		Since MAP is not called frequently this is OK speedwise,
**	but this may consitute a difference from the real hardware
**	(suppose something changed the map pointers without invalidating
**	the page table).
** Also note:
**	MAP can be invoked by a PXCT, so needs to use XRW mapping when
**	checking out the refill.
** Question:
**	What happens if NXM interrupts are enabled for APR and a NXM
**	happens as a result of MAP?  Is it ignored, or is the interrupt
**	still triggered although the page fail trap isn't?
**	For the time being, I'll assume YES.  This is probably a non-issue
**	anyway as enabling those ints is discouraged.
*/
insdef(i_map)
{
    register vmptr_t vmp;
    register w10_t w;
    register h10_t pfent;
    register h10_t pflags;
    register paddr_t pa;

#if KLH10_CPU_KL
    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO))
	return i_muuo(op, ac, e);
#endif

    if ((vmp = pag_refill(cpu.vmap.xrw, e, VMF_NOTRAP))) {
	pa = vmp - cpu.physmem;		/* Recover physical addr */
	pfent = cpu.pag.pr_flh;		/* And fetch access bits for page */

	/* Now transform access bits into MAP result bits. */
#if KLH10_MCA25			/* PF_KEEP is a re-use of PF_VIRT */
	pflags = PF_ACC;
	if (pfent & PT_KEEP)	pflags |= PF_KEEP;
#else
	pflags = PF_ACC | PF_VIRT;
#endif
#if KLH10_CPU_KL
	if (pfent & PT_PACC)	pflags |= PF_PUB;
#endif
	if (pfent & PT_WACC)	pflags |= PF_WRT;
	if (pfent & PT_CACHE)	pflags |= PF_CSH;
	if (pfent & CST_MBIT)	pflags |= PF_MOD;  /* Special non-PT bit */
	if (cpu.vmap.xrw == cpu.pr_umap) pflags |= PF_USER;

    } else {
	pflags = cpu.pag.pr_flh;	/* Just get fail bits already set up */
	pa = cpu.pag.pr_fref & MASK22;	/* And recover failing addr */
    }
    LRHSET(w, (pflags | (pa >> H10BITS)), pa & H10MASK);
    ac_set(ac, w);			/* Store result in given AC */
    return PCINC_1;
}
#endif /* KLH10_PAG_KL */

#if KLH10_PAG_KI

/* MAP instruction for KI (T10) paging.
**	KI paging MAP is different enough from KL/T20 that it makes
**	more sense to keep it a separate routine.  In particular, there
**	is no way for a map entry to "fail", even though a pag_refill call
**	could cause a failure.
*/

insdef(i_map)
{
    register h10_t pflags;
    register paddr_t pa;
    register pment_t *p;
    register pagno_t pag;
    register h10_t mapent;	/* Map entry set up by software */
    register vaddr_t addr;	/* Address of map entry word */
    register w10_t w;

#if KLH10_CPU_KL
    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO))
	return i_muuo(op, ac, e);
#endif

    /* Find map entry.  This does a refill lookup instead of checking
    ** the hardwage page table.
    */
    pag = va_page(e);		/* Get virtual page number */
    p = cpu.vmap.xrw;			/* Use XRW map (may be PXCT'd) */
    if (p == cpu.pr_umap)		/* Assume user map (most common) */
	addr = cpu.mr_ubraddr + UPT_PMU + (pag>>1);
    else if (p == cpu.pr_emap) {	/* Else must be exec map */
	if (pag & 0400)
	    addr = cpu.mr_ebraddr + EPT_PME400 + ((pag&0377)>>1);
	else {
	    if (pag >= 0340)		/* Get special seg from UPT? */
		addr = cpu.mr_ubraddr + UPT_PME340 + ((pag-0340)>>1);
	    else
		addr = cpu.mr_ebraddr + EPT_PME0 + (pag>>1);
	}
    } else {
	if (p != pr_pmap)	/* Permit phys map to return 0 */
	    panic("[i_map: unknown map %lo!]", (long)p);
	op10m_setz(w);		/* Return 0 (what else?) */
	ac_set(ac, w);
	return PCINC_1;
    }

    /* Fetch map entry from PDP-10 memory.  This is a halfword table
    ** hence we test low bit of page # to see if entry is in LH or RH.
    */
    mapent = (pag & 01)			/* Get RH or LH depending on low bit */
	? RHPGET(vm_physmap(addr)) : LHPGET(vm_physmap(addr));

    if (mapent & PM_ACC) {		/* Valid mapping? */
	/* Now transform access bits into MAP result bits. */
	pflags =
		  (p == cpu.pr_umap  ? PF_USER : 0)	/* U */
		| ((mapent & PM_ACC) ? PF_ACC : 0)	/* A */
		| ((mapent & PM_WRT) ? PF_WRT : 0)	/* W */
		| ((mapent & PM_SFT) ? PF_SFT : 0)	/* S */
							/* T=0 */
		| ((mapent & PM_PUB) ? PF_PUB : 0)	/* P */
		| ((mapent & PM_CSH) ? PF_CSH : 0)	/* C */
		| PF_VIRT;				/* V=1 */

	/* Always return phys addr, even if bogus and would NXM if tried */
	pa = pag_pgtopa(mapent & PM_PAG) | va_pagoff(e);
	LRHSET(w, (pflags | (pa >> H10BITS)), pa & H10MASK);
    } else
	LRHSET(w, PF_VIRT, 0);	/* Invalid mapping */

    ac_set(ac, w);		/* Store result in given AC */
    return PCINC_1;
}
#endif /* KLH10_PAG_KI */

/* DEC (mainly TOPS-20) Page register instructions */

#if KLH10_PAG_KL && KLH10_CPU_KS

/* WRSPB (70240 = BLKI MTR,) - Write SPT Base Address
**	Sets SPB from c(E).
**	IO-class instruction, assumed legal only in Exec or if User-IOT set.
** Note: one might expect this to invalidate the cache and page table,
** but the PRM makes no mention of this action.  So we don't.
*/
ioinsdef(io_wrspb)
{
    cpu.pag.pr_spb = fetch32(e);
    return PCINC_1;
}

/* RDSPB (70200 = BLKI TIM,) - Read SPT Base Address
**	Reads SPB into c(E).
**	IO-class instruction, assumed legal only in Exec or if User-IOT set.
*/
ioinsdef(io_rdspb)
{
    store32(e, cpu.pag.pr_spb);
    return PCINC_1;
}

/* WRCSB (70244 = DATAI MTR,) - Write CST Base Address
**	Sets CSB from c(E).
**	IO-class instruction, assumed legal only in Exec or if User-IOT set.
** Note: one might expect this to invalidate the cache and page table,
** but the PRM makes no mention of this action.  So we don't.
*/
ioinsdef(io_wrcsb)
{
    cpu.pag.pr_csb = fetch32(e);
    return PCINC_1;
}

/* RDCSB (70204 = DATAI TIM,) - Read CST Base Address
**	Reads CSB into c(E).
**	IO-class instruction, assumed legal only in Exec or if User-IOT set.
*/
ioinsdef(io_rdcsb)
{
    store32(e, cpu.pag.pr_csb);
    return PCINC_1;
}

/* WRCSTM (70254 = DATAO MTR,) - Write CST Mask Register
**	Sets CSTM from c(E).
**	IO-class instruction, assumed legal only in Exec or if User-IOT set.
*/
ioinsdef(io_wrcstm)
{
    cpu.pag.pr_cstm = vm_read(e);
    return PCINC_1;
}

/* RDCSTM (70214 = DATAO TIM,) - Read CST Mask Register
**	Reads CSTM into c(E).
**	IO-class instruction, assumed legal only in Exec or if User-IOT set.
*/
ioinsdef(io_rdcstm)
{
    vm_write(e, cpu.pag.pr_cstm);
    return PCINC_1;
}

/* WRPUR (70250 = BLKO MTR,) - Write Process Use Register
**	Sets PUR from c(E).
**	IO-class instruction, assumed legal only in Exec or if User-IOT set.
*/
ioinsdef(io_wrpur)
{
    cpu.pag.pr_pur = vm_read(e);
    return PCINC_1;
}

/* RDPUR (70210 = BLKO TIM,) - Read Process Use Register
**	Reads PUR into c(E).
**	IO-class instruction, assumed legal only in Exec or if User-IOT set.
*/
ioinsdef(io_rdpur)
{
    vm_write(e, cpu.pag.pr_pur);
    return PCINC_1;
}

#endif /* KL-paging && KS */

#if KLH10_CPU_KL

/* DEC KL10 Cache Instructions
**
**	These are here because they have more to do with paging
**	than anything else.  For the KLH10, they basically do
**	nothing at all, since there is no cache to manage.
*/

/*
**   -   (70140 = BLKI  CCA,) - Sweep Cache, uselessly
** SWPIA (70144 = DATAI CCA,) - Sweep Cache, Invalidate All Pages
** SWPVA (70150 = BLKO  CCA,) - Sweep Cache,   Validate All Pages
** SWPUA (70154 = DATAO CCA,) - Sweep Cache,     Unload All Pages
**   -   (70160 = CONO  CCA,) - Sweep Cache, uselessly
** SWPIO (70164 = CONI  CCA,) - Sweep Cache, Invalidate One Page
** SWPVO (70170 = CONSZ CCA,) - Sweep Cache,   Validate One Page
** SWPUO (70174 = CONSO CCA,) - Sweep Cache,     Unload One Page
*/
ioinsdef(io_swp)	/* Invoked directly by BLKI and CONO */
{
    /* Pretend sweep is done instantly, and hope nothing breaks. */
#if 0	/* Always off, to avoid needing more than 16 bits */
    cpu.aprf.aprf_set &= ~APRR_SWPBSY;
#endif
    cpu.aprf.aprf_set |= APRF_SWPDON;	/* Say sweep done */
    apr_picheck();			/* Trigger int if desired */
    return PCINC_1;
}

ioinsdef(io_swpia) { return io_swp(e); }
ioinsdef(io_swpva) { return io_swp(e); }
ioinsdef(io_swpua) { return io_swp(e); }
ioinsdef(io_swpio) { return io_swp(e); }
ioinsdef(io_swpvo) { return io_swp(e); }
ioinsdef(io_swpuo) { return io_swp(e); }


/* WRFIL (70010 = BLKO APR,) - Write Refill Table
**	KL10 hack for programming the cache.  Fortunately no need to
**	do anything here!
*/
ioinsdef(io_wrfil)
{
    return PCINC_1;
}

#endif	/* KL */

#if KLH10_CPU_KL

/* KL10 Address Break and Memory Maintenance instructions */

/* (70014 = DATAO APR,) - KL: Set Address Break register
**	Sets address break from c(E).
*/
ioinsdef(io_do_apr)
{
    uint32 w;

    /* Remove any prior address break */	

    if (cpu.mr_abk_pagno != -1) {

	/* Set pager access flags back to their true value */
	cpu.mr_abk_pmap[cpu.mr_abk_pagno] |= cpu.mr_abk_pmflags;

	/* Set break page to value that never matches */
	cpu.mr_abk_pagno = -1;
    }

    /* Record info for new address break, split out for fast access */

    w = fetch32(e);
    if (w & ((ABK_IFETCH | ABK_READ | ABK_WRITE) << PAG_VABITS)) {

	/* Break address, page containing it, break conditions */
	cpu.mr_abk_addr = w & MASK23;
	cpu.mr_abk_pagno = va_page(cpu.mr_abk_addr);
	cpu.mr_abk_cond = w >> PAG_VABITS;
	
	/* Make mask to clear page access flags that satisfy break condition */
	cpu.mr_abk_pmmask = -1;
	if (cpu.mr_abk_cond & (ABK_IFETCH | ABK_READ))
	    cpu.mr_abk_pmmask &= ~VMF_READ;
	if (cpu.mr_abk_cond & ABK_WRITE)
	    cpu.mr_abk_pmmask &= ~VMF_WRITE;	    

	/* Save page access flags and clear them so all refs cause a refill */
	cpu.mr_abk_pmap =
	    (cpu.mr_abk_cond & ABK_USER) ? cpu.pr_umap : cpu.pr_emap;
	cpu.mr_abk_pmflags = cpu.mr_abk_pmap[cpu.mr_abk_pagno] & VMF_ACC;
	cpu.mr_abk_pmap[cpu.mr_abk_pagno] &= cpu.mr_abk_pmmask;
    }

    return PCINC_1;
}

/* (70004 = DATAI APR,) - KL: Read Address Break register
**	Reads address break conditions into c(E).
*/
ioinsdef(io_di_apr)
{
    w10_t w;
    W10_XSET (w, cpu.mr_abk_cond << (PAG_VABITS - H10BITS), 0);
    vm_write(e, w);
    return PCINC_1;
}


/* RDERA (70040 = BLKI PI,) - KL: Read Error Address Register
**	Reads ERA into c(E).
**
** To fake out the APR locking flag hackery, simply zap the ERA anytime
** this is called and no locking flag is set.
*/
ioinsdef(io_rdera)
{
    w10_t w;

    if (!(cpu.aprf.aprf_set & (APRF_NXM|APRF_MBPAR|APRF_ADPAR)))
	cpu.pag.pr_era = 0;	/* No locking flags, zap ERA */

    LRHSET(w, (cpu.pag.pr_era >> 18) & 07777, cpu.pag.pr_era & H10MASK);
    vm_write(e, w);
    return PCINC_1;
}


/* SBDIAG (70050 = BLKO PI,) - KL: S Bus Diagnostic Function
**	Send c(E) to S-bus memory controller, read return word into E+1.
**	Controller # in bits 0-4, function code in 31-35.
**
** Apparently function 0 returns -1 for a "non-ex Fbus" controller, 0 for a
**	non-existent controller?
**
** Documentation for the function and return words can be found in
** the PRM, Appendix G, "Handling Memory".
**
** The support here is the minimum necessary to fake out TOPS-20.
** For now it pretends to have MF20 memories with the maximum
** amount of physical memory (22 bits).  Actually it takes advantage of
** the trivial way T20 does its memory init by simply pretending to have a
** single MF20 (controller 010) that responds "here!" to the entire
** physical memory range!
*/

/* SBDIAG function word fields */
#define SBD_FCTLR 0760000	/* LH: Controller # (MF20s are 10-17) */
#define SBD_FFUN      037	/* RH: Function # */
# define SBD_FN_0	0
# define SBD_FN_1	01
# define SBD_FN_2	02
/* Functions 6 and 7 are only used to attempt error correction. */
/* 7 is used by KL diags (SUBKL) to initialize memory. */
# define SBD_FN_7	07
# define SBD_FN_12	012

#define SBD_F0CLR	 010000	/* LH: F0 Clear error flags */
#define SBD_R0ERRS	0770000	/* LH: Error bits */

#define SBD_R1TYP	  01700	/* LH: Controller Type Field */
# define SBD_R1TMA20	  00100	/* LH:   MA20 */
# define SBD_R1TDMA20	  00200	/* LH:   DMA20 */
# define SBD_R1TMB20	  00300	/* LH:   MB20 */
# define SBD_R1TMF20	  00500	/* LH:   MF20 */
#define SBD_R1OFFL	02000	/* RH: Off Line */

#define SBD_F12BLKNO	0176000	/* RH: F12 Block # - high 6 bits of */
				/*	22-bit phys addr (block=64K) */
#define SBD_R12BLKNH	010	/* LH: Block Disable ("Not here") */


ioinsdef(io_sbdiag)
{
    register w10_t w;
    register paddr_t pa;

    w = vm_read(e);		/* Read the function word */
    if ((LHGET(w) >> 13) == 010) {	/* Controller # we support? */

	switch (RHGET(w) & SBD_FFUN) {	/* Get function # */
	case SBD_FN_0:
	    LRHSET(w, 06000, 0);	/* Say nothing wrong */
	    break;

	case SBD_FN_1:
	    LRHSET(w, SBD_R1TMF20, 0);	/* Say MF20 and online */
	    break;

	case SBD_FN_2:
	    op10m_setz(w);		/* Say 64K chips */
	    break;

	case SBD_FN_7:
	    if (!op10m_tlnn(w, 04))	/* If 7-14 enabled, return them */
		LHSET(w, 0);		/* else clear LH */
	    RHSET(w, 0);		/* always clear RH */
	    break;

	case SBD_FN_12:
	    pa = (RHGET(w) & SBD_F12BLKNO) >> 10;	/* Get block # */
	    pa <<= (16 - PAG_BITS);	/* Find phys page # it represents */
	    if (pa < PAG_MAXPHYSPGS)
		op10m_setz(w);		/* Win, say phys mem is there */
	    else
		LRHSET(w, SBD_R12BLKNH, 0);	/* Ugh, say no block */
	    break;

	default:
#if KLH10_DEBUG
	    fprintf(stderr, "[io_sbdiag: Unimpl function: %#lo,,%#lo]\r\n",
			(long)LHGET(w), (long)RHGET(w));
#endif
	    op10m_setz(w);
	    break;
	}
    } else
	op10m_setz(w);

    /* Store result */
    va_inc(e);			/* Bump to E+1 */
    vm_write(e, w);		/* Store result word in E+1 */
    return PCINC_1;
}


#endif /* KL */

/* (70054 = DATAO PI,) Sets console lights from c(E).
 */
ioinsdef(io_do_pi)
{
    register w10_t w;
    w = vm_read(e);		/* get lights data */

#if KLH10_DEV_LITES
    lights_pgmlites(LHGET(w), RHGET(w));
#endif

    return PCINC_1;
}

#if KLH10_PAG_KL

/* DEC TOPS-20 (KL) paging */

/* PAG_REFILL - Called when a virtual mem ref failed due to invalid
**	access bits in the page map.  Attempts a refill of the entry.
**    On success, returns a vmptr_t to the physical memory location.
**    On failure, depending on whether VMF_NOTRAP is set in the access flags,
**	Not set - calls pag_fail() to carry out a page fault trap.
**	Set - returns NULL.
**
** Note that if a MCA25 is defined to be present, what was formerly
**	PF_VIRT becomes PF_KEEP.  This change is not supported at the
**	moment for failing refills; pag_t20map() does not report the
**	access bits it found for failing references.
**	However, there is nothing in the T20 monitor that tests for this.
*/

static vmptr_t pag_t20map(pment_t *, pagno_t, pment_t);

vmptr_t
pag_refill(register pment_t *p,	/* Page map pointer */
	   vaddr_t e,		/* Virtual address to map */
	   pment_t f)		/* Access flags */
{
    register vmptr_t vmp;
    register h10_t pflh;
    register pagno_t vpag;

    vpag = va_xapage(e);

#if KLH10_CPU_KL
    /* If an address break is set, see if this vaddr is in that page */
    if (vpag == cpu.mr_abk_pagno && p == cpu.mr_abk_pmap) {
	/* Page fault if we hit the address and satisfy the conditions */
	if ((e == cpu.mr_abk_addr
	     || ((f & VMF_DWORD) && e + 1 == cpu.mr_abk_addr))
	    && ! cpu.mr_inafi
	    /* Read-modify-write cycles request VMF_WRITE only, so
	    ** assume ABK_READ hits whether or not VMF_READ is on
	    */
	    && (((f & VMF_WRITE) && (cpu.mr_abk_cond & ABK_WRITE))
		|| ((f & VMF_IFETCH) ? (cpu.mr_abk_cond & ABK_IFETCH)
				     : (cpu.mr_abk_cond & ABK_READ)))) {
	    cpu.pag.pr_flh = PMF_ADRERR;	/* report address break */
	    goto pfault;			/* go set VIRT etc */
	}

	/* No break, recalculate access using true pager flags */
	if (f & cpu.mr_abk_pmflags) {
	    /* abk_pagno is in section 0-37, no need to range check vpag */
	    return vm_physmap(pag_pgtopa(p[vpag]&PAG_PAMSK) | va_pagoff(e));
	}
    }
#endif
 
     /* Note page number passed to pag_t20map is the FULL XA page, which on
     ** a KL is 12+9=21 bits, not the supported 5+9=14 virtual.
     */
    if ((vmp = pag_t20map(p, vpag, (f & VMF_ACC)))) /* Try mapping */
	return vmp + va_pagoff(e);		/* Won, return mapped ptr! */

    /* Ugh, analyze error far enough to build page fail word LH bits,
    ** so that pag_fail can be invoked either now or later.
    **
    ** Note that here is where we check for and set the APR condition bits
    ** for (eg) NXM.  If a PI is triggered (not a good idea, according to PRM
    ** p.4-42), it will not take effect until
    ** after the page fail trap has happened and control returns to the
    ** main instruction loop.  
    */
 pfault:
    cpu.pag.pr_fmap = p;
    cpu.pag.pr_facf = f;
    switch (pflh = cpu.pag.pr_flh) {
#if KLH10_CPU_KS
	case PMF_PARERR:	/* Uncorrectable memory error */
	    /* Phys addr set in pr_fref */
	    cpu.aprf.aprf_set |= APRF_BMD;	/* Report as APR flag too */
	    apr_picheck();	/* Maybe set PI (will happen after pagefail) */
	    pflh |= PF_VIRT;
	    break;
	case PMF_NXMERR:	/* Nonexistent memory */
	    /* Phys addr set in pr_fref */
	    cpu.aprf.aprf_set |= APRF_NXM;	/* Report as APR flag too */
	    apr_picheck();	/* Maybe set PI (will happen after pagefail) */
	    pflh |= PF_VIRT;
	    break;

#elif KLH10_CPU_KL
	case PMF_ISCERR:	/* Illegal section # */
	    /* PRM 3-41 says don't set PF_VIRT for this error! */
	    /* NOTE: Address part is undefined by PRM, but real KL seems to
	    ** report only 23 bits of virtual address.
	    */
	    cpu.pag.pr_fref = va_30(e) & MASK23;	/* Remember virt ref */
	    break;

	/* Here we are using PMF_NXMERR to indicate not an ARX parity error
	** but a reference to non-existent physical memory, which sets
	** APR CONI bit 25 as well as bit 27.
	** The ERA (Error Address Register) also needs to be set.  For now
	** just use the failing phys addr ref.
	*/
	case PMF_NXMERR:	/* Nonexistent physical memory */
	    /* Phys addr set in pr_fref, may have very high bits */
	    cpu.pag.pr_era = cpu.pag.pr_fref;
	    cpu.aprf.aprf_set |= APRF_NXM | APRF_MBPAR;
	    apr_picheck();	/* Maybe set PI (will happen after pagefail) */
	    pflh |= PF_VIRT;
	    break;
#endif
	case PMF_OTHERR:	/* Random inaccessibility (soft) */
	    if (f & VMF_WRITE)
		pflh |= PF_WREF;
	    /* Drop thru */
	case PMF_WRTERR:	/* Write violation (soft) */
	default:
#if KLH10_CPU_KS
	    cpu.pag.pr_fref = va_insect(e);	/* Remember virtual addr ref */
#elif KLH10_CPU_KL
	    /* NOTE: real KL seems to report only 23 bits of virtual address.
	    */
	    cpu.pag.pr_fref = va_30(e) & MASK23; /* Remember virtual ref */
#endif
	    pflh |= PF_VIRT;
	    break;
    }
    if (p == cpu.pr_umap)
	pflh |= PF_USER;
    cpu.pag.pr_flh = pflh;	/* Store complete flags back */

    if (!(f & VMF_NOTRAP))	/* If caller isn't suppressing traps, */
	pag_fail();		/* take page failure trap now! */
    return NULL;		/* Otherwise just return NULL */
}


/* PAG_T20MAP - Perform address mapping.
**    On success, returns vmptr_t to start of physical page.
**	The summarized access bits in pr_flh will be used only by MAP.
**    On failure, returns NULL.
**	pr_flh in this case will contain a PMF_xxx value to be used by
**	the refill code to build a page fail word.  MAP ignores it.
**
** WARNING: No checks are made to see whether the CST base address has
** anything reasonable in it.  This is a possible source of problems
** on the KL which can update the SPT or CST pointers anytime it likes,
** without special notification to the pager.
*/
static vmptr_t
pag_t20map(register pment_t *p,	/* Page map pointer */
	   pagno_t vpag,	/* Virtual Page # */
	   pment_t f)		/* Access flags */
{
    register w10_t w;
    register h10_t accbits = 0;
    register paddr_t paddr;
    register pagno_t pag, pgn;

    accbits = PT_WACC | PT_CACHE /* | PT_PACC */ ;	/* Start with these */

    /* Find which map to refill from.
    ** Get appropriate section pointer (always 0 on KS)
    */
    if (p == cpu.pr_umap) {	/* Assume user map (most common) - UBR */
	paddr = cpu.mr_ubraddr + UPT_SC0;
    } else if (p == cpu.pr_emap) {	/* Else must be exec map - EBR */
	paddr = cpu.mr_ebraddr + UPT_SC0;
    } else if (p == pr_pmap) {		/* If phys map, trigger NXM */
	cpu.pag.pr_flh = PMF_NXMERR;	/* NXM during phys ref */
	cpu.pag.pr_fref = pag_pgtopa(vpag);
	cpu.pag.pr_fstr = "NXM with phys map";
	return NULL;			/* Fail - non-ex memory in phys ref */
    } else
	panic("pag_t20map: unknown page table! p=0x%lX, pg=%#lo",
				(long)p, (long)vpag);

#if KLH10_CPU_KL
    if (vpag & ~PAG_NAMSK) {	/* Non-zero section bits? */
	if (vpag >= PAG_MAXVIRTPGS) {
	    cpu.pag.pr_flh = PMF_ISCERR;	/* Illegal Address */
	    /* cpu.pag.pr_fref = 0; */		/* Set in pag_refill */
	    cpu.pag.pr_fstr = "Illeg sect #";
	    return NULL;	    /* Fail - Illegal virt address */
	}
	paddr += (vpag >> (PAG_NABITS-PAG_BITS));	/* Get section # */
	pag = vpag & PAG_NAMSK;			/* Find in-section page # */
    }
      else
#endif
	pag = vpag;			/* Get page # for table lookup */

    for (;;) {
	if (paddr >= cpu.pag.pr_physnxm) {
	    cpu.pag.pr_flh = PMF_NXMERR;
	    cpu.pag.pr_fref = paddr;
	    cpu.pag.pr_fstr = "NXM fetching sect ptr";
	    return NULL;	    /* Fail - NXM fetching section pointer */
	}
	w = vm_pget(vm_physmap(paddr));	/* Get section pointer */

	switch (LHGET(w) >> 15) {	/* Find pointer type */
	default:
	case PTT_NOACC:
	    cpu.pag.pr_flh = PMF_OTHERR;
	    cpu.pag.pr_fstr = "noacc sect ptr";
	    return NULL;		/* Fail - inaccessible pointer */

	case PTT_IMMED:
	    accbits &= LHGET(w);	/* Mask access bits (P,W,C) */
	    break;			/* Won, drop out with ptr to map */

	case PTT_SHARED:
	    accbits &= LHGET(w);		/* Mask access bits (P,W,C) */
	    paddr = PAG_PR_SPBPA + RHGET(w);	/* Find SPT ent addr */
	    if (paddr >= cpu.pag.pr_physnxm) {
		cpu.pag.pr_flh = PMF_NXMERR;
		cpu.pag.pr_fref = paddr;
		cpu.pag.pr_fstr = "NXM for sect SPT ent";
		return NULL;		    /* Fail - NXM fetching SPT entry */
	    }
	    w = vm_pget(vm_physmap(paddr));	/* Get SPT entry */
	    break;				/* Won, have page addr */

	case PTT_INDIRECT:
	    /* DEC PRM p.3-35 and p.4-22 says "Memory status is kept only
	    ** for the page maps." i.e. not for section table pages.  So any
	    ** indirect section pointers generate no CST checks/updates.
	    */
	    accbits &= LHGET(w);		/* Mask access bits (P,W,C) */
	    pgn = LHGET(w) & PT_IIDX;		/* Save idx into next table */
	    paddr = PAG_PR_SPBPA + RHGET(w);	/* Find SPT ent addr */
	    if (paddr >= cpu.pag.pr_physnxm) {
		cpu.pag.pr_flh = PMF_NXMERR;
		cpu.pag.pr_fref = paddr;
		cpu.pag.pr_fstr = "NXM for @ sect SPT ent";
		return NULL;		    /* Fail - NXM fetching SPT entry */
	    }
	    w = vm_pget(vm_physmap(paddr));	/* Get SPT entry */
	    if (LHGET(w) & SPT_MDM) {
		cpu.pag.pr_flh = PMF_OTHERR;
		cpu.pag.pr_fstr = "non-core sect map";
		return NULL;		/* Fail - inaccessible page map */
	    }
	    paddr = pag_pgtopa(RHGET(w) & SPT_PGN) + pgn;
	    continue;			/* Loop to get next section pointer */
	}
	break;		/* Break from switch is break from loop */
    }

    /* Now have the page address of the page map to use!
    ** w contains the final pointer word.
    ** pag is the page # to look up in the page map.
    ** accbits has the access bits so far.
    */
    for (;;) {
	if (LHGET(w) & SPT_MDM) {
	    cpu.pag.pr_flh = PMF_OTHERR;
	    cpu.pag.pr_fstr = "non-core page map";
	    return NULL;		/* Fail - inaccessible page map */
	}
	pgn = RHGET(w) & SPT_PGN;

	/* Check page # here before possibly clobbering CST */
	if (pgn > (PAG_MAXPHYSPGS-1)) {
	    cpu.pag.pr_flh = PMF_NXMERR;
	    cpu.pag.pr_fref = pag_pgtopa(pgn) + pag;
	    cpu.pag.pr_fstr = "map in NX page";
	    return NULL;		/* Fail - map in non-ex page */
	}
	/* Carry out CST update if CST exists. */
	if (PAG_PR_CSBPA) {
	    w = vm_pget(vm_physmap(PAG_PR_CSBPA + pgn));	/* Get CST */
	    if ((LHGET(w) & CST_AGE) == 0) {
		cpu.pag.pr_flh = PMF_OTHERR;
		cpu.pag.pr_fstr = "map age trap";
		return NULL;			/* Fail - age trap */
	    }
	    op10m_and(w, PAG_PR_CSTMWD);		/* AND it with CSTM */
	    op10m_ior(w, PAG_PR_PURWD);			/* IOR with PUR */
	    vm_pset(vm_physmap(PAG_PR_CSBPA+pgn), w);	/* Store back in CST */
	}

	/* OK, now pluck out page map entry */
	paddr = pag_pgtopa(pgn) + pag;
	w = vm_pget(vm_physmap(paddr));		/* Get page map entry */

	/* Handle map pointer */
	switch (LHGET(w) >> 15) {		/* Get map pointer type */
	default:
	case PTT_NOACC:
	    cpu.pag.pr_flh = PMF_OTHERR;
	    cpu.pag.pr_fstr = "noacc page ptr";
	    return NULL;		/* Fail - inaccessible pointer */

	case PTT_IMMED:
	    accbits &= LHGET(w);	/* Mask access bits (P,W,C) */
	    break;			/* Won, drop out with ptr to page */

	case PTT_SHARED:
	    accbits &= LHGET(w);		/* Mask access bits (P,W,C) */
	    paddr = PAG_PR_SPBPA + RHGET(w);	/* Find SPT ent addr */
	    if (paddr >= cpu.pag.pr_physnxm) {
		cpu.pag.pr_flh = PMF_NXMERR;
		cpu.pag.pr_fref = paddr;
		cpu.pag.pr_fstr = "NXM for page SPT ent";
		return NULL;		/* Fail - NXM fetching SPT entry */
	    }
	    w = vm_pget(vm_physmap(paddr));	/* Get SPT entry */
	    break;			/* Won, drop out with page addr */

	case PTT_INDIRECT:
	    /* Note these effect CST update for secondary page maps by looping.
	    */
	    accbits &= LHGET(w);	/* Mask access bits (P,W,C) */
	    pag = LHGET(w) & PT_IIDX;	/* Clobber page # with index! */
	    paddr = PAG_PR_SPBPA + RHGET(w);	/* Find SPT ent addr */
	    if (paddr >= cpu.pag.pr_physnxm) {
		cpu.pag.pr_flh = PMF_NXMERR;
		cpu.pag.pr_fref = paddr;
		cpu.pag.pr_fstr = "NXM for @ page SPT ent";
		return NULL;		/* Fail - NXM fetching SPT entry */
	    }
	    w = vm_pget(vm_physmap(paddr));	/* Get SPT entry */
	    continue;				/* Loop to handle it */
	}
	break;
    }

    /* Now have final page address */
    if (LHGET(w) & SPT_MDM) {	/* Check storage medium field */
	cpu.pag.pr_flh = PMF_OTHERR;
	cpu.pag.pr_fstr = "non-core page";
	return NULL;		/* Fail - inaccessible pointer to page */
    }
    pag = RHGET(w) & SPT_PGN;

    /* Check page # here before possibly clobbering CST */
    if (pag > (PAG_MAXPHYSPGS-1)) {
	cpu.pag.pr_flh = PMF_NXMERR;
	cpu.pag.pr_fref = pag_pgtopa(pag);	/* Don't know offset here */
	cpu.pag.pr_fstr = "NX phy page";
	return NULL;		/* Fail - non-ex page # */
    }

    /* Carry out CST update if CST exists */
    if (PAG_PR_CSBPA) {
	w = vm_pget(vm_physmap(PAG_PR_CSBPA + pag));	/* Get CST */
	if ((LHGET(w) & CST_AGE) == 0) {
	    cpu.pag.pr_flh = PMF_OTHERR;
	    cpu.pag.pr_fstr = "page age trap";
	    return NULL;		/* Fail - age trap */
	}
	op10m_and(w, PAG_PR_CSTMWD);		/* AND it with CSTM */
	op10m_ior(w, PAG_PR_PURWD);		/* IOR with PUR */

	/* Last check - attempted write reference?  If so, may need to
	** also set M bit in CST entry.
	*/
	if (accbits & PT_WACC) {		/* If page is writable */
	    if (f & VMF_WRITE)		/* and this is write ref */
		op10m_iori(w, CST_MBIT);	/* then force the M bit */
	    accbits |= RHGET(w) & CST_MBIT;	/* Turn on M if set in CST */
					/* (note symbol mix with PT_xxx) */
	} else if (f & VMF_WRITE) {	    
	    /* Page not writable, but trying to do a write ref.  Fail. */
	    vm_pset(vm_physmap(PAG_PR_CSBPA+pag), w);
	    cpu.pag.pr_flh = PMF_WRTERR;
	    cpu.pag.pr_fstr = "W in RO Page";
	    return NULL;			/* Fail, page not writable */
	}
	vm_pset(vm_physmap(PAG_PR_CSBPA+pag), w);	/* Store back in CST */

    } else {
	/* CST doesn't exist (this can happen for TOPS-10 using T20 paging).
	** Not clear what to do about reporting the M bit.
	** Currently the hardware page table emulation doesn't retain such
	** a bit, in order to keep the entry size at 16 bits.  So we'll
	** fake it for now by pretending that M is always set if W is;
	** i.e. all writable pages are assumed to be modified.
	** And let's hope nobody pays attention to it.  T20 at least
	** doesn't appear to.
	** Note: error msg is deliberately worded differently from CST
	** case, to maintain uniqueness of all messages.  Helps debug.
	*/
	if (accbits & PT_WACC) {	/* If page is writable */
	    accbits |= CST_MBIT;	/* then always turn on M */
					/* (note symbol mix with PT_xxx) */
	} else if (f & VMF_WRITE) {	    
	    /* Page not writable, but trying to do a write ref.  Fail. */
	    cpu.pag.pr_flh = PMF_WRTERR;
	    cpu.pag.pr_fstr = "W of RO page";
	    return NULL;			/* Fail, page not writable */
	}
    }
    cpu.pag.pr_flh = accbits;		/* Remember bits in case MAP */

    /* Won completely, update internal page map!
    ** VMF_READ serves the function of a 'valid' bit, and
    ** VMF_WRITE serves as the 'M' (modified) bit.  M should be set only
    **	if the page is writable *and* M is set in the CST.
    ** There is no need or support for an internal 'C' (cacheable) bit.
    ** Likewise for any other bits (P, K) at the moment.
    */
    p[vpag] = pag | VMF_READ
		 | ((accbits & CST_MBIT) ? VMF_WRITE : 0);

#if KLH10_CPU_KL
    /* If page vpag contains the address break address, clear its
    ** access flags so that refs will cause a refill.
    */
    if (vpag == cpu.mr_abk_pagno && p == cpu.mr_abk_pmap) {
	cpu.mr_abk_pmflags = p[vpag] & VMF_ACC;
	p[vpag] &= cpu.mr_abk_pmmask;
    }
#endif

    return vm_physmap(pag_pgtopa(pag));
}

#endif /* T20 (KL) paging */

#if KLH10_EXTADR

/* PAG_IISET - Called to set up for taking an Illegal Indirection page fail.
** PAG_IIFAIL - Called from effective address calculation if an
**	illegal indirect word is seen (bits 0,1 == 11).
**	The PRM says nothing about what the page fail word contains for
**	this case; what I'll do is provide E, where c(E) contains the 
**	bad indirect word.  This appears to be correct, according to the
**	DFKED diagnostics.
**	The PRM also says nothing about the handling of this when not
**	paging!
*/
void
pag_iiset(vaddr_t e,
	  pment_t *map)
{
    cpu.pag.pr_flh = (PMF_IIERR | PF_VIRT
			| (map == cpu.pr_umap ? PF_USER : 0)
			);
    cpu.pag.pr_fref = va_30(e);
    cpu.pag.pr_fstr = "Illeg Indir";
    cpu.pag.pr_fmap = map;
    cpu.pag.pr_facf = 0;
}

void
pag_iifail(vaddr_t e,
	   pment_t *map)
{
    pag_iiset(e, map);
    pag_fail();		/* What to do if not paging? */
}

#endif /* KLH10_EXTADR */

/* PMOVE, PMOVEM.  Sort of related to paging, so put here.
*/

#if KLH10_CPU_KLX

/* PMOVE - [op 052] Move from physical memory address (late KL addition)
**	c(E) contains physical address of word to fetch into AC.
*/
insdef(i_pmove)
{
    register w10_t w;
    register paddr_t pa;

    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO))	/* Standard IO-legal test */
	return i_muuo(op, ac, e);

    w = vm_read(e);
    pa = ((paddr_t)LHGET(w)<<18) | RHGET(w);
    if (pa >= ((paddr_t)PAG_MAXPHYSPGS << PAG_BITS)) {
	/* Non-ex phys mem ref, do page-fail */
	pag_nxmfail(pa, VMF_READ, "PMOVE NXM");
	op10m_setz(w);			/* If no trap, read 0 */
	ac_set(ac, w);
    } else
	ac_set(ac, vm_pget(vm_physmap(pa)));	/* Fetch c(PA) into AC */

    return PCINC_1;
}

/* PMOVEM - [op 053] Move to physical memory address (late KL addition)
**	c(E) contains physical address of word to store AC into.
*/
insdef(i_pmovem)
{
    register w10_t w;
    register paddr_t pa;

    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO))	/* Standard IO-legal test */
	return i_muuo(op, ac, e);

    w = vm_read(e);
    pa = ((paddr_t)LHGET(w)<<18) | RHGET(w);
    if (pa >= ((paddr_t)PAG_MAXPHYSPGS << PAG_BITS)) {
	/* Non-ex phys mem ref, do page-fail */
	pag_nxmfail(pa, VMF_WRITE, "PMOVEM NXM");
	/* If no trap, just do nothing */
    } else
	vm_pset(vm_physmap(pa), ac_get(ac));	/* Store AC in c(PA) */

    return PCINC_1;
}

#endif /* KLH10_CPU_KLX */


/* PAG_NXMFAIL - Handle physical memory map NXM error, eg if paging off.
**
** What to do if something like PMOVE/M references non-existent phys memory?
** There's no doc that says anything about this.
** What I'll do here is take a page failure trap of type PMF_NXMERR, which
** is the right thing on the KS and seems close enough on the KL.
** The APR bits set differ, however.
** Based on the MCA25 doc (p.A-8), bit 0 is also set if in user mode.
**	See T20 pag_refill() for similar code.
**	Note: for KL with KI paging, the DFKDA diagnostic expects code 36,
**	"AR Parity", for a NXM error.
*/
static void
pag_nxmfail(paddr_t pa,
	    pment_t f,		/* Access flags */
	    char *str)
{
    cpu.pag.pr_fmap = pr_pmap;		/* Physical map */
    cpu.pag.pr_facf = f;
    cpu.pag.pr_fref = pa;		/* Phys failure addr */
    cpu.pag.pr_fstr = str;		/* Failure string for debugging */

#if KLH10_PAG_ITS			/* ITS/KS: just a guess */
    cpu.pag.pr_flh = PF_NXM | PF_PHY | (PCFTEST(PCF_USR) ? PF_USR : 0);
#else					/* KS: Non-ex memory, KL: AR parity */
    cpu.pag.pr_flh = PMF_NXMERR | (PCFTEST(PCF_USR) ? PF_USER : 0);
#endif

    cpu.aprf.aprf_set |=
#if KLH10_CPU_KS
		APRF_NXM;		/* Report as APR flag too (B27) */
#elif KLH10_CPU_KL
		APRF_NXM | APRF_MBPAR;	/* KL has different flags */
    cpu.pag.pr_era = pa;	/* Also remember phys addr here */
#endif

    apr_picheck();		/* Maybe set PI (will happen after pagefail) */
    if (cpu.mr_paging)		/* If paging, */
	pag_fail();		/* Take page failure trap now! */

    /* Ugh!!  Return to caller, who should carry out some default */
    if (cpu.mr_exsafe) {
	pfbeg();		/* Barf about it */
	fprintf(stderr, "Paging OFF, no trap!]\r\n");
	if (cpu.mr_exsafe >= 2)
	    apr_halt(HALT_EXSAFE);
    }
}
