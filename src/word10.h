/* WORD10.H - Definitions and minor operations on PDP-10 36-bit words
*/
/* $Id: word10.h,v 2.6 2002/05/21 09:59:55 klh Exp $
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
 * $Log: word10.h,v $
 * Revision 2.6  2002/05/21 09:59:55  klh
 * Allow derivation of WORD10_INT even if model is USEHWD.
 * Fixed some bugs in 32-bit and 36-bit signed conversions.
 *
 * Revision 2.5  2002/03/28 16:55:58  klh
 * Additional check for existence of 64-bit type
 * Make USEHWD the default for x86 platforms
 *
 * Revision 2.4  2001/11/19 10:37:38  klh
 * Fixed USEGCCSPARC to work again for Suns.
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*
	Define representational architecture of a 36-bit PDP-10 word.

There are many possible ways to represent a 36-bit word, but only a
few specific methods are provided for.  All of them rely on the following
basic type definitions, which specify the C integer types of the native
compiler in terms of their size in N bits.  Each may have more than N,
but cannot have less.

MANDATORY (must be defined):	<N> (native C integer type of at least N bits)
		WORD10_INT18 = <18+>
		WORD10_INT19 = <19+>
		WORD10_INT32 = <32+>
Optional (may be undefined):
		WORD10_INT36 = <36 exactly>
		WORD10_INT37 = <37+>
		WORD10_INT64 = <64+>

If the mandatory types (18, 19, and 32 bits) are not predefined, they
will be automatically determined from limits.h; ANSI C guarantees that
a 32+ bit type will always exist.  The other types are always optional
unless specifically required by a particular model.  Certain models
can check to see if larger types exist and do some things more
efficiently if so, but this is not required -- partly because all of
the possible combinations quickly become unpleasantly messy.

(1) USEHWD model: Halfword structure
		WORD10_USEHWD = 1
	This representation declares a w10_t as a structure of two
halfwords, with each 18-bit halfword right-justified in a native
integer type of at least 18 bits.  This is the only model that will
work on machines without a native C integer type of at least 36 bits.
The ordering of the two halfwords in memory is irrelevant.

(2) USENAT model: Native 36-bit integer
		WORD10_USENAT = 1
		WORD10_INT36 = <exactly-36-bit type>
	This model can be used if a native C integer type of exactly 36
bits is available.  Currently this implies using the KCC compiler on a
real PDP-10.  I don't expect this to be useful as more than a
curiousity, but it is easy to do except for emulating arithmetic flags.

(3) USEINT: Native 37+ bit integer
		WORD10_USEINT = 1
		WORD10_INT37 = <native type holding >= 37 bits>
	This model holds the 36-bit word right-justified in the native
type.  This is the basic one to use if a native type of more than 36
bits is available (typically 64).  However, depending on the host
system's C compiler or machine architecture, USEHUN may be a more
efficient alternative.  Note also the possibility of adding WORD10_BITF to
this model.

(4) USEHUN: Hybrid union; both halfword structure and native 37+bit integer.
		WORD10_USEHUN = 1
		WORD10_INT37 = <native type holding >= 37 bits>
	This model is an alternative to USEINT which might be more desirable
on some platforms where the 37+bit type has natural 18+bit type boundaries.
It is a hybrid of USEHWD and USEINT where a halfword structure is laid over
a native integer, with a possible gap in between.
	Specifically, with a 64-bit integer type, each halfword would
be stored right-justified in the two 32-bit subtypes.  If its value is
examined as a 64-bit integer, the left-half bits will appear to be shifted up
14 bits away from the right-half bits.  Halfwords can thus be readily
manipulated (I bet you didn't realize the PDP-10 depends so heavily on
a halfword architecture!) and certain fullword operations can still be
carried out using the full integer type.  Arithmetic operations do require
special care however to compensate for the gap bits.

(5) USEGCCSPARC: Hybrid; only usable with GCC.
		WORD10_USEGCCSPARC = 1
	This is a special version of USEHUN which is necessitated by a
misfeature of SUN SPARC systems that, in turn, requires the GCC
compiler to circumvent.  Thanks to the unholy legacy of PCC, the
historical convention for struct/union passing on Sparcs always
requires passing a pointer, regardless of how small the structure
might be.  GCC extensions to C include a "long long" type that is
vastly more efficient for this purpose, although it is much slower for
arithmetic in general.  GCC also allows in-line functions, which in
this case permit efficient casting of this type to a struct/union type
for most operations.
	Hence this model.  It uses a "native" 64-bit integer type (ie,
long long) for storing the 36-bit word, but actually manipulates the
data as if it were a structure of two 32-bit halfwords.  If its value
were examined as a 64-bit integer, the left-half bits would appear to
be shifted up 14 bits away from the right-half bits.


Only one of the above models may be requested.  For either of USENAT or
USEINT, a refinement is also possible if the compiler/platform supports
an overlaid subdivision into bitfields:

	WORD10_BITF = 1
	WORD10_BITFBIGEND = 1 or 0 to indicate whether compiler
		assigns bitfields from left (big, 1) or right (little, 0)

There are also auxiliary definitions for double-words and quad-words.
It's possible that some platforms will support integer types big enough
to hold these values (72+ bits), resulting in additional models.
However, no provision is made for this.

===================>>>>>   !!!NOTE!!!   <<<<<=======================

	In all models, unused bits MUST BE ZERO!!!
				   ==== == ====

The code always assumes this, in order to avoid the overhead of needless
pre-masking.  Anything that modifies the contents of a w10_t must always
ensure that the final result is properly masked.  When dealing with
AC or memory words, it is highly advisable to avoid any modification until
the complete result is available for storage, in order to allow for
the possibility of multiprocess or even multiprocessor operation.

*/

