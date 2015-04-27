/* WXTEST.C - Test program for word10.h definitions
*/
/* $Id: wxtest.c,v 2.8 2002/05/21 09:56:10 klh Exp $
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
 * $Log: wxtest.c,v $
 * Revision 2.8  2002/05/21 09:56:10  klh
 * Fixed 32-bit conversion tests to catch problems in high 4 bits.
 * Fixed 36-bit conversion tests to run whenever possible, not just
 *     when w10_t is an integer type.
 *
 * Revision 2.7  2002/04/26 05:21:28  klh
 * Add missing include of <string.h>
 *
 * Revision 2.6  2002/03/28 16:56:33  klh
 * Adapt to new word10.h
 *
 * Revision 2.5  2001/11/19 10:19:55  klh
 * Solaris port: rename INTMAX_MAX to INTMAX_SMAX
 *
 * Revision 2.4  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*
	The only purpose of this program is to do some rudimentary testing
of the facilities defined by word10.h to help verify that they are
working properly.  Simply running other programs that use them is not
guaranteed to be complete or reliable, and it's time-consuming to do
this for all possible variations.
*/

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "rcsid.h"
#include "word10.h"

#ifdef RCSID
 RCSID(wxtest_c,"$Id: wxtest.c,v 2.8 2002/05/21 09:56:10 klh Exp $")
#endif

#ifndef TRUE
# define TRUE 1
# define FALSE 0
#endif

int swprint = 1;
int swverbose = 0;
int nerrors = 0;

/* Misc apparatus */

char usage[] = "\
Usage: %s -[qvh]\n\
    -q  Quiet\n\
    -v  Verbose\n\
    -h  Help (this stuff)\n";

int dotests(void);
int txtest(void);
int tinttypes(void);
int tmasks(void);

main(int argc, char **argv)
{
    char *cp;

    if (argc > 1) {
	if (argv[1][0] != '-') {
	    fprintf(stderr, usage, argv[0]);
	    exit(1);
	}
	for (cp = &argv[1][0]; *++cp; ) switch (*cp) {
	    case 'q': swprint = 0; break;
	    case 'v': swprint = swverbose = 1; break;
	    case 'h': fprintf(stdout, usage, argv[0]);
		    exit(0);
	    default:
		fprintf(stderr, usage, argv[0]);
		exit(1);
	}
    }

    dotests();
    return nerrors;
} 

/* Test tables & constants */

/* Note using "INTMAX_SMAX" instead of "INTMAX_MAX" -- latter conflicts
   with a Solaris def
*/
#if defined(WORD10_LLONG_MAX)
# define INTMAX long long
# define INTMAX_NAME "long long"
# define INTMAX_SMAX WORD10_LLONG_MAX
# define  SUFF(v) v ## LL
# define USUFF(v) v ## ULL
#elif defined(ULONG_MAX)
# define INTMAX long
# define INTMAX_NAME "long"
# define INTMAX_SMAX LONG_MAX
# define  SUFF(v) v ## L
# define USUFF(v) v ## UL
#else
# define INTMAX int
# define INTMAX_NAME "int"
# define INTMAX_SMAX INT_MAX
# define  SUFF(v) v
# define USUFF(v) v
#endif
#define UINTMAX unsigned INTMAX

#define WXHBITS 18
#define WXHMASK 0777777
#define WXPARGS(v) (long)((v) >> WXHBITS), (long)((v) & WXHMASK)


struct txent {
    char *tx_type;
    char *tx_macro;
    int tx_val;
    int tx_bits;
} txtab[] = {
    { "unknown","WORD10_TX_UNK",	WORD10_TX_UNK },	/* 0 */
    { "char",	"WORD10_TX_CHAR",	WORD10_TX_CHAR },	/* 1 */
    { "short",	"WORD10_TX_SHORT",	WORD10_TX_SHORT },	/* 2 */
    { "int",	"WORD10_TX_INT",	WORD10_TX_INT },	/* 3 */
    { "long",	"WORD10_TX_LONG",	WORD10_TX_LONG },	/* 4 */
    { "long long","WORD10_TX_LLONG",	WORD10_TX_LLONG },	/* 5 */
    { NULL, NULL, 0 }
};


