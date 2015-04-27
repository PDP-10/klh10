/* PRMSTR.C - Parameter & String Parsing support
*/
/* $Id: prmstr.c,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: prmstr.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/* These routines are intended to be used by miscellaneous independent
** device programs or modules as well as the KLH10 main command parser,
** hence must avoid any dependencies on other code.
*/

#include <stdio.h>
#include <stdlib.h>	/* Malloc and friends */
#include <string.h>
#include <ctype.h>

#include "rcsid.h"
#include "prmstr.h"

#ifdef RCSID
 RCSID(prmstr_c,"$Id: prmstr.c,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

static int prmvalscan(char **, int, union prmval *, FILE *);
static int prmvalset(struct prmvar_s *, union prmval *, FILE *, FILE *);
static int s_tol(char *, char **, long *, int);

static char *prmvtypnam[] = {
# define prmvdef(en, str) str
	PRMV_TYPES
# undef prmvdef
};


/* PRM_VARSET - Parse and set one parameter variable according to table.
**	Returns TRUE unless parsing error.
**	Mungs the input string!
*/
int prm_varset(char **acp,
	       struct prmvar_s *tab,
	       FILE *of,
	       FILE *ef)
{
    register char *cp = *acp;
    int res;
    struct prmvar_s *p1, *p2;
    struct prmvcx_s px;

    if (!(cp = strchr(cp, '='))) {
	if (ef)
	    fprintf(ef, "Bad syntax for \"%s\", must be <var>=<value>\n", cp);
	return FALSE;
    }

    *cp++ = '\0';	/* Mung param string, separate name and val */

    res = s_keylookup(*acp, (voidp_t)tab, sizeof(*p1),
		      (voidp_t *)&p1, (voidp_t *)&p2);
    if (!p1) {
	if (ef)
	    fprintf(ef, "Unknown variable: \"%s\"\n", *acp);
	return FALSE;
    }
    if (p2) {
	if (ef)
	    fprintf(ef, "Ambiguous variable: \"%s\", \"%s\"%s\n",
		p1->prmv_name, p2->prmv_name, res > 2 ? ", ..." : "");
	return FALSE;
    }

    *acp = cp;
    if (!prmvalscan(acp, p1->prmv_typf, &px.prmvcx_val, ef)) {
	if (ef)
	    fprintf(ef, "Bad syntax for \"%s\", expecting %s value: \"%s\"?\n",
		p1->prmv_name, prmvtypnam[p1->prmv_typf & PRMVF_TYPE], cp);
	return FALSE;
    }

    /* Value parsed into temporary holder, now set up rest of context and
       invoke appropriate set function!
    */
    px.prmvcx_var = p1;		/* Note variable */
    px.prmvcx_str = cp;		/* and original parameter string */
    px.prmvcx_of = of;		/* Set up I/O in context */
    if (!(px.prmvcx_ef = ef))
	px.prmvcx_ef = of;

    if (p1->prmv_set) {			/* Has its own set function? */
	return (*(p1->prmv_set))(&px);	/* Do it! */
    }
    return prmvp_set(&px);		/* Do standard param set */
}

/* PRM_VARLINESET - Parse and set entire line of variables/parameters
**	Returns TRUE unless parsing error
**	Mungs the input string!
*/
int prm_varlineset(char *s,
		   struct prmvar_s *tab,
		   FILE *of,
		   FILE *ef)
{
#define NTOKS 100
    char tokbuf[300+NTOKS];		/* Big work buffer */
    char *tokarr[NTOKS];		/* Lots of tokens */
    char **tkp;
    char *tcp = tokbuf;
    size_t tcnt = sizeof(tokbuf);
    size_t slen = strlen(s);
    int n;

    n = s_tokenize(tokarr, NTOKS-1, &tcp, &tcnt, &s, &slen);
    tokarr[n] = NULL;
    for (tkp = tokarr; *tkp; ++tkp) {
	if (!prm_varset(tkp, tab, of, ef)) {
	    if (ef && tkp[1]) {		/* Error?  See if more left */
		fprintf(ef, "Parsing aborted.\n");
	    }
	    return FALSE;
	}
    }
    return TRUE;
#undef NTOKS
}


/* PRMVALSCAN - Parse a value, given type to expect.
**	Not yet completely consistent with respect to behavior of
**	updated CP - still expects pre-tokenized input ending with NUL.
**	"ef" is not really used at the moment but could be later.
*/
static int
prmvalscan(char **acp,
	   int typf,
	   union prmval *vp,
	   FILE *ef)
{
    register char *cp = *acp;
    char *endp;
    long ltmp;
    int ret;

    if (!cp) return 0;
    switch (typf & PRMVF_TYPE) {
    case PRMVT_BOO:
	vp->vi = (int)strtol(cp, &endp, 10);		/* Try as number */
	if (cp != endp) {
	    *acp = endp;
	    if (*endp == '\0')		/* See if gobbled entire string */
		return TRUE;
	    return FALSE;		/* Looked like number but wasn't? */
	}
	/* Later do full keyword scan - for now use ugliness */
	else if (s_match(cp, "on" )==2) vp->vi = TRUE;
	else if (s_match(cp, "of" )==2) vp->vi = 0;
	else if (s_match(cp, "off")==2) vp->vi = 0;
	else {
	    return FALSE;		/* Not a known boolean keyword */
	}
	*acp += strlen(cp);
	return TRUE;
    case PRMVT_DEC:
	vp->vi = (int)strtol(cp, &endp, 10);
	if (cp == endp) {
	    return FALSE;		/* Not a number at all */
	}
	if (*endp == '.') ++endp;
	*acp = endp;
	if (*endp != '\0') {
	    return FALSE;		/* Only partially a number */
	}
	return TRUE;
    case PRMVT_OCT:
	ret = s_tonum(cp, &ltmp);
	if (ret) vp->vi = (int)ltmp;
	return ret;
    case PRMVT_WRD:
	ret = s_towd(cp, &vp->vw);
	return ret;
    case PRMVT_STR:
	vp->vs = cp;
	*acp += strlen(cp);
	return TRUE;
    }
    return FALSE;			/* Unknown value type */
}

/* Standard variable set, given context
 */
int
prmvp_set(struct prmvcx_s *px)
{
    struct prmvar_s *p = px->prmvcx_var;
    union prmval *vp = &px->prmvcx_val;
    FILE *of = px->prmvcx_of;
    FILE *ef = px->prmvcx_ef;

    if (of)
	fprintf(of,"   %s: ", p->prmv_name);
    switch (p->prmv_typf & PRMVF_TYPE) {
    case PRMVT_BOO:
	printf("%s  =>  %s\n", *(int *)(p->prmv_loc) ? "On" : "Off",
			vp->vi ? "On" : "Off");
	*(int *)(p->prmv_loc) = vp->vi;
	break;
    case PRMVT_OCT:
	printf("%#.0o  =>  %#.0o\n", *(int *)(p->prmv_loc), vp->vi);
	*(int *)(p->prmv_loc) = vp->vi;
	break;
    case PRMVT_DEC:
	printf("%d.  =>  %d.\n", *(int *)(p->prmv_loc), vp->vi);
	*(int *)(p->prmv_loc) = vp->vi;
	break;
    case PRMVT_WRD:
	printf("%lo,,%lo  =>  %lo,,%lo\n",
		(long)LHGET(*(w10_t *)(p->prmv_loc)),
		(long)RHGET(*(w10_t *)(p->prmv_loc)),
		(long)LHGET(vp->vw), (long)RHGET(vp->vw));
	*(w10_t *)(p->prmv_loc) = vp->vw;
	break;
    case PRMVT_STR:
	if (*(char **)(p->prmv_loc)) printf("\"%s\"", *(char **)(p->prmv_loc));
	else printf("NULL");
	if (vp->vs) printf("  =>  \"%s\"\n", vp->vs);
	else printf("NULL\n");

	{	/* Ensure have new string before flushing old one! */
	    char *tmp = NULL;

	    if (vp->vs && !(tmp = s_dup(vp->vs))) {
		printf(" Error: malloc failed for new string, var not set!\n");
		return FALSE;
	    }
	    /* OK, free up old if necessary */
	    if ((p->prmv_typf & PRMVF_DYNS) && *(char **)(p->prmv_loc))
		free(*(char **)(p->prmv_loc));
	    *(char **)(p->prmv_loc) = tmp;
	}
	p->prmv_typf |= PRMVF_DYNS;
	break;
    }

    return TRUE;
}


int
prm_varshow(register struct prmvar_s *p,
	    FILE *of)
{
    int res = TRUE;

    fprintf(of, "  %-10s= ", p->prmv_name);
    switch (p->prmv_typf & PRMVF_TYPE) {
    case PRMVT_BOO:
	fprintf(of, "%s", *(int *)p->prmv_loc ? "On" : "Off");
	break;
    case PRMVT_DEC:
	fprintf(of, "%d.", *(int *)p->prmv_loc);
	break;
    case PRMVT_OCT:
	fprintf(of, ((*(int *)p->prmv_loc) ? "%#.0o" : "0"),
			*(int *)p->prmv_loc);
	break;
    case PRMVT_STR:
	fprintf(of, "\"%s\"", *(char **)p->prmv_loc);
	break;
    case PRMVT_WRD:
      {
	w10_t w = *(w10_t *)p->prmv_loc;

	if (LHGET(w)) fprintf(of, "%lo,,", (long)LHGET(w));
	fprintf(of, "%lo", (long)RHGET(w));
      }
	break;
    default:
	fprintf(of,"<Unknown variable type %#.0o\?\?>", p->prmv_typf);
	res = FALSE;
    }
    if (p->prmv_help)
	fprintf(of, " \t%s", p->prmv_help);
    fprintf(of, "\n");
    return res;
}

#if 0

static char *
wdscan(char **acp)
{
    register char *cp = *acp, *s;

    if (isspace(*cp))		/* Skip to word */
        while (isspace(*++cp));
    s = cp;
    switch (*s) {
	case 0:	return NULL;		/* Nothing left */
	default:			/* Normal word */
	    while (!isspace(*++s) && *s);
	    if (*s) *s++ = 0;		/* Tie off word */
	    break;
    }
    *acp = s;
    return cp;
}

#endif

/* Like strtol() but allows trailing '.' to force decimal.
**	base 8 - default is octal unless '.' trails number
**	base 10 - default is decimal
*/
static int
s_tol(register char *cp,
      register char **aep,
      long *ares,
      int base)
{
    register char *s;
    int sign = 1;

    if (!cp || !*cp) {
	*aep = cp;
	return 0;
    }
    if (*cp == '-') {
	sign = -1;
	++cp;
    }
    if (*cp == '0' && (cp[1] == 'x' || cp[1] == 'X')) {
	*ares = strtol(cp, aep, 16) * sign;
	return 1;
    }
    for (s = cp; isdigit(*s); ++s);	/* Skip all digits */
    if (*s == '.') {
	*ares = strtol(cp, aep, 10) * sign;	/* Force decimal! */
	if (aep && *aep == s)
	    (*aep)++;			/* Skip over point */
	return 1;

    }

    /* Nothing explicit, use default. */
    *ares = strtol(cp, aep, base) * sign;
    return 1;
}

int
s_tonum(char *cp,
	long *aloc)
{
    *aloc = 0;
    if (!s_tol(cp, &cp, aloc, 8))	/* Default is octal */
	return 0;
    return *cp ? 0 : 1;		/* Fail if anything left in string */
}

int
s_todnum(char *cp,
	 long *aloc)
{
    *aloc = 0;
    if (!s_tol(cp, &cp, aloc, 10))	/* Default is decimal */
	return 0;
    return *cp ? 0 : 1;		/* Fail if anything left in string */
}


/* S_TOWD - Returns 0 if failed, 1 if one value, 2 if two values
**	interpreted as 2 halfwords.
*/
int
s_towd(char *str,
       w10_t *wp)
{
    long num;			/* Known to be at least 32 bits */
    register char *cp;

    if (cp = strchr(str, ',')) {
	*cp++ = 0;
	if (*cp == ',') cp++;
	if (s_tonum(str, &num))
	    LHSET(*wp, num & H10MASK);
	else return 0;
	if (s_tonum(cp, &num))
	    RHSET(*wp, num & H10MASK);
	else return 0;
	return 2;

    } else if (s_tonum(str, &num)) {
	LHSET(*wp, (num>>18)&H10MASK);
	RHSET(*wp, num&H10MASK);
	return 1;
    }
    return 0;
}

static S_KEYSBEGIN(prmbools)
	S_KEYDEF("on",	(char *)1)
	S_KEYDEF("off",	(char *)0)
	S_KEYDEF("yes",	(char *)1)
	S_KEYDEF("no",	(char *)0)
	S_KEYDEF("true",(char *)1)
	S_KEYDEF("false",(char *)0)
S_KEYSEND

int
s_tobool(register char *cp,
	 int *ip)
{
    int kix;
    char *endp;

    *ip = (int)strtol(cp, &endp, 10);		/* Try as number */
    if (cp != endp) {
	if (*endp == '\0')	/* See if gobbled entire string */
	    return TRUE;
	return FALSE;		/* Looked like number but wasn't? */
    }
    /* Not number, do full keyword scan */
    if (s_xkeylookup(cp, prmbools, sizeof(prmbools[0]),
		     (voidp_t *)NULL, (voidp_t *)NULL,
		     &kix, (int *)NULL) == 1) {
	*ip = (prmbools[kix].prmk_p ? 1 : 0);
	return TRUE;
    }
    return FALSE;		/* Not a known boolean keyword */
}

/* PRM_NEXT - Routine to parse next parameter name or name=value pair
**	from input string.
**		<var> <var>=<val> ...
*/
int
prm_next(register struct prmstate_s *p)
{
    register char *cp;
    char *bufp = p->prm_bcp;		/* Use temps so originals not */
    size_t bufcnt = p->prm_bln-1;	/* affected by call to s_1token */

    /* First get next token, update input string pointers */
    p->prm_name = cp = s_1token(&bufp, &bufcnt,
				&p->prm_icp, &p->prm_iln);

    if (!cp)
	return PRMK_DONE;	/* No more tokens, input done! */

    /* Split off value, if any */
    if (p->prm_val = strchr(cp, '='))
	*(p->prm_val)++ = '\0';

    /* Look up name in keyword table */
    switch (p->prm_nhits = s_xkeylookup(cp, p->prm_keytab, p->prm_keysiz,
		(voidp_t *)(&p->prm_key), (voidp_t *)(&p->prm_key2),
		&p->prm_idx, &p->prm_idx2)) {
    case 0:
	/* Lookup failed, pass error down */
	return PRMK_NONE;	/* Set: name, val */

    case 1:
	/* Success, just one match, pass on parameter value if one */
	return p->prm_idx;	/* Set: name, val, key, idx */ 

    default:
	/* Ambiguous, pass on parameter keyword. */
	return PRMK_AMBI;	/* Set: name, val, key, key2, idx, idx2 */
    }
}

/* S_XKEYLOOKUP - core table lookup function.  Works for any table
 *	where the first element of each entry is a keyword char pointer.
 */
int
s_xkeylookup(char *cp,			/* Keyword to look up */
	     register voidp_t keytab,	/* Keyword table */
	     register size_t keysiz,	/* Size of entry in table */
	     voidp_t *key1,
	     voidp_t *key2,		/* First 2 returned keys */
	     int *kix1,
	     int *kix2)			/* Indices of these keys */
{
    register voidp_t k1, k2;
    register char *ts;
    register int kidx, kx1, kx2;
    register int pmatches = 0;

    k1 = k2 = NULL;
    kx1 = kx2 = PRMK_NONE;
    if (*cp) {
      for (kidx = 0; ts = ((struct prmkey_s *)keytab)->prmk_key;
			kidx++,
			keytab = (voidp_t)(((char *)keytab) + keysiz)) {
	switch (s_match(cp, ts)) {
	    case 1:	/* Matched partially, continue but keep first two */
		pmatches++;
		if (!k1) k1 = keytab, kx1 = kidx;
		else if (!k2) k2 = keytab, kx2 = kidx;
		break;
	    case 2:			/* Matched exactly, win now */
		if (key1) *key1 = keytab;
		if (kix1) *kix1 = kidx;
		if (key2) *key2 = NULL;
		if (kix2) *kix2 = PRMK_NONE;
		return 1;
	}
      }
    }
    /* No exact match, return results of full scan */
    if (key1) *key1 = k1;
    if (kix1) *kix1 = kx1;
    if (key2) *key2 = k2;
    if (kix2) *kix2 = kx2;
    return pmatches;
}


int
s_keylookup(char *cp,		/* Keyword to look up */
	    register void *keytab,	/* Keyword table */
	    register size_t keysiz,	/* Size of entry in table */
	    void **k1, void **k2)	/* First 2 returned keys */
{
    return s_xkeylookup(cp, keytab, keysiz, k1, k2, (int *)NULL, (int *)NULL);
}

void *
s_fkeylookup(char *cp,		/* Keyword to look up */
	     register void *keytab,	/* Keyword table */
	     register size_t keysiz)	/* Size of entry in table */
{
    voidp_t key1;

    return ((1 == s_xkeylookup(cp, keytab, keysiz, &key1, (voidp_t *)NULL,
			      (int *)NULL, (int *)NULL))
	    ? key1 : NULL);
}

/* Tokenize a command string.
**	Copies tokens into work buffer, up to given number.
**	Returns # of tokens parsed.
*/
int
s_tokenize(char **arr,
	   int arrlen,
	   char **tcp,
	   size_t *tcnt,
	   char **fcp,
	   size_t *fcnt)
{
    register int n = 0;
    char *cp;

    while (n < arrlen && *tcnt && (cp = s_1token(tcp, tcnt, fcp, fcnt))) {
	arr[n++] = cp;
    }
    return n;
}

/* Parse off a token.
**	Updates all args.
**	source pointer/count = last char read.  May be NUL.
**	dest pointer/count = last char written (NUL always follows it).
**	dest count = 0 if token was truncated.
**	Returns NULL if no token found.
*/
char *
s_1token(char **tcp,
	 size_t *tcnt,
	 char **fcp,
	 size_t *fcnt)
{
    register char *t, *f = *fcp;
    register size_t tct, fct = *fcnt;

    if (isspace(*f)) {		/* Skip to word */
	do --fct;
        while (isspace(*++f));
    }
    if (!*f) {
	*fcp = f;
	*fcnt = fct;
	return NULL;
    }

    t = *tcp;
    tct = *tcnt;
    for (; *f && fct; --fct, f++) {
	switch (*f) {

	/* Break chars should go here.  Maybe pass breakstring? */
	case ' ':		/* Whitespace becomes break */
	case '\t':		/* Will never be first thing in token */

	case '\n':
	case '\r':
	    if (t == *tcp) {	/* If first char of token, keep it */
		/* Nothing yet, so make break char be single-char token */
		if (tct > 1) --tct, *t++ = *f;
		else tct = 0;
	    }
	    break;

	/* Quote char? */
	case PRMQUOTCHAR:
	    ++f;			/* Point to next char */
	    if (--fct <= 0)		/* Ugh, none left, store nothing */
		break;
	    /* Drop thru to unconditionally store quoted char */

	default:
	    if (tct > 1) --tct, *t++ = *f;	/* Add char if room */
	    else tct = 0;
	    continue;				/* Get another */
	}
	break;		/* Break from switch is break from loop */
    }

    /* OK, tie off token.  Will always write at least NUL char if
    ** there was even 1 char of room in the to-buffer, since the
    ** accumulation code above is careful to stop before the last char.
    */
    if (*tcnt > 0) {
	*t++ = '\0';	/* Write terminating NUL */
	if (tct > 0)
	    --tct;
    }
    *fcp = f;		/* Update from-buffer */
    *fcnt = fct;
    f = *tcp;		/* Remember start of token, for return val */
    *tcp = t;		/* Update to-buffer */
    *tcnt = tct;
    return f;
}


/* S_EZTOKEN - Like s_1token but simpler to use.
**	Updates only the source pointer; assumes nul-terminated.
**	Returns # of chars in token; 0 if all gone.
*/
size_t
s_eztoken(char *tcp,
	  size_t tcnt,
	  char **fcp)
{
    size_t otcnt = tcnt;
    size_t fcnt = *fcp ? strlen(*fcp) : 0;

    if (s_1token(&tcp, &tcnt, fcp, &fcnt))
	return otcnt - tcnt;
    return 0;
}

/* S_MATCH - compare strings, ignoring case.
**	Returns 0 if mismatched
**	        1 if S1 is initial substring of S2
**		2 if S1 is exact match of S2
*/
int
s_match(register char *s1,
	register char *s2)
{
    register int c1, c2;

    while (c1 = *s1++) {
	if (isupper(c1))		/* Must test with isupper cuz some */
	    c1 = tolower(c1);		/* implementations fuck up otherwise */
	if (isupper(c2 = *s2++))
	    c2 = tolower(c2);
	if (c1 != c2)
	    return 0;
    }
    return *s2 ? 1 : 2;
}


/* S_DUP - Duplicate string, using malloc
**	Returns NULL if alloc failed
*/
char *
s_dup(register char *s)
{
    register char *cp;

    if (!s)
	return NULL;
    if (cp = (char *)malloc(strlen(s)+1))
	strcpy(cp, s);
    return cp;
}