#ifndef WORD10_INCLUDED
#define WORD10_INCLUDED 1

#ifdef RCSID
 RCSID(word10_h,"$Id: word10.h,v 2.6 2002/05/21 09:59:55 klh Exp $")
#endif

/* Attempt to see what integer type sizes are available.
** This code is not meant to be comprehensive, but should work for
** most compiler/platform combinations.  It assumes:
**	+  Machine is 2s-complement with high bit the sign.
**	+  Preprocessor can use right-shift (>>) in #if expressions.
**	+  <limits.h> exists.
**	+  The <type>_MAX values have all bits 1 except the sign bit.
**
** Note: The size tests are made by comparing <type>_MAX rather than
** U<type>_MAX to avoid stressing the arithmetic capabilities of
** some preprocessors.
** 
** Note: ANSI C compilers are required to support an integer datatype
** of at least 32 bits, so this is our worst-case assumption and simplifies
** some tests.  1L<<31 should always be non-zero.
**
** Note: this code tests for the existence of "long long" and uses it if
** necessary.  However, the "1LL" and "1ULL" constructs can't be used
** in preprocessor tests because GCC's PP is screwed up and doesn't understand
** its own datatypes!  So we just assume that if long long exists, it's
** at least 64 bits in length.
**
** Note finally the peculiar cascaded shifts for testing sizes of more than 32.
** This is because ANSI C does not define the result of a single logical shift
** when the # of bits shifted is >= the # of bits in the type; some
** machines can only shift modulo 32 or 64, thus (a<<32) might be a no-op
** instead of returning 0!  Even worse, some do nothing whatsoever if the
** shift count is larger than the word size.  A shift of 31 should always
** work, however.
*/

#include <limits.h>	/* Find platform/compiler limits */
#include "cenv.h"	/* May need CENV_CPU_ defs */

#define WORD10_TX_UNK	0	/* Type indices for available native C types */
#define	WORD10_TX_CHAR	1
#define	WORD10_TX_SHORT	2
#define	WORD10_TX_INT	3
#define	WORD10_TX_LONG	4
#define	WORD10_TX_LLONG	5	/* Long long */

/* Need a few constants for the following tests */
#define MASK17      0377777
#define MASK18      0777777
#define MASK31 017777777777


#ifndef WORD10_INT18
#  if (SHRT_MAX >= MASK17)
#    define WORD10_INT18 short
#    define WORD10_ITX18 WORD10_TX_SHORT
#  elif (INT_MAX >= MASK17)
#    define WORD10_INT18 int
#    define WORD10_ITX18 WORD10_TX_INT
#  else
#    define WORD10_INT18 long
#    define WORD10_ITX18 WORD10_TX_LONG
#  endif
#endif
#ifndef WORD10_INT19
#  if (INT_MAX >= MASK18)
#    define WORD10_INT19 int
#    define WORD10_ITX19 WORD10_TX_INT
#  else
#    define WORD10_INT19 long
#    define WORD10_ITX19 WORD10_TX_LONG
#  endif
#endif
#ifndef WORD10_INT32
#  if (INT_MAX >= MASK31)
#    define WORD10_INT32 int
#    define WORD10_ITX32 WORD10_TX_INT
#    define WORD10_INT32_MAX INT_MAX
#  else
#    define WORD10_INT32 long
#    define WORD10_ITX32 WORD10_TX_LONG
#    define WORD10_INT32_MAX LONG_MAX
#  endif
#endif

/* Note whether our 32-bit type is exactly 32 bits.  Sometimes it's bigger.
*/
#if WORD10_INT32_MAX == MASK31
#  define WORD10_INT32_EXACT 1
#else
#  define WORD10_INT32_EXACT 0
#endif


/* Above 32 life gets more adventurous.
**	This code must avoid using any values larger than 32 bits, unless
**	WORD10_LLONG_MAX is defined (we have to use our own definition
**	to overcome the idiosyncracies of various platforms, sigh!)
**	If defined, we presume that the "long long" datatype is finally
**	available and has at least the 64 bits it is required to have.
**	
**	LLONG_MAX and ULLONG_MAX are the "official" definitions of ISO C99.
**	But also test for __LONG_LONG_MAX__ as that is often defined
**	by GCC even when the local limits.h is a mess of conflicting
**	conditionals.
*/
#ifndef WORD10_LLONG_MAX
#  if defined(LLONG_MAX)
#    define WORD10_LLONG_MAX LLONG_MAX
#  elif defined(__LONG_LONG_MAX__)
#    define WORD10_LLONG_MAX __LONG_LONG_MAX__
#  endif
#endif

#ifndef WORD10_INT36
#  if ((INT_MAX >> 4) >= MASK31)	/* See if pos int has 36 bits */
#    define WORD10_INT36 int
#    define WORD10_ITX36 WORD10_TX_INT
#    if ((INT_MAX >> 4) == MASK31)	/* See if exactly 36 bits */
#      define WORD10_INT36_EXACT 1
#    endif
#  elif ((LONG_MAX >> 4) >= MASK31)	/* See if pos long has 36 bits */
#    define WORD10_INT36 long
#    define WORD10_ITX36 WORD10_TX_LONG
#    if ((LONG_MAX >> 4) == MASK31)	/* See if exactly 36 bits */
#      define WORD10_INT36_EXACT 1
#    endif
#  elif defined(WORD10_LLONG_MAX)	/* Assume long long has >=64 bits */
#    define WORD10_INT36 long long
#    define WORD10_ITX36 WORD10_TX_LLONG
#    define WORD10_INT36_EXACT 0
#  endif
#endif
#ifndef WORD10_INT36_EXACT
#  define WORD10_INT36_EXACT 0
#endif