struct maskent {
	char *me_mac;
	int me_bits;
	UINTMAX me_val;
} masktab[] = {
	{ "MASK12", 12,	MASK12 },
	{ "MASK14", 14,	MASK14 },
	{ "MASK16", 16,	MASK16 },
	{ "MASK17", 17,	MASK17 },
	{ "MASK18", 18,	MASK18 },
	{ "MASK22", 22,	MASK22 },
	{ "MASK23", 23,	MASK23 },
	{ "MASK30", 30,	MASK30 },
	{ "MASK31", 31,	MASK31 },
	{ "MASK32", 32,	MASK32 },
#ifdef MASK36
	{ "MASK36", 36,	MASK36 },
#endif
	{ "", 0, 0 }
};

int
dotests(void)
{

    if (swprint) {
	printf("Default word10 model: %s\n", WORD10_MODEL);
	printf("Maximum integer type used: %s\n", INTMAX_NAME);
    }
    nerrors += txtest();	/* Show native integer types available */
    nerrors += tmasks();	/* Verify masks */
    nerrors += tinttypes();	/* Verify types */

    nerrors += testfmt();	/* Verify bit sets and identify format */
    nerrors += test32();	/* Verify 32-bit conversions */
#ifdef WORD10_INT
    nerrors += test36();	/* Verify 36-bit conversions */
#endif

    return nerrors;
}

/* Verify and set up TX table
*/
int
txtest(void)
{
    register int i, j;
    int nerrs = 0;

    for (i = 0; txtab[i].tx_type; ++i) {
	struct txent *t = &txtab[i];
	UINTMAX uv;

	if (t->tx_val != i) {
	    printf("ITX screwup, entry %d \"%s\" misdefined as %d\n",
		   i, t->tx_type, t->tx_val);
	    ++nerrs;
	    /* Keep going anyway */
	}

	/* Verify whether the type exists and find its exact bit size */
	switch (t->tx_val) {
	case WORD10_TX_UNK:   continue;
	case WORD10_TX_CHAR:  uv = (UINTMAX) ((unsigned char)-1); break;
	case WORD10_TX_SHORT: uv = (UINTMAX) ((unsigned short)-1); break;
	case WORD10_TX_INT:   uv = (UINTMAX) ((unsigned int)-1); break;
	case WORD10_TX_LONG:  uv = (UINTMAX) (~(unsigned long)0); break;
	case WORD10_TX_LLONG:
#ifdef WORD10_LLONG_MAX
	    uv = (UINTMAX) ~((unsigned long long)0);
#else
	    uv = 0;
	    if (swverbose)
		printf("Warning: no support for \"long long\"\n");
#endif
	    break;
	default:
	    printf("Type error: %s has unknown WORD10_TX index %d\n",
		   t->tx_type, t->tx_val);
	    nerrs++;
	    continue;
	}
	/* Find exact size in bits */
	for (j = 0; uv; ++j)
	    uv >>= 1;
	t->tx_bits = j;
	if (swverbose) {
	    printf("Type \"%s\" (%s) has %d bits\n",
		   t->tx_type, t->tx_macro, t->tx_bits);
	}
    }
    if (swprint) {
	printf("Type index test %s\n",
		(nerrs ? "Failed" : "Passed"));
    }
    return nerrs;
}

/* Verify masks
*/
int
tmasks(void)
{
    register int i, j;
    int nerrs = 0;

    for (i = 0; j = masktab[i].me_bits; ++i) {
	UINTMAX mask = 1;
	/* Generate mask in a particularly stupid way to ensure that the
	   clever ways worked.
	*/
	while (--j > 0) {
	    mask <<= 1;
	    mask |= 1;
	}
	if (swverbose) {
	    printf("Testing MASK%d %lo,,%lo == %lo,,%lo\n",
		   masktab[i].me_bits,
		   WXPARGS(masktab[i].me_val),
		   WXPARGS(mask));
	}

	if (mask != masktab[i].me_val) {
	    printf("Mask error: %s defined as %lo,,%lo computed as %lo,,%lo\n",
			masktab[i].me_mac,
			WXPARGS(masktab[i].me_val),
			WXPARGS(mask));
	    ++nerrs;
	}
    }
    if (swprint) {
	printf("Mask test %s\n",
		(nerrs ? "Failed" : "Passed"));
    }
    return nerrs;
}


/* Verify existence of various integer types and their sizes
 */
