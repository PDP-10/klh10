/* PRMSTR.H - Parameter & String Parsing support definitions
*/
/* $Id: prmstr.h,v 2.4 2001/11/10 21:28:59 klh Exp $
*/
/*  Copyright © 1994, 2001 Kenneth L. Harrenstien
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
 * $Log: prmstr.h,v $
 * Revision 2.4  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#ifndef PRMSTR_INCLUDED
#define PRMSTR_INCLUDED 1

#ifdef RCSID
 RCSID(prmstr_h,"$Id: prmstr.h,v 2.4 2001/11/10 21:28:59 klh Exp $")
#endif

#include "word10.h"

/* Canonical C true/false values */
#ifndef TRUE
# define TRUE 1
#endif
#ifndef FALSE
# define FALSE 0
#endif

/* Commonly needed type (at one time "void" was unknown) */
typedef void *voidp_t;

#define PRMQUOTCHAR '\\'

/* Interaction-oriented variable=value parsing stuff */
/* Primarily for use by main command parser */

union prmval {
    int vi;
    long vl;
    char *vs;
    w10_t vw;
};

struct prmvar_s;		/* Forward tag */
struct prmvcx_s {		/* Parameter variable context */
    struct prmvar_s *prmvcx_var;	/* Pointer to var being processed */
    union prmval prmvcx_val;	/* Current value */
    char *prmvcx_str;		/* Actual parameter string */
    FILE *prmvcx_of;		/* Normal output if any */
    FILE *prmvcx_ef;		/* Error output if any */
};

#define PRMVAR(name, help, typf, loc, set, sho) \
		{ name, help, typf, (voidp_t)(loc), set, sho }

struct prmvar_s {
    char *prmv_name;	/* Variable name */
    char *prmv_help;	/* Minimal help text */
    int   prmv_typf;	/* Type and flags */
    voidp_t prmv_loc;	/* Generic pointer */
    int  (*prmv_set)(struct prmvcx_s *);
    int  (*prmv_sho)(struct prmvcx_s *);
};

#define PRMV_TYPES \
	prmvdef(PRMVT_NULL=0, "NULL"),	\
	prmvdef(PRMVT_BOO, "boolean"),	\
	prmvdef(PRMVT_DEC, "decimal"),	\
	prmvdef(PRMVT_OCT, "octal"),	\
	prmvdef(PRMVT_STR, "string"),	\
	prmvdef(PRMVT_WRD, "10word"),	\
	prmvdef(PRMVT_6, "<unknown6>"),	\
	prmvdef(PRMVT_7, "<unknown7>"), \
	prmvdef(PRMVT_N, NULL)

enum {		/* Define enums for variable types */
# define prmvdef(i,s) i
	PRMV_TYPES
# undef prmvdef
};

#define PRMVF_TYPE	07	/* Mask for type number (PRMVT_ value) */
#define PRMVF_DYNS	0100	/* String is dynamically allocated */
				/* (This could be problematical if there
				** are aliases for a DYN var's name, since
				** the flag is updated in only one place)
				** (Could invent PRMVT_ALIAS w/ptr?)
				*/

extern int prm_varset(char **, struct prmvar_s *, FILE *, FILE *);
extern int prm_varlineset(char *, struct prmvar_s *, FILE *, FILE *);
extern int prm_varshow(struct prmvar_s *, FILE *);
extern int prmvp_set(struct prmvcx_s *);


/* Key lookup stuff */

struct prmkey_s {
	char *prmk_key;
	char *prmk_p;	/* Actually a (void *) */
};

#define S_KEYSBEGIN(name) struct prmkey_s name[] = {
#define S_KEYDEF(key,nod)  { key, (char *)(nod) },
#define S_KEYSEND	   { 0, 0 } };

extern void *s_fkeylookup(char *cp, voidp_t tab, size_t entsiz);
extern int   s_keylookup (char *cp, voidp_t tab, size_t entsiz,
			  voidp_t *k1, voidp_t *k2);
extern int   s_xkeylookup(char *cp, voidp_t tab, size_t entsiz,
			  voidp_t *k1, voidp_t *k2, int *x1, int *x2);

/* Random useful string stuff */

extern int s_tokenize(char **, int, char **, size_t *, char **, size_t *);

extern char *s_1token(char **, size_t *, char **, size_t *);
extern size_t s_eztoken(char *, size_t , char **);

extern int s_tobool(char *, int *);
extern int s_tonum(char *, long *);
extern int s_todnum(char *, long *);
extern int s_towd(char *, w10_t *);
extern char *s_dup(char *);
extern int s_match(char *, char *);

/* New parameter parsing stuff */

/* Iterative scheme to process string of the form
	<var> <var>=<val> ...

    foo()
    {
	struct prmstate_s prm;
	char workbuff[<size>];
	...
	prm_init(&prm, workbuff, sizeof(workbuff),
		inpstr, strlen(inpstr),
		keytab, keysiz);
	...
	for (;;) switch (prm_next(&prm)) {
	    case PRMK_DONE:
	    case PRMK_NONE: // prm_name & prm_val set
	    case PRMK_AMBI: // Ditto, plus prm_matches, prm_idx, prm_idx2
	    case <other>:   // Specific unique match case
	    default:	    // Handle matches not supported
	}
    }
*/




#define PRMK_NONE (-1)	/* No match */
#define PRMK_DONE (-2)	/* Input string done */
#define PRMK_AMBI (-3)	/* Token ambiguous */

struct prmstate_s {
	/* Vars that must be initialized */
	char *prm_icp;	/* Input string */
	size_t prm_iln;	/* Input length */
	char *prm_bcp;	/* Buffer pointer */
	size_t prm_bln;	/* Buffer length */
	size_t prm_keysiz;	/* Size of each keyword table entry */
	struct prmkey_s *
		prm_keytab;	/* Pointer to keyword table */

	/* Vars returned by each call to prm_next() */
	char *prm_name;	/* Parameter name if one */
	char *prm_val;	/* Parameter value if one */
	int prm_nhits;	/* # of matches */
	int prm_idx;	/* Index of 1st match */
	int prm_idx2;	/* Index of 2nd match */
	struct prmkey_s *
		prm_key,	/* Pointers to matching entries */
		prm_key2;
};

#define prm_init(prm, buff, blen, inpstr, inplen, keytab, keysiz) ( \
	(prm)->prm_bcp = (buff),	\
	(prm)->prm_bln = (blen),	\
	(prm)->prm_icp = (inpstr),	\
	(prm)->prm_iln = (inplen),	\
	(prm)->prm_keytab = (struct prmkey_s *)(keytab),	\
	(prm)->prm_keysiz = (keysiz) )

extern int prm_next(struct prmstate_s *);

#endif /* ifndef PRMSTR_INCLUDED */