#ifndef WORD10_INT37
#  if ((INT_MAX >> 5) >= MASK31)	/* See if pos int has 36 bits */
#    define WORD10_INT37 int
#    define WORD10_ITX37 WORD10_TX_INT
#  elif ((LONG_MAX >> 5) >= MASK31)	/* See if pos long has 36 bits */
#    define WORD10_INT37 long
#    define WORD10_ITX37 WORD10_TX_LONG
#  elif defined(WORD10_LLONG_MAX)	/* Assume long long has >=64 bits */
#    define WORD10_INT37 long long
#    define WORD10_ITX37 WORD10_TX_LLONG
#  endif
#endif

/* This one is a little trickier; find the smallest type with 64 bits,
 * without using values larger than 32 bits.  Don't know if it's safe
 * to shift more than 32 bits until we try it with 31 first!  If the
 * first test with a 31-bit shift passes, the type has at least 62 bits.
 */
#ifndef WORD10_INT64
#  if ((INT_MAX >> 31) >= MASK31) && ((INT_MAX >> 33) >= MASK31)
#    define WORD10_INT64 int
#    define WORD10_ITX64 WORD10_TX_INT
#  elif ((LONG_MAX >> 31) >= MASK31) && ((LONG_MAX >> 33) >= MASK31)
#    define WORD10_INT64 long
#    define WORD10_ITX64 WORD10_TX_LONG
#  elif defined(WORD10_LLONG_MAX)	/* Assume long long has 64+ bits */
#    define WORD10_INT64 long long
#    define WORD10_ITX64 WORD10_TX_LLONG
#  endif
#endif


/* This test doesn't work for GCC because after 10 years the preprocessor
** STILL doesn't recognize its own "LL" suffix denoting a long long!
*/
#if 0
#ifndef WORD10_INT72
#  if ((LONG_MAX >> 31) >= MASK31) && ((LONG_MAX >> 41) >= MASK31)
#    define WORD10_INT72 long
#    define WORD10_ITX72 WORD10_TX_LONG
#  elif defined(WORD10_LLONG_MAX) && ((WORD10_LLONG_MAX >> 31) >= MASK31) \
 			   && ((WORD10_LLONG_MAX >> 41) >= MASK31)
#    define WORD10_INT72 long long
#    define WORD10_ITX72 WORD10_TX_LLONG
#  endif
#endif
#endif

/* Determine what representation model to use for a PDP-10 word.
*/

#ifndef WORD10_USEHWD		/* Ensure all flags have a defined value */
#  define WORD10_USEHWD 0
#endif
#ifndef WORD10_USENAT
#  define WORD10_USENAT 0
#endif
#ifndef WORD10_USEINT
#  define WORD10_USEINT 0
#endif
#ifndef WORD10_USEHUN
#  define WORD10_USEHUN 0
#endif
#ifndef WORD10_USEGCCSPARC
#  define WORD10_USEGCCSPARC 0
#endif

/* If none of the above are set, determine a default.  Native
** model is never the default.
*/
#if !(WORD10_USEHWD|WORD10_USENAT|WORD10_USEINT|WORD10_USEHUN \
	| WORD10_USEGCCSPARC)
#  if CENV_CPU_SPARC && defined(__GNUC__)
#    undef  WORD10_USEGCCSPARC
#    define WORD10_USEGCCSPARC 1
#  elif CENV_CPU_I386		/* USEHWD has proven best on x86s so far */
#    undef  WORD10_USEHWD
#    define WORD10_USEHWD 1
#  else
#    ifndef WORD10_INT37	/* If big integers not supported, */
#      undef  WORD10_USEHWD	/* only choice is halfword model */
#      define WORD10_USEHWD 1
#    else			/* But if have big integers, */
#      undef  WORD10_USEINT	/* use simple integer model (or HUN?) */
#      define WORD10_USEINT 1
#    endif
#  endif
#endif


/* Now set byte and bitfield ordering defs.
**	These are completely system-dependent (and can vary even on the
**	same platform) but in practice most compilers for a particular
**	platform tend to do things the same way, so these defaults will
**	generally work.
**   WORD10_SMEMBIGEND should be TRUE if structure members within a machine
**	word are assigned in left-to-right (big-end) order.
**   WORD10_BITFBIGEND should be TRUE if bitfields within a machine
**	word are assigned in left-to-right (big-end) order.
** Usually these are the same.
*/
#ifndef WORD10_SMEMBIGEND
#  define WORD10_SMEMBIGEND CENV_CPUF_BIGEND
#endif
#ifndef WORD10_BITFBIGEND
#  define WORD10_BITFBIGEND WORD10_SMEMBIGEND
#endif


/* Now check out BITF setting.
**	The default is not to try bitfields even though they might provide
**	more efficiency, since the way they are used is non-portable
**	and needs to be verified for each platform.
*/
#ifndef WORD10_BITF		/* Unless explicitly requested, */
#  define WORD10_BITF 0		/* portable default does without bitfields */
#endif