#ifndef WORD10_INT18
# error "WORD10_INT18 must be defined"
#endif
#ifndef WORD10_INT19
# error "WORD10_INT19 must be defined"
#endif
#ifndef WORD10_INT32
# error "WORD10_INT32 must be defined"
#endif

struct typent {
    char *te_name;
    int te_bits;
    int te_itx;
} typetab[] = {
    { "WORD10_INT18", 18, WORD10_ITX18 },
    { "WORD10_INT19", 19, WORD10_ITX19 },
    { "WORD10_INT32", 32, WORD10_ITX32 },
#ifdef WORD10_INT36
    { "WORD10_INT36", 36, WORD10_ITX36 },
#endif
#ifdef WORD10_INT37
    { "WORD10_INT37", 37, WORD10_ITX37 },
#endif
#ifdef WORD10_INT64
    { "WORD10_INT64", 64, WORD10_ITX64 },
#endif
#ifdef WORD10_INT72
    { "WORD10_INT72", 72, WORD10_ITX72 },
#endif
#ifdef WORD10_INT
    { "WORD10_INT", 36, WORD10_ITX },
#endif
    { NULL, 0, 0 }
};

/* Verify typedefs were set up */
 int18  v_int18 = 0;
uint18 v_uint18 = 0;
 int19  v_int19 = 0;
uint19 v_uint19 = 0;
 int32  v_int32 = 0;
uint32 v_uint32 = 0;

#ifdef WORD10_INT36
 int36  v_int36 = 0;
uint36 v_uint36 = 0;
#endif
#ifdef WORD10_INT37
 int37  v_int37 = 0;
uint37 v_uint37 = 0;
#endif
#ifdef WORD10_INT64
 int64  v_int64 = 0;
uint64 v_uint64 = 0;
#endif
#ifdef WORD10_INT72
 int72  v_int72 = 0;
uint72 v_uint72 = 0;
#endif

#ifdef WORD10_INT
 w10int_t  v_intw10 = 0;
w10uint_t v_uintw10 = 0;
#endif

int
tinttypes(void)
{
    register int i, j;
    int nerrs = 0;
    struct typent *t;
    struct txent *tx;

    for (i = 0; typetab[i].te_name; ++i) {
	UINTMAX uv;
	t = &typetab[i];

	/* Verify that the type has at least the number of bits claimed */
	switch (t->te_itx) {
	case WORD10_TX_CHAR:
	case WORD10_TX_SHORT:
	case WORD10_TX_INT:
	case WORD10_TX_LONG:
	case WORD10_TX_LLONG:
	    if ((tx = &txtab[t->te_itx])->tx_bits < t->te_bits) {
		printf("Type error: %s wants %d bits but only has %d\n",
		       t->te_name, t->te_bits, tx->tx_bits);
		++nerrs;
	    } else if (swverbose)
		printf("Type %s is \"%s\" with %d bits\n",
		       t->te_name, tx->tx_type, tx->tx_bits);

	    break;
	default:
	    printf("Type error: %s has unknown WORD10_TX %d\n",
		   t->te_name, t->te_itx);
	    nerrs++;
	    continue;
	}
    }

    /* Extra stuff */
#ifndef WORD10_INT32_EXACT
    printf("WORD10_INT32_EXACT is undefined!\n");
    ++nerrs;
#else
    if ((WORD10_INT32_EXACT==0) == (txtab[WORD10_ITX32].tx_bits == 32)) {
	printf("WORD10_INT32_EXACT incorrect: %s but has %d bits\n",
	       (WORD10_INT32_EXACT ? "TRUE" : "FALSE"),
	       txtab[WORD10_ITX32].tx_bits);
	++nerrs;
    } else if (swverbose)
	printf("WORD10_INT32_EXACT correct: %s (%d bits)\n",
	       (WORD10_INT32_EXACT ? "TRUE" : "FALSE"),
	       txtab[WORD10_ITX32].tx_bits);
#endif

#ifdef WORD10_INT
# ifndef WORD10_INT36_EXACT
    printf("WORD10_INT36_EXACT is undefined!\n");
    ++nerrs;
# else
    if ((WORD10_INT36_EXACT==0) == (txtab[WORD10_ITX36].tx_bits == 36)) {
	printf("WORD10_INT36_EXACT incorrect: %s but has %d bits\n",
	       (WORD10_INT36_EXACT ? "TRUE" : "FALSE"),
	       txtab[WORD10_ITX36].tx_bits);
	++nerrs;
    } else if (swverbose)
	printf("WORD10_INT36_EXACT correct: %s (%d bits)\n",
	       (WORD10_INT36_EXACT ? "TRUE" : "FALSE"),
	       txtab[WORD10_ITX36].tx_bits);
# endif
#endif

    if (swprint) {
	printf("Type defs test %s\n",
		(nerrs ? "Failed" : "Passed"));
    }
    return nerrs;
}

