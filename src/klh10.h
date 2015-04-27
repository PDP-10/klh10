/* KLH10.H - General Configuration Definitions
*/
/* $Id: klh10.h,v 2.10 2003/02/23 18:23:38 klh Exp $
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
 * $Log: klh10.h,v $
 * Revision 2.10  2003/02/23 18:23:38  klh
 * Bump version: 2.0H
 *
 * Revision 2.9  2002/05/21 16:54:32  klh
 * Add KLH10_I_CIRC to allow any sys to have CIRC
 *
 * Revision 2.8  2002/05/21 09:41:58  klh
 * Bump version: 2.0G
 *
 * Revision 2.7  2002/04/24 07:40:10  klh
 * Bump version to 2.0E
 *
 * Revision 2.6  2002/03/28 16:51:39  klh
 * Version 2.0D update
 *
 * Revision 2.5  2002/03/21 09:50:38  klh
 * New version
 *
 * Revision 2.4  2001/11/19 10:39:05  klh
 * Bump version: 2.0A
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#ifndef KLH10_INCLUDED
#define KLH10_INCLUDED 1

#ifndef  KLH10_USE_RCSID	/* For now, default to always on */
# define KLH10_USE_RCSID 1
#endif
#if KLH10_USE_RCSID
# include "rcsid.h"
#endif
#ifdef RCSID
 RCSID(klh10_h,"$Id: klh10.h,v 2.10 2003/02/23 18:23:38 klh Exp $")
#endif

/* Preliminary legalisms (heavy sigh) */

#ifndef KLH10_COPYRIGHT
# define KLH10_COPYRIGHT "\
    Copyright © 2002 Kenneth L. Harrenstien -- All Rights Reserved."
#endif
#ifndef  KLH10_WARRANTY
# define KLH10_WARRANTY "This program comes \"AS IS\" with ABSOLUTELY NO WARRANTY."
#endif
#ifndef  KLH10_VERSION
# define KLH10_VERSION "V2.0H"
#endif
#ifndef  KLH10_CLIENT
# define KLH10_CLIENT "Generic"
#endif


/* C environment setup definitions.
*/

#include "cenv.h"	/* Get CENV_CPU_ and CENV_SYS_ */

/* Canonical C true/false values */
#define TRUE 1
#define FALSE 0

/* For convenience when passing/setting a function pointer to NULL, to
   show its nature without specifying the entire prototype.  NULL alone
   is sufficient in ANSI C.
*/
#define NULLPROC (NULL)

/* Compilation switches defining desired emulation target */

/* Define CPU type to emulate.
**	For now, ignore peculiarities such as Foonly and Systems Concepts
**	since they were primarily emulations of some DEC type.
*/
#ifndef  KLH10_CPU_6	/* DEC PDP-6 (Model 166 processor) */
# define KLH10_CPU_6 0
#endif
#ifndef  KLH10_CPU_KA	/* DEC KA10 */
# define KLH10_CPU_KA 0
#endif
#ifndef  KLH10_CPU_KI	/* DEC KI10 */
# define KLH10_CPU_KI 0
#endif
#ifndef  KLH10_CPU_KS	/* DEC KS10 (2020) */
# define KLH10_CPU_KS 0
#endif
#ifndef  KLH10_CPU_KL0	/* DEC KL10 (single section - KL10A?) */
# define KLH10_CPU_KL0 0
#endif
#ifndef  KLH10_CPU_KLX	/* DEC KL10 (extended KL10B, 0 or non-0 section) */
# define KLH10_CPU_KLX 0
#endif
#ifndef  KLH10_CPU_KN	/* KLH KN10 (placeholder for non-HW features) */
# define KLH10_CPU_KN 0
#endif
#ifndef  KLH10_CPU_XKL	/* XKL XKL-1 (TOAD-1 System), a super-extended KL */
# define KLH10_CPU_XKL 0
#endif

#if !(KLH10_CPU_6|KLH10_CPU_KA|KLH10_CPU_KI \
	|KLH10_CPU_KS|KLH10_CPU_KL0|KLH10_CPU_KLX|KLH10_CPU_KN|KLH10_CPU_XKL)
