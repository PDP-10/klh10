/* KN10PAG.H - Pager state and register definitions
*/
/* $Id: kn10pag.h,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: kn10pag.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#ifndef KN10PAG_INCLUDED
#define KN10PAG_INCLUDED 1

#ifdef RCSID
 RCSID(kn10pag_h,"$Id: kn10pag.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* Address space definitions, in terms of bits */

/* Naming note:
**	Use:  For:  
**	  PA	Physical Address.
**	  NA	"iN-section" or "Normal" 18-bit Address.
**	  XA	full eXtended virt Address (30-bit).
**	  VA	Virtual Address actually supported (18 or 23-bit).
**
**	  XS	full eXtended Section # (12-bit)
**	  VS	Virtual Section # actually supported (5-bit)
*/

#ifndef PAG_PABITS	/* # bits of address physically available */
# if KLH10_CPU_KLX
#  define PAG_PABITS 22	/* 22 MAX!  Can set less for debugging */
# else
#  define PAG_PABITS 19	/* True for KS10 anyway */
# endif
#endif

#define PAG_NABITS 18	/* Normal in-section virtual address bits */
#define PAG_XSBITS 12	/* # bits virtual section possible */
#define PAG_XABITS 30	/* # virtual bits possible in extended mode (18+12) */

#if KLH10_CPU_KLX
# define PAG_VSBITS  5		/* # bits virtual section actually supported */
# define PAG_MAXBITS PAG_XABITS	/* Max address of any kind */
#else
# define PAG_VSBITS  0		/* # bits virtual section actually supported */
# define PAG_MAXBITS PAG_PABITS	/* Max address of any kind */
#endif

	/* # virtual address bits actually supported! (18 or 23) */
#define PAG_VABITS  (PAG_VSBITS+PAG_NABITS)


#if KLH10_PAG_ITS
# define PAG_BITS 10	/* # bits of address encompassed by a page */
#else
# define PAG_BITS 9
#endif

#define PAG_VMFBITS 2	/* # of access bits needed in page table */
			/* (See VMF_ flags) */

/* The remaining definitions are more or less all derived
** from the above parameters.
**	Note some of the defs should be unsigned but aren't, as they are
**	used in some #if tests and some preprocs don't like 1UL.
**	Still guaranteed positive since long is >= 32 bits.
*/

/* Max # of pages the KLH10 emulates in physical memory.
*/
#ifndef PAG_MAXPHYSPGS
# define PAG_MAXPHYSPGS (1L<<(PAG_PABITS-PAG_BITS))
#endif

/* Max # of virtual pages this pager will support.
*/
#define PAG_MAXVIRTPGS (1L<<(PAG_VABITS-PAG_BITS))

/* # bits needed in a page table entry.
**	Only have to support available phys mem.
*/
#define PAG_PMEBITS (PAG_VMFBITS+PAG_PABITS-PAG_BITS)


/* Now declare three types:
**	paddr_t - fast type big enough to hold the biggest phys/virt address.
**	pagno_t - fast type big enough to hold a physical page #.
**	pment_t - compact type for holding a hardware page table entry.
**
** For explanation of the #if tests, see similar code in word10.h that
** defines WORD10_INT18.
*/
#ifndef INT_MAX
# include <limits.h>
#endif

/* paddr_t - Physical address (always integral type)
**	This is a macro instead of the typedef it should be, because
**	on some Nazi U-boxes it is cleverly typedefed in <sys/types.h>, and
**	C has no way to undefine (or even test) for such things.
*/
#ifndef KLH10_PADDR_T		/* Permit compile-time override */
# if (INT_MAX > (1L<<(PAG_MAXBITS-2)))
#  define KLH10_PADDR_T int	/* Assume int is fastest if it's big enough */
# else
#  define KLH10_PADDR_T long
# endif
#endif
/*	typedef unsigned KLH10_PADDR_T paddr_t; */
#define	paddr_t unsigned KLH10_PADDR_T

#ifndef KLH10_PAGNO_T		/* Permit compile-time override */
# if (INT_MAX > (PAG_MAXPHYSPGS>>2))
#  define KLH10_PAGNO_T int
# else
#  define KLH10_PAGNO_T long
# endif
#endif
	typedef unsigned KLH10_PAGNO_T pagno_t;

#ifndef KLH10_PMENT_T		/* Permit compile-time override */
# if (SHRT_MAX > (1L<<(PAG_PMEBITS-2)))
#  define KLH10_PMENT_T short
# elif (INT_MAX > (1L<<(PAG_PMEBITS-2)))
#  define KLH10_PMENT_T int
# else
#  define KLH10_PMENT_T long
# endif
#endif
	typedef unsigned KLH10_PMENT_T pment_t;


#define PAG_SIZE (1<<PAG_BITS)	/* # words in a page */
#define PAG_MASK (PAG_SIZE-1)	/* Mask for address bits within page */

/* Note: These masks are for right-justified page numbers! */
#define PAG_NAMSK ((1UL<<(PAG_NABITS-PAG_BITS))-1) /* Mask for # in-sect pgs */
#define PAG_PAMSK ((1UL<<(PAG_PABITS-PAG_BITS))-1) /* Mask for # phys pgs */
#define PAG_XAMSK ((1UL<<(PAG_XABITS-PAG_BITS))-1) /* for XA max # virt */
#define PAG_VAMSK ((1UL<<(PAG_VABITS-PAG_BITS))-1) /* for supported # virt */

#define PAG_NXSMSK (PAG_XAMSK & ~PAG_VAMSK)	/* Non-ex sect bits in pg # */

/* Extract page # from, and turn page # into, a physical address. */
#define pag_patopg(a) ((pagno_t)((a)>>PAG_BITS)&PAG_PAMSK) /* Phy to page */
#define pag_pgtopa(p) (((paddr_t)(p)) << PAG_BITS)	   /* Page to phy */

/* To extract page # from given virtual address,
**	see va_page(va) and va_xapage(va).
*/

