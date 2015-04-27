/* KLH10S.H - General Configuration String Definitions
*/
/* $Id: klh10s.h,v 2.5 2002/05/21 16:54:32 klh Exp $
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
 * $Log: klh10s.h,v $
 * Revision 2.5  2002/05/21 16:54:32  klh
 * Add KLH10_I_CIRC to allow any sys to have CIRC
 *
 * Revision 2.4  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */
/*
**	This include file is used only to map the KLH10 compile-time config
**	parameters into strings suitable for runtime display.
*/

#ifndef KLH10S_INCLUDED
#define KLH10S_INCLUDED 1

#ifdef RCSID
 RCSID(klh10s_h,"$Id: klh10s.h,v 2.5 2002/05/21 16:54:32 klh Exp $")
#endif

/* Environment configuration switches.
**    These specify the TARGET platform; code assumes no cross-compilation.
**
**	CENV_CPU_x = host CPU architecture
**	CENV_SYS_x = host OS
*/
#if CENV_SYS_V7			/* Basic vanilla Unix */
# define KLH10S_CENV_SYS_ "V7"
#elif CENV_SYS_SUN		/* SunOS 4.x */
# define KLH10S_CENV_SYS_ "SUN"
#elif CENV_SYS_SOLARIS		/* SunOS 5.x */
# define KLH10S_CENV_SYS_ "SOLARIS"
#elif CENV_SYS_NEXT		/* NeXT */
# define KLH10S_CENV_SYS_ "NEXT"
#elif CENV_SYS_MAC		/* Apple Mac */
# define KLH10S_CENV_SYS_ "MAC"
#elif CENV_SYS_BSDI		/* 386 BSDI */
# define KLH10S_CENV_SYS_ "BSDI"
#elif CENV_SYS_NETBSD		/* NetBSD */
# define KLH10S_CENV_SYS_ "NETBSD"
#elif CENV_SYS_FREEBSD		/* FreeBSD */
# define KLH10S_CENV_SYS_ "FREEBSD"
#elif CENV_SYS_OPENBSD		/* FreeBSD */
# define KLH10S_CENV_SYS_ "OPENBSD"
#elif CENV_SYS_LINUX		/* Linux */
# define KLH10S_CENV_SYS_ "LINUX"
#elif CENV_SYS_DECOSF		/* DEC OSF/1 */
# define KLH10S_CENV_SYS_ "DECOSF"
#elif CENV_SYS_MOONMAC		/* Special stuff saved for Dave Moon */
# define KLH10S_CENV_SYS_ "MOONMAC"
#elif CENV_SYS_BSD		/* Generic BSD */
# define KLH10S_CENV_SYS_ "BSD"
#endif

#if CENV_CPU_ALPHA		/* DEC Alpha AXP series */
# define KLH10S_CENV_CPU_ "ALPHA"
#elif CENV_CPU_ARM		/* DEC/Intel ARM series */
# define KLH10S_CENV_CPU_ "ARM"
#elif CENV_CPU_I386		/* Intel 386/486 */
# define KLH10S_CENV_CPU_ "I386"
#elif CENV_CPU_M68		/* MC680x0 series */
# define KLH10S_CENV_CPU_ "M68"
#elif CENV_CPU_PDP10		/* DEC PDP10 series */
# define KLH10S_CENV_CPU_ "PDP10"
#elif CENV_CPU_PPC		/* IBM/Motorola PowerPC series */
# define KLH10S_CENV_CPU_ "PPC"
#elif CENV_CPU_SPARC		/* SUN SPARC series */
# define KLH10S_CENV_CPU_ "SPARC"
#else
# define KLH10S_CENV_CPU_ "unknown"
#endif

/* Compilation switches defining desired emulation target */

/* Define CPU type to emulate.
*/
#if KLH10_CPU_6		/* DEC PDP-6 (Model 166 processor) */
# define KLH10S_CPU_ "PDP-6"
#elif KLH10_CPU_KA	/* DEC KA10 */
# define KLH10S_CPU_ "KA10"
#elif KLH10_CPU_KI	/* DEC KI10 */
# define KLH10S_CPU_ "KI10"
#elif KLH10_CPU_KS	/* DEC KS10 (2020) */
# define KLH10S_CPU_ "KS10"
#elif KLH10_CPU_KL0	/* DEC KL10 (single section - KL10A?) */
# define KLH10S_CPU_ "KL10-1sect"
#elif KLH10_CPU_KLX	/* DEC KL10 (extended KL10B, 0 or non-0 section) */
# define KLH10S_CPU_ "KL10-extend"
#elif KLH10_CPU_KN	/* KLH KN10 (software machine) */
# define KLH10S_CPU_ "KN10"
#elif KLH10_CPU_XKL	/* XKL XKL-1 (TOAD-1) super-extended KL10B */
# define KLH10S_CPU_ "XKL-1"
#else
# define KLH10S_CPU_ "unknown"
#endif

/* Define SYSTEM type emulated machine supports.
**	Primarily affects paging, but sometimes a few other things.
**	These switches are comparable to KS/KL ucode conditionals, since
**	each system tends to have its own peculiar variety of ucode (or
**	even different hardware, for KA/KI).
*/
#if KLH10_SYS_ITS		/* MIT ITS system */
# define KLH10S_SYS_ "ITS"
#elif KLH10_SYS_10X		/* BBN TENEX system */
# define KLH10S_SYS_ "10X"
#elif KLH10_SYS_T10		/* DEC TOPS-10 system */
# define KLH10S_SYS_ "T10"
#elif KLH10_SYS_T20		/* DEC TOPS-20 system */
# define KLH10S_SYS_ "T20"
#else
# define KLH10S_SYS_ "unknown"
#endif