# undef  KLH10_CPU_KS
# define KLH10_CPU_KS 1	/* Use KS10 as default */
#endif

#define KLH10_CPU_KL (KLH10_CPU_KL0 || KLH10_CPU_KLX || KLH10_CPU_XKL)


/* Define SYSTEM type emulated machine supports.
**	Primarily affects paging, but sometimes a few other things.
**	These switches are comparable to KS/KL ucode conditionals, since
**	each system tends to have its own peculiar variety of ucode (or
**	even different hardware, for KA/KI).
*/
#ifndef  KLH10_SYS_ITS		/* MIT ITS system */
# define KLH10_SYS_ITS 0
#endif
#ifndef  KLH10_SYS_WTS		/* Stanford WAITS system */
# define KLH10_SYS_WTS 0
#endif
#ifndef  KLH10_SYS_10X		/* BBN TENEX system */
# define KLH10_SYS_10X 0
#endif
#ifndef  KLH10_SYS_T10		/* DEC TOPS-10 system */
# define KLH10_SYS_T10 0
#endif
#ifndef  KLH10_SYS_T20		/* DEC TOPS-20 system */
# define KLH10_SYS_T20 0
#endif

#if !(KLH10_SYS_ITS|KLH10_SYS_WTS|KLH10_SYS_10X|KLH10_SYS_T10|KLH10_SYS_T20)
# undef  KLH10_SYS_ITS
# define KLH10_SYS_ITS 1	/* Default for now is ITS (yeah!) */
#endif

/* Now define additional flags peculiar to each system/CPU, which
** describe the hardware features emulated ("what" as opposed to "how").
**	These could go into kn10*.h config files, where
**	* = 3-letter CPU or SYS identifier.
*/

/* Select pager to use.
**	DEC has had 3 different varieties of memory mgt:
**		KA relocation (not emulated)
**		KI paging (emulated; also called "Tops-10 paging")
**		KL paging (emulated; also called "Tops-20 paging")
**	For ITS this depended on the machine.  Only the KS is emulated here.
**	For TENEX a special BBN pager was used.  This is not emulated,
**		but would be quite similar to KL paging.
*/
#ifndef  KLH10_PAG_KI
# define KLH10_PAG_KI 0
#endif
#ifndef  KLH10_PAG_KL
# define KLH10_PAG_KL 0
#endif
#ifndef  KLH10_PAG_ITS
# define KLH10_PAG_ITS 0
#endif

/* If no paging scheme explicitly selected, pick default */
#if !(KLH10_PAG_KI | KLH10_PAG_KL | KLH10_PAG_ITS)
# undef  KLH10_PAG_KI
# define KLH10_PAG_KI KLH10_SYS_T10
# undef  KLH10_PAG_KL
# define KLH10_PAG_KL KLH10_SYS_T20
# undef  KLH10_PAG_ITS
# define KLH10_PAG_ITS KLH10_SYS_ITS
#endif

#if KLH10_SYS_ITS		/* ITS normally includes all of these */
# ifndef  KLH10_ITS_JPC
#  define KLH10_ITS_JPC 1	/* Include ITS JPC feature */
# endif
# if KLH10_ITS_JPC
#  undef  KLH10_JPC
#  define KLH10_JPC 1		/* Include general-purpose JPC, for debug */
# endif
# ifndef  KLH10_ITS_1PROC
#  define KLH10_ITS_1PROC 1	/* Include ITS 1-proceed feature */
# endif
#endif

#ifndef  KLH10_I_CIRC		/* True to include ITS CIRC instruction */
# define KLH10_I_CIRC (KLH10_SYS_ITS)
#endif

#ifndef  KLH10_MCA25		/* MCA25 KL Cache/Paging Upgrade */
# define KLH10_MCA25 (KLH10_CPU_KL && KLH10_SYS_T20)
#endif

#ifndef  KLH10_EXTADR		/* True to support extended addressing */
# define KLH10_EXTADR (KLH10_CPU_KLX || KLH10_CPU_XKL)
#endif

/* Peripheral Devices
**	Determine here which ones will be available for use.
**	Further configuration is done at runtime.
*/

/* KL10 devices (old-style IO bus devices) */