/* Test conversions to and from w10_t and native integer types.
 */

/* TEST THE INTEGER ARGS!
   do char and uchar, then INTMAX and UINTMAX.
   do all bit positions, plus all-ones and all masks.
 */

/* Easiest to test 36-bit stuff first, if possible */

/* General algorithm for all patterns:
   Store pattern, fetch via LH/RH to ensure safe, then fetch
   pattern and compare.
 */

#ifdef WORD10_INT

#define SIGN36 (USUFF(1)<<35)

int
test36(void)
{
    register int i, j;
    unsigned char *ucp;
    signed char sc;
    unsigned char uc;
    INTMAX sm;
    UINTMAX um;
    w10_t w;
    int nerrs = 0;

    /* Test zero set */
    memset(&w, -1, sizeof(w));	/* Fill with 1-bits */
    W10_U36SET(w, 0);
    if (W10_LH(w) && W10_RH(w)) {
	printf("test36: Word clear failed\n");
	++nerrs;
    }
    for (i = 0, ucp = (unsigned char *)&w; i < sizeof(w); ++i) {
	if (*ucp) {
	    printf("test36: Excess bits still set after clear\n");
	    ++nerrs;
	    break;
	}
    }

#define PATTEST(desc,varsign,var,init,iter,setsign,getsign) \
	for (i = 0, var = init; var; iter, ++i) { \
	    UINTMAX umax, vown;		\
	    if (setsign)		\
		W10_S36SET(w, var);	\
	    else W10_U36SET(w, var);	\
	    if (getsign)		\
		umax = (INTMAX)W10_S36(w);	\
	    else umax = W10_U36(w);	\
	/* Do own conversion for check */\
	    if (varsign)		\
		vown = ((INTMAX)var)&MASK36; \
	    else				\
		vown = ((UINTMAX)var)&MASK36;	\
	    if (getsign && (vown & SIGN36))	\
		vown |= ~(UINTMAX)MASK36;	\
	    if (vown != umax) {		\
		printf("test36: %s %d: %lo,,%lo != %lo,,%lo\n", desc, i, \
		       WXPARGS((UINTMAX)vown), WXPARGS(umax)); \
		++nerrs;		\
		break;			\
	    }				\
	}

#define SI 1
#define UI 0

    /* Do 1-bit pattern for un/signed char */
    PATTEST("1-bit uchar U36", UI, uc, 1, uc<<=1, UI, UI)
    PATTEST("1-bit schar U36", SI, sc, 1, sc<<=1, UI, UI)
    PATTEST("1-bit uchar S36", UI, uc, 1, uc<<=1, SI, SI)
    PATTEST("1-bit schar S36", SI, sc, 1, sc<<=1, SI, SI)
    /* Do 1-bit pattern for un/signed INTMAX */
    PATTEST("1-bit umax U36", UI, um, 1, um<<=1, UI, UI)
    PATTEST("1-bit smax U36", SI, sm, 1, sm<<=1, UI, UI)
    PATTEST("1-bit umax S36", UI, um, 1, um<<=1, SI, SI)
    PATTEST("1-bit smax S36", SI, sm, 1, sm<<=1, SI, SI)

    /* Do high-mask pattern for un/signed char */
    PATTEST("himask uchar U36", UI, uc, -1, uc<<=1, UI, UI)
    PATTEST("himask schar U36", SI, sc, -1, sc<<=1, UI, UI)
    PATTEST("himask uchar S36", UI, uc, -1, uc<<=1, SI, SI)
    PATTEST("himask schar S36", SI, sc, -1, sc<<=1, SI, SI)
    /* Do high-mask pattern for un/signed INTMAX */
    PATTEST("himask umax U36",UI, um,USUFF(-1),um<<=1,UI,UI)
    PATTEST("himask smax U36",SI, sm, SUFF(-1),sm<<=1,UI,UI)
    PATTEST("himask umax S36",UI, um,USUFF(-1),um<<=1,SI,SI)
    PATTEST("himask smax S36",SI, sm, SUFF(-1),sm<<=1,SI,SI)

#define UCI CHAR_MAX
#define MCI INTMAX_SMAX

    /* Do low-mask pattern for un/signed char */
    PATTEST("lomask uchar U36",UI, uc,UCI,uc>>=1,UI,UI)
    PATTEST("lomask schar U36",SI, sc,UCI,sc>>=1,UI,UI)
    PATTEST("lomask uchar S36",UI, uc,UCI,uc>>=1,SI,SI)
    PATTEST("lomask schar S36",SI, sc,UCI,sc>>=1,SI,SI)
    /* Do low-mask pattern for un/signed INTMAX */
    PATTEST("lomask umax U36",UI, um,MCI,um>>=1,UI,UI)
    PATTEST("lomask smax U36",SI, sm,MCI,sm>>=1,UI,UI)
    PATTEST("lomask umax S36",UI, um,MCI,um>>=1,SI,SI)
    PATTEST("lomask smax S36",SI, sm,MCI,sm>>=1,SI,SI)

#undef UCI
#undef MCI
#undef SI
#undef UI

    if (swprint) {
	printf("36-bit conversions test %s\n",
		(nerrs ? "Failed" : "Passed"));
    }
    return nerrs;
}
#endif /* WORD10_INT */