/* Virtual Address Facilities (includes EA calculation)
**
**	vaddr_t is the object type used to store a full PDP-10
**		virtual address.  This is NOT the same as a physical
**		address and may not even be an integral type.
**
**	Both extended and non-extended definitions are integrated here.
**	There are complicated enough however that perhaps a separate file
**	would be a good idea (vaddr.h?).
*/

    typedef int32 vaddr_t;		/* Later make unsigned */
    typedef int32 vaint_t;		/* For future code */
    typedef uint32 vauint_t;

#if !KLH10_EXTADR

# define va_lh(va)   ((va) >> H10BITS)
# define va_ac(va)   ((va) & AC_MASK)
# define va_sect(va)  (0)
# define va_sectf(va) (0)
# define va_insect(va) ((va) & H10MASK)
# define va_pagoff(va) ((va) & PAG_MASK)	/* Get offset within page */
# define va_page(va)   ((pagno_t)((va)>>PAG_BITS)&PAG_VAMSK)  /* Get page # */
# define va_xapage(va) ((pagno_t)((va)>>PAG_BITS)&PAG_XAMSK)  /* FULL page!! */
# define va_30(va) (va)

# define va_ladd(va,n) ((va) = ((va)+(n))&H10MASK)
# define va_linc(va) va_ladd(va,1)	/* Local increment by 1 */
# define va_add(va,n) va_ladd(va,n)	/* General same as local */
# define va_inc(va) va_linc(va)		/* Ditto */
# define va_dec(va) va_ladd(va,-1)

# define va_lmake(va, s, n) ((va) = (n))
# define va_gmake(va, s, n) ((va) = (n))
# define va_lmake30(va, pa) ((va) = (pa))
# define va_hmake(va, lh, rh) ((va) = ((lh)<<H10BITS) | (rh))
# define va_setlocal(va) (0)		/* No-op */
# define va_isodd(va) ((va) & 01)	/* Address is odd (low bit set) */
# define va_isglobal(va) (0)		/* Never true */

# define va_Vmake(f,s,n) (n)

# define va_lfrword(va, w) va_lmake(va, 0, RHGET(w))
# define va_gfrword(va, w) va_gmake(va, 0, RHGET(w))

#endif /* !KLH10_EXTADR */

#if 1	/* Always include, even if EXTADR. */

/* Effective Address calculation.
**	The ea_calc() macro should be used everyplace an effective address
**		must be computed from a word in the I(X)Y format.
**	The one exception is byte pointers, which use ea_bpcalc() in order
**		to use the correct context for the ac/mem mapping.
**
** WARNING: This macro is an exception to the general rule of returning
** correctly masked results.  E will have garbage high bits for a (X)Y
** reference.  The masking is currently left out to bum a little more speed.
**
** TEMPORARY WARNING: For XA the (X)Y stuff *must* be masked!
*/

#define ea_calc(i)   ea_mxcalc(i, cpu.acblk.xea,  cpu.vmap.xea)  /* Normal E */
#define ea_bpcalc(i) ea_mxcalc(i, cpu.acblk.xbea, cpu.vmap.xbea) /* BytPtr E */

#if 0
# define ea_mxcalc(iw, acp, m) \
  (op10m_tlnn(iw, IW_I|IW_X) \
     ? (op10m_tlnn(iw, IW_I) ? RHGET(ea_wcalc(iw,acp,m)) /* I set, use fn */\
	: (RHGET(iw) + ac_xgetrh(LHGET(iw)&IW_X,acp))) /* X only, add in */\
     : RHGET(iw))				/* Simple case, no I or X */
#else

# define ea_mxcalc(iw, acp, m) \
  (op10m_tlnn(iw, IW_I|IW_X) \
     ? (op10m_tlnn(iw, IW_I) ? ea_fncalc(iw,acp,m) /* I set, use fn */\
	: va_Vmake(VAF_LOCAL, 0, \
		(RHGET(iw) + ac_xgetrh(LHGET(iw)&IW_X,acp)) & H10MASK)) \
     : va_Vmake(VAF_LOCAL, 0, RHGET(iw)))	/* Simple case, no I or X */
extern vaddr_t ea_fncalc(w10_t, acptr_t, pment_t *);
#endif
	/* Do full EA calc, returning last word */
extern w10_t ea_wcalc(w10_t, acptr_t, pment_t *);

#endif /* Was !KLH10_EXTADR, now 1 */

#if !KLH10_EXTADR
	/* Provide for code compatibility when not using extended
	** addressing.  Later invent some generic name for these
	** either-config facilities.
	*/
# define xea_calc(iw,s)      ea_calc(iw)
# define va_xeacalc(va,iw,s) ((va) = ea_calc(iw))
#endif

#if KLH10_EXTADR

#define VAF_LGFLAG (~((~(vauint_t)0)>>1))
#define VAF_GLOBAL VAF_LGFLAG
#define VAF_LOCAL  0

#define VAF_NBITS PAG_NABITS	/* 18 bits in-section */
#define VAF_SBITS PAG_XSBITS	/* 12 bits section # */
				/* (Even though pager may only support 5) */
#define VAF_VSBITS PAG_VSBITS	/* # bits virtual section possible */
#define VAF_SPOS VAF_NBITS

#define VAF_SMSK  (((vauint_t)1<<VAF_SBITS)-1)
#define VAF_VSMSK (((vauint_t)1<<VAF_VSBITS)-1)
#define VAF_NMSK H10MASK
#define VAF_SFLD  (VAF_SMSK<<VAF_SPOS)
#define VAF_VSFLD (VAF_VSMSK<<VAF_SPOS)
#define VAF_NFLD VAF_NMSK
#define VAF_AFLD 017
#define VAF_30MSK MASK30

#define VAF_S1 ((vauint_t)1<<VAF_SPOS)		/* S Low bit in S field */
#define VAF_NNOTA (VAF_NFLD&~VAF_AFLD)		/* Non-AC bits in N field */
#define VAF_SBAD (VAF_SFLD & ~VAF_VSFLD)	/* Bad sect bits in S field */

/* Stuff with counterparts in !EXTADR */