/* Basic integer typedefs */

typedef		 WORD10_INT18  int18;	/* Smallest type of >= 18 bits */
typedef unsigned WORD10_INT18 uint18;
typedef 	 WORD10_INT19  int19;	/* Smallest type of >= 19 bits */
typedef unsigned WORD10_INT19 uint19;
typedef		 WORD10_INT32  int32;	/* Smallest type of >= 32 bits */
typedef unsigned WORD10_INT32 uint32;
#ifdef WORD10_INT36
 typedef	  WORD10_INT36  int36;	/* Smallest type of >= 36 bits */
 typedef unsigned WORD10_INT36 uint36;
#endif
#ifdef WORD10_INT37
 typedef	  WORD10_INT37  int37;	/* Smallest type of >= 37 bits */
 typedef unsigned WORD10_INT37 uint37;
#endif
#ifdef WORD10_INT64
 typedef	  WORD10_INT64  int64;	/* Smallest type of >= 64 bits */
 typedef unsigned WORD10_INT64 uint64;
#endif
#ifdef WORD10_INT72
 typedef	  WORD10_INT72  int72;	/* Smallest type of >= 72 bits */
 typedef unsigned WORD10_INT72 uint72;
#endif

/* Fundamental PDP-10 word definitions.
**	There is only a single PDP-10 word representation for any actual
**	instance of this code.  However, there can exist several ways of
**	defining this representation in C, each of which might be
**	more useful than the others in certain situations.
*/

/* Halfword - fundamental PDP-10 storage unit
*/
#define H10BITS 18		/* Number of bits in PDP-10 halfword */
#define H10OVFL ((uint19)1<<H10BITS)	 /* Halfword overflow bit */
#define H10SIGN ((uint18)1<<(H10BITS-1)) /* Halfword sign bit */
#define H10MASK (H10OVFL-1)	/* Halfword Mask (all-ones value) */
#define H10ONES H10MASK
#define H10MAGS	(H10SIGN-1)	/* Halfword magnitude bits (all but sign) */
#define W10BITS (2*H10BITS)	/* Number of bits in PDP-10 fullword */
	/* Note that the above bits and masks are all unsigned, to avoid
	** unpleasant surprises when shifted.
	*/

/* Define word as "w10int_t" integer type, if possible.
*/
#if WORD10_USEHWD		/* Halfword struct */
#  define WORD10_MODEL "USEHWD"
#  define WORD10_STRUCT
#  if defined(WORD10_INT36)	/* May also have integer */
#    define WORD10_INT WORD10_INT36
#    define WORD10_ITX WORD10_ITX36
#  else
#    undef  WORD10_INT
#  endif
#elif WORD10_USENAT		/* Solid 36-bit integer */
#  define WORD10_MODEL "USENAT"
#  define WORD10_INT WORD10_INT36
#  define WORD10_ITX WORD10_ITX36
#elif WORD10_USEINT		/* Solid 37+bit integer */
#  define WORD10_MODEL "USEINT"
#  define WORD10_INT WORD10_INT37
#  define WORD10_ITX WORD10_ITX37
#elif WORD10_USEHUN		/* Union of gapped integer & struct */
#  define WORD10_MODEL "USEHUN"
#  define WORD10_INT WORD10_INT37
#  define WORD10_ITX WORD10_ITX37
#  define WORD10_STRUCT
#elif WORD10_USEGCCSPARC	/* Gapped integer */
#  define WORD10_MODEL "USEGCCSPARC"
#  define WORD10_INT WORD10_INT37
#  define WORD10_ITX WORD10_ITX37
#  define WORD10_STRUCT
#endif
#ifdef WORD10_INT
	typedef	WORD10_INT		w10int_t;
	typedef	unsigned WORD10_INT	w10uint_t;
#endif

/* Define word as "w10struct_t" structure, if possible.
   Note ordering test.
*/
#ifdef WORD10_STRUCT
	typedef struct {
#  if WORD10_SMEMBIGEND
		unsigned WORD10_INT18 lh, rh;
#  else
		unsigned WORD10_INT18 rh, lh;
#  endif
	} w10struct_t;
#endif

/* Define bitfield structs within word, if possible.
*/
#if WORD10_BITF
	/* First check for existence of overflow bits */
#  if WORD10_USENAT
#    undef  WORD10_OVBITS	/* Nope, none */
#    define WORD10_OVDEF
#  else
#    define WORD10_OVBITS (((sizeof(w10uint_t)/sizeof(char))*CHAR_BIT) - 36)
#    define WORD10_OVDEF int ovfl : WORD10_OVBITS;
#  endif
	struct {
#  if WORD10_BITFBIGEND		/* Left-to-right order? */
		WORD10_OVDEF
		signed int lh : H10BITS;
		signed int rh : H10BITS;
#  else				/* Right-to-left order */
		signed int rh : H10BITS;
		signed int lh : H10BITS;
		WORD10_OVDEF
#  endif
	} w10sbitf_t;
	struct {
#  if WORD10_BITFBIGEND		/* Left-to-right order? */
		WORD10_OVDEF
		unsigned int lh : H10BITS;
		unsigned int rh : H10BITS;
#  else				/* Right-to-left order */
		unsigned int rh : H10BITS;
		unsigned int lh : H10BITS;
		WORD10_OVDEF
#  endif
	} w10ubitf_t;
#  undef WORD10_OVDEF		/* No longer needed */
#endif	/* WORD10_BITF */