#if 0
 define H10SIGN32 (1<<(17-4))		/* LH sign bit of a 32-bit value */
 define W10SIGN32 (((uint32)1)<<31)	/* Word sign bit of a 32-bit value */
#endif

#define SIGN32 (USUFF(1)<<31)

int
test32(void)
{
    register int i, j;
    unsigned char *ucp;
    signed char sc;
    unsigned char uc;
    INTMAX sm;
    UINTMAX um;
    w10_t w;
    int nerrs = 0;

    /* 32-bit special: verify macros are correct */
    if (H10SIGN32 != 020000) {		/* LH sign bit of a 32-bit value */
	printf("H10SIGN32 incorrect: %lo\n", (long)H10SIGN32);
	++nerrs;
    }
    if (W10SIGN32 != 020000000000L) {	/* Word sign bit of a 32-bit value */
	printf("W10SIGN32 incorrect: %lo\n", (long)W10SIGN32);
	++nerrs;
    }

    /* Test zero set */
    memset(&w, -1, sizeof(w));	/* Fill with 1-bits */
    W10_U32SET(w, 0);
    if (W10_LH(w) && W10_RH(w)) {
	printf("test32: Word clear failed\n");
	++nerrs;
    }
    for (i = 0, ucp = (unsigned char *)&w; i < sizeof(w); ++i) {
	if (*ucp) {
	    printf("test32: Excess bits still set after clear\n");
	    ++nerrs;
	    break;
	}
    }

#undef PATTEST
#define PATTEST(desc,varsign,var,init,iter,setsign,getsign) \
    for (i = 0, var = init; var; iter, ++i) { \
	UINTMAX umax, vown;		\
	uint32 mylh, myrh;		\
	if (setsign) {			\
	    W10_S32SET(w, var);		\
	    myrh =  (((int32)var) & MASK18); \
	    mylh = ((((int32)var)>>18) & MASK18); \
	    if (((int32)var) & SIGN32)	\
		mylh |= 0760000;	\
	} else {			\
	    W10_U32SET(w, var);		\
	    myrh =  (((uint32)var) & MASK18); \
	    mylh = ((((uint32)var)>>18) & MASK18); \
	}				\
	/* Verify word rep is correct */\
	if (W10_LH(w) != mylh || W10_RH(w) != myrh) { \
	    printf("test32: %s %d: wd rep differs: %lo,,%lo != %lo,,%lo\n", \
		   desc, i, (long)W10_LH(w), (long)W10_RH(w), \
		   (long)mylh, (long)myrh); \
	    ++nerrs;			\
	    break;			\
	}				\
	if (getsign)			\
	    umax = (INTMAX)W10_S32(w);	\
	else umax = W10_U32(w);	\
    /* Do own conversion for check */	\
	if (varsign)			\
	    vown = ((INTMAX)var)&MASK32; \
	else				\
	    vown = ((UINTMAX)var)&MASK32;	\
	if (getsign && (vown & SIGN32))	\
	    vown |= ~(UINTMAX)MASK32;	\
	if (vown != umax) {		\
	    printf("test32: %s %d: %lo,,%lo != %lo,,%lo\n", desc, i, \
		   WXPARGS((UINTMAX)vown), WXPARGS(umax)); \
	    ++nerrs;			\
	    break;			\
	}				\
    }

#define SI 1
#define UI 0

    /* Do 1-bit pattern for un/signed char */
    PATTEST("1-bit uchar U32", UI, uc, 1, uc<<=1, UI, UI)
    PATTEST("1-bit schar U32", SI, sc, 1, sc<<=1, UI, UI)
    PATTEST("1-bit uchar S32", UI, uc, 1, uc<<=1, SI, SI)
    PATTEST("1-bit schar S32", SI, sc, 1, sc<<=1, SI, SI)
    /* Do 1-bit pattern for un/signed INTMAX */
    PATTEST("1-bit umax U32", UI, um, 1, um<<=1, UI, UI)
    PATTEST("1-bit smax U32", SI, sm, 1, sm<<=1, UI, UI)
    PATTEST("1-bit umax S32", UI, um, 1, um<<=1, SI, SI)
    PATTEST("1-bit smax S32", SI, sm, 1, sm<<=1, SI, SI)

    /* Do high-mask pattern for un/signed char */
    PATTEST("himask uchar U32", UI, uc, -1, uc<<=1, UI, UI)
    PATTEST("himask schar U32", SI, sc, -1, sc<<=1, UI, UI)
    PATTEST("himask uchar S32", UI, uc, -1, uc<<=1, SI, SI)
    PATTEST("himask schar S32", SI, sc, -1, sc<<=1, SI, SI)
    /* Do high-mask pattern for un/signed INTMAX */
    PATTEST("himask umax U32",UI, um,USUFF(-1),um<<=1,UI,UI)
    PATTEST("himask smax U32",SI, sm, SUFF(-1),sm<<=1,UI,UI)
    PATTEST("himask umax S32",UI, um,USUFF(-1),um<<=1,SI,SI)
    PATTEST("himask smax S32",SI, sm, SUFF(-1),sm<<=1,SI,SI)

#define UCI CHAR_MAX
#define MCI INTMAX_SMAX

    /* Do low-mask pattern for un/signed char */
    PATTEST("lomask uchar U32",UI, uc,UCI,uc>>=1,UI,UI)
    PATTEST("lomask schar U32",SI, sc,UCI,sc>>=1,UI,UI)
    PATTEST("lomask uchar S32",UI, uc,UCI,uc>>=1,SI,SI)
    PATTEST("lomask schar S32",SI, sc,UCI,sc>>=1,SI,SI)
    /* Do low-mask pattern for un/signed INTMAX */
    PATTEST("lomask umax U32",UI, um,MCI,um>>=1,UI,UI)
    PATTEST("lomask smax U32",SI, sm,MCI,sm>>=1,UI,UI)
    PATTEST("lomask umax S32",UI, um,MCI,um>>=1,SI,SI)
    PATTEST("lomask smax S32",SI, sm,MCI,sm>>=1,SI,SI)

#undef UCI
#undef MCI
#undef SI
#undef UI

    if (swprint) {
	printf("32-bit conversions test %s\n",
		(nerrs ? "Failed" : "Passed"));
    }
    return nerrs;
}


