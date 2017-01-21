/* CMDLINE.H - header file for command line processing functions
*/
/*  Copyright © 1992, 1993, 2001 Kenneth L. Harrenstien
/*  Copyright © 2017 Olaf Seibert
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
#ifndef CMDBUFLEN
# define CMDBUFLEN 512
#endif
#ifndef CMDMAXARG
# define CMDMAXARG 10
#endif

struct cmd_s {
	struct cmkey_s *cmd_keys;/* Command table */
	int cmd_flags;		/* State flags */
	char *cmd_prm;		/* Pointer to command prompt */
	char *cmd_buf;		/* Pointer to start of buffer */
	size_t cmd_blen;	/* Size of buffer */
	int cmd_left;		/* # chars left for current cmd being input */
	char *cmd_inp;		/* Input deposit pointer */
	char *cmd_rdp;		/* Readout pointer */
	size_t cmd_rleft;	/* # chars left to read  */

	/* Provide all command routines with their desired arguments */
	char *cmd_arglin;	/* Original pointer to start of args on line */
	int cmd_argc;			/* # of tokens */
	char *cmd_argv[CMDMAXARG+1];	/* Array of token pointers */
	char *cmd_tdp;			/* Next free loc in token buffer */
	size_t cmd_tleft;		/* # chars free in token buffer  */
	char cmd_tokbuf[CMDBUFLEN+CMDMAXARG];

#if 0
	char *cmd_wbf;		/* Pointer to work buffer */
	size_t cmd_wblen;	/* Size in chars */
	char *cmd_wbp;		/* Current deposit ptr */
	size_t cmd_wbleft;	/* # chars left */
#endif
};

#define CMDF_ACTIVE 01	/* Activation char seen, execute accumulated cmd */
#define CMDF_INACCUM 02	/* In accumulation phase */
#define CMDF_NOPRM 040	/* Disable prompt */

struct cmkey_s {
	char *cmk_key;
	union cmnode *cmk_p;
};
struct cmrtn_s {
	void (*cmr_vect)(struct cmd_s *);  /* Function to call */
	int cmr_flgs;		/* Misc flags */
	char *cmr_synt;		/* Arg syntax */
	char *cmr_help;		/* Short one-line help */
	char *cmr_desc;		/* Long description */
};

#define CMRF_NOARG 01	/* Command takes no args */
#define CMRF_TOKS  010	/* Command wants whole line tokenized, via cm */
#define CMRF_TLIN  020	/* Command wants overall line arg, via cm */
#define CMRF_CMPTR 040	/* Command wants just cmd state ptr */

union cmnode {			/* All possible nodes for a keyword */
	struct cmrtn_s cmn_rtn;
};


/* Predeclarations */
void cmdinit(struct cmd_s *, struct cmkey_s *, char *, char *, size_t);
int cmdexec(struct cmd_s *);
int cmdaccum(struct cmd_s *);

struct cmkey_s *cmdkeylookup(char *, struct cmkey_s *, struct cmkey_s **);
char *cmdlsetup(struct cmd_s *);
char *cmdlcopy(struct cmd_s *cm, char *line);
void fc_gques(struct cmd_s *cm);
void fc_ghelp(struct cmd_s *cm);

/* CMDDEF is used to define top-level commands.  It does not accumulate
**	them into a table (C is far too puny for that) but gathers together
**	various information that a higher-level table can then point to.
*/
#define CMDDEF(deflab, func, flgs, argsyn, minihelp, longdesc)	\
    static void func(struct cmd_s *);				\
    static struct cmrtn_s deflab = { func, flgs, argsyn, minihelp, longdesc };

#define KEYSBEGIN(name)	struct cmkey_s name[] = {
#define KEYDEF(key,nod)   { key, (union cmnode *)(&nod) },
#define KEYSEND		  { 0, 0 } };