/*============ Finally define the PDP-10 word: "w10_t"! =============*/

#if WORD10_USEHWD
	typedef w10struct_t w10_t;	/* PDP-10 Word value - struct */

#elif WORD10_USEGCCSPARC
	typedef w10uint_t w10_t;	/* PDP-10 Word value - gap integer */
	typedef union {
		w10uint_t i;
		w10struct_t uwd;
	} w10union_t;

#elif (WORD10_USENAT | WORD10_USEINT)
#  if !WORD10_BITF
	typedef w10uint_t w10_t;	/* PDP-10 Word value - solid integer */
#  else /* WORD10_BITF */
	typedef union {
		w10uint_t i;
		w10sbitf_t swd;
		w10ubitf_t uwd;
	} w10_t;			/* PDP-10 Word value - union */
#  endif

#elif WORD10_USEHUN
	typedef union {
		w10uint_t i;
		w10struct_t uwd;
	} w10_t;			/* PDP-10 Word value - union */

#endif

/* Define remaining PDP-10 data types */
typedef /*signed*/ WORD10_INT18 h10_t;	/* PDP-10 Half-Word integer value */
typedef struct { w10_t w[2]; } dw10_t;	/* PDP-10 Double-Word value */
typedef struct {dw10_t d[2]; } qw10_t;	/* PDP-10 Quad-Word value */

/* Note: for consistency with PDP-10 word order in memory,
** the high-value word of a double is w[0], and the low word is w[1].
** Similarly for quads.  No ordering is defined or needed for halfwords.
*/


/* Some simple mask values
**	This may not be the right place for them, but some are needed by
**	the macro facilities provided for managing w10_t values.
*/

/* Right-justified integer masks
*/
#define MASK12        07777
#define MASK14       037777
#define MASK16      0177777
/*efine MASK17      0377777 */ /* Defined earlier */
/*efine MASK18      0777777 */ /* Defined earlier */
#define MASK22    017777777
#define MASK23    037777777
#define MASK30  07777777777
/*efine MASK31 017777777777 */ /* Defined earlier */
#define MASK32 037777777777
#ifdef WORD10_INT36
	/* Do mask this way to avoid needing suffix - sigh */
# define MASK36 ((((WORD10_INT36)1)<<36)-1)
#endif

/* Word-format integer masks
**	Not necessarily right-justified!!
*/
#if WORD10_USENAT || WORD10_USEINT
#  define W10HISHFT	H10BITS
#  define W10MASK	MASK36
#  define W10SIGN	(((w10uint_t)1)<<(W10BITS-1))
#  define W10MAGS	(W10SIGN-1)
#elif WORD10_USEGCCSPARC || WORD10_USEHUN
   /* Special bit arrangement.  Assumes halfwords defined with INT18 */
#  define W10HISHFT	(sizeof(WORD10_INT18)*CHAR_BIT)
#  define W10MASK	((((w10uint_t)H10MASK)<<W10HISHFT)|H10MASK)
#  define W10SIGN	(((w10uint_t)1)<<(W10HISHFT+H10BITS-1))
#  define W10MAGS	((((w10uint_t)(H10MASK>>1))<<W10HISHFT)|H10MASK)
#else /* WORD10_USEHWD */
#  define W10HISHFT	--error_no_W10HISHFT--
#  define W10MASK	--error_no_W10MASK--
#  define W10SIGN	--error_no_W10SIGN--
#  define W10MAGS	--error_no_W10MAGS--
#endif

/* Macro facilities for basic manipulation of PDP-10 data types.
**	All referenced data is assumed to be already masked appropriately,
**	so that redundant mask operations can be avoided whenever possible.
**	This requires some care!
**
** The _LH/_RH macros all evaluate their argument only once, and return
**	the desired value.
** The SET macros, however, must always be used with an lvalue as the first
**	arg since it may be evaluated more than once, and their value must
**	not be used for anything.
** The LHVAR/RHVAR macros exist to provide a way of treating the LH and RH
**	as "lvalue" variables -- ie their address can be taken and a value
**	can be assigned to them.  For some models these do not exist.
** The VAR and IMM macros should not be used except by further macros
**	(here or in kn10ops.h) which *KNOW* what they are doing, since
**	they only exist for certain models.
*/

#if WORD10_USEGCCSPARC
static inline w10_t w10_ximm(h10_t l, h10_t r)
	{ register w10union_t wu; wu.uwd.lh = l; wu.uwd.rh = r; return wu.i; }
static inline h10_t w10_lh(w10_t w)
	{ register w10union_t wu; wu.i = w; return wu.uwd.lh; }
static inline h10_t w10_rh(w10_t w)
	{ register w10union_t wu; wu.i = w; return wu.uwd.rh; }
static inline w10_t w10_lhset(w10_t w, h10_t h)
	{ register w10union_t wu; wu.i = w; wu.uwd.lh = h; return wu.i; }
static inline w10_t w10_rhset(w10_t w, h10_t h)
	{ register w10union_t wu; wu.i = w; wu.uwd.rh = h; return wu.i; }
