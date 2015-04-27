/* ENADDR.C - Utility to manage ethernet interface addresses
*/
/* $Id: enaddr.c,v 2.6 2002/03/18 04:19:17 klh Exp $
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
 * $Log: enaddr.c,v $
 * Revision 2.6  2002/03/18 04:19:17  klh
 * Add promiscuous mode on/off
 *
 * Revision 2.5  2001/11/19 10:18:34  klh
 * Solaris port: add dp_strerror def
 *
 * Revision 2.4  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*
    Utility to manage ethernet interface addresses.
    Allows testing osdnet.c's interface query code, and for a specific
    interface allows one to read or set the MAC address, as well
    as add or delete multicast addresses that the interface will recognize.

    "ifconfig" can do some but not all of these things.

    Originally intended to replace Larry Sendlosky's mini-utils, where:

	LWS prog    Equivalent
	--------    -------------------------
	rln0addr:   enaddr ln0			Read ether addr
	sln0addr:   enaddr ln0 AA:0:4:0:AC:60	Set  ether addr
	defln0addr: enaddr ln0 default		Reset ether addr (if possible)
	sln0mcat:   enaddr ln0  +AB:0:0:4:0:0 \	Set multicast addrs
				+9:0:2B:0:0:FF \
				+AB:0:0:3:0:0 \
				+AB:0:0:1:0:0 \
				+AB:0:0:2:0:0
*/

#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>

#include "cenv.h"
#include "rcsid.h"
#include "osdnet.h"

#ifdef RCSID
 RCSID(enaddr_c,"$Id: enaddr.c,v 2.6 2002/03/18 04:19:17 klh Exp $")
#endif

#ifndef TRUE
# define TRUE 1
#endif
#ifndef FALSE
# define FALSE 0
#endif

     /* Parameters */
char *ifc;
int endef = FALSE;
char *enstr = NULL;
unsigned char pa_new[6];

#define MAXMCAT 16
struct mcat {
    int mcdel;
    unsigned char mcaddr[6];
};
int nmcats = 0;
struct mcat mcat[MAXMCAT];

int promiscf   = FALSE;	/* True if promisc mode specified */
int promiscon  = FALSE;	/* Desired mode (True = on) */

unsigned char pa_cur[6];
unsigned char pa_def[6];

static char *sprinteth(char *, unsigned char *);
static void penetaddr(char *ifc, unsigned char *cur, unsigned char *def);
static int  pareth(char *, unsigned char *);

int debugf = 0;

char usage[] = "\
Usage: enaddr [-v] [<ifc> [default | <ifaddr>] [+<addmcast>] [-<delmcast>]]\n\
    -v      Outputs debug and config info for all interfaces\n\
   <ifc>    Specific interface to read or modify\n\
   default  Reset ether addr to HW default, if known\n\
   <ifaddr> Set ether addr to this (form x:x:x:x:x:x)\n\
   +<mcast> Add    multicast addr  (same form)\n\
   -<mcast> Delete multicast addr  (same form)\n\
   +promisc Turn on  promiscuous mode\n\
   -promisc Turn off promiscuous mode\n";


/* Error and Diagnostic logging stuff.
   Set up for eventual extraction into a separate module
 */

#if 1	/* ENADDR */
# define LOG_EOL "\n"
# undef  LOG_DP
# define LOG_PROGNAME "enaddr"
# define DP_DBGFLG debugf
#else
# define LOG_EOL "\r\n"
# define LOG_DP dp
# if KLH10_SIMP
#  define LOG_PROGNAME "simp"
# elif KLH10_DEV_DPIMP
#  define LOG_PROGNAME "dpimp"
# elif KLH10_DEV_DPNI20
#  define LOG_PROGNAME "dpni20"
# endif
#endif

#if 1	/* Error and diagnostic stuff */

#if CENV_SYSF_STRERROR
# include <string.h>		/* For strerror() */
#endif

/* Error and diagnostic output */

static const char *log_progname = LOG_PROGNAME;