/* Determine and display word format being used */


/* Word format tables */

#define B0 36
unsigned char wfmt_dbw8[] = {
    0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0, B0,  1,  2,  3,
    4,  5,  6,  7,  8,  9,  10, 11,
    12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 33, 34, 35
};
unsigned char wfmt_dlw8[] = {
    28, 29, 30, 31, 32, 33, 34, 35,
    20, 21, 22, 23, 24, 25, 26, 27,
    12, 13, 14, 15, 16, 17, 18, 19,
    4,  5,  6,  7,  8,  9,  10, 11,
    0,  0,  0,  0, B0,  1,  2,  3,
    0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0
};
unsigned char wfmt_dbh4[] = {
    0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0, B0,  1,
    2,  3,  4,  5,  6,  7,  8,  9,
    10, 11, 12, 13, 14, 15, 16, 17,
    0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  18, 19,
    20, 21, 22, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 33, 34, 35
};
unsigned char wfmt_dlh4[] = {
    28, 29, 30, 31, 32, 33, 34, 35,
    20, 21, 22, 23, 24, 25, 26, 27,
    0,  0,  0,  0,  0,  0,  18, 19,
    0,  0,  0,  0,  0,  0,  0,  0,
    10, 11, 12, 13, 14, 15, 16, 17,
    2,  3,  4,  5,  6,  7,  8,  9,
    0,  0,  0,  0,  0,  0, B0,  1,
    0,  0,  0,  0,  0,  0,  0,  0
};