#ifndef  KLH10_DEV_DTE
# define KLH10_DEV_DTE KLH10_CPU_KL
#endif
#ifndef  KLH10_DEV_RH20
# define KLH10_DEV_RH20 KLH10_CPU_KL
#endif
#ifndef  KLH10_DEV_NI20
# define KLH10_DEV_NI20 KLH10_CPU_KL
#endif

/* KS10 devices (new-style Unibus devices) */

#ifndef  KLH10_DEV_RH11
# define KLH10_DEV_RH11 KLH10_CPU_KS
#endif
#ifndef  KLH10_DEV_DZ11		/* KS10 DZ11 also part of basic system? */
# define KLH10_DEV_DZ11 KLH10_SYS_ITS	/* Try just ITS for now */
#endif
#ifndef  KLH10_DEV_LHDH		/* KS10 LHDH IMP interface */
# define KLH10_DEV_LHDH KLH10_SYS_ITS	/* Only on ITS for now */
#endif
#ifndef  KLH10_DEV_CH11		/* KS10 CH11 Chaosnet interface? */
# define KLH10_DEV_CH11 KLH10_SYS_ITS	/* Only on ITS for now */
#endif

/* Generic controller drive devices - work for either bus */

#ifndef  KLH10_DEV_RPXX
# define KLH10_DEV_RPXX (KLH10_DEV_RH20 | KLH10_DEV_RH11)
#endif
#ifndef  KLH10_DEV_TM03
# define KLH10_DEV_TM03 (KLH10_DEV_RH20 | KLH10_DEV_RH11)
#endif

/* Universal devices - currently just one pseudo-dev */

#ifndef  KLH10_DEV_HOST
# define KLH10_DEV_HOST 1
#endif
#ifndef  KLH10_DEV_LITES
# define KLH10_DEV_LITES 0
#endif

/* CPU and PI configuration (PI includes IO)
**	The parameters defined earlier specify WHAT to emulate; by contrast,
**	these specify HOW the emulation should be done.
*/

#ifndef  KLH10_PCCACHE	/* True to include experimental PC cache stuff */
# define KLH10_PCCACHE 1
#endif
#ifndef  KLH10_JPC	/* True to include JPC feature */
# define KLH10_JPC 1	/* For now, always - helps debug! */
#endif

/* MEMORY - Select emulation method
**	Sharable memory has pitfalls but is useful for subproc DMA and
**	perhaps future SMP implementation.
*/
#ifndef  KLH10_MEM_SHARED	/* TRUE to use sharable memory segment */
# define KLH10_MEM_SHARED 0
#endif

/* REAL-TIME CLOCK - Select emulation method
*/
#ifndef  KLH10_RTIME_SYNCH	/* Synchronized - use count */
# define KLH10_RTIME_SYNCH 0
#endif
#ifndef  KLH10_RTIME_OSGET	/* OS value used for all references */
# define KLH10_RTIME_OSGET 0
#endif
#ifndef  KLH10_RTIME_INTRP	/* Interrupt-driven (not implemented) */
# define KLH10_RTIME_INTRP 0
#endif
#if !(KLH10_RTIME_SYNCH|KLH10_RTIME_OSGET|KLH10_RTIME_INTRP)
# undef  KLH10_RTIME_OSGET
# define KLH10_RTIME_OSGET 1	/* Default to asking system */
#endif

/* INTERVAL-TIME CLOCK - Select emulation method
*/
#ifndef  KLH10_ITIME_SYNCH	/* Synchronized - use count */
# define KLH10_ITIME_SYNCH 0
#endif
#ifndef  KLH10_ITIME_INTRP	/* Interrupt-driven */
# define KLH10_ITIME_INTRP 0
#endif
#if !(KLH10_ITIME_SYNCH|KLH10_ITIME_INTRP)
# undef  KLH10_ITIME_SYNCH
# define KLH10_ITIME_SYNCH 1	/* Default to synchronous counter */
#endif

/* INTERVAL-TIME CLOCK - set default interval in HZ
**	This is the actual interval time in HZ that will be enforced
**	unless the user explicitly does a "set clk_ithzfix".
**	A value of 0 allows the 10 to set it to anything.
**	60 is a good default; 30 may be needed on slower/older hardware.
*/
#ifndef  KLH10_CLK_ITHZFIX
# define KLH10_CLK_ITHZFIX 60
#endif

