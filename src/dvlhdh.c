/* DVLHDH.C - ACC LH-DH IMP Interface emulation
*/
/* $Id: dvlhdh.c,v 2.4 2001/11/19 10:47:54 klh Exp $
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
 * $Log: dvlhdh.c,v $
 * Revision 2.4  2001/11/19 10:47:54  klh
 * Init dpimp_blobsiz when dpimp seg created.
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include "klh10.h"

#if !KLH10_DEV_LHDH && CENV_SYS_DECOSF
	/* Stupid gubbish needed to prevent OSF/1 AXP compiler from
	** halting merely because compiled file is empty!
	*/
static int decosfcclossage;
#endif

#if KLH10_DEV_LHDH	/* Moby conditional for entire file */

#include <stdio.h>
#include <string.h>

#if KLH10_DEV_SIMP
# include <unistd.h>	/* For access(), fcntl() */
# include <fcntl.h>
# include <errno.h>
# include <signal.h>	/* For kill() */
# include <sys/ioctl.h>
#   undef I_PUSH	/* Avoid name conflicts with 10 instr ops */
#   undef I_POP
#endif

#include "kn10def.h"
#include "kn10dev.h"
#include "prmstr.h"	/* For parameter parsing */
#include "dvuba.h"
#include "dvlhdh.h"

#if KLH10_DEV_SIMP || KLH10_DEV_DPIMP
# include "dpimp.h"
#endif

#ifdef RCSID
 RCSID(dvlhdh_c,"$Id: dvlhdh.c,v 2.4 2001/11/19 10:47:54 klh Exp $")
#endif

#define IMPBUFSIZ (SIH_HSIZ+SI_LDRSIZ+SI_MAXMSG+500)	/* Plenty of slop */


#ifndef LHDH_NSUP
# define LHDH_NSUP 1		/* Only one supported */
#endif

#define REG(u,name) ((u)->lh_reg[name])

struct lhdh {
    struct device lh_dv;	/* Generic 10 device structure */

    /* LHDH-specific vars */

    /* LHDH internal registers */
    dvureg_t lh_reg[LHR_N];	/* Storage for registers (16 bits each) */

    /* I/O signalling flags */
    int lh_ipireq;	/* Input  side doing PI request */
    int lh_opireq;	/* Output side doing PI request */
    int lh_inwakflg;	/* Always TRUE if doing SIGURG wakeup */
    int lh_inactf;	/* TRUE if input active (enabled) */
    int lh_outactf;	/* TRUE if output active */
    int lh_iwcnt;		/* Input word count */
    int lh_owcnt;		/* Output word count */
    unsigned char *lh_iptr;	/* Pointer to input data */
    unsigned char *lh_optr;	/* Pointer to output data */

    /* New clock timer stuff, for input polling */
    struct clkent *lh_chktmr;	/* Timer for periodic run re-checks */
    int lh_docheck;		/* TRUE if timer active */

    /* Misc config info not set elsewhere */
    char *lh_ifnam;	/* Native platform's interface name */
    int lh_dedic;	/* TRUE if interface dedicated (else shared) */
    int lh_doarp;	/* TRUE to do ARP hackery (if shared) */
    int lh_backlog;	/* Max # input msgs to queue up in kernel */
    int lh_rdtmo;	/* # secs to timeout on packetfilter reads */
    unsigned char lh_ipadr[4];	/* KLH10 IP address to filter on */
    unsigned char lh_gwadr[4];	/* Gateway IP address to use when needed */
    unsigned char lh_ethadr[6];	/* Ether address to use, if dedicated */

    char *lh_dpname;	/* Pointer to dev process pathname */
    int lh_dpidly;	/* # secs to sleep when starting DP */
    int lh_dpdbg;	/* Initial DP debug flag */

#if KLH10_DEV_DPIMP
    int lh_dpstate;	/* TRUE if dev process has finished its init */
    struct dp_s lh_dp;	/* Handle on dev process */
    unsigned char *lh_sbuf;	/* Pointers to shared memory buffers */
    unsigned char *lh_rbuf;
    int lh_rcnt;	/* # chars in received packet input buffer */
#endif

#if KLH10_DEV_SIMP
    int lh_imppid;	/* PID of SIMP subprocess */
    struct impio {
	int io_fd;		/* FD to carry out I/O on */
	unsigned char io_buf[IMPBUFSIZ];
    } lh_impi, lh_impo;		/* Input & output thread devices */
#endif
};

static int nlhdhs = 0;
struct lhdh dvlhdh[LHDH_NSUP];
			/* Can be static, but left external for debugging */

/* Function predecls */

static int lhdh_conf(FILE *f, char *s, struct lhdh *lh);
static int lhdh_init(struct device *d, FILE *of);
static dvureg_t lhdh_pivec(struct device *d);
static dvureg_t lhdh_read(struct device *d, uint18 addr);
static void lhdh_write(struct device *d, uint18 addr, dvureg_t val);
static void lhdh_clear(struct device *d);
static void lhdh_powoff(struct device *d);

static void lh_incheck(struct lhdh *lh);
static void lh_clear(struct lhdh *lh);
static void lh_oint(struct lhdh *lh);
static void lh_iint(struct lhdh *lh);
static void lh_igo(struct lhdh *lh);
static void lh_ogo(struct lhdh *lh);
static void lh_idone(struct lhdh *lh);
static void lh_odone(struct lhdh *lh);
static int  lh_io(struct lhdh *lh, int wrtf);
static int  lh_bcopy(struct lhdh *lh, int wrtf, paddr_t mem, int wcnt);
static void showpkt(FILE *f, char *id, unsigned char *buf, int cnt);

	/* Virtual IMP low-level stuff */
static int  imp_init(struct lhdh *lh, FILE *of);
static int  imp_start(struct lhdh *lh);
static void imp_stop(struct lhdh *lh);
static void imp_kill(struct lhdh *lh);
static int  imp_incheck(struct lhdh *lh);
static void imp_inxfer(struct lhdh *lh);
static int  imp_outxfer(struct lhdh *lh);

/* Configuration Parameters */

#define DVLHDH_PARAMS \
    prmdef(LHDHP_DBG, "debug"),	/* Initial debug value */\
    prmdef(LHDHP_BR,  "br"),	/* BR priority */\
    prmdef(LHDHP_VEC, "vec"),	/* Interrupt vector */\
    prmdef(LHDHP_ADDR,"addr"),	/* Unibus address */\
\
    prmdef(LHDHP_IP, "ipaddr"),   /* IP address of KLH10, if shared */\
    prmdef(LHDHP_GW, "gwaddr"),   /* IP address of prime GW to use */\
    prmdef(LHDHP_EN, "enaddr"),   /* Ethernet address to use (override) */\
    prmdef(LHDHP_IFC,"ifc"),      /* Ethernet interface name */\
    prmdef(LHDHP_BKL,"backlog"),/* Max bklog for rcvd pkts (else sys deflt) */\
    prmdef(LHDHP_DED,"dedic"),    /* TRUE= Ifc dedicated (else shared) */\
    prmdef(LHDHP_ARP,"doarp"),    /* TRUE= if shared, do ARP hackery */\
    prmdef(LHDHP_RDTMO,"rdtmo"),  /* # secs to timeout on packetfilter read */\
    prmdef(LHDHP_DPDLY,"dpdelay"),/* # secs to sleep when starting DP */\
    prmdef(LHDHP_DPDBG,"dpdebug"),/* Initial DP debug value */\
    prmdef(LHDHP_DP, "dppath")    /* Device subproc pathname */


enum {
# define prmdef(i,s) i
	DVLHDH_PARAMS
# undef prmdef
};

static char *lhdhprmtab[] = {
# define prmdef(i,s) s
	DVLHDH_PARAMS
# undef prmdef
	, NULL
};

static int pareth(char *cp, unsigned char *adr);
static int parip(char *cp, unsigned char *adr);

/* LHDH_CONF - Parse configuration string and set defaults.
**	At this point, device has just been created, but not yet bound
**	or initialized.
** NOTE that some strings are dynamically allocated!  Someday may want
** to clean them up nicely if config fails or device is uncreated.
*/