# define va_lh(va) (((va) >> H10BITS)&H10MASK)
# define va_ac(va) ((va) & AC_MASK)
# define va_sect(va) (((va)>>VAF_SPOS)&VAF_SMSK)
# define va_sectf(va) ((va) & VAF_SFLD)
# define va_insect(va) ((va) & H10MASK)
# define va_pagoff(va) ((va) & PAG_MASK)	/* Get offset within page */
# define va_page(va)   ((pagno_t)((va)>>PAG_BITS)&PAG_VAMSK)  /* Get page # */
# define va_xapage(va) ((pagno_t)((va)>>PAG_BITS)&PAG_XAMSK)  /* FULL page!! */
# define va_30(va) ((va) & VAF_30MSK)

# define va_ladd(va,n) ((va) = (((va)&~VAF_NFLD)|(((va)+(n))&VAF_NFLD)))
# define va_linc(va) va_ladd(va,1)	/* Local increment by 1 */
# define va_add(va,n) (va_islocal(va) ? va_ladd(va,n)  : va_gadd(va,n))
# define va_inc(va)   (va_islocal(va) ? va_linc(va)    : va_ginc(va))
# define va_dec(va)   (va_islocal(va) ? va_ladd(va,-1) : va_gadd(va,-1))

# define va_lmake(va, s, n) ((va) =              ((s)<<VAF_SPOS) | (n))
# define va_gmake(va, s, n) ((va) = VAF_GLOBAL | ((s)<<VAF_SPOS) | (n))
# define va_lmake30(va, pa) ((va) = (pa))
# define va_hmake(va, lh, rh) ((va) = ((lh)<<H10BITS) | (rh))
# define va_setlocal(va) ((va) &= ~VAF_GLOBAL)
# define va_isodd(va) ((va) & 01)	/* Address is odd (low bit set) */
# define va_isglobal(va) ((va) & VAF_GLOBAL)


/* New stuff with no counterpart in !EXTADR */

# define va_gadd(va,n) ((va) = ((va)+(n)) | VAF_GLOBAL)
# define va_ginc(va) va_gadd(va,1)
# define va_gdec(va) va_gadd(va,-1)
# define va_islocal(va) (!va_isglobal(va))
# define va_isext(va) ((va)&VAF_SFLD)

# define va_setinsect(va, h) ((va) = ((va)&~VAF_NFLD) | (h))
# define va_toword(va, w) LRHSET(w, va_sect(va), va_insect(va))
# define va_gmake30(va, pa) ((va) = VAF_GLOBAL | (pa))

# define va_Vmake(f,s,n) ((f)|((s)<<VAF_SPOS)|(n))
# define va_30frword(w) ((((uint32)(LHGET(w)&MASK12))<<H10BITS) | RHGET(w))

#define va_lfrword(va, w) va_lmake(va, LHGET(w)&VAF_SMSK, RHGET(w))
#define va_gfrword(va, w) va_gmake(va, LHGET(w)&VAF_SMSK, RHGET(w))

/* True if in-section part is an AC # */
#define va_isacinsect(va) (((va)&VAF_NNOTA)==0)	/* TRUE if N part is AC */

/* True if is an AC ref that must be converted to 1,,AC
**	Note global ref of 0,,AC does refer to ACs, but is *NOT* converted
** to 1,,AC.  See i_xmovei() for more comments.
*/
#define va_iscvtacref(va) (va_isacinsect(va) && va_islocal(va) && va_isext(va))

/* True if section # is 0 or 1 */
#define va_issect01(va) (((va)&(VAF_SFLD&~VAF_S1))==0)

/* True if is an AC ref */
#define va_isacref(va) (va_isacinsect(va) \
			&& (va_islocal(va) || va_issect01(va)))

/* True if no unsupported section bits are set (high 7 of 12) */
#define va_isoksect(va) (((va) & (VAF_SFLD&~VAF_VSFLD))==0)

/* Store VA in word, canonicalizing AC ref to 1,,AC if necessary */
#define va_toword_acref(va, w) (va_iscvtacref(va) \
	? (LRHSET(w, 1, va_insect(va)), 0)	\
	: (LRHSET(w, va_sect(va), va_insect(va)), 0) )
	

#endif /* KLH10_EXTADR */



#if KLH10_EXTADR

#define xea_calc(iw,s) xea_mxcalc(iw,s,cpu.acblk.xea, cpu.vmap.xea)

#define va_xeacalc(va,iw,s) \
	((va) = xea_mxcalc(iw, s, cpu.acblk.xea, cpu.vmap.xea))
#define va_xeabpcalc(va,iw,s) \
	((va) = xea_mxcalc(iw, s, cpu.acblk.xbea, cpu.vmap.xbea))

#define xea_mxcalc(iw, s, acp, m) \
  (op10m_tlnn(iw, IW_I|IW_X) \
     ? ((op10m_tlnn(iw,IW_I) || (s)) \
	? xea_xcalc(iw, (unsigned)s, acp, m)	/* I, or X in NZ S, use fn */\
	: va_Vmake(VAF_LOCAL, s, H10MASK &	/* Has X only, add in */\
			(RHGET(iw) + ac_xgetrh(iw_x(iw),acp))))		\
     : va_Vmake(VAF_LOCAL, s, RHGET(iw)))	/* Simple case, no I or X */

/* Extended EA calc if starting with EFIW */
#define va_xeaefcalc(va, iw, acp, m) \
   (op10m_tlnn(iw, IW_EI) 		/* See if Indirect bit set */\
    ? ((va) = xea_fnefcalc(iw,acp,m))	/* Ugh, use function to handle @ */\
    : (op10m_tlnn(iw, IW_EX)		/* Do simple X inline */\
	? va_gmake30(va, (va_30frword(iw)	\
			+ va_30frword(ac_xget(iw_ex(iw),acp))) & VAF_30MSK) \
	: va_gmake30(va, va_30frword(iw))	/* Simple Y is all */\
    ))

	/* Normal full EA calculation, extended */
extern vaddr_t xea_xcalc(w10_t, unsigned, acptr_t, pment_t *);
	/* Special EA calc for PXCT */
extern vaddr_t xea_pxctcalc(w10_t, unsigned, acptr_t, pment_t *, int, int);
	/* Special for byte ptr EFIWS */
