/* DVDTE.C - Emulates DTE20 10/11 interface for KL10
*/
/* $Id: dvdte.c,v 2.7 2002/04/24 07:56:08 klh Exp $
*/
/*  Copyright © 1993, 2001 Kenneth L. Harrenstien
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
 * $Log: dvdte.c,v $
 * Revision 2.7  2002/04/24 07:56:08  klh
 * Add os_msleep, using nanosleep
 *
 * Revision 2.6  2002/03/28 16:49:35  klh
 * Another DTE_NQNODES bump to 300
 *
 * Revision 2.5  2002/03/26 06:18:24  klh
 * Add correct timezone to DTE's time info
 *
 * Revision 2.4  2002/03/21 09:50:08  klh
 * Mods for CMDRUN (concurrent mode)
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include "klh10.h"

#if KLH10_DEV_DTE		/* Moby conditional for entire file */

#include <stddef.h>	/* For size_t etc */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "kn10def.h"	/* This includes OSD defs */
#include "kn10ops.h"
#include "kn10dev.h"
#include "dvdte.h"
#include "prmstr.h"	/* For parameter parsing */

#ifdef RCSID
 RCSID(dvdte_c,"$Id: dvdte.c,v 2.7 2002/04/24 07:56:08 klh Exp $")
#endif

/* Internal DTE packet for RSX20F protocol */

struct dtepkt_s {
			/* These are all 16-bit values */
    unsigned int dtep_tcnt;	/* Total byte count of entire msg */

    unsigned int dtep_cnt;	/* byte count from header field */
    unsigned int dtep_fn;	/* function # */
    unsigned int dtep_dev;	/* Device */
    unsigned char dtep_db0;	/* First data byte */
    unsigned char dtep_db1;	/* Second data byte */

    int dtep_wdmod;	/* TRUE if rest of data in word mode */

    unsigned char dtep_data[256];
};

struct dteq_s {
    struct dteq_s *q_next;

    unsigned int q_sfn;		/* Swapped function # */
    unsigned int q_sdev;	/* Swapped Device */
    unsigned int q_swd1;	/* Swapped First data word */

    int q_bbcnt;		/* Direct byte data byte count */
    int q_wbcnt;		/* Indirect word data byte count */
    unsigned char *q_dcp;	/* Ptr to data (ignored if both counts 0) */
};

/* Swap bytes */
#define SWAB(i) ((((i)>>8)&0377) | (((i)&0377)<<8))

#define SFN_INDBIT SWAB(1<<15)	/* High bit of function is @ bit */

#ifndef  DTE_NQNODES
# define DTE_NQNODES 300	/* Maybe should be dynamic param */
#endif
struct dteq_s *dteqfreep = NULL;
struct dteq_s dteqnodes[DTE_NQNODES];


struct dte {
    struct device dt_dv;	/* Generic 10 device structure */

    /* DTE-specific vars */
    int dt_dten;		/* # of this DTE (0-3) */
    unsigned int dt_cond;	/* Condition bits (only 16 needed) */
    int dt_pilev;		/* PI level bit mask */
    w10_t dt_piwd;		/* PI vector word (fixed after inited) */

    int dt_dowarn;		/* TRUE to print warnings (semi-debug) */
    int dt_ismaster;		/* TRUE if this DTE is master */
    int dt_secptcl;		/* TRUE if using secondary ptcl
				** (monitor mode), else primary */
    int dt_reld11;		/* 11 Reload button */
    int dt_dtmsent;		/* TRUE if date/time sent to 10. */

    /* EPT shadow vars - only updated when entering primary ptcl */
    vmptr_t	 dt_eptcb;	/* VM pointer to DTE ctl blk in EPT */
    unsigned int dt_eptoff;	/* Offset into EPT for this DTE */
    unsigned int dt_eptesz;	/* Examine protection (size of area) */
    vmptr_t      dt_eptexa;	/* Examine Relocation */
    unsigned int dt_eptdsz;	/* Deposit protection (size of area) */
    vmptr_t      dt_eptdep;	/* Deposit Relocation */

    /* Comm region vars (11 side)
    **	These are primarily offsets from the DTE20's examine relocation addr.
    **	Use the proper variable to access the corresponding com region.
    **	Note special case dt_dt10off which is only used for DEPOSIT and
    **	refers to the same place that dt_et10off reads.
    */
				/* FE var */
    int dt_procn;		/* PRMEMN Our processor # */

    unsigned int dt_combase;	/* COMBSE Exa offset to base */
    unsigned int dt_doff;	/* DEPOF  Exa offset to our 11's comdat */
				/*    == Offset from exa reloc to dep reloc */
				/*    i.e. refs same loc as DTE dep reloc! */
    unsigned int dt_et10off;	/* EMYN   Exa off to our 11's to-10 comrgn */
    unsigned int dt_dt10off;	/* DMYN   DEP off to our 11's   "      "   */
    unsigned int dt_ec10off;	/* EHSG   Exa off to 10's general comdat */
    unsigned int dt_et11off;	/* EHSM   Exa off to 10's to-our-11 comrgn */


    /* Stuff for active to-10 (send to 10) xfer */
    int dt_snd_ibit;		/* To-10 "I" bit, set by DATAO */
    int dt_snd_cnt;		/* To-10 byte cnt, set by DATAO */
    w10_t dt_snd_sts;		/* To-10 Status word (internal copy) */
    int dt_snd_state;		/* To-10 sending state */
#	define DTSND_HDR 0	/*	Send header next */
#	define DTSND_DAT 1	/*	Sending data bytes */
#	define DTSND_IND 2	/*	Sending indirect data */
    struct dteq_s *dt_sndq;	/* To-10 send queue */
    struct dteq_s *dt_sndqtail;	/* To-10 send queue tail ptr */
    struct dtepkt_s dt_sndpkt;


    /* Stuff for active to-11 (receive from 10) xfer */
    int dt_rcv_gothdr;		/* TRUE if header received */
    int dt_rcv_indir;		/* TRUE if in indirect xfer */
    int dt_rcv_11qc;		/* Current to-11 queue count */
    struct dtepkt_s dt_rcvpkt;

    /* New clock timer stuff */
    struct clkent *dt_kpal;	/* Timer for keepalive update */

    int32 dt_dlyackms;		/* Delayed ACK timeout in msec */
    struct clkent *dt_dlyack;	/* Timer for Delayed ACK */
    int dt_dlyackf;		/* TRUE if timer active */

    /* KLDCP stuff, mostly clock */
    int dt_clkf;		/* 0=off, 1=on, 2=on with countdown */
    struct clkent *dt_clk;	/* Timer for 60Hz ticks */
    uint32 dt_clkticks;		/* # ticks since last enabled */
    int32 dt_clkcnt;		/* # ticks left before interrupt 10 */

    /* CTY buffer stuff - just output for now */
    int dt_ctyobs;		/* Desired output buffer size to tell 10 */
    int dt_ctyocnt;		/* # chars room left in buffer */
    char *dt_ctyocp;		/* Deposit pointer */
    char dt_ctyobuf[128];
};

static int ndtes = 0;
struct dte dvdte[DTE_NSUP];	/* Device structs for DTE units */

/* Internal predeclarations */

static int dte_conf(FILE *f, char *s, struct dte *dt);
static void dte_picheck(struct dte *dt);
static int  dte_prim(struct dte *dt);
static int  dte_kaltmo(void *arg);
static int  dte_acktmo(void *arg);

static void dte_11db(struct dte *dt);
static void dte_dosecp(struct dte *dt);
static void dte_dbprmp(struct dte *dt);
static void dte_11done(struct dte *dt);
static void dte_rdhd(struct dte *dt);
static void dte_rdbyte(struct dte *dt, int cnt, unsigned char *ucp);
static void dte_rdword(struct dte *dt, int cnt, unsigned char *ucp);
static void dte_wrhd(struct dte *dt, struct dteq_s *q);
static void dte_wrbyte(struct dte *dt, struct dteq_s *q, int cnt);
static void dte_wrword(struct dte *dt, struct dteq_s *q, int wcnt);
static void dte_xctfn(struct dte *dt);
static void dte_showpkt(struct dte *dt, struct dtepkt_s *dtp);
static void dte_10start(struct dte *dt);
static void dte_10xfrbeg(struct dte *dt);
static void dte_11xfrdon(struct dte *dt);
static int  dte_10qpkt(struct dte *dt,
		      unsigned int sfn, unsigned int sdev,
		      unsigned int swd1, int bbcnt, int wbcnt,
		      unsigned char *dcp);
static void dte_dtmsend(struct dte *dt);
static void dte_ctyack(struct dte *dt);
static void dte_ctyiack(struct dte *dt);
static void dte_ctyout(struct dte *dt, int ch);
static void dte_ctysout(struct dte *dt, unsigned char *cp, int len);
static void dte_ctyforce(struct dte *dt);
static void dte_clkset(struct dte *dt, int on);
static int  dte_clktmo(void *arg);

static int   dt_init(struct device *d, FILE *of);
static w10_t dt_pifnwd(struct device *d);
static void  dt_cono(struct device *d, h10_t erh);
static w10_t dt_coni(struct device *d);
static void  dt_datao(struct device *d, w10_t w);
static w10_t dt_datai(struct device *d);
#if 0
static int   dt_readin();	/* Readin boot, if supported */
#endif

/* Configuration Parameters */

#define DVDTE_PARAMS \
    prmdef(DTP_DBG,  "debug"),	/* Initial debug value */\
    prmdef(DTP_MSTR, "master"), /* This is master DTE (default FALSE) */\
    prmdef(DTP_ADLY, "ackdly"),	/* ACK delay in msec (default 0) */\
    prmdef(DTP_OBS,  "obs"),	/* Output buffer alloc (default 64) */\
    prmdef(DTP_WARN, "warn"),	/* Show warnings (default FALSE) */\
    prmdef(DTP_DPDBG,"dpdebug"), /* Initial DP debug value (for later) */\
    prmdef(DTP_DP,   "dppath")	/* Device subproc pathname (for later) */

enum {
# define prmdef(i,s) i
	DVDTE_PARAMS
# undef prmdef
};

static char *dtprmtab[] = {
# define prmdef(i,s) s
	DVDTE_PARAMS
# undef prmdef
	, NULL
};