/* Select pager
*/
#if KLH10_PAG_KI
# define KLH10S_PAG_ "KI"
#elif KLH10_PAG_KL
# define KLH10S_PAG_ "KL"
#elif KLH10_PAG_ITS
# define KLH10S_PAG_ "ITS"
#else
# define KLH10S_PAG_ "unknown"
#endif


/* CPU and PI configuration (PI includes IO) */

/* INTERNAL CLOCK (from KN10CLK)
*/
#if KLH10_CLKTRG_OSINT
# define KLH10S_CLKTRG_ "OSINT"
#elif KLH10_CLKTRG_COUNT
# define KLH10S_CLKTRG_ "COUNT"
#else
# define KLH10S_CLKTRG_ "unknown"
#endif

/* REAL-TIME CLOCK - Select emulation method
*/
#if KLH10_RTIME_SYNCH	/* Synchronized - use count */
# define KLH10S_RTIME_ "SYNCH"
#elif KLH10_RTIME_OSGET	/* OS value used for all references */
# define KLH10S_RTIME_ "OSGET"
#else
# define KLH10S_RTIME_ "unknown"
#endif

/* INTERVAL-TIME CLOCK - Select emulation method
*/
#if KLH10_ITIME_SYNCH	/* Synchronized - use count */
# define KLH10S_ITIME_ "SYNCH"
#elif KLH10_ITIME_INTRP	/* Interrupt-driven */
# define KLH10S_ITIME_ "INTRP"
#else
# define KLH10S_ITIME_ "unknown"
#endif

/* QUANTUM COUNTER - Select emulation method (ITS only)
*/
#if KLH10_QTIME_SYNCH	/* Synchronized - use count */
# define KLH10S_QTIME_ "SYNCH"
#elif KLH10_QTIME_OSREAL	/* Use OS realtime */
# define KLH10S_QTIME_ "OSREAL"
#elif KLH10_QTIME_OSVIRT	/* Use OS virtual (user CPU) time */
# define KLH10S_QTIME_ "OSVIRT"
#else
# define KLH10S_QTIME_ "none"
#endif


/* Hardware features */
#if KLH10_I_CIRC
# define KLH10S_I_CIRC " CIRC"
#else
# define KLH10S_I_CIRC ""
#endif
#if KLH10_MCA25
# define KLH10S_MCA25 " MCA25"
#else
# define KLH10S_MCA25 ""
#endif
#if KLH10_JPC
# define KLH10S_JPC " JPC"
#else
# define KLH10S_JPC ""
#endif

/* KLH10 features */
#if KLH10_DEBUG
# define KLH10S_DEBUG " DEBUG"
#else
# define KLH10S_DEBUG ""
#endif
#if KLH10_PCCACHE
# define KLH10S_PCCACHE " PCCACHE"
#else
# define KLH10S_PCCACHE ""
#endif
#if KLH10_CTYIO_INT
# define KLH10S_CTYIO_INT " CTYINT"
#else
# define KLH10S_CTYIO_INT ""
#endif
#if KLH10_IMPIO_INT
# define KLH10S_IMPIO_INT " IMPINT"
#else
# define KLH10S_IMPIO_INT ""
#endif
#if KLH10_EVHS_INT
# define KLH10S_EVHS_INT " EVHINT"
#else
# define KLH10S_EVHS_INT ""
#endif


/* Devices included with build
*/

#if KLH10_DEV_DTE		/* Console */
# define KLH10S_DEV_DTE " DTE"
#else
# define KLH10S_DEV_DTE ""
#endif

#if KLH10_DEV_RH20		/* KL10 RH20 */
# define KLH10S_DEV_RH " RH20"
#elif KLH10_DEV_RH11		/* KS10 RH11 */
# define KLH10S_DEV_RH " RH11"
#else
# define KLH10S_DEV_RH ""
#endif

    /* Drive units for RH controllers */
#if KLH10_DEV_RPXX
# if KLH10_DEV_DPRPXX
#  define KLH10S_DEV_RPXX " RPXX(DP)"
# else
#  define KLH10S_DEV_RPXX " RPXX"
# endif
#else
# define KLH10S_DEV_RPXX ""
#endif

#if KLH10_DEV_TM03
# if KLH10_DEV_DPTM03
#  define KLH10S_DEV_TM03 " TM03(DP)"
# else
#  define KLH10S_DEV_TM03 " TM03"
# endif
#else
# define KLH10S_DEV_TM03 ""
#endif

#if KLH10_DEV_NI20
# if KLH10_DEV_DPNI20
#  define KLH10S_DEV_NI20 " NI20(DP)"
# else
#  define KLH10S_DEV_NI20 " NI20"
# endif
#else
# define KLH10S_DEV_NI20 ""
#endif

    /* KS10 stuff */
#if KLH10_DEV_DZ11
# define KLH10S_DEV_DZ11 " DZ11"
#else
# define KLH10S_DEV_DZ11 ""
#endif

#if KLH10_DEV_CH11
# define KLH10S_DEV_CH11 " CH11"
#else
# define KLH10S_DEV_CH11 ""
#endif

#if KLH10_DEV_LHDH
# if KLH10_DEV_DPIMP
#  define KLH10S_DEV_LHDH " LHDH(DPIMP)"
# elif KLH10_DEV_SIMP
#  define KLH10S_DEV_LHDH " LHDH(SIMP)"
# else
#  define KLH10S_DEV_LHDH " LHDH"
# endif
#else
# define KLH10S_DEV_LHDH ""
#endif

#endif /* ifndef  KLH10S_INCLUDED */