extern vaddr_t xea_fnefcalc(w10_t, acptr_t, pment_t *);

#endif /* KLH10_EXTADR */

/* Virtual Memory Mapping definitions
**
**	In theory, the format used to store PDP-10
** word data in the "physical memory" area doesn't necessarily have to
** be equivalent to the w10_t data type.  Two distinct sets of macros
** are provided to manipulate register-words and phys-memory-words for
** this reason: ac_xxx() and vm_xxx().
**	However, making them equivalent simplifies the job of vm_xmap(),
** which is responsible for taking a vaddr_t virtual address and returning a
** vmptr_t that can be used to manipulate a physical memory word, because
** it can then return the same kind of pointer if the reference is actually to
** a fast-memory AC rather than to physical memory.
**	If they were different data types then either vmptr_t would
** have to somehow distinguish between pointing to an AC and pointing
** to a memory word, or pointers would have to be avoided in favor
** of word fetch/store macros.  Ugh.
*/

/* Note that the following definitions are parallel to those for the ACs. */

typedef w10_t *vmptr_t;		/* Set type of data in phys mem */

struct vmregs {
	pment_t	*cur,	/* Current context page map */
		*prev,	/* Previous context map */
	/* Following maps always set to one of above two (current or prev) */
		*xea,	/* PXCT bit 9 - Eff Addr calc */
		*xrw,	/* PXCT bit 10 - c(E) read/write */
		*xbea,	/* PXCT bit 11 - Byte Ptr EA calc, others */
		*xbrw,	/* PXCT bit 12 - Byte data, others */
	/* Following maps are for picking appropriate mode map without
	** having to test whether paging is on or off.
	*/
		*user,	/* cpu.pr_umap if paging, else pr_pmap */
		*exec;	/* cpu.pr_emap if paging, else pr_pmap */
};

/* Macro to simplify changing of map pointers */
#define vmap_set(new, old) \
	((cpu.vmap.xea = cpu.vmap.xrw = \
		cpu.vmap.xbea = cpu.vmap.xbrw = cpu.vmap.cur = (new)), \
	 (cpu.vmap.prev = (old)))


/* Access flags used in the vm_xmap() macro MUST be only ONE of VMF_READ
** or VMF_WRITE.  Combining them doesn't work; VMF_WRITE implies
** both read and write.
** The hairy definitions are intended to result in the smallest possible
** bit usage for the "hardware" page table entries, both to save storage
** (cache) space and to permit immediate-mode operations for platforms
** that benefit from them (eg the SPARC has 13-bit immediate operands).
*/
#define VMF_READ  ((pment_t)1<<(PAG_PMEBITS-1))	/* Read access ("Valid") */
#define VMF_WRITE ((pment_t)1<<(PAG_PMEBITS-2))	/* Write access */
#define VMF_ACC (VMF_READ|VMF_WRITE)	/* Both, for masking */
#if 0	/* Cannot do this check at compile time due to typedefs in exprs */
# if (VMF_ACC & PAG_PAMSK)
	/*#*/error "Pager access bits overlap phys page number!!"
# endif
#endif

#define VMF_NOTRAP ((pment_t)1<<(PAG_PMEBITS-3)) /* Flag for pag_refill() */
					/* NOT used in map entry! */

/* Synonym for VMF_READ to distinguish instruction fetch references in case
** that ever becomes important.  For example, it might provide a way
** of indicating that the address (ie PC) is forcibly interpreted as
** local.
*/
#define VMF_FETCH (VMF_READ)


/* VM_XMAP - Map virtual address in given context to physical address.
**	Page faults if requested access cannot be granted, unless
**	the VMF_NOTRAP flag is supplied.
**	Returns a vmptr_t pointer to physical memory containing word value.
**	This may be a "fast memory AC".
** vm_xtrymap - similar but never invokes the page refill routine; it
**	only checks out the "hardware" map.  This is useful for examining
**	things from the console without altering anything.
*/
#if !KLH10_EXTADR
#define vm_xmap(v,f,a,m)	/* Vaddr, AccessFlags, ACblock, MapPointer */\
    (((v) & (H10MASK&(~AC_MASK)))==0		\
	? ac_xmap((v)&AC_MASK, (a))		/* AC ref, use AC block */\
	: ( ((m)[va_page(v)] & ((f)&VMF_ACC)) /* Mem ref, check map */\
	     ? vm_physmap(pag_pgtopa((m)[va_page(v)]&PAG_PAMSK) \
						| ((v) & PAG_MASK)) \
	     : pag_refill((pment_t *)(m),(vaddr_t)(v),(pment_t)(f)) \
	))

#define vm_xtrymap(v,f,a,m)	/* Vaddr, AccessFlags, ACblock, MapPointer */\
    (((v) & (H10MASK&(~AC_MASK)))==0		\
	? ac_xmap((v)&AC_MASK, (a))		/* AC ref, use AC block */\
	: ( ((m)[va_page(v)] & ((f)&VMF_ACC)) /* Mem ref, check map */\
	     ? vm_physmap(pag_pgtopa((m)[va_page(v)]&PAG_PAMSK) \
						| ((v) & PAG_MASK)) \
	     : (vmptr_t)0	\
	))
#endif /* !KLH10_EXTADR */

#if KLH10_EXTADR	/* Hairy extended-addressing mapping! */

#define vm_xmap(v,f,a,m)	/* Vaddr, AccessFlags, ACblock, MapPointer */\
    (va_isacref(v)				\
	? ac_xmap(va_ac(v), (a))		/* AC ref, use AC block */\
	: ((va_isoksect(v)			/* Verify section OK */\
	   && ((m)[va_page(v)] & ((f)&VMF_ACC))) /* Mem ref, check map */\
	     ? vm_physmap(pag_pgtopa((m)[va_page(v)]&PAG_PAMSK) \
						| va_pagoff(v)) \
	     : pag_refill((pment_t *)(m),(vaddr_t)(v),(pment_t)(f)) \
	))

