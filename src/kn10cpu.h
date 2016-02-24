/* KN10CPU.H - Exports from kn10cpu.c
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

#ifndef KN10CPU_INCLUDED
#define KN10CPU_INCLUDED 1

extern void apr_init(void);
extern void apr_init_aprid(void);
extern int apr_run(void);
extern void pi_devupd(void);
extern void apr_check(void);
extern void pxct_undo(void);	/* Stuff needed by KN10PAG for page fail trap */
extern void trap_undo(void);
#if KLH10_ITS_1PROC
extern void a1pr_undo(void);
#elif KLH10_CPU_KI || KLH10_CPU_KL
extern void afi_undo(void);
#endif

#endif
