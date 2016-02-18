/* KLH10EXP.H - Exports from klh10.c
*/
/*  Copyright © 2015 Olaf "Rhialto" Seibert
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

#ifndef KLH10EXP_INCLUDED
#define KLH10EXP_INCLUDED 1

void klh10_main(int argc, char **argv);
void fe_aprcont(int, int, vaddr_t, int);
void fe_shutdown(void);
void fe_traceprint(w10_t, vaddr_t);
void fe_begpcfdbg(FILE *f);
void fe_endpcfdbg(FILE *f);
void pishow(FILE *);
void pcfshow(FILE *, h10_t);
void insprint(FILE *, int);
void pinstr(FILE *f, register w10_t w, int flags, vaddr_t e);
/* void panic(char *, ...); */	/* Declared in kn10def.h */

extern int proc_bkgd;

#endif /* ifndef KLH10EXP_INCLUDED */