#define vm_xtrymap(v,f,a,m)	/* Vaddr, AccessFlags, ACblock, MapPointer */\
    (va_isacref(v)				\
	? ac_xmap(va_ac(v), (a))		/* AC ref, use AC block */\
	: ((va_isoksect(v)			/* Verify section OK */\
	   && ((m)[va_page(v)] & ((f)&VMF_ACC))) /* Mem ref, check map */\
	     ? vm_physmap(pag_pgtopa((m)[va_page(v)]&PAG_PAMSK) \
						| va_pagoff(v)) \
	     : (vmptr_t) 0	\
	))

/* Special map for doing fetch of c(PC).
**	Uses PC_ macros to obtain address parts, and knows that it is always
**		local-format, which simplifies test for AC reference.
**	However, AC-ref test must also test for valid section #.
**	Note use of PC_VADDR; will need to encapsulate call to pag_refill
**		if a non-vaddr format is used for PC.
*/
#define vm_PCmap(f, a, m) \
    (PC_ISACREF				\
	? ac_xmap(PC_AC, (a))		/* AC ref, use AC block */\
	: ( ((PC_PAGE & PAG_NXSMSK)==0 /* Verify section OK */\
	   && ((m)[PC_PAGE] & ((f)&VMF_ACC)))	/* Mem ref, check map */\
	     ? vm_physmap(pag_pgtopa((m)[PC_PAGE]&PAG_PAMSK) | PC_PAGOFF) \
	     : pag_refill((pment_t *)(m),(vaddr_t)PC_VADDR,(pment_t)(f)) \
	))
#define vm_PCfetch() \
	vm_pget(vm_PCmap(VMF_FETCH,cpu.acblk.xea,cpu.vmap.xea))

#endif /* KLH10_EXTADR */

#define vm_xeamap(v,f)	vm_xmap(v,f,cpu.acblk.xea,cpu.vmap.xea)
#define vm_xrwmap(v,f)	vm_xmap(v,f,cpu.acblk.xrw,cpu.vmap.xrw)
#define vm_xbeamap(v,f) vm_xmap(v,f,cpu.acblk.xbea,cpu.vmap.xbea)
#define vm_xbrwmap(v,f) vm_xmap(v,f,cpu.acblk.xbrw,cpu.vmap.xbrw)

#define vm_execmap(v,f)	vm_xmap(v,f,cpu.acs.ac,cpu.vmap.exec)
#define vm_usermap(v,f)	vm_xmap(v,f,cpu.acs.ac,cpu.vmap.user)
#define vm_physmap(a) (&cpu.physmem[(a)])	/* Physical address mapping */

/* Given pointer to phys mem, convert to and from words and halfwords */
#define vm_pget(p)	(*(p))
#define vm_pgetlh(p)	LHPGET(p)
#define vm_pgetrh(p)	RHPGET(p)
#define vm_pset(p,w)	(*(p) = (w))
#define vm_psetlh(p,h)	LHPSET(p,h)
#define vm_psetrh(p,h)	RHPSET(p,h)
#define vm_psetxwd(p,l,r) XWDPSET(p,l,r)
#define vm_padd(p,i)	((p)+(i))	/* To formalize word offsets */

/* Define default memory read/write operations.  Note that most of these
** use the XRW map context, not the "current" context, in case of PXCT.
*/
#define vm_fetch(a)	vm_pget(vm_xeamap(a, VMF_FETCH))
#define vm_read(a)	vm_pget(vm_xrwmap(a, VMF_READ))
#define vm_readlh(a)	vm_pgetlh(vm_xrwmap(a, VMF_READ))
#define vm_readrh(a)	vm_pgetrh(vm_xrwmap(a, VMF_READ))
#define vm_write(a, w)	vm_pset(vm_xrwmap(a, VMF_WRITE), w)
#define vm_writelh(a,v)	vm_psetlh(vm_xrwmap(a, VMF_WRITE), v)
#define vm_writerh(a,v)	vm_psetrh(vm_xrwmap(a, VMF_WRITE), v)

#define vm_redmap(a) vm_xrwmap(a, VMF_READ)
#define vm_modmap(a) vm_xrwmap(a, VMF_WRITE)
#define vm_wrtmap(a) vm_xrwmap(a, VMF_WRITE)

/* Facilities for handling double-word refs.
**	Do not use vm_dpread for reading directly into an AC pair because
**	there may be a page fault on the second word.  vm_issafedouble
**	is TRUE if the reference crosses neither AC17/MEM20 or page boundaries.
**	Note how the test checks to see if all word-offset addr bits are 1,
**	i.e. if address points to last word in a page.
*/
#define vm_issafedouble(a) (((a)&H10MASK) != AC_17 && ((~(a))&PAG_MASK))
#define vm_pgetd(p)	(*(dw10_t *)(p))	/* Use only if issafedouble */
#define vm_psetd(p,d)	(*(dw10_t *)(p) = (d))	/* Use only if issafedouble */

/* NOTE: vm_dread is a special macro for reading a double-word, which
**	CANNOT be used in the same way as vm_read() (ie its value cannot
**	be used in an assignment), because for the slow case there is no
**	way to assemble an anonymous structure value.  (Yes, I know GNU C
**	has extensions for this, but ANSI doesn't yet.)
** ALSO:
**	The address must be an lvalue, and MAY BE CLOBBERED!!!
**	A page fault can happen between the two word reads.
**
** p.s. The reason for the stupid "value" of integer 0 is because some
**	so-called C compilers botch conditional expressions that have
**	type void.  Better ones just optimize it away.
*/
#define vm_dread(a,d) \
    (vm_issafedouble(a)					/* If safe double, */\
	? ((d) = vm_pgetd(vm_xrwmap(a,VMF_READ)), 0)	/* use fast way */\
	: ((d).w[0] = vm_read(a), 			/* Ugh, slow way */\
		va_inc(a), (d).w[1] = vm_read(a), 0))