#  define W10_XIMM(l,r)	w10_ximm(l,r)
#  define W10_XINIT(l,r) ((((w10uint_t)(l))<<32)|(r))
#  define W10_XSET(w,l,r) (w = w10_ximm(l,r))
#  define W10_VAR(w)	(w)
#  define W10_LHVAR(wu)	((wu).uwd.lh)		/* Only for w10union_t */
#  define W10_RHVAR(wu)	((wu).uwd.rh)
#  define W10_LH(w)	w10_lh(w)
#  define W10_RH(w)	w10_rh(w)
#  define W10_LHSET(w,v)	((w) = w10_lhset(w,v))
#  define W10_RHSET(w,v)	((w) = w10_rhset(w,v))
#  define W10P_LH(p)	(((w10union_t *)(p))->uwd.lh)
#  define W10P_RH(p)	(((w10union_t *)(p))->uwd.rh)
#  define W10P_LHSET(p,v)	(((w10union_t *)(p))->uwd.lh = (v))
#  define W10P_RHSET(p,v)	(((w10union_t *)(p))->uwd.rh = (v))
#  define W10P_XSET(p,l,r) (*(p) = w10_ximm(l,r))

#elif WORD10_USEHWD
#  define W10_XIMM(l,r)	--illegal--
#  if WORD10_SMEMBIGEND
#    define W10_XINIT(l,r) {(l),(r)}		/* Data constant initializer */
#  else
#    define W10_XINIT(l,r) {(r),(l)}		/* Data constant initializer */
#  endif
#  define W10_XSET(w,l,r) (W10_LHSET(w,l),W10_RHSET(w,r))
#  define W10_VAR(w)	--illegal--
#  define W10_LHVAR(w)	((w).lh)
#  define W10_RHVAR(w)	((w).rh)
#  define W10_LH(w)	((w).lh)
#  define W10_RH(w)	((w).rh)
#  define W10_LHSET(w,v)	((w).lh = (v))
#  define W10_RHSET(w,v)	((w).rh = (v))
#  define W10P_LH(p)	((p)->lh)
#  define W10P_RH(p)	((p)->rh)
#  define W10P_LHSET(p,v)	((p)->lh = (v))
#  define W10P_RHSET(p,v)	((p)->rh = (v))
#  define W10P_XSET(p,l,r) (W10P_LHSET(p,l), W10P_RHSET(p,r))

#elif WORD10_USEHUN || WORD10_BITF
#  define W10_XIMM(l,r)	(((w10uint_t)(l)<<W10HISHFT)|(r))
#  define W10_XINIT(l,r) { W10_XIMM(l,r) }
#  define W10_XSET(w,l,r) ((w).i = W10_XIMM(l,r))
#  define W10_VAR(w)	((w).i)
#  define W10_LHVAR(w)	((w).uwd.lh)
#  define W10_RHVAR(w)	((w).uwd.rh)
#  define W10_LH(w)	((w).uwd.lh)
#  define W10_RH(w)	((w).uwd.rh)
#  define W10_LHSET(w,v)	((w).uwd.lh = (v))
#  define W10_RHSET(w,v)	((w).uwd.rh = (v))
#  define W10P_LH(p)	((p)->uwd.lh)
#  define W10P_RH(p)	((p)->uwd.rh)
#  define W10P_LHSET(p,v)	((p)->uwd.lh = (v))
#  define W10P_RHSET(p,v)	((p)->uwd.rh = (v))
#  define W10P_XSET(p,l,r) ((p)->i = W10_XIMM(l,r))

#elif WORD10_USEINT || WORD10_USENAT
#  define W10_XIMM(l,r)	((((w10uint_t)(l))<<H10BITS)|(r))
#  define W10_XINIT(l,r)	W10_XIMM(l,r)
#  define W10_XSET(w,l,r) ((w) = W10_XIMM(l,r))
#  define W10_VAR(w)	(w)
#  define W10_LHVAR(w)	--illegal--
#  define W10_RHVAR(w)	--illegal--
#  define W10_LH(w)	((uint18)((w)>>H10BITS))
#  define W10_RH(w)	((uint18)((w)&H10MASK))
#  define W10_LHSET(w,v)	W10_XSET((w),(v),W10_RH(w))
#  define W10_RHSET(w,v)	((w) = ((w)&(~(w10uint_t)H10MASK))|(v))
#  define W10P_LH(p)	W10_LH(*(p))
#  define W10P_RH(p)	W10_RH(*(p))
#  define W10P_LHSET(p,v)	W10_LHSET(*(p),(v))
#  define W10P_RHSET(p,v)	W10_RHSET(*(p),(v))
#  define W10P_XSET(p,l,r) (*(p) = W10_XIMM(l,r))
#endif


/* Macros for double-words and quad-words.
 *	These are minimal because code is generally expected to know that
 *	they are arrays of w10_t (as they must be, to remain compatible with
 *	memory references) and can simply access the contents directly.
 *	The initializers are provided as a convenience to help get the
 *	pesky braces correct.
 */
#define DW10_HI(d) ((d).w[0])
#define DW10_LO(d) ((d).w[1])
#define DW10_SET(d,hi,lo) ((d).w[0] = (hi), (d).w[1] = (lo))
/* #define DW10_INIT(hi,lo) {{hi, lo}} */
#define DW10_XINIT(h0,h1,h2,h3) {{W10_XINIT(h0,h1),W10_XINIT(h2,h3)}}

#define QW10_XINIT(h0,h1,h2,h3,l0,l1,l2,l3) \
		{{DW10_XINIT(h0,h1,h2,h3), DW10_XINIT(l0,l1,l2,l3)}}


/* Deprecated macros, retained for backward compatibility
 */