/* DTE_CONF - Parse configuration string and set defaults.
**	At this point, device has just been created, but not yet bound
**	or initialized.
** NOTE that some strings are dynamically allocated!  Someday may want
** to clean them up nicely if config fails or device is uncreated.
*/
static int dte_conf(FILE *f, char *s, struct dte *dt)
{
    int i, ret = TRUE;
    struct prmstate_s prm;
    char buff[200];
    long lval;

    /* First set defaults for all configurable parameters */
    DVDEBUG(dt) = FALSE;
    dt->dt_ismaster = FALSE;
    dt->dt_dlyackms = 0;
    dt->dt_dowarn = FALSE;
    dt->dt_ctyobs = sizeof(dt->dt_ctyobuf)/2;
#if KLH10_DEV_DPDTE
    dt->dt_dpname = "dpdte";		/* Pathname of device subproc */
    dt->dt_dtdbg = FALSE;
#endif

    prm_init(&prm, buff, sizeof(buff),
		s, strlen(s),
		dtprmtab, sizeof(dtprmtab[0]));
    while ((i = prm_next(&prm)) != PRMK_DONE) {
	switch (i) {
	case PRMK_NONE:
	    fprintf(f, "Unknown DTE parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;
	case PRMK_AMBI:
	    fprintf(f, "Ambiguous DTE parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;
	default:	/* Handle matches not supported */
	    fprintf(f, "Unsupported DTE parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;

	case DTP_DBG:		/* Parse as true/false boolean or number */
	    if (!prm.prm_val)	/* No arg => default to 1 */
		DVDEBUG(dt) = 1;
	    else if (!s_tobool(prm.prm_val, &DVDEBUG(dt)))
		break;
	    continue;

	case DTP_ADLY:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    dt->dt_dlyackms = lval;
	    continue;

	case DTP_OBS:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    if (lval < 0 || lval > 0xFF)	/* Ensure an 8-bit value */
		lval = 0xFF;
	    dt->dt_ctyobs = lval;
	    continue;

	case DTP_MSTR:		/* Parse as true/false boolean */
	    if (!prm.prm_val) {		/* No arg => default to TRUE */
		dt->dt_ismaster = TRUE;
		continue;
	    }
	    if (!s_tobool(prm.prm_val, &dt->dt_ismaster))
		break;
	    continue;

	case DTP_WARN:		/* Parse as true/false boolean */
	    if (!prm.prm_val) {		/* No arg => default to TRUE */
		dt->dt_dowarn = TRUE;
		continue;
	    }
	    if (!s_tobool(prm.prm_val, &dt->dt_dowarn))
		break;
	    continue;

#if KLH10_DEV_DPDTE
	case DTP_DPDBG:		/* Parse as true/false boolean or number */
	    if (!prm.prm_val)	/* No arg => default to 1 */
		dt->dt_dpdbg = 1;
	    else if (!s_tobool(prm.prm_val, &(dt->dt_dpdbg)))
		break;
	    continue;

	case DTP_DP:		/* Parse as simple string */
	    if (!prm.prm_val)
		break;
	    dt->dt_dpname = s_dup(prm.prm_val);
	    continue;
#endif
	}
	ret = FALSE;
	fprintf(f, "DTE param \"%s\": ", prm.prm_name);
	if (prm.prm_val)
	    fprintf(f, "bad value syntax: \"%s\"\n", prm.prm_val);
	else
	    fprintf(f, "missing value\n");
    }

    /* Param string all done, do followup checks */

    return ret;
}

struct device * dvdte_create(FILE *f, char *s)
{
    register struct dte *dt;
    static int onceinit = 0;

    if (!onceinit) {
	/* Do once-only global init */
	register int i;
	register struct dteq_s *q;

	for (q = dteqnodes, i = DTE_NQNODES; --i > 0; ++q)
	    q->q_next = q+1;
	q->q_next = NULL;		/* Last node's next is 0 */
	dteqfreep = dteqnodes;		/* Start of freelist */

	onceinit = TRUE;
    }

    /* Parse string to determine which DTE to use, config, etc etc
    ** But for now, just allocate sequentially.  Hack.
    */ 
    if (ndtes >= DTE_NSUP) {
	fprintf(f, "Too many DTEs, max: %d\n", DTE_NSUP);
	return NULL;
    }
    dt = &dvdte[ndtes];		/* Pick unused DTE */
    memset((char *)dt, 0, sizeof(*dt));	/* Clear it out */
    dt->dt_dten = ndtes++;	/* Remember its number */

    /* Initialize generic device part of DTE struct */
    iodv_setnull(&dt->dt_dv);	/* Initialize as null device */
    dt->dt_dv.dv_pifnwd = dt_pifnwd;
    dt->dt_dv.dv_cono = dt_cono;
    dt->dt_dv.dv_coni = dt_coni;
    dt->dt_dv.dv_datao = dt_datao;
    dt->dt_dv.dv_datai = dt_datai;

    dt->dt_dv.dv_init = dt_init;

    if (!dte_conf(f, s, dt))		/* Do configuration stuff */
	return NULL;

    return &dt->dt_dv;
}


static int dt_init(struct device *d, FILE *of)
{
    register struct dte *dt = (struct dte *)d;

    dt->dt_cond = 0;			/* Clear all CONI bits */
    dt->dt_pilev = 0;
    dt->dt_eptoff = DTE_CB(dt->dt_dten);	/* Find its EPT offset */
    dt->dt_eptesz = dt->dt_eptdsz = 0;		/* No exa/dep areas yet */
    LRHSET(dt->dt_piwd, PIFN_FVEC,		/* Build vectored PI fn wd */
			dt->dt_eptoff + DTE_CBINT);

    if (dt->dt_ismaster) {		/* If this one is master, */
	dt->dt_secptcl = TRUE;
    } else {
	dt->dt_cond |= DTE_CIRM;	/* say restricted, not master DTE */
	dt->dt_secptcl = FALSE;
    }
    dt->dt_dtmsent = FALSE;

    dt->dt_ctyocnt = sizeof(dt->dt_ctyobuf);	/* Reset CTY output buffer */
    dt->dt_ctyocp = dt->dt_ctyobuf;

    /* Stuff that belongs in a "dt_11reboot" section */

    /* Note the setting of the to-10 status word is done entirely from
    ** the FE and any existing value in the 10 is ignored.  At boot
    ** time we start with all counts clear (particularly TO10IC, which is
    ** bumped each time we send something to the 10).
    */
    LRHSET(dt->dt_snd_sts, DTE_CMT_TST|DTE_CMT_QP, 0);

    /* Set up timer to bump keepalive every 1/2 sec. */
    if (!dt->dt_kpal)
	dt->dt_kpal = clk_tmrget(dte_kaltmo, (void *)dt,
						CLK_USECS_PER_SEC/2);

    /* Set up ACK delay timer (mostly useful for T10) */
    if (dt->dt_dlyackms) {
	dt->dt_dlyack = clk_tmrget(dte_acktmo, (void *)dt,
				dt->dt_dlyackms * 1000);
	clk_tmrquiet(dt->dt_dlyack);	/* Immediately make it quiescent */
	dt->dt_dlyackf = FALSE;
    }

    return TRUE;
}

/* PI: Get function word
**	This has the potential to get really hairy depending on how closely
**	we want to emulate what the DTE actually does.
**	For now, never emulate the exa/dep or byte xfer PI0 functions.
**	Only handle normal vectoring through the EPT word.
*/
static w10_t dt_pifnwd(struct device *d)
{
    return ((struct dte *)d)->dt_piwd;
}

/* CONO 18-bit conds out
**	Args D, ERH
** Returns nothing
*/
static insdef_cono(dt_cono)
{
    register struct dte *dt = (struct dte *)d;
    register unsigned int cond = dt->dt_cond;

    if (dt->dt_dv.dv_debug) {
	fprintf(dt->dt_dv.dv_dbf, "[DTE: Cono %lo", (long)erh);
	if ((erh & DTE_COPIENB) && !(erh & DTE_CIPI)) {
	    fputs(" (PI off)", dt->dt_dv.dv_dbf);
	}
	fputs("]\r\n", dt->dt_dv.dv_dbf);
    }

    if (erh & DTE_COPIENB) {		/* Enabling PI? */
	cond &= ~(DTE_CIPI0|DTE_CIPI);		/* Clear old cond bits */
	cond |= erh & (DTE_CIPI0|DTE_CIPI);	/* And insert new bits */

	/* If changing PI assignment, do tricky stuff */
	dt->dt_pilev = (1 << (7-(cond & DTE_CIPI))) & 0177; /* 0 if PIA=0 */
	if (dt->dt_dv.dv_pireq && (dt->dt_dv.dv_pireq != dt->dt_pilev)) {
	    /* Changed PIA while PI outstanding; flush, let picheck re-req */
	    (*dt->dt_dv.dv_pifun)(&dt->dt_dv, 0);	/* clear it. */
	}

	/* Check PI now or at end */
    }
    if (erh & DTE_COCL11) {	/* Clearing TO11DN and TO11ER? */
	cond &= ~(DTE_CI11DN|DTE_CI11ER);
    }
    if (erh & DTE_COCL10) {	/* Clearing TO10DN and TO10ER? */
	cond &= ~(DTE_CI10DN|DTE_CI10ER);
    }
    if (erh & DTE_CO10DB) {	/* Clearing TO10DB doorbell? */
	cond &= ~(DTE_CI10DB);
    }

    if (erh & DTE_COCR11) {	/* Clearing reload-11 button? */
	dt->dt_reld11 = 0;
    }
    if (erh & DTE_COSR11) {	/* Setting reload-11 button? */
	dt->dt_reld11 = -1;
    }

    dt->dt_cond = cond;

    if (erh & DTE_CO11DB) {	/* Requesting TO11DB doorbell? */
	dt->dt_cond |= DTE_CI11DB;	/* Turn doorbell on */
	dte_11db(dt);		/* Invoke virtual PDP-11 */
    }

    dte_picheck(dt);		/* Check for possible PI changes */
}

/* CONI 36-bit conds in
**	Args D
** Returns condition word
*/
static insdef_coni(dt_coni)
{
    register w10_t w;
    LRHSET(w, 0, ((struct dte *)d)->dt_cond);

    if (((struct dte *)d)->dt_dv.dv_debug) {
	fprintf(((struct dte *)d)->dt_dv.dv_dbf,
			"[DTE: Coni= %lo]\r\n", (long)RHGET(w));
    }
    return w;
}

/* DATAO word out
**	Args D, W
** Returns nothing.
**	This sets up the receive byte/word count for transfers to
**	the 10 from the 11, and starts xfer.
**	Also sets up a termination flag.
*/
static insdef_datao(dt_datao)
{
    register struct dte *dt = (struct dte *)d;
    register unsigned int rh = RHGET(w);	/* Get RH, OK to truncate */

    if (dt->dt_dv.dv_debug) {
	fprintf(dt->dt_dv.dv_dbf, "[DTE: Datao %lo,,%lo]\r\n",
			(long)LHGET(w), (long)RHGET(w));
    }

    dt->dt_snd_ibit = rh & DTE_TO10IB;	/* Set "I" bit either on or off */

    /* Get positive byte cnt */
    dt->dt_snd_cnt = -((rh & DTE_TO10BC) | ~DTE_TO10BC);

    dte_10xfrbeg(dt);		/* Start transfer */
}

/* DATAI word in
**	Args D
** Returns data word.  For the DTE this is always 0.
*/
static insdef_datai(dt_datai)
{
    register w10_t w;
    op10m_setz(w);
    return w;
}

/* DTE_PI - trigger PI for selected DTE.
**	This could perhaps be an inline macro, but for now
**	having it as a function helps debug.
*/
static void dte_pi(register struct dte *dt)
{
    if (dt->dt_dv.dv_debug) {
	fprintf(dt->dt_dv.dv_dbf, "[dte_pi: %o]", dt->dt_pilev);
    }

    if (dt->dt_pilev			/* If have non-zero PIA */
      && !(dt->dt_dv.dv_pireq)) {	/* and not already asking for PI */
	(*dt->dt_dv.dv_pifun)(&dt->dt_dv, dt->dt_pilev); /* then do it! */
    }
}

/* DTE_PICHECK - Check DTE conditions to see if PI should be attempted.
*/
static void dte_picheck(register struct dte *dt)
{
    /* If any possible interrupt bits are set */
    if (dt->dt_cond & (DTE_CI10DB
		 | DTE_CI11DN | DTE_CI11ER
		 | DTE_CI10DN | DTE_CI10ER)) {
	dte_pi(dt);
	return;
    }
    /* Here, shouldn't be requesting PI, so if our request bit is set,
    ** turn it off.
    */
    if (dt->dt_dv.dv_pireq) {		/* If set while shouldn't be, */
	(*dt->dt_dv.dv_pifun)(&dt->dt_dv, 0);	/* Clear it! */
    }
}


/* Auxiliaries for doing Examine & Deposit via DTE reloc specs in EPT.
**
*/

#define dte_fetch(dt, off) \
	((dt)->dt_eptesz > (off) \
		? vm_pget((dt)->dt_eptexa + (off)) \
		: dte_badexa(dt, off))

#define dte_store(dt, off, w) \
	((dt)->dt_eptdsz > (off) \
		? (void)vm_pset((dt)->dt_eptdep + (off), (w)) \
		: dte_baddep(dt, off, w))


/* DTE_EPTUPD - Update internal vars with latest EPT stuff
*/
static void dte_eptupd(register struct dte *dt)
{
    register vmptr_t vp;
    register w10_t w;
    register paddr_t pa;

    vp = vm_physmap(cpu.mr_ebraddr + dt->dt_eptoff);
    dt->dt_eptcb = vp;				/* Remember ptr to CB */
    dt->dt_eptesz = vm_pgetrh(vp+DTE_CBEPW);	/* Exa prot (size of area) */
    dt->dt_eptdsz = vm_pgetrh(vp+DTE_CBDPW);	/* Dep prot (size of area) */

    w = vm_pget(vp+DTE_CBERW);			/* Get exam reloc word */
    pa = ((LHGET(w)<<H10BITS) | RHGET(w)) & MASK22;	/* Make phy address */
    dt->dt_eptexa = vm_physmap(pa);		/* Examine Relocation */

    w = vm_pget(vp+DTE_CBDRW);			/* Get exam reloc word */
    pa = ((LHGET(w)<<H10BITS) | RHGET(w)) & MASK22;	/* Make phy address */
    dt->dt_eptdep = vm_physmap(pa);		/* Deposit Relocation */
}

static w10_t dte_badexa(register struct dte *dt,
			unsigned int off)
{
    register w10_t w;

    fprintf(dt->dt_dv.dv_dbf, "[DTE: Bad exa %o (prot=%o)]",
		off, dt->dt_eptesz);
    op10m_setz(w);
    return w;
}

static void dte_baddep(register struct dte *dt,
		       unsigned int off,
		       register w10_t w)
{
    fprintf(dt->dt_dv.dv_dbf, "[DTE: Bad dep %o (prot=%o)]",
		off, dt->dt_eptdsz);
}

/* Special hack for keep-alive counter.
**	Should be invoked by some clock interrupt function.
*/
int dte_kaltmo(void *arg)
{
    register struct dte *dt = (struct dte *)arg;
    register w10_t w;

    if (dt->dt_secptcl)		/* Only update if in primary ptcl */
	return CLKEVH_RET_REPEAT;

    /* In primary ptcl, do keepalive update.
    ** This code assumes that the DTE deposit relocation points to the
    ** start of the 11's own (comdat) region.
    */
    if (dt->dt_eptdsz <= DTE_CMOW_KAC)	/* Check deposit protection */
	return CLKEVH_RET_REPEAT;

    w = vm_pget(dt->dt_eptdep + DTE_CMOW_KAC);
    op10m_inc(w);				/* Bump the counter */
    vm_pset(dt->dt_eptdep + DTE_CMOW_KAC, w);	/* Store it back */

    return CLKEVH_RET_REPEAT;
}


/* Delayed-ACK timeout routine.
** According to LWS, "instanteous" cty acks from emulated dte causes
**	a race condition which TOPS-10 will never win.
** (Output does work, but works much better with a slight device delay to
** avoid the race).
*/
static int dte_acktmo(void *arg)
{
    register struct dte *dt = (struct dte *)arg;

    if (dt->dt_dlyackf) {
	dte_ctyiack(dt);	/* Do immediate ACK now */
	dt->dt_dlyackf = FALSE;
    }
    return CLKEVH_RET_QUIET;	/* Go quiescent after firing */
}

/* DTE_PRIM - Enter primary protocol (RSX20F)
**	This depends on a lot of assumptions about the protocol
**	which are documented elsewhere (dvdte.h, dte.doc)
**
**	Assumes our local EPT vars are set up.
*/

static int dte_prim(register struct dte *dt)
{
    register w10_t w;

    dte_eptupd(dt);			/* Re-init from EPT vars! */

    /* First attempt to read word 0 of our examine area.  This should
    ** be a word in COMPTR format.
    */
    w = dte_fetch(dt, 0);		/* Get first word */
    if (op10m_skipe(w)) {
	fprintf(dt->dt_dv.dv_dbf, "[DTE: Zero COMPTR?]");
	return 0;
    }

    dt->dt_procn = (LHGET(w)>>6)&037;	/* Find our processor # */
    dt->dt_combase = DTE_CMBAS(w);	/* Derive offset to base */

    /* For now, cheat a bit while deriving offsets, rather than 
    ** calculating the sizes etc from the com region data.
    */

    /* Find offset to our 11's comdat (fixed) region.
    ** By conspiracy this will be the same as our deposit reloc 0.
    */
    dt->dt_doff = dt->dt_combase + (RHGET(w) & MASK16);

    dt->dt_dt10off =  16;		/* Comdat is 16 wds; comrgn follows */
    dt->dt_et10off = dt->dt_doff + 16;	/* " */

    /* This cheats by assuming combase is same as the 10's comdat region,
    ** instead of deriving it from CMPPT field.
    */
    dt->dt_ec10off = dt->dt_combase;
    dt->dt_et11off = dt->dt_ec10off + 16 + (8 * dt->dt_dten);

    /* Initialize internal transfer vars */
    dt->dt_rcv_gothdr = FALSE;
    dt->dt_rcv_indir = FALSE;

#if 0
    /* Buggy -- don't clobber each time we re-enter primary ptcl! */

    /* Should this be inited from 10's memory?  No; FE doesn't. */
    LRHSET(dt->dt_snd_sts, DTE_CMT_TST|DTE_CMT_QP, 0);
#endif
    return 1;
}

/* Doorbell rung to virtual 11, initiate some action.
**
*/

static void dte_11db(register struct dte *dt)
{
    /* Wake up and see what needs to be done */
    
    if (dt->dt_dv.dv_debug)
	fprintf(dt->dt_dv.dv_dbf, "[DTE%d: 11DB]", dt->dt_dten);

    if (dt->dt_secptcl)
	dte_dosecp(dt);		/* Do secondary protocol */
    else {
	dte_dbprmp(dt);		/* Do primary */
	if (dt->dt_secptcl)	/* If failed and switched, then */
	    dte_dosecp(dt);	/* do secondary protocol instead */
    }

    dt->dt_cond &= ~DTE_CI11DB;	/* Routine should turn off 11DB! */
}

/* Do secondary protocol stuff
**	All refs to EPT must use true current EPT address.
*/
static void dte_dosecp(register struct dte *dt)
{
    register w10_t w;

    w = vm_pget(vm_physmap(cpu.mr_ebraddr + DTEE_CMD));
    switch (RHGET(w) & DTECMDF_CMD) {
	default:
	    fprintf(dt->dt_dv.dv_dbf, "[DTECMD: unknown %lo]\r\n",
				(long)RHGET(w));

	    /* RSX20F interprets all unrecognized secondary ptcl cmds as
	    ** "output char".  So drop through.
	    */

	case DTECMD_MNO:	/* Output char in monitor mode */
	    if (dt->dt_dv.dv_debug)
		fprintf(dt->dt_dv.dv_dbf, "[DTECMD:O %o]",(int)RHGET(w)&0377);
	    fe_ctyout((int)RHGET(w)&0377);

	    op10m_seto(w);	/* Set TMD flag -1 to confirm "done" */

	    /* Actually this should be a secondary-ptcl feature mask!
	    ** See T10 DTEPRM for details of bits.  Low bit 0 means support
	    ** cmd 13 (get date/time).
	    */
	    op10m_trz(w, 01);		/* Say cmd 13 now supported! */
	    vm_pset(vm_physmap(cpu.mr_ebraddr + DTEE_TMD), w);

	    /* Additionally, trigger to-10 doorbell! */
	    dt->dt_cond |= DTE_CI10DB;		/* Set 10 doorbell */
	    dte_pi(dt);
	    break;

	case DTECMD_EMP:	/* Enter Secondary (monitor mode) ptcl */
	    if (dt->dt_dv.dv_debug)
		fprintf(dt->dt_dv.dv_dbf, "[DTECMD: enter sec]");
	    dt->dt_secptcl = TRUE;
	    break;

	case DTECMD_EPP:	/* Enter Primary ptcl */
	    if (dt->dt_dv.dv_debug)
		fprintf(dt->dt_dv.dv_dbf, "[DTECMD: enter prm]");
	    if (!dte_prim(dt)) {	/* Enter it, set up vars */
		fprintf(dt->dt_dv.dv_dbf, "[DTE: Can't enter prim ptcl]\r\n");
		break;
	    }
	    dt->dt_secptcl = FALSE;
	    dt->dt_dtmsent = FALSE;	/* Pretend haven't sent date/time */
	    dte_dbprmp(dt);		/* Now do primary immediately! */
	    if (!dt->dt_secptcl) {
		/* If successfully entered prim ptcl, queue up ack-all stuff */
		/* (for now, skip the LPT and LPT1 allocs) */
		(void) dte_10qpkt(dt,
			SWAB(DTE_QFN_ACK),	/* Function 15 Ack All */
			SWAB(07),		/* Random NZ device */
			SWAB(0),		/* Random data */
			0, 0, NULL);		/* No other data */

		/* If date/time not yet sent, queue a date/time packet now?
		*/
		if (!dt->dt_dtmsent)
		    dte_dtmsend(dt);	/* Send date-time to 10 */
	    }
	    break;		/* If bombs, merely ignore it */

	case DTECMD_RTM:	/* Get date/time */
	    if (dt->dt_dv.dv_debug)
		fprintf(dt->dt_dv.dv_dbf, "[DTECMD: Get DTM]");
	{
	    /* Bits 4-19 of DTECMD are EPT offset to store a 3-word value, with
	    ** 2 right-justified 16-bit values in each word:
	    ** 0:	<NZ-if-valid>			<full-year (eg 1993.)>
	    ** 1:	<month (0=jan)><day(0=1st)>	<DOW (0=Mon)><DST-flag>
	    ** 2:	<secs-since-midnite/2>		<-unused->
	    */
	    /* Note T10 ignores DOW+DST info. */
	    /* T20 treats DST info as: 0200 = DST flag bit
	    **			   0177 = Timezone
	    */
	    
	    struct tm t;
	    register uint32 i;
	    register vmptr_t vp;
	    int zone;

	    /* Get high 16-bit field (B4-19) from DTECMD word */
	    i = ((LHGET(w) << 2) | (RHGET(w) >> 16)) & MASK16;
	    if (i & (~0777)) {
		fprintf(dt->dt_dv.dv_dbf, "[DTE: bad GDT offset %lo]",
			(long)i);
		break;			/* Don't return date/time */

	    }
	    vp = vm_physmap(cpu.mr_ebraddr + i);

	    if (!os_tmget(&t, &zone))	/* Get current date/time */
		break;			/* Failed, return nothing */

	    LRHSET(w, 0,
		((1<<16)		/* Set validity flag non-zero */
		 | (t.tm_year + 1900)	/* Make year full A.D. quantity */
		) & H10MASK);
	    vm_pset(vp, w);

	    i = t.tm_mday - 1;		/* Get day-of-month (0-31) */
	    LRHSET(w,
		((t.tm_mon<<6) 
		 | (i >> 2)) & H10MASK,
		((i << 16)
		 | (((t.tm_wday+6) % 7)<<8)
		 | (t.tm_isdst ? 0200 : 0) | (zone & 0177)) & H10MASK);
	    ++vp;
	    vm_pset(vp, w);

	    /* Time is secs/2 to fit in a 16-bit word */
	    i = (((((long)t.tm_hour * 60) + t.tm_min) * 60) + t.tm_sec) >> 1;
	    LRHSET(w, (i >> 2) & H10MASK,
		(i << 16) & H10MASK);
	    ++vp;
	    vm_pset(vp, w);

	}
	    break;

	/* KLDCP commands */

	case DTECMD_DCP_CTYO:	/* KLDCP output char to CTY */
	    if (dt->dt_dv.dv_debug)
		fprintf(dt->dt_dv.dv_dbf, "[DTEO: %o]", (int)RHGET(w)&0377);
	    fe_ctyout((int)RHGET(w)&0377);
	    break;

	case DTECMD_DCP_RDSW:		/* Read data switches */
	    vm_pset(vm_physmap(cpu.mr_ebraddr + DTEE_F11), cpu.mr_dsw);
	    break;

	case DTECMD_DCP_DDTIN:		/* DDT input mode */
	  {
	    register int ch;
	    if ((ch = fe_ctyin()) < 0)
		op10m_setz(w);		/* No input, return 0 */
	    else {
		ch &= 0177;		/* Mask for safety */
		fe_ctyout(ch);		/* Echo, sigh */
		LRHSET(w, 0, ch & 0377);
	    }
	    vm_pset(vm_physmap(cpu.mr_ebraddr + DTEE_F11), w);
	  }
	    break;

	case DTECMD_DCP_TIW:		/* TTY input, wait */
	  {
	    /* This one is dangerous as it actually halts the 10
	    ** until some TTY input happens!  But doing it right is painful
	    ** and only diagnostics should be using this anyway.
	    */
	    register int ch, tmo = 180;
	    osstm_t stm;

	    if (DVDEBUG(dt))
		fprintf(DVDBF(dt), "[DTE KLDCP_TIW: %d sec...", tmo);

	    OS_STM_SET(stm, tmo);
	    for (;;) {
		if ((ch = fe_ctyin()) >= 0) {
		    ch &= 0177;			/* Mask for safety */
		    fe_ctyout(ch);		/* Echo, sigh */
		    LRHSET(w, 0, ch & 0377);
		    if (DVDEBUG(dt))
			fprintf(DVDBF(dt), " %o]", ch);
		    break;
		}
		if (os_msleep(&stm) <= 0) {
		    op10m_setz(w);		/* No input, return 0 */
		    if (DVDEBUG(dt))
			fprintf(DVDBF(dt), " timeout]");
		    break;
		}
	    }
	    vm_pset(vm_physmap(cpu.mr_ebraddr + DTEE_F11), w);
	  }
	    break;


	case DTECMD_DCP_CLDI:		/* Clear DDT input mode */
	    break;

	case DTECMD_DCP_PGM:		/* Program control; low byte subcmd */
	{
	    register int cmd = RHGET(w)&0377;	/* Find subcmd */
	    switch (cmd) {
	    case 01:
		fprintf(DVDBF(dt), "[DTE KLDCP: Fatal pgm error in 10]\r\n");
		break;
	    case 02:
		fprintf(DVDBF(dt), "[DTE KLDCP: Error halt in 10]\r\n");
		break;
	    case 03:
		fprintf(DVDBF(dt), "[DTE KLDCP: End of program]\r\n");
		break;
	    case 04:
		fprintf(DVDBF(dt), "[DTE KLDCP: End of pass]\r\n");
		break;
	    case 05:
		/* Get clock default word.  See dvdte.h for bits. */
		LRHSET(w,0,0);		/* For now, just claim normal clk */
		if (DVDEBUG(dt))
		    fprintf(DVDBF(dt), "[DTE KLDCP: Clk src %lo,,%lo]\r\n",
			(long)LHGET(w), (long)RHGET(w));
		break;
	    default:
		fprintf(DVDBF(dt), "[DTE KLDCP PGM(%o) unimplemented]", cmd);
	    }
	}
	    break;

	case DTECMD_DCP_CLK:		/* Clock control; low byte subcmd */
	{
	    register int cmd = RHGET(w)&0377;	/* Find subcmd */
	    uint32 ticks;

	    switch (cmd) {
	    case DTECMD_DCPCLK_OFF:
		if (DVDEBUG(dt))
		    fprintf(DVDBF(dt), "[DTE KLDCP: Clock off]");
		dte_clkset(dt, 0);
		break;
	    case DTECMD_DCPCLK_ON:
		if (DVDEBUG(dt))
		    fprintf(DVDBF(dt), "[DTE KLDCP: Clock on]");
		dte_clkset(dt, 1);		/* Make clock enabled */
		break;
	    case DTECMD_DCPCLK_WAIT:
		w = vm_pget(vm_physmap(cpu.mr_ebraddr + DTEE_T11));
		ticks = (LHGET(w)<<18) | RHGET(w);
		if (DVDEBUG(dt))
		    fprintf(DVDBF(dt), "[DTE KLDCP: Clock wait %ld.]",
				(long)ticks);
		dt->dt_clkcnt = ticks;
		dte_clkset(dt, 2);		/* Enable clock, do wait */
		break;
	    case DTECMD_DCPCLK_READ:
		if (DVDEBUG(dt))
		    fprintf(DVDBF(dt), "[DTE KLDCP: Clock read %ld.]",
				(long) dt->dt_clkticks);
		LRHSET(w, (dt->dt_clkticks >> 18) & H10MASK,
			(dt->dt_clkticks & H10MASK));
		vm_pset(vm_physmap(cpu.mr_ebraddr + DTEE_F11), w);
		break;
	    default:
		fprintf(DVDBF(dt), "[DTE KLDCP CLK(%o) unimplemented]", cmd);
	    }
	}
	    break;
	}


    /* After all commands (even bogus ones) set cmd-done flag -1 as
    ** an ACK from the 11.  This isn't needed for monitor-mode
    ** TTY output under RSX20F (only KLDCP), but shouldn't hurt.
    */
    op10m_seto(w);
    vm_pset(vm_physmap(cpu.mr_ebraddr + DTEE_FLG), w);
}

/* KLDCP clock facilities */

/* Turn clock on or off, setting new state. */

static void dte_clkset(register struct dte *dt, int on)
{
    dt->dt_clkf = on;		/* Set new state now */

    if (on) {
	dt->dt_clkticks = 0;	/* Reset count of ticks */
	if (dt->dt_clk)		/* Activate timer */
	    clk_tmractiv(dt->dt_clk);
	else			/* Gotta get it for 1st time */
	    dt->dt_clk = clk_tmrget(dte_clktmo, (void *)dt,
				CLK_USECS_PER_SEC/60);
    } else {
	if (dt->dt_clk)
	    clk_tmrquiet(dt->dt_clk);	/* Make quiescent */
    }
}

/* Handle clock timeout. */

static int dte_clktmo(void *arg)
{
    register struct dte *dt = (struct dte *)arg;

    dt->dt_clkticks++;		/* Update tick count */
    switch (dt->dt_clkf) {
	case 2:			/* Clock-wait mode? */
	    if (--(dt->dt_clkcnt) > 0)
		break;		/* If still waiting, do nothing */
	    /* Wait timed out, drop thru to tickle 10 */

	case 1:
	    /* KLDCP clock tick!  Set loc 445 nonzero and send a TO10 DB */
	  {
	    register w10_t w;
	    if (DVDEBUG(dt))
		fprintf(DVDBF(dt), "[DTE KLDCP clock timeout]");
	    op10m_seto(w);
	    vm_pset(vm_physmap(cpu.mr_ebraddr + DTEE_CKF), w);
	    dt->dt_cond |= DTE_CI10DB;		/* Set 10 doorbell */
	    dte_pi(dt);
	  }
	    break;
    }
    return CLKEVH_RET_REPEAT;
}

/* Do primary protocol stuff in response to doorbell
**	EPT refs can use pre-computed & saved pointers/offsets for speed.
*/
static void dte_dbprmp(register struct dte *dt)
{
    register w10_t w;
    register int i, qct;

    /* First verify we actually have a comm area by checking DTE's examine
    ** protection word, then fetching the comrgn status word to see if
    ** the 10's "to-11" area is valid.
    ** Real FE does this with just the attempted fetch, but I want to
    ** avoid spurious error messages from dte_fetch().
    */
    if (op10m_skipe(vm_pget(dt->dt_eptcb + DTE_CBEPW))
      || (w = dte_fetch(dt, dt->dt_et11off + DTE_CMTW_STS),
	 !op10m_tlnn(w, DTE_CMT_TST))) {

	if (dt->dt_dv.dv_debug)
	    fprintf(dt->dt_dv.dv_dbf, "[DTE: Leaving prim ptcl: %lo,,%lo]\r\n",
			(long)LHGET(w), (long)RHGET(w));

	/* Ugh, must go into secondary ptcl. */
	if (!dt->dt_ismaster)
	    panic("[DTE: Non-master DTE %d trying to enter sec ptcl?]",
					dt->dt_dten);
	dt->dt_secptcl = TRUE;
	return;		/* Just return, let caller invoke dte_dosecp() */
    }
    if (op10m_tlnn(w, (DTE_CMT_PWF|DTE_CMT_L11|DTE_CMT_INI))) {
	fprintf(dt->dt_dv.dv_dbf, "[DTE: Unsupp sts bits %lo,,%lo]",
		(long)LHGET(w), (long)RHGET(w));
	return;
    }

    /* OK, so assume comm areas are all OK.  Get info about queued
    ** packet and gobble it up.
    */
    if (op10m_trnn(w, DTE_CMT_IP)) {	/* -10 saying this is indirect? */
	/* Verify indirect xfer in progress */
	if ( ! dt->dt_rcv_indir) {
	    panic("[DTE: -10 IIP but -11 not?]");
	}
	dt->dt_rcvpkt.dtep_wdmod =	/* Remember if full-word transfer */
		op10m_tlnn(w, DTE_CMT_FWD);

	/* Get count word */
	w = dte_fetch(dt, dt->dt_et11off + DTE_CMTW_CNT);
	qct = RHGET(w) & DTE_CMT_QCT;		/* Find # bytes this xfer */

	/* Handle indirect xfer (finish off-- either byte or word mode)
	** Note this assumes
	** only ONE indirect is ever done, right after a msg header.
	*/
	if (qct >= sizeof(dt->dt_rcvpkt.dtep_data)) {
	    fprintf(dt->dt_dv.dv_dbf,
			"[DTE: (RCV QCT %d.) > (max pkt size %d.)]",
			qct, (int)sizeof(dt->dt_rcvpkt.dtep_data));
	    qct = sizeof(dt->dt_rcvpkt.dtep_data);	/* Truncate data */
	}
	if (dt->dt_rcvpkt.dtep_wdmod)
	    dte_rdword(dt, qct, &dt->dt_rcvpkt.dtep_data[0]);
	else
	    dte_rdbyte(dt, qct, &dt->dt_rcvpkt.dtep_data[0]);
	dt->dt_rcvpkt.dtep_tcnt += qct;	/* Update total bytes read */

	dte_11xfrdon(dt);		/* To-11 xfer done, wrap up */
	dte_xctfn(dt);			/* Execute function for rcvd msg */
	dte_11done(dt);			/* Wrap up */
	dt->dt_rcv_gothdr = FALSE;
	dt->dt_rcv_indir = FALSE;
	return;
    }
    if (dt->dt_rcv_indir) {
	panic("[DTE: -11 IIP but -10 not?]");
    }


    /* Handle direct header, always byte mode.
    ** Check TO11QC field (# in queue of -10).
    */
    i = (RHGET(w) & DTE_CMT_11IC);
    if (i == dt->dt_rcv_11qc) {		/* If no different from current val, */
	/* Just set TOIT bit and return */
	if (dt->dt_dv.dv_debug)
	    fprintf(dt->dt_dv.dv_dbf, "[DTE: 11QC %o unchanged, 11DB ignored]",
			i);

	op10m_tro(dt->dt_snd_sts, DTE_CMT_TOT);	/* Set TOIT bit in to-10 sts */
	dte_store(dt, dt->dt_dt10off + DTE_CMTW_STS, dt->dt_snd_sts);
	return;
    } else if (i != ((dt->dt_rcv_11qc + 1) & 0377)) {
	/* Error, queue count mismatch.
	** Note the 0377 mask in the above test, to handle wraparound.
	*/
	fprintf(dt->dt_dv.dv_dbf, "[DTE: (RCV 11QC %o) != (11's 11QC %o+1)]",
			i, dt->dt_rcv_11qc);
	/* Recover by just continuing, to set new QC */
    }

    /* New header ready to slurp up! */
    dt->dt_rcv_11qc = i;		/* Set our new msg count value */

    /*
    ** CMPCT has the total # bytes in the protocol message.
    ** CMQCT has the # bytes to fetch in this hardware xfer.
    ** If indirect, more bytes follow in next wakeup, will have to set
    ** up state to remember what we got.
    ** No single packet will be larger than 254.+10. bytes -- will be broken
    ** up if so.
    */
    w = dte_fetch(dt, dt->dt_et11off + DTE_CMTW_CNT);
    qct = RHGET(w) & DTE_CMT_QCT;	/* Find # bytes this xfer */
    op10m_rshift(w, 16);		/* Get DTE_CMT_PCT field */
    i = RHGET(w) & MASK16;		/* Get CMPCT */

#define MAXXFER ((int)(sizeof(dt->dt_rcvpkt.dtep_data)+DTE_QMH_SIZ))
#if 0	/* Commented out - T10 apparently hits this constantly */
    if (i < qct) {
	fprintf(dt->dt_dv.dv_dbf, "[DTE: To-11 PCT:%d. < QCT:%d.]", i, qct);
    }
#endif
    if (i > MAXXFER) {
	fprintf(dt->dt_dv.dv_dbf, "[DTE: (RCV PCT %d.) > (max pkt size %d.)]",
			i, MAXXFER);
	/* Truncate data? Let actual xfer handle it. */
    }
    if (qct > MAXXFER) {
	fprintf(dt->dt_dv.dv_dbf, "[DTE: (RCV QCT %d.) > (max pkt size %d.)]",
			qct, MAXXFER);
	qct = MAXXFER;			/* Truncate data */
    }
#undef MAXXFER

    /* Now read header directly into our internal structure. */
    if (qct < DTE_QMH_SIZ) {
	fprintf(dt->dt_dv.dv_dbf, "[DTE: To-11 QCT:%d. < Header]", qct);
	dte_11xfrdon(dt);		/* Punt, tell 10 xfer is done */
	return;
    }

    dte_rdhd(dt);		/* Read 10 bytes from current to-11 BP */
    dt->dt_rcv_gothdr = TRUE;	
    dt->dt_rcv_indir = FALSE;

    /* Check to see if more data follows or not */
    if (dt->dt_rcvpkt.dtep_fn & DTE_QMH_INDIRF) {
	/* Indirect for rest of data... so set up and return.
	** Note that count field of msg header is ignored!
	*/
	if (dt->dt_dv.dv_debug)
	    fprintf(dt->dt_dv.dv_dbf, "[DTE: @ fn: %o]",dt->dt_rcvpkt.dtep_fn);
	dt->dt_rcv_indir = TRUE;	/* Remember doing indirect */
	dte_11xfrdon(dt);		/* Give 10 an xfer-done PI */
	return;				/* That's all -- give 10 its turn */
    }

    /* Not indirect -- fully direct message.  Check msg count word to see
    ** if it has more data beyond the ten header bytes we read.  Actually,
    ** we ignore it except for doing an error check, and pay attention
    ** mostly to CMQCT.  We already know that CMQCT is small enough for
    ** a complete direct transfer.
    */
    if (dt->dt_rcvpkt.dtep_cnt != qct) {
	fprintf(dt->dt_dv.dv_dbf, "[DTE: To-11 direct QCT:%d. != Hdr:%d.]",
		qct, dt->dt_rcvpkt.dtep_cnt);
    }

    if ((qct -= DTE_QMH_SIZ) > 0) {		/* More data? */
	/* Yep, gobble up additional data in byte mode. */

	if (dt->dt_dv.dv_debug)
	    fprintf(dt->dt_dv.dv_dbf, "[DTE: R %d, fn=%o]",
			qct, dt->dt_rcvpkt.dtep_fn);

	dte_rdbyte(dt, qct, &dt->dt_rcvpkt.dtep_data[0]);
	dt->dt_rcvpkt.dtep_tcnt += qct;	/* Update total bytes read */
    }

    /* Direct xfer all done, clear TOIT bit and tickle PI */
    dte_11xfrdon(dt);			/* Give 10 an xfer-done PI */

    /* Execute function now, or via 11done? */
    dte_xctfn(dt);

    /* Now carry out functions as if 11-done interrupt happened! */
    dte_11done(dt);

    dt->dt_rcv_gothdr = FALSE;
    dt->dt_rcv_indir = FALSE;
}

static void dte_11xfrdon(register struct dte *dt)
{
    /* Receiver (11) clears TOIT status bit in area used for sending to 10
    */
    op10m_trz(dt->dt_snd_sts, DTE_CMT_TOT);	/* Clr TOIT bit in to-10 sts */
    dte_store(dt, dt->dt_dt10off + DTE_CMTW_STS, dt->dt_snd_sts);

    dt->dt_cond |= DTE_CI11DN;		/* Say To-11 transfer done */

    /* PI will be triggered by CONO code just before it returns */
}

/*
    When gets To-11 Done Interrupt (always, whenever DTE to-11
    xfer counts out):

		Clears to-11 Done flag in DTE (DON11C)
		Gets back saved byte count from start of xfer (TO11BS)
			(Note was word count if not byte mode), uses that
			to verify ending address of xfer.
			Crashes if doesn't match predicted end of xfer.
		Checks to see whether reading header (start of packet) or
			is doing part of an indirect.

		If start of packet:
			Swaps bytes of function, count, and device words to
				get them into PDP-11 order.
			Checks device and function for valid ranges.  Only
				checks low byte of function word.  If
				invalid, crashes.
			Checks high bit of function wd to see if indirect
			data follows.  If so:
				Clears TOIT bit in 10's to-11 status, and
				that's all.  This appears to rely on the
				10 getting a xfer-done interrupt (ie I-bit
				set by 11?) so it can set up the indirect
				xfer and ring the 11's doorbell again.
				Anyway, returns.
			If not (ie fully direct), checks message's count
			word to see if any more data beyond the 10 header
			bytes.
			If no more data,
				Assumes xfer done, queues the msg,
				clears TOIT bit, returns.
			If more data,
				Starts new xfer with new count; sets
				I-bit so 10 gets interrupted when xfer done.
				Updates EQSZ (internal CMQCT value) tho not
				sure if this does us any good.
			Returns.


		Else, doing indirect:
			Assume packet all done, queues the msg, clears
			TOIT bit, returns.

*/
static void dte_11done(register struct dte *dt)
{

    /* Transform data into header */


}

/* Read packet/message header (10 bytes)
**	Note BPs must be interpreted as being in executive virtual space,
**	which isn't necessarily the same as physical.
**	However, this code cheats by assuming data doesn't cross a page
**	boundary.
*/
static void dte_rdhd(register struct dte *dt)
{
    register w10_t bp, w;
    register vmptr_t abp, vp;
    register vaddr_t va;
    register unsigned int i;

    /* For now, assume DTE's starting BP is 8-bit OWLBP at beg of word.
    ** Verify this before continuing.
    */
    abp = dt->dt_eptcb + DTE_CBOBP;		/* Get BP addr */
    bp = vm_pget(abp);
    if (LHGET(bp) != 0441000) {
	fprintf(dt->dt_dv.dv_dbf, "[DTE: Bad to-11 BP %lo,,%lo]",
		(long)LHGET(bp), (long)RHGET(bp));
	return;			/* For now, punt entirely */
    }

    /* Normal fast case. */
    va_gmake(va, 0, RHGET(bp));		/* Make exec virt addr, sect 0 */
    vp = vm_execmap(va, VMF_READ|VMF_NOTRAP);	/* Map it, don't trap */
    if (!vp) {
	panic("DTE: Page fail reading R hdr at %lo", (long)RHGET(bp));
	/* Later do real page-fail with special device data?
	** See T20 APRSRV for details of what it expects.
	*/
    }

    w = vm_pget(vp);			/* Get 1st word */

    dt->dt_rcvpkt.dtep_tcnt = DTE_QMH_SIZ;	/* 10 bytes read so far */

    dt->dt_rcvpkt.dtep_cnt = (LHGET(w)>>2);	/* Get 16-bit count */
    dt->dt_rcvpkt.dtep_fn =			/* Get 16-bit function */
		((LHGET(w)&03)<<14) | (RHGET(w)>>4);

    /* Get 16-bit device from next 10 word */
    dt->dt_rcvpkt.dtep_dev = (vm_pgetlh(vp+1)>>2);

    /* Get first 2 data bytes from high 16 bits of next 10 word after that */
    dt->dt_rcvpkt.dtep_db1 = (i = (vm_pgetlh(vp+2)>>2)) & 0377;
    dt->dt_rcvpkt.dtep_db0 = (i >> 8) & 0377;

    dt->dt_rcvpkt.dtep_wdmod = FALSE;		/* Assume byte mode */

    /* Now update BP as if read 10 bytes */
    op10m_addi(bp, 2);
    LHSET(bp, 0241000);
    vm_pset(abp, bp);		/* Store back in EPT */
}


static h10_t bp8lhtab[5] = {
	0041000,
	0141000,
	0241000,
	0341000,
	0441000
};
static h10_t bp9lhtab[5] = {
	0001100,
	0111100,
	0221100,
	0331100,
	0441100
};

/* Read packet/message byte data
**	This also cheats by ignoring page boundaries once a physical memory
**	address is obtained.
*/
static void dte_rdbyte(register struct dte *dt, register int cnt, register unsigned char *ucp)
{
    register w10_t bp, w;
    register vmptr_t abp, vp;
    register int i;
    register vaddr_t va;

    /* For now, assume DTE's starting BP is 8-bit OWLBP.
    ** Verify this before continuing.
    */
    abp = dt->dt_eptcb + DTE_CBOBP;	/* Get BP addr */
    bp = vm_pget(abp);

    /* Normal fast case. */
    va_gmake(va, 0, RHGET(bp));		/* Make exec virt addr, sect 0 */
    vp = vm_execmap(va, VMF_READ|VMF_NOTRAP);	/* Map it, don't trap */
    if (!vp) {
	panic("DTE: Page fail reading R data at %lo", (long)RHGET(bp));
    }

    i = LHGET(bp) >> 15;		/* Use high octal digit as index */
    if (i > 0)		/* Set up first word */
	w = vm_pget(vp);

    if (i <= 4 && (bp8lhtab[i] == LHGET(bp))) {
	/* Do 8-bit byte loop */
	while (--cnt >= 0) {
	    if (--i < 0) {			/* Bump to next word? */
		op10m_inc(bp);
		++vp;
		w = vm_pget(vp);
		i = 3;
	    }
	    switch (i) {
	case 0:	*ucp++ = (RHGET(w) >> 4) & 0377;	break;
	case 1:	*ucp++ = ((LHGET(w) & 03)<<6) | (RHGET(w) >> 12);	break;
	case 2:	*ucp++ = (LHGET(w) >>  2) & 0377;	break;
	case 3:	*ucp++ = (LHGET(w) >> 10) & 0377;	break;
	    }
	}
	/* Now update BP as if read cnt bytes */
	LHSET(bp, bp8lhtab[i]);		/* Set up appropriate LH */

    } else if (i <= 4 && (bp9lhtab[i] == LHGET(bp))) {
	/* Do 9-bit byte loop */
	while (--cnt >= 0) {
	    if (--i < 0) {			/* Bump to next word? */
		op10m_inc(bp);
		++vp;
		w = vm_pget(vp);
		i = 3;
	    }
	    switch (i) {
	case 0:	*ucp++ =  RHGET(w) & 0377;		break;
	case 1:	*ucp++ = (RHGET(w) >> 9) & 0377;	break;
	case 2:	*ucp++ =  LHGET(w) & 0377;		break;
	case 3:	*ucp++ = (LHGET(w) >> 9) & 0377;	break;
	    }
	}
	/* Now update BP as if read cnt bytes */
	LHSET(bp, bp9lhtab[i]);		/* Set up appropriate LH */

    } else {
	fprintf(dt->dt_dv.dv_dbf, "[DTE: Bad to-11 BP %lo,,%lo]",
		(long)LHGET(bp), (long)RHGET(bp));
	return;			/* For now, punt entirely */
    }

    vm_pset(abp, bp);		/* Store BP back in EPT */
}


/* Read packet/message word data
**	This also cheats by ignoring page boundaries once a physical memory
**	address is obtained.
*/
static h10_t bp16lhtab[5] = {
	0042000,
	0242000,
	0442000
};
static void dte_rdword(register struct dte *dt,
		       register int cnt,	/* Note: Byte count! */
		       register unsigned char *ucp)
{
    register w10_t bp, w;
    register vmptr_t abp, vp;
    register int i;
    register vaddr_t va;

    /* For now, assume DTE's starting BP is 16-bit OWLBP.
    ** Verify this before continuing.
    */
    abp = dt->dt_eptcb + DTE_CBOBP;	/* Get BP addr */
    bp = vm_pget(abp);
    i = LHGET(bp) >> 16;		/* Use high 2 bits as index */
    if (i > 3 || (bp16lhtab[i] != LHGET(bp))) {
	fprintf(dt->dt_dv.dv_dbf, "[DTE: Bad to-11 BP %lo,,%lo]",
		(long)LHGET(bp), (long)RHGET(bp));
	return;			/* For now, punt entirely */
    }

    /* Normal fast case. */
    va_gmake(va, 0, RHGET(bp));		/* Make exec virt addr, sect 0 */
    vp = vm_execmap(va, VMF_READ|VMF_NOTRAP);	/* Map it, don't trap */
    if (!vp) {
	panic("DTE: Page fail reading R indir at %lo", (long)RHGET(bp));
    }
    if (i > 0)		/* Set up first word */
	w = vm_pget(vp);

    cnt >>= 1;			/* Make count be a word count, round down */
    while (--cnt >= 0) {
	if (--i < 0) {			/* Bump to next word? */
	    op10m_inc(bp);
	    ++vp;
	    w = vm_pget(vp);
	    i = 1;
	}
	switch (i) {
	case 0:			/* Low 16-bit word */
	    *ucp++ = ((LHGET(w) & 03)<<6) | (RHGET(w) >> 12);
	    *ucp++ = (RHGET(w) >> 4) & 0377;
	    break;
	case 1:	
	    *ucp++ = (LHGET(w) >> 10) & 0377;
	    *ucp++ = (LHGET(w) >>  2) & 0377;
	    break;
	}
    }

    /* Now update BP as if read cnt bytes */
    LHSET(bp, bp16lhtab[i]);	/* Set up appropriate LH */
    vm_pset(abp, bp);		/* Store back in EPT */
}


static void dte_xctfn(register struct dte *dt)
{
    register int i;
    register unsigned char *cp;

    if (dt->dt_dv.dv_debug) {
	fputs("[DTE: R msg: ", dt->dt_dv.dv_dbf);
	dte_showpkt(dt, &dt->dt_rcvpkt);
	fputs("]\r\n", dt->dt_dv.dv_dbf);
    }

    switch (dt->dt_rcvpkt.dtep_fn & 0377) {

    case DTE_QFN_LCI:	/* 01 LINE COUNT IS */
	/* This message is sent by the 10 upon startup init.
	** 11 must respond with a From-11 #2 (DTE_QFN_ALS) message, possibly
	** other messages later.  (eg date/time)
	*/
	dte_10qpkt(dt,
		SWAB(DTE_QFN_ALS),	/* Function 02 CTY Alias is */
		SWAB(DTE_QDV_CTY),	/* CTY dev */
		SWAB(DTE_DLS_CTY),	/* CTY DLS Line */
		0, 0, NULL);		/* No other data */

	/* Send output buffer allocation */
	if (dt->dt_ctyobs > 0) {	/* Optional, can turn off */
	    dte_10qpkt(dt,
		SWAB(DTE_QFN_HLA),	/* Function 023 Here is Line Alloc */
		SWAB(DTE_QDV_CTY),	/* CTY dev */
		SWAB((DTE_DLS_CTY<<8) | dt->dt_ctyobs),	/* Line 0, alloc */
		0, 0, NULL);		/* No other data */
	}

	dte_dtmsend(dt);		/* Also send date/time */
	return;

#if 0	/* to-11 Unsupported by 11 FE */
    case DTE_QFN_ALS:	/* 02 CTY device alias */
#endif

    case DTE_QFN_HSD:	/* 03 HERE IS STRING DATA */
	if (dt->dt_rcvpkt.dtep_db0 == DTE_DLS_CTY) {	/* If line 0 (CTY) */

    case DTE_QFN_STA:	/* 014 SEND TO ALL (SENT TO 11 ONLY) */
			/*	For now, same as string data to CTY */
	    if ((i = dt->dt_rcvpkt.dtep_tcnt - DTE_QMH_SIZ) > 0)
		if (i > dt->dt_rcvpkt.dtep_db1)
		    i = dt->dt_rcvpkt.dtep_db1;	/* # bytes in string */
#if 1	/* New string regime */
		dte_ctysout(dt, dt->dt_rcvpkt.dtep_data, i);
		dte_ctyforce(dt);
#else
		for (cp = dt->dt_rcvpkt.dtep_data; --i >= 0;)
		    dte_ctyout(dt, *cp++);
#endif
	    dte_ctyack(dt);		/* Temp hack - always ACK for now */
	}
	return;

    case DTE_QFN_HLC:	/* 04 HERE ARE LINE CHARACTERS */
	/* If 1st pair is line 0 (CTY), then output data char */
	if (dt->dt_rcvpkt.dtep_db0 == DTE_DLS_CTY)
	    dte_ctyout(dt, dt->dt_rcvpkt.dtep_db1);	/* Yep, do it! */

	/* Same for all additional pairs */
	if ((i = dt->dt_rcvpkt.dtep_tcnt - DTE_QMH_SIZ) > 0) {
	    for (cp = dt->dt_rcvpkt.dtep_data; i > 0; i -= 2, cp += 2)
		if (*cp == DTE_DLS_CTY)
		    dte_ctyout(dt, cp[1]);
	}
#if 1
	dte_ctyforce(dt);	/* Force out any buffered CTY output */
#endif
	dte_ctyack(dt);		/* Temp hack - always ACK for now */
	return;

    case DTE_QFN_RDS:	/* 05 REQUEST DEVICE STATUS */
    case DTE_QFN_SDO:	/* 06 SPECIAL DEVICE OPERATION */
    case DTE_QFN_STS:	/* 07 HERE IS DEVICE STATUS */
#if 0	/* Unsupported by 11 FE */
    case DTE_QFN_ESD:	/* 010 ERROR ON DEVICE */
#endif
	break;

    case DTE_QFN_RTD:	/* 011 REQUEST TIME OF DAY */
	dte_dtmsend(dt);	/* No data, just respond with date/time */
	return;

    case DTE_QFN_HTD:	/* 012 HERE IS TIME OF DAY */
    case DTE_QFN_FDO:	/* 013 FLUSH OUTPUT (SENT TO 11 ONLY) */
	break;
			/* 014 Handled up near 03 */
    case DTE_QFN_LDU:	/* 015 A LINE DIALED UP (FROM 11 ONLY) */
    case DTE_QFN_LHU:	/* 016 A LINE HUNG UP OR LOGGED OUT */
    case DTE_QFN_LBE:	/* 017 LINE BUFFER BECAME EMPTY */
    case DTE_QFN_XOF:	/* 020 XOF COMMAND TO THE FE */
    case DTE_QFN_XON:	/* 021 XON COMMAND TO THE FE */
    case DTE_QFN_SPD:	/* 022 SET TTY LINE SPEED */
    case DTE_QFN_HLA:	/* 023 HERE IS LINE ALLOCATION */
#if 0	/* Unsupported by 11 FE */
    case DTE_QFN_HRW:	/* 024 HERE IS -11 RELOAD WORD */
#endif
    case DTE_QFN_ACK:	/* 025 GO ACK ALL DEVICES AND UNITS */
    case DTE_QFN_TOL:	/* 026 TURN OFF/ON LINE (0 off, 1 on) */
    case DTE_QFN_EDR:	/* 027 ENABLE/DISABLE DATASETS */
    case DTE_QFN_LTR:	/* 030 LOAD TRANSLATION RAM */
    case DTE_QFN_LVF:	/* 031 LOAD VFU */
    case DTE_QFN_MSG:	/* 032 SUPPRESS SYSTEM MESSAGES TO TTY */
    case DTE_QFN_KLS:	/* 033 SEND KLINIK DATA TO THE -11 */
    case DTE_QFN_XEN:	/* 034 ENABLE XON (SENT TO 11 ONLY) */
    case DTE_QFN_BKW:	/* 035 BREAK-THROUGH WRITE */
    case DTE_QFN_DBN:	/* 036 DEBUG MODE ON */
    case DTE_QFN_DBF:	/* 037 DEBUG MODE OFF */
    case DTE_QFN_SNO:	/* 040 IGNORE NEW CODE FOR SERIAL NUMBERS */
	break;
    default:
	fprintf(dt->dt_dv.dv_dbf, "[DTE: Unknown R fn: %o]\r\n",
		dt->dt_rcvpkt.dtep_fn);
	return;			/* Do nothing after complaining */
    }

    /* If get here, function is known but can't be xctd. */
    if (dt->dt_dv.dv_debug) {
	/* Packet already shown, don't show it again */
	fprintf(dt->dt_dv.dv_dbf, "[DTE: Unimpl R fn: %o]\r\n", 
		dt->dt_rcvpkt.dtep_fn);
    } else if (dt->dt_dowarn) {
	fputs("[DTE: Unimpl R fn: ", dt->dt_dv.dv_dbf);
	dte_showpkt(dt, &dt->dt_rcvpkt);
	fputs("]\r\n", dt->dt_dv.dv_dbf);
    }
}

static void dte_showpkt(register struct dte *dt,
			register struct dtepkt_s *dtp)
{
    register FILE *f = dt->dt_dv.dv_dbf;
    register int i;
    register unsigned char *cp;

    if ((i = dtp->dtep_tcnt) != dtp->dtep_cnt)
	fprintf(f, "tot=%d. ", i);
    fprintf(f, "cnt=%d. fn=%o dev=%o d0=%o d1=%o",
		dtp->dtep_cnt, dtp->dtep_fn, dtp->dtep_dev,
		dtp->dtep_db0, dtp->dtep_db1);
    if ((i -= DTE_QMH_SIZ) > 0) {
	if (dtp->dtep_wdmod)
	    fputs(" WDMOD", f);
	if (i > sizeof(dtp->dtep_data))
	    i = sizeof(dtp->dtep_data);	/* Truncate too-large count */
	cp = &dtp->dtep_data[0];
	while (--i >= 0)
	    fprintf(f, " %3o", *cp++);
    }
}

/* Stuff for sending TO the 10.
**	Must maintain a small queue, sigh.
*/

/* General procedure:

When 11 wants to send something to 10:
	Modifies its to-10 region:
		QSIZE set to 0,0,<# bytes in pkt>.
		STS has its TO10IC byte incremented.  (FE TO11QC, misnomer)
	Sets up DTE from its side for data xfer (byte mode, buffer addr)
	Triggers To-10 doorbell!

10 handles doorbell interrupt:
	Notices TO10IC has incremented by one (if not, no xfer started).
	Sets its copy of TO10IC to be same (stored in 10's to-11 region for
		this 11)
	Sets its CMTOT bit (likewise in 10's to-11 status).
	Clears doorbell
	Gets CMQCT from 11's to-10 region (what 11 just set up)
	Sets up DTE byte ptr.
	Does DATAO with right count (# bytes, or # words).
		Adds "I" bit if can read all of CMQCT into buffer, else
		leaves off (fragmented read).
	Returns.

<DTE does xfer>

10-done interrupt always given to 10.
11-done interrupt only given to 11 if "I" bit was set.

*/

/* Called when 10 gives DATAO to initiate transfer.
*/
static void dte_10xfrbeg(register struct dte *dt)
{
    register struct dteq_s *q;
    register int i;

    /* See if anything ready to send */
    if (!(q = dt->dt_sndq))
	return;				/* Nope, just return silently */

    /* Yep, send it! */
    if (dt->dt_dv.dv_debug)
	fprintf(dt->dt_dv.dv_dbf, "[dte_10xfrbeg: sta=%d %o %o %o %d. %d.]",
		dt->dt_snd_state,
		q->q_sfn, q->q_sdev, q->q_swd1, q->q_bbcnt, q->q_wbcnt);


    /* Determine our state.  We could be:
    **	- Starting header xfer
    **	- in middle of header/data xfer
    **	- Starting indirect data xfer
    **	- in middle of indirect data xfer
    */

    /* For now, assume always have enough room to send at least the 10 header
    ** bytes, so no intermediate states will exist for it.
    */

    switch (dt->dt_snd_state) {
    case DTSND_HDR:
	if (dt->dt_snd_cnt < DTE_QMH_SIZ) {
	    fprintf(dt->dt_dv.dv_dbf, "[dte_10xfrbeg: bad S cnt in DATAO: %d]",
			dt->dt_snd_cnt);
	    return;
	}
	/* Write header, updates BP */
	dte_wrhd(dt, q);

	/* Update count, determine new state */
	if (q->q_bbcnt || q->q_wbcnt)		/* More data to send? */
	    dt->dt_snd_state =			/* Yes, see if indirect */
		(q->q_sfn & SFN_INDBIT) ? DTSND_IND : DTSND_DAT;
	dt->dt_snd_cnt -= DTE_QMH_SIZ;
	if (dt->dt_snd_cnt <= 0 || dt->dt_snd_state != DTSND_DAT)
	    break;
	/* Drop through to continue xfer with direct byte data */

    case DTSND_DAT:
	if (!(i = q->q_bbcnt)) {
	    fprintf(dt->dt_dv.dv_dbf, "[dte_10xfrbeg: No bcnt??]");
	    break;
	}
	if (i > dt->dt_snd_cnt)
	    i = dt->dt_snd_cnt;		/* Partial xfer */
	dte_wrbyte(dt, q, i);
	dt->dt_snd_cnt -= i;
	break;

    case DTSND_IND:
	if (!(i = q->q_wbcnt)) {
	    fprintf(dt->dt_dv.dv_dbf, "[dte_10xfrbeg: No wbcnt??]");
	    break;
	}
	i >>= 1;			/* Make byte cnt into word cnt */
	if (i > dt->dt_snd_cnt)
	    i = dt->dt_snd_cnt;		/* Partial xfer */
	dte_wrword(dt, q, i);
	dt->dt_snd_cnt -= i;
	break;

    default:
	panic("[dte_10xfrbeg: bad state %o", dt->dt_snd_state);
    }

    if (dt->dt_snd_cnt > 0)
	fprintf(dt->dt_dv.dv_dbf, "[dte_10xfrbeg: 10cnt left: %d]",
		dt->dt_snd_cnt);

    /* Transmission done, always give 10 an interrupt. */
    dt->dt_cond |= DTE_CI10DN;		/* Set 10-Done bit */

    /* If I-bit set, "interrupt" 11 as well.  This basically tells the
    ** 11 that this packet xfer is done, set up for next one.  Here
    ** it is interpreted to simply take current packet off queue and
    ** line up the next one.
    */
    if (dt->dt_snd_ibit) {

	/* If packet all done, take it off queue now */
	if (!(dt->dt_sndq = q->q_next))	/* Remove from queue */
	    dt->dt_sndqtail = NULL;	/* If became empty, fix up tail */
	q->q_next = dteqfreep;		/* Add node to head of freelist */
	dteqfreep = q;

	/* Now should we rig up com area and push to-10 doorbell again,
	** or wait for 10 to handle its PI?  Damn the torpedoes...
	*/
	if (dt->dt_sndq)
	    dte_10start(dt);
    } else {
	/* Packet not all done.  Make sure we have more stuff, just to
	** double-check.
	*/
	if (!q->q_bbcnt && !q->q_wbcnt)
	    fprintf(dt->dt_dv.dv_dbf, "[dte_10xfrbeg: out of data, no I bit]");

	/* If more stuff to be done, what else needs to be set up?
	** Should dte_10start() be invoked to set up com reg differently??
	** YES!!!!!!
	*/

	/* ## MUST WRITE CODE HERE IF SENDING INDIRECT ## */
    }

    dte_pi(dt);			/* Always attempt PI for 10 */
}

/* Write to-10 message header in 10's memory.
**	This routine cheats in a number of ways:
**	(1) Assumes header is all in contiguous phys memory
**	(2) Assumes deposit BP is 8-bit OWLBP to start of wd.
**	(3) Clears unused low bits of 3 words (4, 4, 20).
*/
static void dte_wrhd(register struct dte *dt, register struct dteq_s *q)
{
    register w10_t bp, w;
    register vmptr_t abp, vp;
    register vaddr_t va;
    register unsigned int i;

    /* For now, assume DTE's starting BP is 8-bit OWLBP at beg of word.
    ** Verify this before continuing.
    */
    abp = dt->dt_eptcb + DTE_CBIBP;	/* Get BP addr */
    bp = vm_pget(abp);
    if (LHGET(bp) != 0441000) {
	fprintf(dt->dt_dv.dv_dbf, "[DTE: Bad to-10 BP %lo,,%lo]",
		(long)LHGET(bp), (long)RHGET(bp));
	return;			/* For now, punt entirely */
    }

    if (dt->dt_dv.dv_debug)
	fprintf(dt->dt_dv.dv_dbf, "[dte_wrhd: %o %o %o %d. %d. -> %lo]",
		q->q_sfn, q->q_sdev, q->q_swd1, q->q_bbcnt, q->q_wbcnt,
		(long)RHGET(bp));

    /* Normal fast case. */
    va_gmake(va, 0, RHGET(bp));		/* Make exec virt addr, sect 0 */
    vp = vm_execmap(va, VMF_WRITE|VMF_NOTRAP);	/* Map it, don't trap */
    if (!vp) {
	panic("DTE: Page fail writing to-10 hdr at %lo", (long)RHGET(bp));
    }

    /* Set 1st word.  Note @ bit in function already set up. */
    i = DTE_QMH_SIZ + q->q_bbcnt + q->q_wbcnt;
    i = SWAB(i);			/* Get total byte count */
    LRHSET(w, ((h10_t)i << 2) | (((q->q_sfn)>>14)&03),
		(((h10_t)q->q_sfn << 4) & H10MASK));
    vm_pset(vp, w);			/* Set 1st word */

    LRHSET(w, ((h10_t)q->q_sdev<<2), 0);
    vm_pset(vp+1, w);			/* Set 2nd word */

    LRHSET(w, ((h10_t)q->q_swd1<<2), 0);
    vm_pset(vp+2, w);			/* Set 3rd word */

    /* Now update BP as if written 10 bytes */
    op10m_addi(bp, 2);
    LHSET(bp, 0241000);
    vm_pset(abp, bp);			/* Store back in EPT */
}

/* Write packet/message byte data
**	This also cheats by ignoring page boundaries once a physical memory
**	address is obtained.
*/
static void dte_wrbyte(register struct dte *dt, register struct dteq_s *q, register int cnt)
{
    register w10_t bp, w;
    register vmptr_t abp, vp;
    register int i;
    register vaddr_t va;
    register uint18 uhw;
    register unsigned char *ucp;

    /* For now, assume DTE's starting BP is 8-bit OWLBP.
    ** Verify this before continuing.
    */
    abp = dt->dt_eptcb + DTE_CBIBP;	/* Get BP addr */
    bp = vm_pget(abp);
    i = LHGET(bp) >> 15;		/* Use high octal digit as index */
    if (i > 4 || (bp8lhtab[i] != LHGET(bp))) {
	fprintf(dt->dt_dv.dv_dbf, "[DTE: Bad to-10 BP %lo,,%lo]",
		(long)LHGET(bp), (long)RHGET(bp));
	return;			/* For now, punt entirely */
    }

    /* Normal fast case. */
    va_gmake(va, 0, RHGET(bp));		/* Make exec virt addr, sect 0 */
    vp = vm_execmap(va, VMF_WRITE|VMF_NOTRAP);	/* Map it, don't trap */
    if (!vp) {
	panic("DTE: Page fail writing S hdr at %lo", (long)RHGET(bp));
    }

    if (!(ucp = q->q_dcp)) {
	panic("dte_wrbyte: no data ptr");
    }
    q->q_bbcnt -= cnt;		/* Pre-update count of data */

    w = vm_pget(vp);		/* Set up first word */

    while (--cnt >= 0) {
	if (--i < 0) {			/* Bump to next word? */
	    op10m_inc(bp);
	    vm_pset(vp, w);		/* Store word we're done with */
	    ++vp;
	    w = vm_pget(vp);
	    i = 3;
	}
	uhw = *ucp++ & 0377;
#define UHBYTE ((uint18)0377)
	switch (i) {
	case 0:	RHSET(w, (RHGET(w) & ~(UHBYTE<<4)) | (uhw<<4));
		break;
	case 1:	LRHSET(w, (LHGET(w) & ~03) | ((uhw >> 6) & 03),
			(RHGET(w) & 07777) | ((uhw & 077)<<(8+4)));
		break;
	case 2:	LHSET(w, (LHGET(w) & ~(UHBYTE<<2)) | (uhw << 2));
		break;
	case 3:	LHSET(w, (LHGET(w) & ~(UHBYTE<<(8+2))) | (uhw << (8+2)));
		break;
#undef UHBYTE
	}
    }
    q->q_dcp = ucp;		/* Update queue pkt data ptr */
    vm_pset(vp, w);		/* Store word we're done with */

    /* Now update BP as if written cnt bytes */
    LHSET(bp, bp8lhtab[i]);	/* Set up appropriate LH */
    vm_pset(abp, bp);		/* Store back in EPT */
}

static void dte_wrword(register struct dte *dt,
		       register struct dteq_s *q,
		       register int wcnt)	/* Note: word count! */
{
    register w10_t bp, w;
    register vmptr_t abp, vp;
    register int i;
    register vaddr_t va;

    /* For now, assume DTE's starting BP is 16-bit OWLBP.
    ** Verify this before continuing.
    */
    abp = dt->dt_eptcb + DTE_CBIBP;	/* Get BP addr */
    bp = vm_pget(abp);
    i = LHGET(bp) >> 16;		/* Use high 2 bits as index */
    if (i > 3 || (bp16lhtab[i] != LHGET(bp))) {
	fprintf(dt->dt_dv.dv_dbf, "[DTE: Bad to-10 BP %lo,,%lo]",
		(long)LHGET(bp), (long)RHGET(bp));
	return;			/* For now, punt entirely */
    }

    /* Normal fast case. */
    va_gmake(va, 0, RHGET(bp));		/* Make exec virt addr, sect 0 */
    vp = vm_execmap(va, VMF_WRITE|VMF_NOTRAP);	/* Map it, don't trap */
    if (!vp) {
	panic("DTE: Page fail writing S indir at %lo", (long)RHGET(bp));
    }

    if (i > 0)		/* Set up first word */
	w = vm_pget(vp);
#if 0
    cnt >>= 1;			/* Make count be a word count, round down */
    while (--cnt >= 0) {
	if (--i < 0) {			/* Bump to next word? */
	    op10m_inc(bp);
	    ++vp;
	    w = vm_pget(vp);
	    i = 1;
	}
	switch (i) {
	case 0:			/* Low 16-bit word */
	    *ucp++ = ((LHGET(w) & 03)<<6) | (RHGET(w) >> 12);
	    *ucp++ = (RHGET(w) >> 4) & 0377;
	    break;
	case 1:	
	    *ucp++ = (LHGET(w) >> 10) & 0377;
	    *ucp++ = (LHGET(w) >>  2) & 0377;
	    break;
	}
    }
#else
    panic("dte_wrword: not implemented yet");
#endif

    /* Now update BP as if read cnt bytes */
    LHSET(bp, bp16lhtab[i]);	/* Set up appropriate LH */
    vm_pset(abp, bp);		/* Store back in EPT */
}

/* Code for 11 side to queue up packets for 10.
*/


/* Set up and enqueue a packet
*/
static int dte_10qpkt(register struct dte *dt,
		      unsigned int sfn,
		      unsigned int sdev,
		      unsigned int swd1,
		      int bbcnt,
		      int wbcnt,
		      unsigned char *dcp)
{
    register struct dteq_s *q;

    if (dt->dt_dv.dv_debug)
	fprintf(dt->dt_dv.dv_dbf, "[dte_10qpkt: %o %o %o %d. %d.]",
				sfn, sdev, swd1, bbcnt, wbcnt);

    /* First get a free queue element */
    if (!(q = dteqfreep)) {
	fprintf(dt->dt_dv.dv_dbf, "[DTE: no free QPs!]");
	return FALSE;
    }
    dteqfreep = q->q_next;		/* Pluck off freelist */

    /* Fill it out */
    q->q_next = NULL;
    q->q_sfn = sfn;
    q->q_sdev = sdev;
    q->q_swd1 = swd1;
    q->q_bbcnt = bbcnt;
    if (q->q_wbcnt = (bbcnt ? 0 : wbcnt))	/* If sending words, */
	q->q_sfn |= SFN_INDBIT;		/* Force msg to use indirect data */
    if (bbcnt || wbcnt)
	q->q_dcp = dcp;

    /* Put on tail of queue */
    if (dt->dt_sndqtail) {
	dt->dt_sndqtail->q_next = q;	/* Queue exists */
	dt->dt_sndqtail = q;		/* Update ptr to tail */
	/* Assume xfer in progress, no need to restart it */

	/* Ring doorbell always, or only if no xfer in progress?
	** For now, always ring, just in case.
	*/
	dt->dt_cond |= DTE_CI10DB;		/* Set 10 doorbell */
	dte_pi(dt);

    } else {
	dt->dt_sndq = q;		/* No queue, create it */
	dt->dt_sndqtail = q;		/* Update ptr to tail */

	/* No xfr in progress, so start one.  Rings doorbell. */
	dte_10start(dt);		/* Start xfer going */
    }
    return TRUE;
}

/* Start xfer going */
static void dte_10start(register struct dte *dt)
{
    register struct dteq_s *q;
    register w10_t w;

    if (!(q = dt->dt_sndq))
	return;

    dt->dt_snd_state = DTSND_HDR;	/* Init our sending state */

    /* Modify 11's to-10 region:
    **		STS has its TO10IC byte incremented.  (FE TO11QC, misnomer)
    **		QSIZE set to 0,0,<# bytes in direct pkt xfer>.
    ** Sets up any internal state needed to properly handle 10's DATAO.
    ** Then triggers To-10 doorbell!
    ** WARNING!!!! This code clobbers DTE_CMT_IP and DTE_CMT_TOT in the
    ** RH of the STS word... must preserve them if/when that becomes
    ** important to support indirect xfers!
    */
    RHSET(dt->dt_snd_sts, (RHGET(dt->dt_snd_sts) + (1<<8))
		& (DTE_CMT_10IC | DTE_CMT_11IC));
    if (dt->dt_dv.dv_debug)
	fprintf(dt->dt_dv.dv_dbf, "[dte_10start: sts %lo,,%lo]",
			(long)LHGET(dt->dt_snd_sts),
			(long)RHGET(dt->dt_snd_sts));

    dte_store(dt, dt->dt_dt10off + DTE_CMTW_STS, dt->dt_snd_sts);

    LRHSET(w, 0, DTE_QMH_SIZ + q->q_bbcnt);	/* Total direct data this xfer */
    dte_store(dt, dt->dt_dt10off + DTE_CMTW_CNT, w);

    dt->dt_cond |= DTE_CI10DB;		/* Set to-10 doorbell */
    dte_pi(dt);				/* Trigger 10 PI */
}

/* Date/time hackery so 11 can send true date/time to 10.
**	The From-11 format used here differs from that sent by a real 11.
**	A real -11 sends a word-indirect mode packet with the date/time info
**		starting in the word-indirect block.
**	But this code sends a byte direct packet with the date/time info
**		starting with the 1st-word data.
**		This is done for simplicity.
**
**	Also, note a static buffer is used.  This should be OK even if
**	multiple DTEs or packets are involved, since we always want to
**	send the most recent time data!
*/
#define DTM_LEN 8

static void dte_dtmsend(register struct dte *dt)
{
    static unsigned char dtmbuf[DTM_LEN];
    struct tm t;
    register unsigned int i;
    int valid, zone;

    /* Fill out data for packet */
    if (os_tmget(&t, &zone)) {
	i = t.tm_year + 1900;		/* Make year full A.D. quantity */
	dtmbuf[0] = (i >> 8) & 0377;	/* Set high byte of year */
	dtmbuf[1] = (i & 0377);		/* Set low byte */

	dtmbuf[2] = t.tm_mon;		/* Set month (0-11) */
	dtmbuf[3] = t.tm_mday - 1;	/* Set day-of-month (0-31) */
	dtmbuf[4] = (t.tm_wday+6) % 7;	/* Set day-of-week */
					/* DEC: 0=Mon, Unix: 0=Sun */
	dtmbuf[5] = (t.tm_isdst ? 0200 : 0)	/* Set DST flag */
	    | (zone & 0177);		/* and timezone (integral hour) */

	/* Time is secs/2 to fit in a 16-bit word */
	i = (((((long)t.tm_hour * 60) + t.tm_min) * 60) + t.tm_sec) >> 1;
	dtmbuf[6] = (i >> 8) & 0377;
	dtmbuf[7] = (i & 0377);

	valid = MASK16;		/* 1st byte non-Z means date/time valid! */
    } else {
	valid = 0;		/* 1st byte 0 means date/time invalid */
	memset(dtmbuf, 0, DTM_LEN);	/* Clear rest of bytes */
    }

    dte_10qpkt(dt,
		SWAB(DTE_QFN_HTD),	/* Function 012 Here is ToD */
		SWAB(DTE_QDV_CLK),	/* Use clock as "device" */
		valid,			/* 1st wd is valid flag */
		DTM_LEN, 0, dtmbuf);	/* Send date/time data! */

    dt->dt_dtmsent = TRUE;		/* Say date/time was sent! */
}

/* External for DVCTY code to get into here.  Sorta hackish for now.
**	Later spiff up with string input for greater efficiency.
*/
int dte_ctysin(int cnt)
{
    register struct dte *dt = &dvdte[0];	/* Assumption for now */
    register int ch;

    /* If no DTE defined yet or no input, just return */
    if (!ndtes || (ch = fe_ctyin()) < 0)	/* Get single char */
	return 0;				/* None left */

    dt = &dvdte[0];				/* Assumption for now */
    if (dt->dt_secptcl) {
	/* Do secondary protocol CTY input */
	register vmptr_t vpf, vpc;
	register w10_t w;

	vpf = vm_physmap(cpu.mr_ebraddr + DTEE_MTI);	/* Mon TTY in flg */
	vpc = vm_physmap(cpu.mr_ebraddr + DTEE_F11);	/* From-11 data */
	if (op10m_skipn(vm_pget(vpf))) {
	    /* If flag non-zero, 10 hasn't picked up the char yet */
	    fprintf(stderr, "[DTEI: %o (old %o!)]", ch, (int)vm_pgetrh(vpc));
	} else if (cpu.fe.fe_ctydebug)
	    fprintf(stderr, "[DTEI: %o]", ch);

	/* Drop char in special DTE EPT communication area */
	LRHSET(w, 0, ch);
	vm_pset(vpc, w);

	/* Tell 10 that stuff is there */
	op10m_seto(w);
	vm_pset(vpf, w);

	dt->dt_cond |= DTE_CI10DB;		/* Set to-10 doorbell */
	dte_pi(dt);				/* Trigger 10 PI */

    } else {
	/* Do primary protocol CTY input */

	if (! dte_10qpkt(dt,
		SWAB(DTE_QFN_HLC),	/* Queue a line input packet */
		SWAB(DTE_QDV_CTY),	/* from CTY device */
		SWAB((DTE_DLS_CTY<<8) | ch),	/* Line 0, data */
		0, 0, NULL)) {		/* Nothing else in msg */
	    fprintf(stderr, "[DTEI: %o dropped]", ch);
	} else if (cpu.fe.fe_ctydebug)
	    fprintf(stderr, "[DTEI: %o]", ch);
    }

    return cnt-1;				/* One less input char! */
}

static int ctyalloc = 10;	/* Hack for now */


/* Send ACK for CTY output, with possible delay.
*/
static void dte_ctyack(register struct dte *dt)
{
    if (dt->dt_dlyackms) {
	if (!dt->dt_dlyackf) {			/* Already awaiting timeout? */
	    clk_tmractiv(dt->dt_dlyack);	/* No, make it active again */
	    dt->dt_dlyackf = TRUE;		/* Timer's ticking... */
	} 
#if 0
	else fprintf(stderr, "[DTE: ack buildup]\r\n");
#endif
	return;
    }
    dte_ctyiack(dt);			/* No delay, do immediate ACK */
}

static void dte_ctyiack(register struct dte *dt)
{
    if (!dte_10qpkt(dt,
		SWAB(DTE_QFN_LBE),	/* Function 17 Line Buffer Empty */
		SWAB(DTE_QDV_CTY),
		SWAB(DTE_DLS_CTY),	/* CTY Line */
		0, 0, NULL)) {		/* No other data */
      fprintf(dt->dt_dv.dv_dbf, "[DTE: CTY ack failed]");
      ctyalloc = 10;
    } else
      ctyalloc = 10;	/* Was 100 but was causing wedging... */
}


#define DTE_CTYPUTC(dt, c) \
	if (--((dt)->dt_ctyocnt) < 0) {	\
	    dte_ctyforce(dt);	/* Force out & reset buffer */\
	    (dt)->dt_ctyocnt--;	\
	}			\
	/* Deposit in buffer, masking off stupid T20 parity */\
	*((dt)->dt_ctyocp)++ = ((c) & 0177)


static void dte_ctyout(register struct dte *dt, register int ch)
{
#if 1
    if (cpu.fe.fe_ctydebug)
	fprintf(stderr, "[DTEO: %o]", ch);
#else
    if (dt->dt_dv.dv_debug)
	fprintf(dt->dt_dv.dv_dbf, "[DTEO: %o]", ch);
#endif

    DTE_CTYPUTC(dt, ch);

#if 0	/* Sigh, keeps wedging anyway */
    if (--ctyalloc <= 0)
	dte_ctyack(dt);
#endif
}

static void dte_ctysout(register struct dte *dt,
			register unsigned char *cp,
			register int len)
{
#if 1
    if (cpu.fe.fe_ctydebug)
	fprintf(stderr, "[DTESO: %d \"%.*s\"]\r\n", len, len, cp);
#else
    if (dt->dt_dv.dv_debug)
	fprintf(dt->dt_dv.dv_dbf, "[DTESO: %d \"%.*s\"]\r\n", len, len, cp);
#endif

    while (--len >= 0) {
	DTE_CTYPUTC(dt, *cp++);
    }
}

/* Force out anything buffered for CTY.  Blocks!
*/
static void dte_ctyforce(register struct dte *dt)
{
    register int chunk;

#if 0
    if (dt->dt_dv.dv_debug)
	fprintf(dt->dt_dv.dv_dbf, "[DTEFRC: %d \"%.*s\"]\r\n", len, len, cp);
#endif
    chunk = sizeof(dt->dt_ctyobuf) - dt->dt_ctyocnt;
    if (chunk > 0) {
	fe_ctysout(dt->dt_ctyobuf, chunk);
    }
    dt->dt_ctyocnt = sizeof(dt->dt_ctyobuf);
    dt->dt_ctyocp = dt->dt_ctyobuf;
}

#endif /* KLH10_DEV_DTE Moby conditional */