/* vm_dwrite() - similar cautions to vm_dread().
**	In this case, a statement is necessary in order to get some
**	temporary pointer variables.
**	This code makes sure that both words can be written into before
**	writing anything.  That way, if a page fault happens nothing
**	has been changed yet.
**
** p.s. The reason for the silly if (1) is so a semicolon can follow
**	the macro invocation without screwing up code that thinks it really
**	is a void expression rather than a statement.
*/
#define vm_dwrite(a,d) \
	if (vm_issafedouble(a)) {		/* If safe double, */\
	   vm_psetd(vm_xrwmap(a,VMF_WRITE), (d)); /* use fast way */\
	} else if (1) {				\
	    register vmptr_t p0__, p1__;	/* Ugh, slow way */\
	    p0__ = vm_xrwmap(a, VMF_WRITE);	\
	    va_inc(a);				\
	    p1__ = vm_xrwmap(a, VMF_WRITE);	\
	    vm_pset(p0__, (d).w[0]);		\
	    vm_pset(p1__, (d).w[1]);		\
	} else

/* Paging hardware definitions */

/* Executive Base Register */

#if KLH10_CPU_KL
# define EBR_CCALK	0400000	/* 18 Cache strategy - look */
# define EBR_CCALD	0200000	/* 19 Cache strategy - load */
#else
# define EBR_CCALK	0
# define EBR_CCALD	0
#endif
#define EBR_T20		040000	/* 21 "Tops-20 paging" - off for ITS */
#define EBR_ENABLE	020000	/* 22 Enable pager */
#if KLH10_CPU_KS
# define EBR_BASEPAG	 03777	/* 25-35 Physical DEC page # of EPT */
#elif KLH10_CPU_KL
# define EBR_BASEPAG	017777	/* 23-35 Physical DEC page # of EPT */
#endif

/* User Base Register */

#define UBR_SETACB	0400000	/* 0 Set AC blocks to those selected */
#if KLH10_CPU_KL
# define UBR_SETPCS	0200000	/* 1 Set PCS (Prev Sect) to that given */
#endif
#define UBR_SET		0100000	/* 2 Set UBR page # to that given */
#if KLH10_MCA25
# define UBR_KEEP	 040000	/* 3 Don't invalidate "Kept" pages */
#endif
#define UBR_ACBCUR	  07000	/*   Current AC block */
#define UBR_ACBPREV	   0700	/*   Previous Context AC block */
#if KLH10_CPU_KL
# define UBR_PCS	    037	/*   Previous Context Section */
#endif
#if KLH10_CPU_KS
# define UBR_BASEPAG	  03777	/* (RH)  Physical DEC page # of UPT */
#elif KLH10_CPU_KL
# define UBR_DNUA	0400000	/* (RH)  Do Not Update Accounts */
# define UBR_BASEPAG	 017777	/* (RH)  Physical DEC page # of UPT */
#endif

#if KLH10_PAG_ITS	/* ITS version of UBR uses full physical addr */
# define UBR_BASELH	     03	/* (LH)	 High bits of UPT phys addr */
# define UBR_BASERH	0777777	/* (RH)  Low bits of   "   "    "   */
# define UBR_RHMASK	UBR_BASERH	/* Bits to keep in UBR RH */
#else
# define UBR_RHMASK	UBR_BASEPAG	/* Bits to keep in UBR RH */
#endif	/* !ITS */


/* Internal "hardware" page tables.
**	The user and exec maps are now in the "cpu" struct for locality.
**	The physical map is rarely used so is left here.
*/
#ifndef EXTDEF
# define EXTDEF extern	/* Default is to declare (not define) vars */
#endif
#if 0
EXTDEF pment_t pr_umap[PAG_MAXVIRTPGS];	/* Internal user mode map table */
EXTDEF pment_t pr_emap[PAG_MAXVIRTPGS];	/*   "      exec  "    "    "   */
#endif
EXTDEF pment_t pr_pmap[PAG_MAXVIRTPGS];	/*   "      physical   "    "   */

/* ITS Pager definitions */

#if KLH10_PAG_ITS
/*
	On a DEC KS10 the page tables are either included in the UPT
and EPT (for TOPS-10) or pointed to by a complicated mechanism (for
TOPS-20).  ITS however uses a different arrangement based on the
original MAC-10 pager built by Holloway for the KA-10.

There are 4 page map registers, called "Descriptor Base Registers".  Each
points to a table of at most 64 words (128 halfwords), with one page
map entry per halfword.
	DBR1 -> User low mem (     0-377777)
	DBR2 -> User high    (400000-777777)
	DBR3 -> EXEC high
	DBR4 -> EXEC low	(didn't exist on KA-10s)
*/

/* Page table entry format */
#define  PM_29	0400000		/* Bit 2.9 of ITS map entry halfword */
#define  PM_28	0200000		/* Bit 2.8 */

#define PM_ACC (PM_29|PM_28)	/* Mask for ITS access bits: */
			/* Note: h/w treats RWF the same as RO. */
#define  PM_ACCNON ((h10_t)00<<16)	/* 00 - invalid, inaccessible */
#define  PM_ACCRO  ((h10_t)01<<16)	/* 01 - Read Only */
#define  PM_ACCRWF ((h10_t)02<<16)	/* 10 - Read/Write/First (== RO) */
#define  PM_ACCRW  ((h10_t)03<<16)	/* 11 - Read/Write */

#define PM_AGE 020000		/* Age bit */
#define PM_CSH 010000		/* Cache enable bit */
#define PM_PAG PAG_PAMSK	/* Physical ITS page number (max 1777?) */
#if (PM_PAG & PM_CSH)
	* ERROR *  "ITS page number field too large"
#endif


/* Format of ITS page fail word */
#define PF_USR	0400000	/* 4.9 Indicates user address space. */
#define PF_NXI	0200000	/* 4.8 Nonexistent IO register. */
#define PF_NXM	0100000	/* 4.7 Nonexistent memory. */
#define PF_PAR	0040000	/* 4.6 Uncorrectable memory error. */
			/*	(AC0 in block 7 has the word unless 4.7 is */
			/*	 also set.) */
			/* 4.5 unused */
#define PF_WRT	0010000	/* 4.4 Soft fault reference called for writing. */
#define PF_29	004000	/* 4.3 - 4.2 Access bits for referenced page in soft */
#define PF_28	002000	/* 	   fault. (from 2.9 & 2.8 of page entry) */
#define PF_PHY	0001000	/* 4.1 Address given was physical. */
			/* 3.9 unused */