struct {
    char *wfname;
    int wflen;
    unsigned char *wfmt;
} wfmts[] = {
    { "DBW8", sizeof(wfmt_dbw8), wfmt_dbw8 },
    { "DLW8", sizeof(wfmt_dlw8), wfmt_dlw8 },
    { "DBH4", sizeof(wfmt_dbh4), wfmt_dbh4 },
    { "DLH4", sizeof(wfmt_dlh4), wfmt_dlh4 }
};

int
testfmt(void)
{
    int nerrs = 0;
    w10_t w;
    unsigned char *wcp = (unsigned char *) &w;
    int bsize = txtab[WORD10_TX_CHAR].tx_bits;
    int wsize = sizeof(w10_t);
    int barrsize = bsize * wsize;
    unsigned char *bitarr = (unsigned char *)calloc(1, (size_t)barrsize);
    register int i, b, bit;

    if (!barrsize || !bitarr) {
	printf("testfmt setup failed!\n");
	return 1;
    }

    /* First set each bit in word, then see where it's located in terms
       of byte & bit offset.
    */
    for (bit = 0; bit < 36; ++bit) {
	memset((void *)&w, 0, sizeof(w));	/* Clear word */

	/* Set single bit in word */
	i = 35-bit;
	if (i < 18)
	    W10_RHSET(w, (1L << i));
	else
	    W10_LHSET(w, (1L << (i-18)));

	/* Find bit in word via byte access, and make note in bitarr
	   table of its location.
	*/
	for (i = 0; i < wsize; ++i)
	    for (b = 0; b < bsize; ++b) {
		if (wcp[i] & (1 << ((bsize-1)-b))) {
		    /* Found it! */
		    if (bitarr[(i*bsize)+b]) {
			printf("testfmt conflict! i=%d b=%d old=%d new=%d\n",
			       i, b, bitarr[(i*bsize)+b] % 36, bit);
			++nerrs;
		    }
		    /* Store 0 as B0 */
		    bitarr[(i*bsize)+b] = (bit ? bit : B0);
		}
	    }
    }

    /* All 36 bits located, now attempt to identify the format
       and perhaps display it.
    */
    for (i = (sizeof(wfmts)/sizeof(wfmts[0])); --i >= 0;) {
	if ((wfmts[i].wflen == barrsize) 
	    && (memcmp(wfmts[i].wfmt, bitarr, barrsize) == 0))
	    break;
    }
    if (i < 0) {
	++nerrs;
	printf("testfmt cannot identify internal format\n");
    } else
	printf("Internal w10_t format: %s\n", wfmts[i].wfname);

    if ((i < 0) || swverbose) {
	printf("Internal w10_t format: %d bytes, %d bits/byte\n",
	       wsize, bsize);
	for (i = 0; i < wsize; ++i) {
	    printf("  %d: ", i);
	    for (b = 0; b < bsize; ++b) {
		bit = bitarr[(i*bsize)+b];
		if (bit)
		    printf(" %2d", (bit==B0) ? 0 : bit);
		else printf("  z");
	    }
	    printf("\n");
	}
    }

    free(bitarr);
    if (swprint) {
	printf("Format test %s\n",
		(nerrs ? "Failed" : "Passed"));
    }
    return nerrs;
}

#undef B0


#if 0 /* Stuff noted for possible future testing */