#define XWDIMM(l,r)	W10_XIMM(l,r)
#define XWDINIT(l,r)	W10_XINIT(l,r)
#define XWDSET(w,l,r)	W10_XSET(w,l,r)
#define XWDVAL(w)	W10_VAR(w)
#define LHVAL(w)	W10_LHVAR(w)
#define RHVAL(w)	W10_RHVAR(w)
#define LHGET(w)	W10_LH(w)
#define RHGET(w)	W10_RH(w)
#define LHSET(w,v)	W10_LHSET(w,v)
#define RHSET(w,v)	W10_RHSET(w,v)
#define LHPGET(p)	W10P_LH(p)
#define RHPGET(p)	W10P_RH(p)
#define LHPSET(p,v)	W10P_LHSET(p,v)
#define RHPSET(p,v)	W10P_RHSET(p,v)
#define XWDPSET(p,l,r)	W10P_XSET(p,l,r)

#define W10XWD(l,r)	W10_XINIT(l,r)
#define LRHSET(w,l,r)	W10_XSET(w,l,r)

#define HIGET(d)	DW10_HI(d)
#define LOGET(d)	DW10_LO(d)

#define DXWDINIT(h0,h1,h2,h3)	DW10_XINIT(h0,h1,h2,h3)
#define QXWDINIT(h0,h1,h2,h3,l0,l1,l2,l3) QW10_XINIT(h0,h1,h2,h3,l0,l1,l2,l3)


/* Conversions to and from w10_t and native integer types.
 *	32-bit conversions are always supported.
 *	36-bit conversions are supported only if a large enough integer
 *		type exists -- WORD10_INT will be defined if so.
 *
 * All w10_t word arguments are assumed to be correctly formatted, but
 * no assumptions are made about the integer arguments; they are
 * extended and/or masked as necessary.
 */

#if 0
/* Macro pseudo-declarations, for documentary purposes */

/* Returns a uint32 containing the low 32 bits of word.
 *	Only 32 bits are returned even if a uint32 is larger.
 */
uint32 W10_U32(w10_t);

/* Returns a signed int32 containing the low 32 bits of word taken as a
 *	32-bit signed value.  This means that bit 1<<31 of the word is
 *	interpreted as a sign bit and extended to fill the rest of the
 *	int32.  It int32 is exactly 32 bits this returns the same
 *	bits as W10_U32, but if larger then the higher bits will be
 *	filled with copies of the sign bit.  This is true even if the
 *	native type could hold the entire 36-bit value!
 *	It's done this way to ensure the behavior doesn't change if
 *	the actual size of an int32 changes.
 */
int32 W10_S32(w10_t);

/* Set word to unsigned 32-bit value, zero high bits of word
 *	This only uses 32 bits even if an int32 is larger.
 */
void W10_U32SET(w10_t, uint32);

/* Set word to 32-bit signed value.
 *	Bit 1<<31 of the int32 is always interpreted as a sign bit and
 *	used to fill the high 4 bits of the word.  This is true even if
 *	a int32 is larger than 32 bits and has a positive value!!!!
 */
void W10_S32SET(w10_t, int32);

/* ========================================================================
 *	The following macros only exist if WORD10_INT is defined
 */

/* Returns a w10uint_t containing all 36 bits as an unsigned value.
 */
w10uint_t W10_U36(w10_t);

/* Returns a w10int_t containing all 36 bits as a signed value.
 *	If a w10int_t is larger than 36 bits, the high order bits will
 *	have copies of the word's sign bit (1<<35)!  Such a value CANNOT
 *	be stored directly back into a w10_t.
 */
w10int_t W10_S36(w10_t);

/* Set word to unsigned 36-bit value.
 *	The argument is cast to a w10uint_t and any higher-order bits
 *	(beyond 36) of the value are ignored.
 */
void W10_U36SET(w10_t, w10uint_t);

/* Set word to 36-bit signed value.
 *	The difference between this and W10_U36SET is that the
 *	argument is cast to a w10int_t (not w10uint_t); if it is a signed
 *	integer of less than 36 bits, its sign is thus automatically
 *	extended by C.
 *	At that point only the low-order 36 bits are used, regardless of
 *	whether the int36 is larger and has a positive or negative value.
 */
void W10_S36SET(w10_t, w10int_t);

#endif

#define H10SIGN32 (1<<(17-4))		/* LH sign bit of a 32-bit value */
#define W10SIGN32 (((uint32)1)<<31)	/* Word sign bit of a 32-bit value */

/* =================================================================== */

/* Internal defs shared by various models that have a halfword structure.
 * Do them in one place to reduce duplication errors!
 * Unfortunately there seems to be no real way to take advantage of
 * a gapped integer; closing and opening the gap is faster with struct members.
 */
#if WORD10_USEHWD || WORD10_USEHUN || WORD10_USEGCCSPARC
# if WORD10_INT32_EXACT
#  define W10STRU_U32(w) ((uint32)((((uint32)W10_LH(w))<<H10BITS) | W10_RH(w)))
#  define W10STRU_S32(w) ((int32) W10STRU_U32(w))
#  define W10STRU_U32SET(w,i) W10_XSET((w), \
			(((uint32)(i))>>H10BITS),((uint32)(i))&H10MASK)
#  define W10STRU_S32SET(w,i) W10_XSET((w), \
			(((int32)(i))>>H10BITS)&H10MASK, ((int32)(i))&H10MASK)
# else
#  define W10STRU_U32(w) ((uint32)(((((uint32)W10_LH(w))<<H10BITS) \
				| W10_RH(w)) & MASK32))
