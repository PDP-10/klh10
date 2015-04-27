/* CENV.H - General C Environment Definitions
*/
/* $Id: cenv.h,v 2.6 2002/03/28 16:48:50 klh Exp $
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
 * $Log: cenv.h,v $
 * Revision 2.6  2002/03/28 16:48:50  klh
 * First pass at using LFS (Large File Support)
 *
 * Revision 2.5  2002/03/21 09:44:43  klh
 * Added DECOSF to CENV_SYSF_TERMIOS
 *
 * Revision 2.4  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/* C environment config definitions, used by various KLH programs.
**
**    These specify the TARGET platform, usually but not necessarily the one
**    being compiled on.
**
**	CENV_CPU_x  = target CPU architecture
**	CENV_SYS_x  = target OS
**	CENV_SYSF_x = target OS features 
**
** Note that the CPU and SYS are expected to be explicitly specified by
** a command line definition.  If no setting is detected this code tries
** a few simple checks, but it is not an error if nothing is set; the
** includer may have portable defaults.
*/

#ifndef CENV_INCLUDED
#define CENV_INCLUDED 1

#ifdef RCSID
 RCSID(cenv_h,"$Id: cenv.h,v 2.6 2002/03/28 16:48:50 klh Exp $")
#endif

/* Machine architecture - alpha order */

#ifndef  CENV_CPU_ALPHA		/* DEC Alpha AXP series */
# define CENV_CPU_ALPHA 0
#endif
#ifndef  CENV_CPU_ARM		/* DEC/Intel ARM series */
# define CENV_CPU_ARM 0
#endif
#ifndef  CENV_CPU_I386		/* Intel 386 and up series */
# define CENV_CPU_I386 0
#endif
#ifndef  CENV_CPU_M68		/* Motorola MC680x0 series */
# define CENV_CPU_M68 0
#endif
#ifndef  CENV_CPU_PDP10		/* DEC PDP10 series */
# define CENV_CPU_PDP10 0
#endif
#ifndef  CENV_CPU_PPC		/* IBM/Motorola PowerPC series */
# define CENV_CPU_PPC 0
#endif
#ifndef  CENV_CPU_SPARC		/* SUN SPARC series */
# define CENV_CPU_SPARC 0
#endif

/* If none of the above were set, try a few semi-standard checks,
 * but don't complain if nothing's found.
 */
#if !(CENV_CPU_M68|CENV_CPU_SPARC|CENV_CPU_PDP10|CENV_CPU_I386 \
     |CENV_CPU_ALPHA|CENV_CPU_PPC)
# if defined(__alpha) || defined(__alpha__)
#  undef  CENV_CPU_ALPHA
#  define CENV_CPU_ALPHA 1
# elif defined(__arm) || defined(__arm__)
#  undef  CENV_CPU_ARM
#  define CENV_CPU_ARM 1
# elif defined(__i386) || defined(__i386__)
#  undef  CENV_CPU_I386
#  define CENV_CPU_I386 1
# elif defined(__ppc) || defined(__ppc__)
#  undef  CENV_CPU_PPC
#  define CENV_CPU_PPC 1
# elif defined(__sparc) || defined(__sparc__)
#  undef  CENV_CPU_SPARC
#  define CENV_CPU_SPARC 1
# elif defined(__COMPILER_KCC__)
#  undef  CENV_CPU_PDP10	/* Not quite right, but close enough */
#  define CENV_CPU_PDP10 1
# endif
#endif

/* Specific CPU Feature defs
   This only has features of interest for KLH10 software.
	Note: endian-ness cannot be assumed if CPU is unknown.
	Note: PowerPC is inherently big-endian, but can support little-endian
		memory addressing!  Platforms so far (Linux & MacOS) use
		big-endian model, but NT/W2K may require little-endian.
 */
#ifndef  CENV_CPUF_BIGEND	/* True if big-endian */
# define CENV_CPUF_BIGEND (CENV_CPU_SPARC|CENV_CPU_M68|CENV_CPU_PDP10 \
			  |CENV_CPU_PPC)
#endif
#ifndef  CENV_CPUF_LILEND	/* True if little-endian */
# define CENV_CPUF_LILEND (CENV_CPU_I386|CENV_CPU_ALPHA|CENV_CPU_ARM)
#endif


/* Operating System - alpha order */