#define PF_IO	0000200	/* 3.8 Indicates an IO operation. */
			/* 3.7-3.6 unused */
#define PF_BYT	0000020	/* 3.5 Indicates a byte IO operation. */
			/* 3.4 - 1.1 IO address */
			/* 	     or		*/
			/* 3.1 - 1.1 Memory address */
#define PF_PNO	0776000	/* 2.9 - 2.2 Virtual page number */


/* Format of the area read and written by LPMR, SPM: */
#define LPMR_DBR1 0	/* User low DBR */
#define LPMR_DBR2 1	/* User high DBR */
#define LPMR_QUANT 2	/* Quantum timer */
#define LPMR_UJPC 3	/* User JPC (if supported) */
#define LPMR_EJPC 4	/* Exec JPC (if supported) */

/* The quantum timer appears to be incremented approximately every
** millisecond (2^12 the KS10 internal clock rate) whenever
** not holding an interrupt (ie no PI in progress).
*/

/* Paging register variables */
struct pagregs {
	int32 pr_dbr1, pr_dbr2, pr_dbr3, pr_dbr4;	/* DBR regs */
	int32 pr_quant;
	vaddr_t pr_ujpc, pr_ejpc;

				/* Common to all systems */
	h10_t pr_flh;		/* Fail word LH bits */
	paddr_t pr_fref;	/* Failing virtual/physical address */
	char *pr_fstr;		/* Failure reason (for debugging) */
	pment_t *pr_fmap;	/* Failing map pointer */
	h10_t pr_facf;		/* Failing ref access bits */
};
#endif /* ITS */

/* T10 (KI) Pager definitions */

#if KLH10_PAG_KI

/* Page table entry format */
#define PM_ACC 0400000		/* B0 - Access allowed (R or RW) */
#define PM_PUB 0200000		/* B1 - Public (not used in KS10) */
#define PM_WRT 0100000		/* B2 - Writable */
#define PM_SFT 0040000		/* B3 - Software (not used by page refill) */
#define PM_CSH 0020000		/* B4 - Cacheable */
#define PM_PAG 0017777		/* Physical DEC page number - 13 bits */
#if (PM_PAG < PAG_PAMSK)
	/*#*/ * ERROR *  "T10 page number field too small"
#endif

/* Page fail bits as returned by MAP or page failure trap */
#define PF_USER	((h10_t)1<<17)	/* B0 - User space reference (else exec) */
#define PF_HARD	((h10_t)1<<16)	/* B1 - Hardware or IO failure (else soft) */
#define PF_ACC  ((h10_t)1<<15)	/* B2 - 'Access' bit from page map entry */
#define PF_WRT	(1<<14)		/* B3 - 'Writable' bit from page map entry */
#define PF_SFT	(1<<13)		/* B4 - 'Soft' bit from page map entry */
#define PF_WREF (1<<12)		/* B5 - Reference was write */
#define PF_PUB	(1<<11)		/* B6 - 'Public' bit from refill eval */
#define PF_CSH	(1<<10)		/* B7 - 'Cacheable' bit from refill eval */
#define PF_VIRT	(1<<9)		/* B8 - Physical or Virtual reference */
	/* (1<<8) */		/* B9 - */
#define PF_IOREF (1<<7)		/* B10 - IO Bus reference */
	/* (1<<6) (1<<5) */	/* B11,B12 - */
#define PF_IOBYTE (1<<4)	/* B13 - Byte mode in failing IO ref */

#if KLH10_CPU_KL	/* Sufficient for now, later merge */
# define PMF_IIERR  ((h10_t)024<<12)	/* Illegal Indirect (EFIW) */
# define PMF_NXMERR ((h10_t)036<<12)	/* AR parity error (pretend NXM) */
#endif					/* 	DFKDA diag expects this */


struct pagregs {
#if KLH10_CPU_KL
	int pr_pcs;		/* Previous Context Section */
	paddr_t pr_pcsf;	/* PCS, shifted into field position */
	paddr_t pr_era;		/* Error Address Register */
#endif /* KL */

	paddr_t pr_physnxm;	/* 1st physical NXM address */

				/* Common to all systems */
	h10_t pr_flh;		/* Page fail code, or refill access bits */
	paddr_t pr_fref;	/* If phys failure, holds phys addr of ref */
	char *pr_fstr;		/* Failure string for debugging */
	pment_t *pr_fmap;	/* Failing hardware map pointer */
	h10_t pr_facf;		/* Failing ref access bits */
};

#endif /* KLH10_PAG_KI */

/* T20 (KL) Pager definitions */

#if KLH10_PAG_KL

/* Bits for page map entry LH as returned by MAP or page failure.
**	Source for this is DEC PRM p.4-26 to p.4-28.
**	Same bits for KL, PRM p.3-40.
*/
#define PF_USER	((h10_t)1<<17)	/* B0 U - User space reference (else exec) */
#define PF_HARD	((h10_t)1<<16)	/* B1 H - Hardware or IO failure (else soft) */
#define PF_ACC  (1<<15)		/* B2 A - Accessible, refill eval completed */
#define PF_MOD	(1<<14)		/* B3 M - 'Modified' bit from h/w page table */
#define PF_WRT	(1<<13)		/* B4 S - 'Writable' bit from refill eval */
#define PF_WREF (1<<12)		/* B5 T - Reference was write */
#define PF_PUB	(1<<11)		/* B6 P - 'Public' bit from refill eval */
#define PF_CSH	(1<<10)		/* B7 C - 'Cacheable' bit from refill eval */
#define PF_VIRT	(1<<9)		/* B8 V - Physical or Virtual reference */
#if KLH10_MCA25
# define PF_KEEP (1<<9)		/* B8 K - Keep page during WRUBR */
#endif

#if KLH10_CPU_KS
		/* (1<<8) */
# define PF_IOREF (1<<7)	/* B10 - IO Bus reference */
		/* (1<<6) (1<<5) */
# define PF_IOBYTE (1<<4)	/* B13 - Byte mode in failing IO ref */
#endif /* KS */