/* Determine representation model in effect */

 ifndef WORD10_USEHWD
 ifndef WORD10_USENAT
 ifndef WORD10_USEINT
 ifndef WORD10_USEHUN
 ifndef WORD10_USEGCCSPARC

 ifndef WORD10_SMEMBIGEND
 ifndef WORD10_BITFBIGEND
 ifndef WORD10_BITF		/* Unless explicitly requested, */


/* Fundamental PDP-10 word definitions. */

/* Halfword - fundamental PDP-10 storage unit */
 define H10BITS 18		/* Number of bits in PDP-10 halfword */
 define H10OVFL ((uint19)1<<H10BITS)	 /* Halfword overflow bit */
 define H10SIGN ((uint18)1<<(H10BITS-1)) /* Halfword sign bit */
 define H10MASK (H10OVFL-1)	/* Halfword Mask (all-ones value) */
 define H10ONES H10MASK
 define H10MAGS	(H10SIGN-1)	/* Halfword magnitude bits (all but sign) */
 define W10BITS (2*H10BITS)	/* Number of bits in PDP-10 fullword */

/* Word defs */
   define WORD10_MODEL "string"
   WORD10_INT	set to appropriate WORD10_INTxx if have a w10int_t def
   WORD10_STRUCT	defined (no value) if have a w10struct_t def

/* Typedefs */
w10int_t, w10uint_t  if WORD10_INT defined
struct w10struct_t if WORD10_STRUCT defined

w10_t	/* Some combo of above */

/* Define remaining PDP-10 data types */
typedef /*signed*/ WORD10_INT18 h10_t;	/* PDP-10 Half-Word integer value */
typedef struct { w10_t w[2]; } dw10_t;	/* PDP-10 Double-Word value */
typedef struct {dw10_t d[2]; } qw10_t;	/* PDP-10 Quad-Word value */

/* Word-format integer masks
**	Not necessarily right-justified!!
*/
 if WORD10_USENAT || WORD10_USEINT || WORD10_USEGCCSPARC || WORD10_USEHUN
   /* Special bit arrangement.  Assumes halfwords defined with INT18 */
   define W10HISHFT	(sizeof(WORD10_INT18)*CHAR_BIT)
   define W10MASK	((((w10uint_t)H10MASK)<<W10HISHFT)|H10MASK)
   define W10SIGN	(((w10uint_t)1)<<(W10HISHFT+H10BITS-1))
   define W10MAGS	((((w10uint_t)(H10MASK>>1))<<W10HISHFT)|H10MASK)
 else /* WORD10_USEHWD */
   define W10HISHFT	--error--
   define W10MASK	--error--
   define W10SIGN	--error--
   define W10MAGS	--error--
 endif

/* Note IMM and VAR may not be legal.  leave undefined instead of --error--? */
   define W10_XIMM(l,r)	OPTIONAL
   define W10_XINIT(l,r)
   define W10_XSET(w,l,r)
   define W10_VAR(w)	OPTIONAL
   define W10_LHVAR(w)	OPTIONAL
   define W10_RHVAR(w)	OPTIONAL
   define W10_LH(w)	w10_lh(w)
   define W10_RH(w)	w10_rh(w)
   define W10_LHSET(w,v)	((w) = w10_lhset(w,v))
   define W10_RHSET(w,v)	((w) = w10_rhset(w,v))
   define W10P_LH(p)	(((w10union_t *)(p))->uwd.lh)
   define W10P_RH(p)	(((w10union_t *)(p))->uwd.rh)
   define W10P_LHSET(p,v)	(((w10union_t *)(p))->uwd.lh = (v))
   define W10P_RHSET(p,v)	(((w10union_t *)(p))->uwd.rh = (v))
   define W10P_XSET(p,l,r) (*(p) = w10_ximm(l,r))

/* Check these?  At least the initializers. */
 define DW10_HI(d) ((d).w[0])
 define DW10_LO(d) ((d).w[1])
 define DW10_SET(d,hi,lo) ((d).w[0] = (hi), (d).w[1] = (lo))
/*  define DW10_INIT(hi,lo) {{hi, lo}} */
 define DW10_XINIT(h0,h1,h2,h3) {{W10_XINIT(h0,h1),W10_XINIT(h2,h3)}}

 define QW10_XINIT(h0,h1,h2,h3,l0,l1,l2,l3) \
		{{DW10_XINIT(h0,h1,h2,h3), DW10_XINIT(l0,l1,l2,l3)}}

#endif /* 0 */