#ifndef  CENV_SYS_BSDI		/* 386 BSDI */
# define CENV_SYS_BSDI 0
#endif
#ifndef  CENV_SYS_DECOSF	/* DEC OSF/1 (Digital Unix, Tru64) */
# define CENV_SYS_DECOSF 0
#endif
#ifndef  CENV_SYS_FREEBSD	/* FreeBSD */
# define CENV_SYS_FREEBSD 0
#endif
#ifndef  CENV_SYS_LINUX		/* Linux */
# define CENV_SYS_LINUX 0
#endif
#ifndef  CENV_SYS_MAC		/* Apple Mac (classic, pre-X) */
# define CENV_SYS_MAC 0
#endif
#ifndef  CENV_SYS_NETBSD	/* NetBSD */
# define CENV_SYS_NETBSD 0
#endif
#ifndef  CENV_SYS_NEXT		/* NeXT */
# define CENV_SYS_NEXT 0
#endif
#ifndef  CENV_SYS_OPENBSD	/* OpenBSD */
# define CENV_SYS_OPENBSD 0
#endif
#ifndef  CENV_SYS_SOLARIS	/* SunOS 5.x */
# define CENV_SYS_SOLARIS 0
#endif
#ifndef  CENV_SYS_SUN		/* SunOS 4.x */
# define CENV_SYS_SUN 0
#endif
#ifndef  CENV_SYS_T20		/* DEC TOPS-20 */
# define CENV_SYS_T20 0
#endif
#ifndef  CENV_SYS_V7		/* Basic vanilla Unix */
# define CENV_SYS_V7 0
#endif
#ifndef  CENV_SYS_W2K		/* MS W2K */
# define CENV_SYS_W2K 0
#endif

/* If none of the above were set, try a few semi-standard checks,
 * but don't complain if nothing's found.
 */
#if !(CENV_SYS_V7|CENV_SYS_SUN|CENV_SYS_SOLARIS|CENV_SYS_NEXT|CENV_SYS_MAC \
     |CENV_SYS_BSDI|CENV_SYS_NETBSD|CENV_SYS_FREEBSD|CENV_SYS_OPENBSD \
     |CENV_SYS_DECOSF|CENV_SYS_LINUX|CENV_SYS_W2K)
# if defined(__osf__) && defined(__digital__)
#  undef  CENV_SYS_DECOSF
#  define CENV_SYS_DECOSF 1
# elif defined(__FreeBSD__)
#  undef  CENV_SYS_FREEBSD
#  define CENV_SYS_FREEBSD 1 
# elif defined(__linux__)
#  undef  CENV_SYS_LINUX
#  define CENV_SYS_LINUX 1
# elif defined(__APPLE__)
#  undef  CENV_SYS_MAC
#  define CENV_SYS_MAC 1 
# elif defined(__NetBSD__)
#  undef  CENV_SYS_NETBSD
#  define CENV_SYS_NETBSD 1 
# elif defined(__OpenBSD__)
#  undef  CENV_SYS_OPENBSD
#  define CENV_SYS_OPENBSD 1 
# elif defined(__sun) && defined(__SVR4)
#  undef  CENV_SYS_SOLARIS
#  define CENV_SYS_SOLARIS 1 
# elif defined(__COMPILER_KCC__)
#  undef  CENV_SYS_T20		/* Not quite right, but close enough */
#  define CENV_SYS_T20 1
# endif
#endif


/* Derive composite switches - may not be entirely accurate,
   but close enough.
*/
#ifndef  CENV_SYS_XBSD	/* All modern BSD variants */
# define CENV_SYS_XBSD (CENV_SYS_NETBSD|CENV_SYS_FREEBSD|CENV_SYS_OPENBSD)
#endif
#ifndef  CENV_SYS_BSD	/* For any BSD-generic stuff (TTY, time) */
# define CENV_SYS_BSD (CENV_SYS_SUN|CENV_SYS_SOLARIS|CENV_SYS_BSDI \
		      |CENV_SYS_XBSD|CENV_SYS_NEXT|CENV_SYS_DECOSF \
		      |CENV_SYS_LINUX)
#endif
#define CENV_SYS_SVR4 0	/* XXX Later: (CENV_SYS_SOLARIS|CENV_SYS_DECOSF) ? */
#define CENV_SYS_UNIX (CENV_SYS_V7|CENV_SYS_BSD|CENV_SYS_SVR4)	/* Any Unix */

/* Specific OS Feature defs
   This only has features of interest for KLH10 software.
 */
#ifndef  CENV_SYSF_BSDTIMEVAL	/* Has "timeval" struct & calls */
# define CENV_SYSF_BSDTIMEVAL (CENV_SYS_UNIX && !CENV_SYS_V7)
#endif
#ifndef  CENV_SYSF_TERMIOS	/* Has termios(3) tty stuff */
# define CENV_SYSF_TERMIOS (CENV_SYS_DECOSF|CENV_SYS_SOLARIS|CENV_SYS_XBSD \
			   |CENV_SYS_LINUX)