char *log_strerror(err)
{
    if (err == -1 && errno != err)
	return log_strerror(errno);
#if CENV_SYSF_STRERROR
    return strerror(err);
#else
#  if CENV_SYS_UNIX
    {
#  if !CENV_SYS_XBSD		/* Already in signal.h */
	extern int sys_nerr;
	extern char *sys_errlist[];
#  endif
	if (0 < err &&  err <= sys_nerr)
	    return sys_errlist[err];
    }
#  endif
    if (err == 0)
	return "No error";
    else {
	static char ebuf[30];
	sprintf(ebuf, "Unknown-error-%d", err);
	return ebuf;
    }
#endif /* !CENV_SYSF_STRERROR */
}



static void log(char *fmt, ...)
{
    fprintf(stderr, "[%s: ", log_progname);
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fputs("]", stderr);
}

static void logln(char *fmt, ...)
{
    fprintf(stderr, "[%s: ", log_progname);
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fputs("]"LOG_EOL, stderr);
}

static void logerror(char *fmt, ...)
{
    fprintf(stderr, LOG_EOL"[%s: ", log_progname);
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fputs("]"LOG_EOL, stderr);
}

static void logerror_ser(int num, char *fmt, ...)
{
    fprintf(stderr, LOG_EOL"[%s: ", log_progname);
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fprintf(stderr, " - %s]"LOG_EOL, log_strerror(num));
}

static void logfatal(int num, char *fmt, ...)
{
    fprintf(stderr, LOG_EOL"[%s: Fatal error: ", log_progname);
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fputs("]"LOG_EOL, stderr);

#if defined(LOG_DP)
    /* DP automatically kills any child as well. */
    dp_exit(&LOG_DP, num);
#endif
}

static void logfatal_ser(int num, char *fmt, ...)
{
    fprintf(stderr, LOG_EOL"[%s: ", log_progname);
    {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
    }
    fprintf(stderr, " - %s]"LOG_EOL, log_strerror(errno));

#if defined(LOG_DP)
    /* DP automatically kills any child as well. */
    dp_exit(&LOG_DP, num);
#endif
}


#define dp_strerror log_strerror
#define dbprint   log
#define dbprintln logln
#define error     logerror
#define syserr    logerror_ser
#define efatal    logfatal
#define esfatal   logfatal_ser

#endif /* Error and Diagnostic stuff */