static int
lhdh_conf(FILE *f, char *s, struct lhdh *lh)
{
    int i, ret = TRUE;
    struct prmstate_s prm;
    char buff[200];
    long lval;

    /* First set defaults for all configurable parameters
	Unfortunately there's currently no way to access the UBA # that
	we're gonna be bound to, otherwise could set up defaults.  Later
	fix this by giving lhdh_create() a ptr to an arg structure, etc.
     */
    DVDEBUG(lh) = FALSE;
    lh->lh_ifnam = NULL;
    lh->lh_backlog = 0;
    lh->lh_dedic = FALSE;
    lh->lh_doarp = TRUE;
    lh->lh_rdtmo = 1;			/* Default to 1 sec timeout check */
    lh->lh_dpidly = 0;
    lh->lh_dpdbg = FALSE;
#if KLH10_DEV_DPIMP
    lh->lh_dpname = "dpimp";		/* Pathname of device subproc */
#elif KLH10_DEV_SIMP
    lh->lh_dpname = "simp";		/* Pathname of device subproc */
#endif

    prm_init(&prm, buff, sizeof(buff),
		s, strlen(s),
		lhdhprmtab, sizeof(lhdhprmtab[0]));
    while ((i = prm_next(&prm)) != PRMK_DONE) {
	switch (i) {
	case PRMK_NONE:
	    fprintf(f, "Unknown LHDH parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;
	case PRMK_AMBI:
	    fprintf(f, "Ambiguous LHDH parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;
	default:	/* Handle matches not supported */
	    fprintf(f, "Unsupported LHDH parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;

	case LHDHP_DBG:		/* Parse as true/false boolean or number */
	    if (!prm.prm_val)	/* No arg => default to 1 */
		DVDEBUG(lh) = 1;
	    else if (!s_tobool(prm.prm_val, &DVDEBUG(lh)))
		break;
	    continue;

	case LHDHP_BR:		/* Parse as octal number */
	    if (!prm.prm_val || !s_tonum(prm.prm_val, &lval))
		break;
	    if (lval < 4 || lval > 7) {
		fprintf(f, "LHDH BR must be one of 4,5,6,7\n");
		ret = FALSE;
	    } else
		lh->lh_dv.dv_brlev = lval;
	    continue;

	case LHDHP_VEC:		/* Parse as octal number */
	    if (!prm.prm_val || !s_tonum(prm.prm_val, &lval))
		break;
	    if (lval < 4 || lval > 0400 || (lval&03)) {
		fprintf(f, "LHDH VEC must be valid multiple of 4\n");
		ret = FALSE;
	    } else
		lh->lh_dv.dv_brvec = lval;
	    continue;

	case LHDHP_ADDR:	/* Parse as octal number */
	    if (!prm.prm_val || !s_tonum(prm.prm_val, &lval))
		break;
	    if (lval < (LHR_N<<1) || (lval&037)) {
		fprintf(f, "LHDH ADDR must be valid Unibus address\n");
		ret = FALSE;
	    } else
		lh->lh_dv.dv_addr = lval;
	    continue;

	case LHDHP_IP:		/* Parse as IP address: u.u.u.u */
	    if (!prm.prm_val)
		break;
	    if (!parip(prm.prm_val, &lh->lh_ipadr[0]))
		break;
	    continue;

	case LHDHP_GW:		/* Parse as IP address: u.u.u.u */
	    if (!prm.prm_val)
		break;
	    if (!parip(prm.prm_val, &lh->lh_gwadr[0]))
		break;
	    continue;

	case LHDHP_EN:		/* Parse as EN address in hex */
	    if (!prm.prm_val)
		break;
	    if (!pareth(prm.prm_val, &lh->lh_ethadr[0]))
		break;
	    continue;

	case LHDHP_IFC:		/* Parse as simple string */
	    if (!prm.prm_val)
		break;
	    lh->lh_ifnam = s_dup(prm.prm_val);
	    continue;

	case LHDHP_BKL:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    lh->lh_backlog = lval;
	    continue;

	case LHDHP_DED:		/* Parse as true/false boolean */
	    if (!prm.prm_val)
		break;
	    if (!s_tobool(prm.prm_val, &lh->lh_dedic))
		break;
	    continue;

	case LHDHP_ARP:		/* Parse as true/false boolean or number */
	    if (!prm.prm_val)
		break;
	    if (!s_tobool(prm.prm_val, &lh->lh_doarp))
		break;
	    continue;

	case LHDHP_RDTMO:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    lh->lh_rdtmo = lval;
	    continue;

	case LHDHP_DPDLY:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    lh->lh_dpidly = lval;
	    continue;

	case LHDHP_DPDBG:		/* Parse as true/false boolean or number */
	    if (!prm.prm_val)	/* No arg => default to 1 */
		lh->lh_dpdbg = 1;
	    else if (!s_tobool(prm.prm_val, &(lh->lh_dpdbg)))
		break;
	    continue;

	case LHDHP_DP:		/* Parse as simple string */
	    if (!prm.prm_val)
		break;
	    lh->lh_dpname = s_dup(prm.prm_val);
	    continue;

	}
	ret = FALSE;
	fprintf(f, "LHDH param \"%s\": ", prm.prm_name);
	if (prm.prm_val)
	    fprintf(f, "bad value syntax: \"%s\"\n", prm.prm_val);
	else
	    fprintf(f, "missing value\n");
    }

    /* Param string all done, do followup checks or cleanup */
    if (!lh->lh_dv.dv_brlev || !lh->lh_dv.dv_brvec || !lh->lh_dv.dv_addr) {
	fprintf(f, "LHDH missing one of BR, VEC, ADDR params\n");
	ret = FALSE;
    }
    /* Set 1st invalid addr */
    lh->lh_dv.dv_aend = lh->lh_dv.dv_addr + (LHR_N * 2);

    /* IPADDR must always be set! */
    if (memcmp(lh->lh_ipadr, "\0\0\0\0", 4) == 0) {
	fprintf(f,
	    "LHDH param \"ipaddr\" must be set\n");
	return FALSE;
    }

    return ret;
}

static int
parip(char *cp, unsigned char *adr)
{
    unsigned int b1, b2, b3, b4;

    if (4 != sscanf(cp, "%u.%u.%u.%u", &b1, &b2, &b3, &b4))
	return FALSE;
    if (b1 > 255 || b2 > 255 || b3 > 255 || b4 > 255)
	return FALSE;
    *adr++ = b1;
    *adr++ = b2;
    *adr++ = b3;
    *adr   = b4;
    return TRUE;
}

static int
pareth(char *cp, unsigned char *adr)
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

/* LHDH interface routines to KLH10 */

struct device *
dvlhdh_create(FILE *f, char *s)
{
    register struct lhdh *lh;

    /* Allocate a LHDH device structure */
    if (nlhdhs >= LHDH_NSUP) {
	fprintf(f, "Too many LHDHs, max: %d\n", LHDH_NSUP);
	return NULL;
    }
    lh = &dvlhdh[nlhdhs++];		/* Pick unused LHDH */

    /* Various initialization stuff */
    memset((char *)lh, 0, sizeof(*lh));

    iodv_setnull(&lh->lh_dv);		/* Init as null device */

    lh->lh_dv.dv_init   = lhdh_init;	/* Set up own post-bind init */
    lh->lh_dv.dv_reset  = lhdh_clear;	/* System reset (clear stuff) */
    lh->lh_dv.dv_powoff = lhdh_powoff;	/* Power-off cleanup */

    /* Unibus stuff */
    lh->lh_dv.dv_pivec = lhdh_pivec;	/* Return PI vector */
    lh->lh_dv.dv_read  = lhdh_read;	/* Read unibus register */
    lh->lh_dv.dv_write = lhdh_write;	/* Write unibus register */

    /* Configure from parsed string and remember for init
    */
    if (!lhdh_conf(f, s, lh))
	return NULL;

    return &lh->lh_dv;
}


static dvureg_t
lhdh_pivec(register struct device *d)
{
    register struct lhdh *lh = (struct lhdh *)d;

    /* This code is peculiar since LHDH is really two devices, and
    ** the output device vector setting is assumed to be 4 more than
    ** that of the input device.
    ** Give priority for now to input device.  Later toggle.
    */
    int vec = 0;

    if (lh->lh_ipireq) {
	lh->lh_ipireq = 0;
	vec = d->dv_brvec;
    } else if (lh->lh_opireq) {
	lh->lh_opireq = 0;
	vec = d->dv_brvec + 4;		/* Note the +4 !! */
    }
    if (!lh->lh_ipireq && !lh->lh_opireq) /* Unless other dir wants PI, */
	(*d->dv_pifun)(d, 0);		/* turn off interrupt request */

    return vec;			/* Return vector to use */
}

static int
lhdh_init(struct device *d, FILE *of)
{
    register struct lhdh *lh = (struct lhdh *)d;

    if (!imp_init(lh, of))
	return FALSE;
    lh_clear(lh);
    return TRUE;
}

/* LHDH_POWOFF - Handle "power-off" which usually means the KLH10 is
**	being shut down.  This is important if using a dev subproc!
*/
static void
lhdh_powoff(struct device *d)
{
    imp_kill((struct lhdh *)d);
}


static void
lhdh_clear(register struct device *d)
{
    lh_clear((struct lhdh *)d);
}

/* LH_CLEAR - clear device */
static void
lh_clear(register struct lhdh *lh)
{
    imp_stop(lh);		/* Kill IMP process, ready line going down */

    if (lh->lh_ipireq) {
	lh->lh_ipireq = 0;	/* Clear any interrupt request */
	(*lh->lh_dv.dv_pifun)(&lh->lh_dv, 0);
    }
    if (lh->lh_opireq) {
	lh->lh_opireq = 0;	/* Clear any interrupt request */
	(*lh->lh_dv.dv_pifun)(&lh->lh_dv, 0);
    }

    lh->lh_inactf = 0;
    lh->lh_outactf = 0;

    REG(lh, LHR_ICS) = LH_RDY | LH_INR;	/* Control and Status, Input side */
    REG(lh, LHR_IDB) = 0;		/* Data Buffer, Input */
    REG(lh, LHR_ICA) = 0;		/* Current Word Address, Input */
    REG(lh, LHR_IWC) = 0;		/* Word Count, Input */
    REG(lh, LHR_OCS) = LH_RDY;		/* Control and Status, Output side */
    REG(lh, LHR_ODB) = 0;		/* Data Buffer, Output */
    REG(lh, LHR_OCA) = 0;		/* Current Word Address, Output */
    REG(lh, LHR_OWC) = 0;		/* Word Count, Output */
}

static dvureg_t
lhdh_read(struct device *d, register uint18 addr)
{
    register struct lhdh *lh = (struct lhdh *)d;
    register int reg;

    reg = (addr - lh->lh_dv.dv_addr) >> 1;
    if (reg < 0 || reg >= LHR_N) {
	/* In theory ought to generate illegal IO register page-fail here,
	    but this should never happen - all addresses for this device
	    are being handled.
	 */
	panic("lhdh_read: Unknown register %lo", (long)addr);
    }

    switch (reg) {
    case LHR_ICS:		/* Control and Status, Input side */
    case LHR_IDB:		/* Data Buffer, Input */
    case LHR_ICA:		/* Current Word Address, Input */
    case LHR_IWC:		/* Word Count, Input */
    case LHR_OCS:		/* Control and Status, Output side */
    case LHR_ODB:		/* Data Buffer, Output */
    case LHR_OCA:		/* Current Word Address, Output */
    case LHR_OWC:		/* Word Count, Output */
	if (DVDEBUG(lh))
	    fprintf(DVDBF(lh), "[LHDH RReg %#o: %#o]\r\n",
					reg, (int)REG(lh, reg));
	break;

    default:
	panic("lhdh_read: Unknown register %o (%lo)", reg, (long)addr);
    }
    return REG(lh, reg);
}

/* Write LHDH registers.
*/
static void
lhdh_write(struct device *d, uint18 addr, register dvureg_t val)
{
    register struct lhdh *lh = (struct lhdh *)d;
    register int reg;

    reg = (addr - lh->lh_dv.dv_addr) >> 1;
    if (reg < 0 || reg >= LHR_N) {
	/* In theory ought to generate illegal IO register page-fail here,
	    but this should never happen - all addresses for this device
	    are being handled.
	 */
	panic("lhdh_write: Unknown register %lo", (long)addr);
    }

    val &= MASK16;
    if (DVDEBUG(lh))
	fprintf(DVDBF(lh), "[LHDH WReg %#o <= %#lo]\r\n",
					reg, (long)val);
    switch (reg) {
    case LHR_ICS:		/* Control and Status, Input side */
	if (val & LH_RST) {
	    lh_clear(lh);
	    return;
	}
	/* Clear all but settable bits in new value, and clear those in reg */
	val &= (LH_IE|LH_A17|LH_A16|LH_RST|LH_GO|LH_SE|LH_HRC);
	REG(lh, LHR_ICS) &= ~(LH_IE|LH_A17|LH_A16|LH_RST|LH_GO|LH_SE|LH_HRC
			| LH_ERR|LH_NXM|LH_MRE);	/* Clear err bits */
	REG(lh, LHR_ICS) |= val;	/* Set register! */

	/* Start or stop IMP process */
	if ((val & LH_HRC)==0) {	/* If dropping Host Ready, */
	    imp_stop(lh);		/* kill IMP process */
	    REG(lh, LHR_ICS) &= ~(LH_HR);	/* and turn off ready lines */
	    REG(lh, LHR_ICS) |= LH_INR;
	    lh->lh_inactf = lh->lh_outactf = FALSE;
	} else {			/* Host Ready turned on */
	    REG(lh, LHR_ICS) |= LH_HR;
	    if (REG(lh, LHR_ICS)&LH_INR) {	/* If IMP not already alive, */
		if (DVDEBUG(lh))
		    fprintf(DVDBF(lh), "[IMP: Starting...]\r\n");
		if (imp_start(lh)) {		/* Start it. */
		    REG(lh, LHR_ICS) &= ~LH_INR; /* Won, say IMP ready! */
		} else
		    if (DVDEBUG(lh))
			fprintf(DVDBF(lh), "[IMP: start failed!]\r\n");
	    }
	}

	/* Start or stop input */
	if (REG(lh, LHR_ICS) & LH_GO)
	    lh_igo(lh);
	else
	    lh->lh_inactf = FALSE;
	return;

    case LHR_OCS:		/* Control and Status, Output side */
	if (val & LH_RST) {
	    lh_clear(lh);
	    return;
	}
	val &= (LH_IE|LH_A17|LH_A16|LH_RST|LH_GO|LH_BB|LH_ELB);
	REG(lh, LHR_OCS) &=
	      ~(LH_IE|LH_A17|LH_A16|LH_RST|LH_GO|LH_BB|LH_ELB
			| LH_ERR|LH_NXM|LH_MRE);	/* Clear err bits */
	REG(lh, LHR_OCS) |= val;	/* Set register! */

	/* Start or stop output */
	if (REG(lh, LHR_OCS) & LH_GO)
	    lh_ogo(lh);
	else
	    lh->lh_outactf = FALSE;
	return;

    case LHR_IDB:		/* Data Buffer, Input */
    case LHR_ICA:		/* Current Word Address, Input */
    case LHR_IWC:		/* Word Count, Input */
    case LHR_ODB:		/* Data Buffer, Output */
    case LHR_OCA:		/* Current Word Address, Output */
    case LHR_OWC:		/* Word Count, Output */
	REG(lh, reg) = val;
	return;
	
    default:
	panic("lhdh_write: Unknown register %o (%lo)", reg, (long)addr);
    }
}

/* Generate LHDH output interrupt */

static void
lh_oint(register struct lhdh *lh)
{
    if (REG(lh, LHR_OCS) & LH_IE) {
	if (DVDEBUG(lh))
	    fprintf(DVDBF(lh), "[LHDH: output int]\r\n");
	lh->lh_opireq = TRUE;
	(*lh->lh_dv.dv_pifun)(&lh->lh_dv,	/* Put up interrupt */
				(int)lh->lh_dv.dv_brlev);
    }
}

/* Generate LHDH input interrupt */

static void
lh_iint(register struct lhdh *lh)
{
    if (REG(lh, LHR_ICS) & LH_IE) {
	if (DVDEBUG(lh))
	    fprintf(DVDBF(lh), "[LHDH: input int]\r\n");
	lh->lh_ipireq = TRUE;
	(*lh->lh_dv.dv_pifun)(&lh->lh_dv,	/* Put up interrupt */
				(int)lh->lh_dv.dv_brlev);
    }
}

/* Activate input side - allow IMP input to be received and processed.
*/
static void
lh_igo(register struct lhdh *lh)
{
    if (REG(lh, LHR_ICS)&LH_INR || !(REG(lh, LHR_ICS)&LH_HR)) {
	REG(lh, LHR_ICS) |= LH_ERR;
	if (DVDEBUG(lh))
	    fprintf(DVDBF(lh), "[LHDH inp err - not up]\r\n");
	lh_iint(lh);
	return;
    }
    lh->lh_inactf = TRUE;		/* OK to start reading input! */

    if (imp_incheck(lh)) {		/* Do initial check for input */
	imp_inxfer(lh);			/* Have input!  Go snarf it! */
	lh_idone(lh);			/* Finish up LH input done */
    } else {
#if !KLH10_IMPIO_INT
	/* No input now, set up to poll for input later */
	if (!lh->lh_docheck) {
	    lh->lh_docheck = TRUE;		/* Say to check again later */
	    clk_tmractiv(lh->lh_chktmr);	/* Activate timer */
	}
#endif
    }
}

/* LH input done - called to finish up IMP input
*/
static void
lh_idone(register struct lhdh *lh)
{
    REG(lh, LHR_ICS) &= ~LH_GO;	/* Turn off GO bit */
    REG(lh, LHR_ICS) |= LH_EOM;	/* Say End-Of-Message */
    if (REG(lh, LHR_IWC) == 0)
	REG(lh, LHR_ICS) |= LH_IBF;	/* Say buffer full */
    lh_iint(lh);			/* Send input interrupt! */

    lh->lh_inactf = FALSE;
#if !KLH10_IMPIO_INT
    clk_tmrquiet(lh->lh_chktmr);	/* Force timer to be quiescent */
    lh->lh_docheck = FALSE;
#endif
}

static void
lh_incheck(register struct lhdh *lh)
{
    /* Verify OK to check for input */
    if (!lh->lh_inactf)
	return;				/* Can't input, ignore */

    if (imp_incheck(lh)) {
	imp_inxfer(lh);			/* Have input!  Go snarf it! */
	lh_idone(lh);			/* Finish up LH input done */
    }
}

/* Activate output side - send a message to the IMP.
** If can't do it because an outbound message is already in progress,
** complain and cause an error.
*/
static void
lh_ogo(register struct lhdh *lh)
{
    if (REG(lh, LHR_ICS)&LH_INR || !(REG(lh, LHR_ICS)&LH_HR)) {
	REG(lh, LHR_OCS) |= LH_ERR;		/* IMP not up */
	if (DVDEBUG(lh))
	    fprintf(DVDBF(lh), "[LHDH out err - not up]\r\n");
	lh_oint(lh);
	return;
    }

    /* For time being, don't worry about partial transfers (sigh),
    ** always assume EOM bit will be set.
    */
    if (!(REG(lh, LHR_OCS)&LH_ELB)) {
	fprintf(DVDBF(lh), "[LHDH out: ELB not set!]\r\n");
    }

    if (!imp_outxfer(lh)) {
	REG(lh, LHR_OCS) |= LH_ERR;		/* IMP not up */
	if (DVDEBUG(lh))
	    fprintf(DVDBF(lh), "[LHDH out err - overrun]\r\n");
	lh_oint(lh);
	return;
    }
    /* SIMP is all done at this point. */
    /* DPIMP will call lh_evhsdon() when ready for more output. */
}

/* LH output done - called to finish up IMP output
*/
static void
lh_odone(register struct lhdh *lh)
{
    REG(lh, LHR_OCS) &= ~LH_GO;		/* Turn off GO bit */
    if (REG(lh, LHR_OWC) == 0)
	REG(lh, LHR_OCS) |= LH_OBE;	/* Say Output Buffer Empty */

    lh_oint(lh);			/* Send output interrupt! */
}

/* Do LHDH I/O.
**	First do housekeeping to set up transfer.
** The transfer has to be broken up if the UBA map
** doesn't point to contiguous half-pages.
**
** It appears that the I/O transfer updates the following:
**	WC - word count (negative)
**	CA - Bus address
**
** Note that ITS always reads 256 PDP-10 words and writes in units of
** PDP-10 words.  The IMP itself is limited to 8159 bits max packet size,
** which amounts to 1019 octets.  The first 96 bits (12 octets) are the
** IMP header, leaving 1007 data bytes.  This is rounded to PDP-10 words,
** 4 octets/word, producing 251 data words (254 words with leader).
**
*/
static int
lh_io(register struct lhdh *lh, int wrtf)
{
    register int wcnt;
    register int cnt2;
    register int i, loopcnt = 0;
    register h10_t map;
    register paddr_t mem;
    register int err = 0;
    struct ubctl *ub = lh->lh_dv.dv_uba;

    if (!wrtf && !(REG(lh, LHR_ICS)&LH_SE)) {	/* Crock... */
	/* If input and Store_Enable turned off, just flush data. */
	return 1;
    }

    for (; wcnt = (wrtf ? REG(lh, LHR_OWC) : REG(lh, LHR_IWC)); ++loopcnt) {

	wcnt = (-(wcnt | ~MASK16))>>1;		/* Find # of PDP10 words */
	if (wcnt > 01000)		/* One DEC page per pass */
	    wcnt = 01000;		/* to simplify UBA hacking */

	/* Determine memory address for sector */
	mem = wrtf ? REG(lh, LHR_OCA) : REG(lh, LHR_ICA);	/* Bus address */
	mem |= (paddr_t)((wrtf?REG(lh, LHR_OCS):REG(lh, LHR_ICS))
			& (LH_A17|LH_A16)) << 12;	/* Add extended bits */
	if (mem & ~0377774) {		/* High bit or low 2 are no-nos */
	    if (wrtf) REG(lh, LHR_OCS) |= LH_NXM | LH_ERR;
	    else REG(lh, LHR_ICS) |= LH_NXM | LH_ERR;
	    if (DVDEBUG(lh))
		fprintf(DVDBF(lh), "[LHDH %s err - mem %#lo]\r\n",
					wrtf? "out" : "in", (long)mem);
	    return 0;
	}
	mem >>= 2;
	/* High 6 bits (bit 17 known clear) are index into UB map */
	map = ub->ubpmap[i = (mem>>9)];
	if (!(map & UBA_QVAL)) {	/* If map entry not valid, */
	    if (wrtf) REG(lh, LHR_OCS) |= LH_NXM | LH_ERR;
	    else REG(lh, LHR_ICS) |= LH_NXM | LH_ERR;
	    if (DVDEBUG(lh))
		fprintf(DVDBF(lh), "[LHDH %s err - map1 %#lo]\r\n",
					wrtf? "out" : "in", (long)map);
	    return 0;
	}
	mem = ((paddr_t)(map & UBA_QPAG) << 9) | (mem & 0777);	/* Find phys addr */

	/* Determine whether transfer will cross DEC page boundary.
	** If so, split into two transfers.
	*/
	cnt2 = wcnt;
	if (((mem & 0777) + wcnt) > 01000) {
	    map = ub->ubpmap[++i];		/* Next map entry */
	    if (i >= 64 || !(map & UBA_QVAL)) {
		if (wrtf) REG(lh, LHR_OCS) |= LH_NXM | LH_ERR;
		else REG(lh, LHR_ICS) |= LH_NXM | LH_ERR;
		if (DVDEBUG(lh))
		    fprintf(DVDBF(lh), "[LHDH %s err - map2 %#lo]\r\n",
					wrtf? "out" : "in", (long)map);
		return 0;
	    }
	    if ((map & UBA_QPAG) != ((mem>>9)+1)) {
		/* Not contiguous phys page, must split up xfer. */
		register int cnt1;
		cnt1 = 01000 - (mem&0777);
		err = lh_bcopy(lh, wrtf, mem, cnt1);
		if (err == cnt1) {				/* If won, */
		    mem = (paddr_t)(map & UBA_QPAG) << 9;	/* do next part */
		    err = lh_bcopy(lh, wrtf, mem, cnt2 - cnt1);
		    if (err >= 0) err += cnt1;
		}
	    }
	} else {	/* Can do single transfer */
	    err = lh_bcopy(lh, wrtf, mem, cnt2);
	}

	/* Success, update registers to track progress.  err has
	** # of PDP10 words transferred.
	*/
	if (err <= 0)	/* If error, */
	    break;	/* stop now */
	if (wrtf) {
	    REG(lh, LHR_OWC) = (REG(lh, LHR_OWC)+(err<<1)) & MASK16;
	    REG(lh, LHR_OCA) = (REG(lh, LHR_OCA)+(err<<2)) & MASK16;
	} else {
	    REG(lh, LHR_IWC) = (REG(lh, LHR_IWC)+(err<<1)) & MASK16;
	    REG(lh, LHR_ICA) = (REG(lh, LHR_ICA)+(err<<2)) & MASK16;
	}
	if (err < cnt2)		/* If transferred less than wanted, */
	    break;		/* means that's all we can do. */
    }
    return err < 0 ? 0 : 1;	/* Return success unless saw err */
}

static int
lh_bcopy(register struct lhdh *lh, int wrtf, paddr_t mem, register int wcnt)
{
    register w10_t w;
    register unsigned char *cp;
    register vmptr_t mp = vm_physmap(mem);
    register int i;

    if (wrtf) {
	cp = lh->lh_optr;
	if (wcnt > lh->lh_owcnt) {
	    /* Too big for message buffer!  Just discard.*/
	    fprintf(DVDBF(lh), "[LHDH lhxfer output too big: %d]\r\n", wcnt);
	    return 0;
	}
	if (lh->lh_owcnt -= wcnt)		/* If there'll be anything left */
	    lh->lh_optr += wcnt * 4;	/* update both vars */
	for (i = wcnt; --i >= 0; ++mp) {
	    w = vm_pget(mp);

#if SICONF_SIMP	/* Simple byte order */
	    *cp++ = LHGET(w) >> 10;
	    *cp++ = (LHGET(w) >> 2) & 0377;
	    *cp++ = ((LHGET(w)&03)<<6) | (RHGET(w) >> 12);
	    *cp++ = (RHGET(w)>>4) & 0377;
#else	/* Unibus byte order, barf */
	    *cp++ = (LHGET(w) >> 2) & 0377;
	    *cp++ = LHGET(w) >> 10;
	    *cp++ = (RHGET(w)>>4) & 0377;
	    *cp++ = ((LHGET(w)&03)<<6) | (RHGET(w) >> 12);
#endif
	}
    } else {
	cp = lh->lh_iptr;
	if (wcnt > lh->lh_iwcnt) wcnt = lh->lh_iwcnt;
	if (lh->lh_iwcnt -= wcnt)		/* If there'll be anything left */
	    lh->lh_iptr += wcnt * 4;	/* update both vars */

	for (i = wcnt; --i >= 0; ++mp, cp += 4) {
#if SICONF_SIMP	/* Simple byte order */
	    LRHSET(w,
		((uint18)cp[0]<<10) | (cp[1]<<2) | (cp[2]>>6),
		((uint18)(cp[2]&077)<<12) | (cp[3]<<4) );
#else	/* Unibus byte order, barf */
	    LRHSET(w,
		((uint18)cp[1]<<10) | (cp[0]<<2) | (cp[3]>>6),
		((uint18)(cp[3]&077)<<12) | (cp[2]<<4) );
#endif
	    vm_pset(mp, w);
	}
    }
    return wcnt;
}

/* VIRTUAL IMP ROUTINES
*/

/* Dummy IMP if none actually being used; pretend it's dead.
 */
#if !KLH10_DEV_SIMP && !KLH10_DEV_DPIMP

static int  imp_init(struct lhdh *lh, FILE *of)  { return TRUE; }
static int  imp_start(struct lhdh *lh) { return 0; } 
static void imp_stop(struct lhdh *lh)  { }
static void imp_kill(struct lhdh *lh)  { }

static void imp_inxfer(struct lhdh *lh)  { }
static int  imp_outxfer(struct lhdh *lh) { return 0; }
static int  imp_incheck(struct lhdh *lh) { return 0; }

#endif

#if KLH10_DEV_SIMP || KLH10_DEV_DPIMP

/* Utility routines used by both SIMP, DPIMP */

static void
showpkt(FILE *f, char *id, unsigned char *buf, int cnt)
{
    char linbuf[200];
    register int i;
    int once = 0;
    register char *cp;

    while (cnt > 0) {
	cp = linbuf;
	if (once++) *cp++ = '\t';
	else sprintf(cp, "%6s: ", id), cp += 8;

	for (i = 16; --i >= 0;) {
	    sprintf(cp, " %3o", *buf++);
	    cp += 4;
	    if (--cnt <= 0) break;
	}
	*cp = 0;
	fprintf(f, "%s\r\n", linbuf);
    }
}
#endif /* KLH10_DEV_SIMP || KLH10_DEV_DPIMP */


#if KLH10_DEV_DPIMP

static void lh_evhrwak(struct device *d, struct dvevent_s *evp);
static void lh_evhsdon(struct device *d, struct dvevent_s *evp);

static int
imp_init(register struct lhdh *lh, FILE *of)
{
    register struct dpimp_s *dpc;
    struct dvevent_s ev;
    size_t junk;

    lh->lh_dpstate = FALSE;
    if (!dp_init(&lh->lh_dp, sizeof(struct dpimp_s),
			DP_XT_MSIG, SIGUSR1, (size_t)IMPBUFSIZ,	   /* in */
			DP_XT_MSIG, SIGUSR1, (size_t)IMPBUFSIZ)) { /* out */
	if (of) fprintf(of, "IMP subproc init failed!\n");
	return FALSE;
    }
    lh->lh_sbuf = dp_xsbuff(&(lh->lh_dp.dp_adr->dpc_todp), &junk);
    lh->lh_rbuf = dp_xrbuff(&(lh->lh_dp.dp_adr->dpc_frdp), &junk);

    lh->lh_dv.dv_dpp = &(lh->lh_dp);	/* Tell CPU where our DP struct is */

    /* Set up DPIMP-specific part of shared DP memory */
    dpc = (struct dpimp_s *) lh->lh_dp.dp_adr;
    dpc->dpimp_dpc.dpc_debug = lh->lh_dpdbg;	/* Init DP debug flag */
    if (cpu.mm_locked)				/* Lock DP mem if CPU is */
	dpc->dpimp_dpc.dpc_flags |= DPCF_MEMLOCK;

    dpc->dpimp_ver = DPIMP_VERSION;
    dpc->dpimp_attrs = 0;
    dpc->dpimp_blobsiz = DPIMP_BLOB_SIZE;

    dpc->dpimp_backlog = lh->lh_backlog;	/* Pass on backlog value */
    dpc->dpimp_dedic = lh->lh_dedic;	/* Pass on dedicated flag */
    dpc->dpimp_doarp = lh->lh_doarp;	/* Pass on DOARP flag */
    dpc->dpimp_rdtmo = lh->lh_rdtmo;	/* Pass on RDTMO value */

    if (lh->lh_ifnam)			/* Pass on interface name if any */
	strncpy(dpc->dpimp_ifnam, lh->lh_ifnam, sizeof(dpc->dpimp_ifnam)-1);
    else
	dpc->dpimp_ifnam[0] = '\0';	/* No specific interface */
    memcpy((char *)dpc->dpimp_ip,	/* Set our IP address for filter */
		lh->lh_ipadr, 4);
    memcpy((char *)dpc->dpimp_gw,	/* Set our GW address for IMP */
		lh->lh_gwadr, 4);
    memcpy(dpc->dpimp_eth,		/* Set EN address if any given */
		lh->lh_ethadr, 6);	/* (all zero if none) */

    /* Register ourselves with main KLH10 loop for DP events */

    ev.dvev_type = DVEV_DPSIG;		/* Event = Device Proc signal */
    ev.dvev_arg.eva_int = SIGUSR1;
    ev.dvev_arg2.eva_ip = &(lh->lh_dp.dp_adr->dpc_todp.dpx_donflg);
    if (!(*lh->lh_dv.dv_evreg)((struct device *)lh, lh_evhsdon, &ev)) {
	if (of) fprintf(of, "IMP event reg failed!\n");
	return FALSE;
    }

    ev.dvev_type = DVEV_DPSIG;		/* Event = Device Proc signal */
    ev.dvev_arg.eva_int = SIGUSR1;
    ev.dvev_arg2.eva_ip = &(lh->lh_dp.dp_adr->dpc_frdp.dpx_wakflg);
    if (!(*lh->lh_dv.dv_evreg)((struct device *)lh, lh_evhrwak, &ev)) {
	if (of) fprintf(of, "IMP event reg failed!\n");
	return FALSE;
    }
    return TRUE;
}

static int
imp_start(register struct lhdh *lh)
{
    register int res;

    if (DVDEBUG(lh))
	fprintf(DVDBF(lh), "[imp_start: starting DP \"%s\"...",
				lh->lh_dpname);

    /* HORRIBLE UGLY HACK: for AXP OSF/1 and perhaps other systems,
    ** the virtual-runtime timer of setitimer() remains in effect even
    ** for the child process of a fork()!  To avoid this, we must
    ** temporarily turn the timer off, then resume it after the fork
    ** is safely out of the way.
    **
    ** Otherise, the timer would go off and the unexpected signal would
    ** chop down the DP subproc without any warning!
    **
    ** Later this should be done in DPSUP.C itself, when I can figure a
    ** good way to tell whether the code is part of the KLH10 or a DP
    ** subproc.
    */
    clk_suspend();			/* Clear internal clock if one */
    res = dp_start(&lh->lh_dp, lh->lh_dpname);
    clk_resume();			/* Resume internal clock if one */

    if (!res) {
	if (DVDEBUG(lh))
	    fprintf(DVDBF(lh), " failed!]\r\n");
	else
	    fprintf(DVDBF(lh), "[imp_start: Start of DP \"%s\" failed!]\r\n",
				lh->lh_dpname);
	return FALSE;
    }
    if (DVDEBUG(lh))
	fprintf(DVDBF(lh), " started!]\r\n");

    return TRUE;
}

/* IMP_STOP - Stops IMP and drops Host Ready by killing IMP subproc,
**	but allow restarting.
*/
static void
imp_stop(register struct lhdh *lh)
{
    if (DVDEBUG(lh))
	fprintf(DVDBF(lh), "[IMP: stopping...");

    dp_stop(&lh->lh_dp, 1);	/* Say to kill and wait 1 sec for synch */

    lh->lh_dpstate = FALSE;	/* No longer there and ready */
    if (DVDEBUG(lh))
	fprintf(DVDBF(lh), " stopped]\r\n");
}


/* IMP_KILL - Kill IMP process permanently, no restart.
*/
static void
imp_kill(register struct lhdh *lh)
{
    if (DVDEBUG(lh))
	fprintf(DVDBF(lh), "[IMP kill]\r\n");

    lh->lh_dpstate = FALSE;
    (*lh->lh_dv.dv_evreg)(	/* Flush all event handlers for device */
		(struct device *)lh,
		NULLPROC,
		(struct dvevent_s *)NULL);
    dp_term(&(lh->lh_dp), 0);	/* Flush all subproc overhead */
    lh->lh_sbuf = NULL;		/* Clear pointers no longer meaningful */
    lh->lh_rbuf = NULL;
}


/* LH_EVHRWAK - Invoked by INSBRK event handling when
**	signal detected from DP saying "wake up"; the DP is sending
**	us an input packet.
*/
static void
lh_evhrwak(struct device *d, struct dvevent_s *evp)
{
    register struct lhdh *lh = (struct lhdh *)d;

    if (DVDEBUG(lh))
	fprintf(DVDBF(lh), "[LHDH input wakeup: %d]",
				(int)dp_xrtest(dp_dpxfr(&lh->lh_dp)));

    /* Always check IMP input in order to process any non-data messages
    ** regardless of whether LH is actively reading,
    ** then invoke general LH check to do data transfer if OK.
    */
    if (imp_incheck(lh) && lh->lh_inactf) {
	imp_inxfer(lh);			/* Have input!  Go snarf it! */
	lh_idone(lh);			/* Finish up LH input done */
    }
}

static void
lh_evhsdon(struct device *d, struct dvevent_s *evp)
{
    register struct lhdh *lh = (struct lhdh *)d;
    register struct dpx_s *dpx = dp_dpxto(&lh->lh_dp);

    if (DVDEBUG(lh))
	fprintf(DVDBF(lh), "[lh_evhsdon: %d]", (int)dp_xstest(dpx));

    if (dp_xstest(dpx)) {	/* Verify message is done */
	lh_odone(lh);		/* Say LH output done */
    }
}


/* Start IMP output.
*/
static int
imp_outxfer(register struct lhdh *lh)
{
    register int cnt;
    register struct dpx_s *dpx = dp_dpxto(&lh->lh_dp);

    /* Make sure we can output message and fail if not */
    if (!dp_xstest(dpx)) {
	fprintf(DVDBF(lh), "[IMP: DP out blocked]\r\n");
	return 0;
    }

    /* Output xfer requested! */
    lh->lh_owcnt = IMPBUFSIZ/4;
    lh->lh_optr = lh->lh_sbuf + DPIMP_DATAOFFSET;
    if (lh_io(lh, TRUE)) {		/* Xfer data from mem! */
	register struct dpimp_s *dpc = (struct dpimp_s *) lh->lh_dp.dp_adr;

	cnt = ((IMPBUFSIZ/4) - lh->lh_owcnt) * 4;
	if (DVDEBUG(lh) & DVDBF_DATSHO)	/* Show data? */
	    showpkt(DVDBF(lh), "PKTOUT", lh->lh_sbuf + DPIMP_DATAOFFSET, cnt);

	dpc->dpimp_outoff = DPIMP_DATAOFFSET;
	dp_xsend(dpx, DPIMP_SPKT, (size_t)cnt + DPIMP_DATAOFFSET);

	if (DVDEBUG(lh))
	    fprintf(DVDBF(lh), "[IMP: Out %d]\r\n", cnt);

    } else {
	/* No real transfer, but pretend completed */
	lh_odone(lh);			/* Say LH output done */
    }
    return 1;
}

static int
imp_incheck(register struct lhdh *lh)
{
    register struct dpx_s *dpx = dp_dpxfr(&lh->lh_dp);

    if (dp_xrtest(dpx)) {	/* Verify there's a message for us */
	switch (dp_xrcmd(dpx)) {
	case DPIMP_INIT:
	    /* Do stuff to turn on IMP ready line? */
	    dp_xrdone(dpx);		/* ACK it */
	    return 0;			/* No actual input */

	case DPIMP_RPKT:		/* Input packet ready! */
	    if (DVDEBUG(lh))
		fprintf(DVDBF(lh), "[IMP: inbuf %ld]\r\n",
			    (long) dp_xrcnt(dpx));
	    return 1;

	default:
	    if (DVDEBUG(lh))
		fprintf(DVDBF(lh), "[IMP: R %d flushed]", dp_xrcmd(dpx));
	    dp_xrdone(dpx);			/* just ACK it */
	    return 0;
	}
    }

    return 0;
}


/* IMP_INXFER - IMP Input.
**	For time being, don't worry about partial transfers (sigh),
**	which might have left part of a previous message still lying
**	around waiting for the next read request.
*/
static void
imp_inxfer(register struct lhdh *lh)
{
    register int err, cnt;
    register struct dpx_s *dpx = dp_dpxfr(&lh->lh_dp);
    register struct dpimp_s *dpc = (struct dpimp_s *) lh->lh_dp.dp_adr;
    register unsigned char *pp;

    /* Assume this is ONLY called after verification by imp_incheck that
    ** an input message is actually ready.
    */
    cnt = dp_xrcnt(dpx);

    /* Adjust for possible offset */
    cnt -= dpc->dpimp_inoff;
    pp = lh->lh_rbuf + dpc->dpimp_inoff;

    if (DVDEBUG(lh))
	fprintf(DVDBF(lh), "[IMP: In %d]\r\n", cnt);
    if (DVDEBUG(lh) & DVDBF_DATSHO)	/* Show data? */
	showpkt(DVDBF(lh), "PKTIN ", pp, cnt);

    /* Message read in!  Now carry out DMA xfer! */
    if (cnt & 03) {		/* If msg len not multiple of 4, pad out */
	for (err = 4 - (cnt&03); --err >= 0; ++cnt)
	    pp[cnt] = '\0';
    }

    /* Set up args for lh_io() */
    lh->lh_iwcnt = cnt / 4;
    lh->lh_iptr = pp;
    lh_io(lh, 0);		/* Do it, update regs */

    dp_xrdone(dpx);		/* Done, can now ACK */
}

#endif /* KLH10_DEV_DPIMP */

#if KLH10_DEV_SIMP		/* Old Piped IMP stuff - kept for posterity */

static void imp_intmo(void *lh);
#if KLH10_IMPIO_INT
static void lh_inwak(struct device *d, struct dvevent_s *evp);
#endif

static int
imp_init(register struct lhdh *lh, FILE *of)
{
    lh->lh_imppid = 0;
    lh->lh_impi.io_fd = -1;
    lh->lh_impo.io_fd = -1;

#if KLH10_IMPIO_INT
  {
    struct dvevent_s ev;

    /* Register ourselves with main KLH10 loop for DP events */
    /* Note this registers for SIGURG, not the normal SIGUSR1.
    ** Also, only input-ready events are signalled, not output-done
    ** (which still blocks for the pipe-technique SIMP)
    */

    ev.dvev_type = DVEV_DPSIG;		/* Event = Device Proc signal */
    ev.dvev_arg.eva_int = SIGURG;
    ev.dvev_arg2.eva_ip = &(lh->lh_inwakflg);
    if (!(*lh->lh_dv.dv_evreg)((struct device *)lh, lh_inwak, &ev)) {
	if (of) fprintf(of, "LHDH event reg failed!\n");
	return FALSE;
    }
    lh->lh_inwakflg = TRUE;	/* Reg clears, but always keep this set! */
  }
#else
    /* Polling - Set up periodic input check timer */
    if (!lh->lh_chktmr)			/* Check once per interval timeout */
	lh->lh_chktmr = clk_itmrget(imp_intmo, (void *)lh);
    clk_tmrquiet(lh->lh_chktmr);	/* Immediately make it quiescent */
    lh->lh_docheck = FALSE;
#endif
    return TRUE;
}

#if KLH10_IMPIO_INT

/* LH_INWAK - Invoked by INSBRK event handling when
**	signal detected from DP saying "wake up"; the DP is sending
**	us an input packet.
*/
static void
lh_inwak(struct device *d, struct dvevent_s *evp)
{
    register struct lhdh *lh = (struct lhdh *)d;

    if (DVDEBUG(lh))
	fprintf(DVDBF(lh), "[LHDH input wakeup]\r\n");

    lh->lh_inwakflg = TRUE;		/* Always reset flag to TRUE */
    lh_incheck(lh);			/* Handle any IMP input now */
}

#else

static void
imp_intmo(void *lh)
{
    lh_incheck((struct lhdh *)lh);
}
#endif /* !KLH10_IMPIO_INT */

static int
imp_start(register struct lhdh *lh)
{
#define PIPEIN 0
#define PIPEOUT 1
    int fdin[2], fdout[2];
    int err;
    char impaddr[32], gwaddr[32];
    int i;
    char signum[32];
    char *impargs[32];		/* Should be enough args */

    /* No-op if already set up, else verify things are consistent */
    if (lh->lh_imppid) {
	if (lh->lh_impi.io_fd >= 0 && lh->lh_impo.io_fd >= 0)
	    return TRUE;
	fprintf(DVDBF(lh),"[IMP: no IO threads??]\r\n");
	imp_stop(lh);
    } else if (lh->lh_impi.io_fd >= 0 || lh->lh_impo.io_fd >= 0) {
	fprintf(DVDBF(lh),"[IMP: IO but no IMP??]\r\n");
	imp_stop(lh);
    }

/* From here on, things get very system-dependent */
#if CENV_SYS_UNIX	/* Originally SUN - generic enough? */

    /* Will need IP addresses in string form */
    sprintf(impaddr, "%d.%d.%d.%d",
		lh->lh_ipadr[0], lh->lh_ipadr[1],
		lh->lh_ipadr[2], lh->lh_ipadr[3]);
    sprintf(gwaddr, "%d.%d.%d.%d",
		lh->lh_gwadr[0], lh->lh_gwadr[1],
		lh->lh_gwadr[2], lh->lh_gwadr[3]);

    /* Fire up the IMP subprocess */
    if ((err = access(lh->lh_dpname, X_OK)) < 0) {	/* Check xct access */
	fprintf(DVDBF(lh), "[IMP: Cannot access \"%s\" - %s]\r\n",
			lh->lh_dpname ? lh->lh_dpname : "(nullptr)",
			os_strerror(err));
	return 0;
    }
    if ((err = pipe(fdin)) < 0) {
	fprintf(DVDBF(lh), "[IMP: Cannot pipe - %s]\r\n", os_strerror(err));
	return 0;
    }
    if ((err = pipe(fdout)) < 0) {
	fprintf(DVDBF(lh), "[IMP: Cannot pipe - %s]\r\n", os_strerror(err));
	close(fdin[0]), close(fdin[1]);
	return 0;
    }

    /* FIX GODDAM SUN LWP LOSSAGE!!!!
    ** The NBIO library for LWPs has decided to carefully smash FD flags
    ** back to their original state whenever a close() is done on them,
    ** which of course affects every process using those flags.  What happens
    ** in a normal fork-exec sequence is that the child attempts to close
    ** the unneeded pipe FDs, however the child is still running in the
    ** goddam NBIO context (goddam unix fork semantics) and the flag
    ** clearing done by NBIO's close in the child also zaps them for
    ** the parent, which needs to keep the FASYNC and FNDELAY flags set
    ** if it wants to avoid blocking!  Goddam Unix FD semantics.
    **
    ** For some reason, the developers forgot to cover all the loopholes
    ** and a way out of this particular madness does exist.  By setting
    ** the F_SETFD flag on the specific doomed FDs, they can be closed
    ** invisibly as a side effect of the exec() call, which NBIO doesn't
    ** know about.  Goddam.  (Well, what do you expect after wasting
    ** several more hours wallowing in yet another Unix cesspool?)
    ** Goddam.
    */

#define FIXSUNLOSSAGE 1
#if FIXSUNLOSSAGE
    /* Force the child to close these FDs as a side effect of its exec()
     */
    if (fcntl(fdin[PIPEIN], F_SETFD, 1) < 0	
      || fcntl(fdout[PIPEOUT], F_SETFD, 1) < 0) {
	fprintf(DVDBF(lh), "[IMP: Cannot fcntl - %s]\r\n", os_strerror(errno));
	close(fdin[0]), close(fdin[1]);
	close(fdout[0]), close(fdout[1]);
	return 0;
    }
#endif
    if ((lh->lh_imppid = fork()) < 0) {
	lh->lh_imppid = 0;
	fprintf(DVDBF(lh), "[IMP: Cannot fork - %s]\r\n", os_strerror(errno));
	close(fdin[0]), close(fdin[1]);
	close(fdout[0]), close(fdout[1]);
	return 0;
    }
    if (lh->lh_imppid == 0) {		/* We're the child? */
	/* Child process code */
#if !FIXSUNLOSSAGE	/* See note above */
	close(fdin[PIPEIN]);
	close(fdout[PIPEOUT]);
#endif
	if (dup2(fdin[PIPEOUT], 1) != 1
	  || dup2(fdout[PIPEIN], 0) != 0) {
	    fprintf(DVDBF(lh), "[IMP: Cannot dup2 - %s]\r\n",
		os_strerror(errno));
	    exit(1);
	}

	/* Set up args for IMP sub-process */
	impargs[i = 0] = "SIMP";
	impargs[++i] = impaddr;		/* KLH10 address */
	impargs[++i] = "-g";		/* Prime GW address */
	impargs[++i] = gwaddr;
	if (lh->lh_dpdbg)
	    impargs[++i] = "-d";	/* Debug flag */
	if (lh->lh_ifnam) {
	    impargs[++i] = "-i";	/* Interface spec */
	    impargs[++i] = lh->lh_ifnam;
	}
#if KLH10_IMPIO_INT
	sprintf(signum, "%d", SIGURG);
	impargs[++i] = "-s";		/* Send SIGURG when input ready */
	impargs[++i] = signum;
#endif
	impargs[++i] = NULL;		/* Tie off arg list */

	execv(lh->lh_dpname, impargs);

	fprintf(DVDBF(lh), "[IMP: execv failed - %s]\r\n",
		os_strerror(errno));
	exit(1);
    } else {
	close(fdin[PIPEOUT]);		/* Parent, flush unused FDs */
	close(fdout[PIPEIN]);
    }
#endif /* CENV_SYS_UNIX */

    /* Now finalize two independent threads to do I/O for it */
    lh->lh_impi.io_fd = fdin[PIPEIN];
    lh->lh_impo.io_fd = fdout[PIPEOUT];

    return TRUE;
}



/* IMP_STOP - Carry out dropping of Host Ready by killing IMP process.
**	The wait() call will be a problem as soon as more devices are
**	emulated by subprocesses (eg magtape).
*/
static void
imp_stop(register struct lhdh *lh)
{
    if (DVDEBUG(lh))
	fprintf(DVDBF(lh), "[IMP stop - %d]\r\n", lh->lh_imppid);

    if (lh->lh_imppid) {
	int status;
	kill(lh->lh_imppid, SIGKILL);
	wait(&status);
	lh->lh_imppid = 0;
    }
    if (lh->lh_impi.io_fd >= 0) {
	close(lh->lh_impi.io_fd);
	lh->lh_impi.io_fd = -1;
    }
    if (lh->lh_impo.io_fd >= 0) {
	close(lh->lh_impo.io_fd);
	lh->lh_impo.io_fd = -1;
    }
}

/* IMP_KILL - Permanent kill, no restart
*/
static void
imp_kill(register struct lhdh *lh)
{
    if (DVDEBUG(lh))
	fprintf(DVDBF(lh), "[IMP kill - %d]\r\n", lh->lh_imppid);

    (*lh->lh_dv.dv_evreg)(	/* Flush all event handlers for device */
		(struct device *)lh,
		(void (*)())NULL,
		(struct dvevent_s *)NULL);
    imp_stop(lh);
}


static int
imp_incheck(register struct lhdh *lh)
{
    /* OSD WARNING: the FIONREAD ioctl is defined to want a "long" on SunOS
    ** and presumably old BSD, but it uses an "int" on Solaris and DEC OSF/1!
    ** Leave undefined for unknown systems to ensure this is checked for
    ** each new port.
    */
#if CENV_SYS_SUN
    long retval;
#elif CENV_SYS_SOLARIS || CENV_SYS_DECOSF || CENV_SYS_XBSD || CENV_SYS_LINUX
    int retval;
#endif

    if (ioctl(lh->lh_impi.io_fd, FIONREAD, &retval)	/* If call fails, */
	|| !retval)			/* or if returned zilch */
	return 0;			/* assume no input waiting */

    if (DVDEBUG(lh))
	fprintf(DVDBF(lh), "[IMP: inpipe %ld]\r\n", (long)retval);

    return (int) retval;
}


/* IMP_INXFER - IMP Input loop.
*/
static int fullread(int fd, unsigned char *buf, int cnt);

static void
imp_inxfer(register struct lhdh *lh)
{
    register int err, cnt;
    register unsigned char *buf = lh->lh_impi.io_buf;
    int fd = lh->lh_impi.io_fd;

    /* Input xfer requested! */
    /* For time being, don't worry about partial transfers (sigh),
    ** which might have left part of a previous message still lying
    ** around waiting for the next read request.
    */
    if (err = fullread(fd, buf, SIH_HSIZ)) {
	fprintf(DVDBF(lh), "[IMP: hdr read failed - %s]\r\n",
				os_strerror(err));
	return;
    }
    if (buf[0] != SIH_HDR || buf[1] != SIH_TDATA) {
	fprintf(DVDBF(lh), "[IMP: bad header - %#o]\r\n", buf[0]);
	/* Perhaps attempt to get back in synch */
	return;
    }
    cnt = ((buf[2]&0377)<<8) + (buf[3]&0377);
    if (cnt >= IMPBUFSIZ) {
	fprintf(DVDBF(lh), "[IMP: count too big - %d]\r\n", cnt);
	/* Perhaps attempt to get back in synch */
	return;
    }
    if (err = fullread(fd, buf, cnt)) {
	fprintf(DVDBF(lh), "[IMP: read failed - %s]\r\n",
				os_strerror(err));
	return;
    }

    if (DVDEBUG(lh))
	fprintf(DVDBF(lh), "[IMP: In %d]\r\n", cnt);
    if (DVDEBUG(lh) & DVDBF_DATSHO)	/* Show data? */
	showpkt(DVDBF(lh), "PKTIN ", buf, cnt);


    /* Message read in!  Now carry out DMA xfer! */
    if (cnt & 03) {		/* If msg len not multiple of 4, pad out */
	for (err = 4 - (cnt&03); --err >= 0; ++cnt)
	    buf[cnt] = '\0';
    }

    /* Set up args for lh_io() */
    lh->lh_iwcnt = cnt / 4;
    lh->lh_iptr = buf;
    lh_io(lh, 0);		/* Do it, update regs */
}

static int
fullread(int fd, unsigned char *buf, int cnt)
{
    register int err;

    while ((err = read(fd, buf, cnt)) != cnt) {
	if (err <= 0) {
	    return err ? errno : EPIPE;		/* Sigh, hack hack */
	}
	cnt -= err;
	buf += err;
    }
    return 0;		/* Counted out, won */
}


static int
imp_outxfer(register struct lhdh *lh)
{
    register int err, cnt;
    register unsigned char *buf = lh->lh_impo.io_buf;
    int fd = lh->lh_impo.io_fd;

    /* Output xfer requested! */
    lh->lh_owcnt = (IMPBUFSIZ - SIH_HSIZ)/4;
    lh->lh_optr = buf + SIH_HSIZ;
    if (lh_io(lh, TRUE)) {		/* Xfer data from mem! */
	cnt = (((IMPBUFSIZ - SIH_HSIZ)/4) - lh->lh_owcnt) * 4;
	buf[0] = SIH_HDR;
	buf[1] = SIH_TDATA;
	buf[2] = (cnt >> 8) & 0377;
	buf[3] = cnt & 0377;
	if (DVDEBUG(lh) & DVDBF_DATSHO)	/* Show data? */
	    showpkt(DVDBF(lh), "PKTOUT", buf+4, cnt);
	err = write(fd, buf, cnt + 4);
	if (DVDEBUG(lh))
	    fprintf(DVDBF(lh), "[IMP: Out %d]\r\n", err);

	if (err != (cnt+4)) {
	    if (err < 0)
		fprintf(DVDBF(lh), "[imp_outxfer: write failed - %s]\r\n",
			    os_strerror(err));
	}
    }

    lh_odone(lh);			/* Say LH output done */
    return 1;
}
#endif /* KLH10_DEV_SIMP */

#endif /* KLH10_DEV_LHDH */