#  define W10STRU_S32(w) ((int32) (((W10_LH(w) & H10SIGN32) \
	? ((((int32)-1)<<31) | (((int32)W10_LH(w))<<H10BITS)|W10_RH(w)) \
	:             MASK32 & (((int32)W10_LH(w))<<H10BITS)|W10_RH(w))))
#  define W10STRU_U32SET(w,i) W10_XSET((w), \
			((((uint32)(i)) >> H10BITS) & MASK14), \
			(i) & H10MASK)
#  define W10STRU_S32SET(w,i) W10_XSET((w), \
		(  (((int32)(i)) & W10SIGN32) \
		? ((((int32)(i))>>H10BITS) & H10MASK)|(H10MASK&~(int32)MASK14)\
		: ((((int32)(i))>>H10BITS) & H10MASK),\
			(((int32)(i)) & H10MASK) )
# endif /* !WORD10_INT32_EXACT */
# ifdef WORD10_INT
#  define W10STRU_U36(w) ((((w10uint_t)W10_LH(w))<<H10BITS) | W10_RH(w))
#  define W10STRU_S36(w) ((W10_LH(w) & H10SIGN) \
			? ((w10int_t)W10STRU_U36(w)) | ~((w10int_t)MASK36) \
			: ((w10int_t)W10STRU_U36(w)) )
#  define W10STRU_U36SET(w,i) W10_XSET((w), \
			((((w10uint_t)(i)) >> H10BITS) & H10MASK), \
			((w10uint_t)(i)) & H10MASK)
#  define W10STRU_S36SET(w,i) W10STRU_U36SET((w),(w10int_t)(i))
# endif /* WORD10_INT */

#endif /* WORD10_USEHWD || WORD10_USEHUN || WORD10_USEGCCSPARC */

/* =================================================================== */
/* Struct members (HWD, HUN) or gapped integer (GCCSPARC) */

#if WORD10_USEHWD || WORD10_USEHUN || WORD10_USEGCCSPARC
# define W10_U32(w)	 W10STRU_U32(w)
# define W10_S32(w)	 W10STRU_S32(w)
# define W10_U32SET(w,i) W10STRU_U32SET(w,i)
# define W10_S32SET(w,i) W10STRU_S32SET(w,i)
# define W10_U36(w)	 W10STRU_U36(w)
# define W10_S36(w)	 W10STRU_S36(w)
# define W10_U36SET(w,i) W10STRU_U36SET(w,i)
# define W10_S36SET(w,i) W10STRU_S36SET(w,i)

/* =================================================================== */

#elif WORD10_USENAT	/* Native 36-bit, no masking needed */
	/* Assume int32 is not exactly 32 bits */
#  define W10_U32(w) ((uint32)(W10_VAR(w)&MASK32))
#  define W10_S32(w) ((int32) ((W10_VAR(w)&H10SIGN32) \
			? (W10_VAR(w) | ~((w10int_t)MASK32)) \
			: (W10_VAR(w) & MASK32) ))
#  define W10_U32SET(w,i) (W10_VAR(w) = ((uint32)(i))&MASK32)
#  define W10_S32SET(w,i) (W10_VAR(w) = (((int32)(i))&W10SIGN32) \
			? (((w10int_t)(i)) | ~((w10int_t)MASK32)) \
			: (((w10int_t)(i)) & MASK32) )
#  define W10_U36(w) ((w10uint_t)W10_VAR(w))
#  define W10_S36(w) (( w10int_t)W10_VAR(w))
#  define W10_U36SET(w,i) (W10_VAR(w) = (w10uint_t)(i))
#  define W10_S36SET(w,i) (W10_VAR(w) = ( w10int_t)(i))

/* =================================================================== */

#elif WORD10_USEINT	/* Solid integer */
# if WORD10_INT32_EXACT
#  define W10_U32(w) ((uint32)W10_VAR(w))
#  define W10_S32(w) (( int32)W10_VAR(w))
#  define W10_U32SET(w,i) (W10_VAR(w) = (w10uint_t)(uint32)(i))
#  define W10_S32SET(w,i) (W10_VAR(w) = (( w10int_t)( int32)(i))&MASK36)
# else
#  define W10_U32(w) ((uint32)(W10_VAR(w)&MASK32))
#  define W10_S32(w) ((int32) ((W10_VAR(w)&W10SIGN32) \
			? (W10_VAR(w) | ~((w10int_t)MASK32)) \
			: (W10_VAR(w) & MASK32) ))
#  define W10_U32SET(w,i) (W10_VAR(w) = (w10uint_t)((uint32)(i))&MASK32)
#  define W10_S32SET(w,i) (W10_VAR(w) = ( w10int_t)(((int32)(i))&W10SIGN32) \
			? ((((w10int_t)(i)) | ~((w10int_t)MASK32)) & MASK36) \
			:  (((w10int_t)(i)) & MASK32) )
# endif /* !WORD10_INT32_EXACT */

# define W10_U36(w) ((w10uint_t)W10_VAR(w))
# define W10_S36(w) (( w10int_t)((W10_VAR(w)&W10SIGN) \
			? (W10_VAR(w) | ~((w10int_t)MASK36)) \
			:  W10_VAR(w)))
# define W10_U36SET(w,i) (W10_VAR(w) = ((w10uint_t)(i))&MASK36)
# define W10_S36SET(w,i) (W10_VAR(w) = (( w10int_t)(i))&MASK36)

#endif

#endif /* ifndef  WORD10_INCLUDED */