#endif
#ifndef  CENV_SYSF_BSDTTY	/* Has old BSD tty stuff */
# define CENV_SYSF_BSDTTY (!CENV_SYSF_TERMIOS && CENV_SYS_BSD)
#endif
#ifndef  CENV_SYSF_SIGSET	/* Has sigsetops(3) and sigaction(2) */
# define CENV_SYSF_SIGSET (CENV_SYS_DECOSF|CENV_SYS_SUN|CENV_SYS_SOLARIS \
			  |CENV_SYS_XBSD|CENV_SYS_LINUX)
#endif
#ifndef  CENV_SYSF_STRERROR	/* Has strerror(3) */
# define CENV_SYSF_STRERROR (CENV_SYS_DECOSF|CENV_SYS_SOLARIS|CENV_SYS_XBSD \
			    |CENV_SYS_LINUX)
#endif
#ifndef  CENV_SYSF_NANOSLEEP	/* Has nanosleep(2) */
# define CENV_SYSF_NANOSLEEP (CENV_SYS_DECOSF|CENV_SYS_SOLARIS|CENV_SYS_XBSD \
			     |CENV_SYS_LINUX)
#endif

/* Large File Support (LFS)
 * See <http://ftp.sas.com/standards/large.file/x_open.20Mar96.html>
 *
 * DECOSF:  default 64-bit  (no macros); fseek 64-bit; no fseeko.
 * FREEBSD: default 64-bit  (no macros); fseek 32-bit; has fseeko.
 * LINUX:   default 32-bit (need macro); fseek 32-bit; has fseeko (need macro).
 * SOLARIS: default 32-bit (need macro); fseek 32-bit; has fseeko (need macro).
 * NETBSD:  default 64-bit  (no macros); fseek 32-bit; no fseeko. (ugh!)
 * MAC/OTH: ? Assume 32-bit OS only, 64 not possible.
 */
#ifndef  CENV_SYSF_LFS	/* Predefining this must predefine the rest */

     /* FreeBSD defaults to 64-bit file off_t but calls the type "quad_t"
      * instead of "long long".  Always has fseeko.
      */
# if CENV_SYS_FREEBSD
#  define CENV_SYSF_LFS 64		/* off_t exists and has 64 bits */
#  define CENV_SYSF_FSEEKO 1		/* And have some flavor of fseeko */
#  define CENV_SYSF_LFS_FMT "q"		/* printf format is quad_t */

     /* Alpha OSF/DU/Tru64 use 64-bit longs
      */
# elif CENV_SYS_DECOSF
#  define CENV_SYSF_LFS 64		/* off_t  exists and has 64 bits */
#  define CENV_SYSF_FSEEKO 1		/* And have some flavor of fseeko */
#  define fseeko fseek			/* off_t == long and no fseeko */
#  define CENV_SYSF_LFS_FMT "l"	/* printf format is long */

     /* Solaris/Linux do not default to 64-bit; must define these macros
      * and make sure cenv.h comes before any other include files.
      */
# elif CENV_SYS_SOLARIS|CENV_SYS_LINUX
#  define CENV_SYSF_LFS 64		/* off_t exists and has 64 bits */
#  define CENV_SYSF_FSEEKO 1		/* And have some flavor of fseeko */
#  ifndef _FILE_OFFSET_BITS
#   define _FILE_OFFSET_BITS=64	/* Use 64-bit file ops */
#  endif
#  ifndef _LARGEFILE_SOURCE
#   define _LARGEFILE_SOURCE		/* Include fseeko, ftello, etc */
#  endif
#  define CENV_SYSF_LFS_FMT "ll"	/* printf format is long long */

     /* Unknown system, but check for existence of standard macros */
# elif _FILE_OFFSET_BITS >= 64
#  define CENV_SYSF_LFS _FILE_OFFSET_BITS	/* Assume off_t exists */
#  define CENV_SYSF_FSEEKO 1		/* Also assume fseeko */
#  define CENV_SYSF_LFS_FMT "ll"	/* printf fmt probably (!) long long */

     /* Out of luck, using plain old longs (likely 32-bit) */
# else
#  define CENV_SYSF_LFS 0		/* No off_t, use long */
#  define CENV_SYSF_FSEEKO 0		/* No fseeko (irrelevant) */
#  define CENV_SYSF_LFS_FMT "l"		/* printf format is long */
# endif
#endif /* ifndef CENV_SYSF_LFS */


#endif /* ifndef CENV_INCLUDED */