/* Special hardware failure codes in high 6 bits. */
#if KLH10_CPU_KS
# define PMF_PARERR ((h10_t)036<<12)	/* Uncorrectable memory error */
# define PMF_NXMERR ((h10_t)037<<12)	/* Nonexistent memory */
# define PMF_IOERR  ((h10_t)020<<12)	/* Nonexistent IO register */
#elif KLH10_CPU_KL
# define PMF_PUBERR ((h10_t)021<<12)	/* Proprietary violation */
# define PMF_ADRERR ((h10_t)023<<12)	/* Address break */
# define PMF_IIERR  ((h10_t)024<<12)	/* Illegal Indirect (EFIW) */
# define PMF_PGTERR ((h10_t)025<<12)	/* Page table parity error */
# define PMF_ISCERR ((h10_t)027<<12)	/* Illegal Address (sect > 037) */
# define PMF_NXMERR ((h10_t)036<<12)	/* AR parity error (pretend NXM) */
# define PMF_ARXERR ((h10_t)037<<12)	/* ARX parity error */
#endif
#define PMF_WRTERR ((h10_t)011<<12)	/* Write violation (soft) */
#define PMF_OTHERR ((h10_t)000<<12)	/* Random inaccessibility (soft) */

/* Section and Map pointer format */
#define PT_PACC	 (1<<14)	/* B3 - 'P' bit, public */
#define PT_WACC  (1<<13)	/* B4 - 'W' bit, writeable */
#define PT_KEEP  (1<<12)	/* B5 - 'K' bit, Keep (KL MCA25) */
#define PT_CACHE (1<<11)	/* B6 - 'C' bit, cacheable */
#define PT_TYPE ((h10_t)07<<15)	/* High 3 bits are pointer type */
#define  PTT_NOACC	0	/* Inaccessible pointer */
#define  PTT_IMMED	1	/* Immediate pointer (see SPT fields) */
#define  PTT_SHARED	2	/* Shared pointer */
				/*	RH: Index into SPT */
#define  PTT_INDIRECT	3	/* Indirect pointer */
#define    PT_IIDX 0777	/*	LH: B9-17 Next index */
				/*	RH: Index into SPT (like shared) */

/* Format of SPT (Special Page Table) entries.
**	The "Page Address" defined here is also used in
**	immediate section/map pointers (type PTT_IMMED).
*/
#define SPT_MDM   077		/* LH: B12-17 Storage medium (0=mem) */
#define SPT_RSVD  0760000	/* RH: B18-22 Reserved */
#define SPT_PGN   0017777	/* RH: B23-35 Page Number */

/* Format of CST (Core Status Table) entry.
**	There is one entry for every physical page on the machine.
*/
#define CST_AGE		0770000	/* LH: Page age - if zero, trap on ref. */
#define CST_HWBITS	017	/* RH: Hardware bits, s/w shouldn't hack */
#define CST_MBIT	01	/* RH: referenced page modified */

/* Paging registers
**	Note that on the KL the "registers" are stored in AC block 6.
**	Assumption is that this is NEVER the current ac block; known
**	to be true for T20 monitor at least.
*/
struct pagregs {
#if KLH10_CPU_KS
	paddr_t pr_spb;	/* SPT Base address (not aligned, unknown size) */
	paddr_t pr_csb;	/* CST Base address (not aligned, # = phys pages) */
	w10_t pr_cstm;	/* CST Mask word */
	w10_t pr_pur;	/* CST Process Use Register */
# define PAG_PR_SPBPA  cpu.pag.pr_spb
# define PAG_PR_CSBPA  cpu.pag.pr_csb
# define PAG_PR_CSTMWD cpu.pag.pr_cstm
# define PAG_PR_PURWD  cpu.pag.pr_pur
#elif KLH10_CPU_KL
# define PAG_PR_SPB	(cpu.acblks[6][3])
# define PAG_PR_CSB	(cpu.acblks[6][2])
# define PAG_PR_SPBPA  (((paddr_t)LHGET(PAG_PR_SPB)<<18) \
				| RHGET(PAG_PR_SPB))
# define PAG_PR_CSBPA  (((paddr_t)LHGET(PAG_PR_CSB)<<18) \
				| RHGET(PAG_PR_CSB))
# define PAG_PR_CSTMWD (cpu.acblks[6][0])
# define PAG_PR_PURWD  (cpu.acblks[6][1])
	int pr_pcs;		/* Previous Context Section */
	paddr_t pr_pcsf;	/* PCS, shifted into field position */
	paddr_t pr_era;		/* Error Address Register */
#endif /* KL */

	paddr_t pr_physnxm;	/* 1st physical NXM address */

				/* Common to all systems */
	h10_t pr_flh;		/* Page fail code, or refill access bits */
	paddr_t pr_fref;	/* If phys failure, holds phys addr of ref */
	char *pr_fstr;		/* Failure string for debugging */
	pment_t *pr_fmap;	/* Failing map pointer */
	h10_t pr_facf;		/* Failing ref access bits */
};

#endif /* KLH10_PAG_KL */

#if KLH10_CPU_KL
# define pag_pcsget() cpu.pag.pr_pcs
# define pag_pcsfget() cpu.pag.pr_pcsf
# define pag_pcsset(s) (cpu.pag.pr_pcsf = ((paddr_t)(s)<<18),\
			cpu.pag.pr_pcs = (s))
#endif

/* Pager function declarations - exported from kn10pag.c */

extern void pag_init(void);	/* Initialize pager stuff */

	/* Called if vm_xmap mapping macros fail.  Refill, trap if page-fail */
extern vmptr_t pag_refill(pment_t *, vaddr_t, pment_t);

extern void pag_fail(void);	/* Effect page-fail trap */

#if KLH10_CPU_KS
extern void pag_iofail(paddr_t, int);	/* PF trap for IO unibus ref */
#endif

#if KLH10_EXTADR		/* PF trap for Illegal-Indirect ref */
extern void pag_iifail(vaddr_t, pment_t *);
extern void pag_iiset(vaddr_t, pment_t *);	/* Set up vars for above */
#endif

#endif /* ifndef  KN10PAG_INCLUDED */