/* QUANTUM COUNTER - Select emulation method (ITS only)
*/
#ifndef  KLH10_QTIME_SYNCH	/* Synchronized - use count */
# define KLH10_QTIME_SYNCH 0
#endif
#ifndef  KLH10_QTIME_OSREAL	/* Use OS realtime */
# define KLH10_QTIME_OSREAL 0
#endif
#ifndef  KLH10_QTIME_OSVIRT	/* Use OS virtual (user CPU) time */
# define KLH10_QTIME_OSVIRT 0
#endif

#if KLH10_SYS_ITS		/* Only default if ITS */
# if !(KLH10_QTIME_SYNCH|KLH10_QTIME_OSREAL|KLH10_QTIME_OSVIRT)
# undef  KLH10_QTIME_SYNCH
# define KLH10_QTIME_SYNCH 1	/* Default to synchronous counter */
# endif
#endif


/* DEVICE I/O WAKEUP - Select I/O checking method for various devices.
**	These parameters select interrupt-driven methods if TRUE;
**	otherwise polling is used.
**	Future alternatives may use threads or subprocesses.
*/
#ifndef  KLH10_CTYIO_INT		/* True to use CTY interrupts */
# define KLH10_CTYIO_INT 0
#endif
#ifndef  KLH10_IMPIO_INT		/* True to use IMP interrupts */
# define KLH10_IMPIO_INT 0
#endif
#ifndef  KLH10_EVHS_INT		/* True to use new event handling scheme */
# define KLH10_EVHS_INT 0
#endif

/* DEVICE SUBPROCESS - Select basic implementation method for various devices
**	These parameters select an asynchronous sub-process method if TRUE;
**	otherwise a blocking method is used.
**	Not all devices will work without subprocesses (e.g. NI20)
**	Future alternatives may use threads.
*/
#ifndef  KLH10_DEV_DPNI20	/* True to use dev subproc for NI20 net */
# define KLH10_DEV_DPNI20 KLH10_DEV_NI20
#endif
#ifndef  KLH10_DEV_DPRPXX	/* True to use dev subproc for RPxx disk */
# define KLH10_DEV_DPRPXX 0
#endif
#ifndef  KLH10_DEV_DPTM03	/* True to use dev subproc for TM03 tape */
# define KLH10_DEV_DPTM03 0
#endif

/* Two different ways to implement IMP subproc */
#ifndef  KLH10_DEV_DPIMP		/* True to use dev subproc for IMP (net) */
# define KLH10_DEV_DPIMP KLH10_DEV_LHDH
#endif
#ifndef  KLH10_DEV_SIMP		/* True to use pipe subproc for IMP (net) */
# define KLH10_DEV_SIMP (KLH10_DEV_LHDH && !KLH10_DEV_DPIMP)
#endif


#ifndef  KLH10_DEV_DP		/* True to include DP subproc support */
# define KLH10_DEV_DP (KLH10_DEV_DPNI20 \
		      |KLH10_DEV_DPRPXX|KLH10_DEV_DPTM03|KLH10_DEV_DPIMP)
#endif

/* Miscellaneous config vars */

#ifndef  KLH10_INITFILE		/* Default initialization command file */
# define KLH10_INITFILE "klh10.ini"
#endif

#ifndef  KLH10_DEBUG		/* TRUE to include debug output code */
# define KLH10_DEBUG 1
#endif

/* Hack for KS T20 CTY output. (see cty_addint() in dvcty.c)
*/
#ifndef  KLH10_CTYIO_ADDINT	/* Set 1 to use hack */
# define KLH10_CTYIO_ADDINT (KLH10_CPU_KS && KLH10_SYS_T20 && KLH10_CTYIO_INT)
#elif KLH10_CTYIO_ADDINT
# if !KLH10_CPU_KS || !KLH10_SYS_T20	/* Unless a KS T20, */
# undef  KLH10_CTYIO_ADDINT		/* force this to 0 */
# define KLH10_CTYIO_ADDINT 0
# endif
#endif

#endif /* ifndef KLH10_INCLUDED */