main(int argc, char **argv)
{
    int i;
    ossock_t s;
    char ebuf[32];

    if (argc < 2) {
	printf("%s", usage);
	exit(1);
    }
    i = 1;
    if (strcmp(argv[i], "-v")==0) {
	debugf = TRUE;
	++i;
    }
    ifc = argv[i++];		/* Interface name if one */

    if ((i < argc)		/* Optional new interface addr? */
      && (argv[i][0] != '+')
      && (argv[i][0] != '-')) {
	enstr = argv[i];
	if (strcmp(enstr, "default") == 0)
	    endef = TRUE;
	else if (!pareth(enstr, pa_new)) {
	    printf("enaddr: bad format for new %s address \"%s\" - use x:x:x:x:x:x\n", ifc, enstr);
	    exit(1);
	}
	++i;			/* Gobbled ifaddr */
    }

    /* Check for optional multicast address munging */
    for (nmcats = 0;  (i < argc) && (nmcats < MAXMCAT);  ++i, ++nmcats) {
	switch (argv[i][0]) {
	    case '+':
		mcat[nmcats].mcdel = FALSE;
		break;
	    case '-':
		mcat[nmcats].mcdel = TRUE;
		break;
	    default:
		printf("enaddr: bad multicast/promisc format \"%s\" - must prefix with + or -\n", argv[i]);
		exit(1);
	}

	if (strcmp(&argv[i][1], "promisc") == 0) {
	    promiscf = TRUE;
	    promiscon = (argv[i][0] == '+');
	    --nmcats;
	    continue;
	}

	if (!pareth(&argv[i][1], mcat[nmcats].mcaddr)) {
	    printf("enaddr: bad multicast format \"%s\" - use x:x:x:x:x:x\n",
			&argv[i][1]);
	    exit(1);
	}
    }

    /* First, show interface info if desired */
    if (debugf) {
	osn_iftab_init(IFTAB_ALL);
    }

    /* Now mung interface if one given */
    if (ifc) {
	/* Open socket to generic network interface */
	if (!osn_ifsock(ifc, &s)) {
	    perror("no socket");
	    exit(1);
	}

	/* Read the default and current MAC address */
	(void) osn_ifeaget(s, ifc, pa_cur, pa_def);

	/* Print the MAC addresses */
	penetaddr(ifc, pa_cur, pa_def);

	if (enstr) {
	    printf("Setting interface %s address from %s to %s\n",
		   ifc, sprinteth(ebuf, pa_cur), enstr);

	    /* Setup the new MAC address - use default or new */
	    (void) osn_ifeaset(s, ifc, (endef ? pa_def : pa_new));

	    /* Read back to confirm */
	    (void) osn_ifeaget(s, ifc, pa_cur, pa_def);
	    penetaddr(ifc, pa_cur, pa_def);
	}

	/* Now check for multicast munging */
	for (i = 0; i < nmcats; ++i) {
	    printf("%s multicast addr %s",
		    (mcat[i].mcdel ? " Deleting" : "   Adding"),
		    sprinteth(ebuf, mcat[i].mcaddr));

	    if (!osn_ifmcset(s, ifc, mcat[i].mcdel, mcat[i].mcaddr)) {
		printf(" ... failed: %s", log_strerror(errno));
		/* Continue anyway.  Note that delete can fail harmlessly
		   if mcat address is already gone.
		*/
	    }
	    printf("\n");
	}

	/* Finally, force promiscuous mode on or off if specified */
	if (promiscf) {
	    printf("Setting promiscuous mode to %s... ",
		   (promiscon ? "ON" : "OFF"));
	    fflush(stdout);
	    if (!osn_ifmcset(s, ifc, !promiscon, NULL)) {
		printf(" failed\n");
		/* Continue anyway */
	    } else
		printf(" won!\n");
	}

	osn_ifclose(s);
    }
}

static char *sprinteth(char *cp, unsigned char *ea)
{
    sprintf(cp, "%x:%x:%x:%x:%x:%x", ea[0], ea[1], ea[2], ea[3], ea[4], ea[5]);
    return cp;
}

static void penetaddr(char *ifc,
		      unsigned char *cur,
		      unsigned char *def)
{
    char ebuf[32];

    if (memcmp(def, "\0\0\0\0\0\0", ETHER_ADRSIZ)==0)
	printf("  %s default address is unknown\n", ifc);
    else
	printf("  %s default address is %s\n", ifc, sprinteth(ebuf, def));
    printf("  %s current address is %s\n", ifc, sprinteth(ebuf, cur));
}


static int pareth(char *cp, unsigned char *adr)
{
    unsigned int b1, b2, b3, b4, b5, b6;
    int cnt;

    cnt = sscanf(cp, "%x:%x:%x:%x:%x:%x", &b1, &b2, &b3, &b4, &b5, &b6);
    if (cnt != 6) {
	/* Later try as single large address #? */
	return FALSE;
    }
    if (b1 > 255 || b2 > 255 || b3 > 255 || b4 > 255 || b5 > 255 || b6 > 255)
	return FALSE;
    *adr++ = b1;
    *adr++ = b2;
    *adr++ = b3;
    *adr++ = b4;
    *adr++ = b5;
    *adr   = b6;
    return TRUE;
}

/* Include OSDNET code, faking out unneeded packetfilter inits
 * XXX: clean this up in future OSDNET.
 */
struct fakepf { int foo; };
#define OSN_PFSTRUCT fakepf

struct OSN_PFSTRUCT *
pfbuild(void *arg, struct in_addr *ipa)
{
    return NULL;
}
struct OSN_PFSTRUCT *
pfeabuild(void *arg, unsigned char *ea)
{
    return NULL;
}

#include "osdnet.c"

