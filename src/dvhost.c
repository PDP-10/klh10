/* DVHOST.C - Fake "Host" device to provide access to native host platform
*/
/* $Id: dvhost.c,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: dvhost.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*
*/
#include "klh10.h"

#if !KLH10_DEV_HOST && CENV_SYS_DECOSF
	/* Stupid gubbish needed to prevent OSF/1 AXP compiler from
	** halting merely because compiled file is empty!
	*/
static int decosfcclossage;
#endif

#if KLH10_DEV_HOST		/* Moby conditional for entire file */

#include <stddef.h>	/* For size_t etc */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "kn10def.h"	/* This includes OSD defs */
#include "kn10dev.h"
#include "kn10ops.h"
#include "dvhost.h"
#include "dvlites.h"
#include "kn10clk.h"	/* Need access to clock stuff */
#include "prmstr.h"	/* For parameter parsing */

#ifdef RCSID
 RCSID(dvhost_c,"$Id: dvhost.c,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

struct host {
    struct device hst_dv;	/* Generic 10 device structure */
#if !KLH10_CPU_KS
    unsigned long hst_lhcond;
    unsigned long hst_cond;
#endif
};
static int nhosts = 0;

#define DVHOST_NSUP 1		/* Should never be anything else! */

struct host dvhost[DVHOST_NSUP];


/* Function predecls */

static int   hst_conf(FILE *f, char *s, struct host *hst);

#if KLH10_CPU_KS
static void hst_write(struct device *,	/* Unibus register write */
		      uint18, dvureg_t);
#else
static void  hst_cono(struct device *, h10_t);	/* CONO 18-bit conds out */
static w10_t hst_coni(struct device *);	/* CONI 36-bit conds in  */
static void  hst_datao(struct device *, w10_t);	/* DATAO word out */
static w10_t hst_datai(struct device *);	/* DATAI word in */
#endif /* !KLH10_CPU_KS */


/* Configuration Parameters */

#if KLH10_CPU_KS
# define DVHOST_PARAMS \
    prmdef(HSTP_DBG, "debug"),	/* Initial debug value */\
    prmdef(HSTP_BR,  "br"),	/* BR priority */\
    prmdef(HSTP_VEC, "vec"),	/* Interrupt vector */\
    prmdef(HSTP_ADDR,"addr")	/* Unibus address */
#else
# define DVHOST_PARAMS \
    prmdef(HSTP_DBG, "debug")	/* Initial debug value */
#endif /* KLH10_CPU_KS */

enum {
# define prmdef(i,s) i
	DVHOST_PARAMS
# undef prmdef
};

static char *hstprmtab[] = {
# define prmdef(i,s) s
	DVHOST_PARAMS
# undef prmdef
	, NULL
};

/* HST_CONF - Parse configuration string and set defaults.
**	At this point, device has just been created, but not yet bound
**	or initialized.
** NOTE that some strings are dynamically allocated!  Someday may want
** to clean them up nicely if config fails or device is uncreated.
*/
static int hst_conf(FILE *f, char *s, struct host *hst)
{
    int i, ret = TRUE;
    struct prmstate_s prm;
    char buff[200];
#if KLH10_CPU_KS
    long lval;
#endif

    /* First set defaults for all configurable parameters
	Unfortunately there's currently no way to access the UBA # that
	we're gonna be bound to, otherwise could set up defaults.  Later
	fix this by giving dvhost_create() a ptr to an arg structure, etc.
     */
    DVDEBUG(hst) = FALSE;

    prm_init(&prm, buff, sizeof(buff),
		s, strlen(s),
		hstprmtab, sizeof(hstprmtab[0]));
    while ((i = prm_next(&prm)) != PRMK_DONE) {
	switch (i) {
	case PRMK_NONE:
	    fprintf(f, "Unknown HOST parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;
	case PRMK_AMBI:
	    fprintf(f, "Ambiguous HOST parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;
	default:	/* Handle matches not supported */
	    fprintf(f, "Unsupported HOST parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;

	case HSTP_DBG:		/* Parse as true/false boolean or number */
	    if (!prm.prm_val)	/* No arg => default to 1 */
		DVDEBUG(hst) = 1;
	    else if (!s_tobool(prm.prm_val, &DVDEBUG(hst)))
		break;
	    continue;

#if KLH10_CPU_KS
	case HSTP_BR:		/* Parse as octal number */
	    if (!prm.prm_val || !s_tonum(prm.prm_val, &lval))
		break;
	    if (lval < 4 || lval > 7) {
		fprintf(f, "HOST BR must be one of 4,5,6,7\n");
		ret = FALSE;
	    } else
		hst->hst_dv.dv_brlev = lval;
	    continue;

	case HSTP_VEC:		/* Parse as octal number */
	    if (!prm.prm_val || !s_tonum(prm.prm_val, &lval))
		break;
	    if (lval < 4 || lval > 0400 || (lval&03)) {
		fprintf(f, "HOST VEC must be valid multiple of 4\n");
		ret = FALSE;
	    } else
		hst->hst_dv.dv_brvec = lval;
	    continue;

	case HSTP_ADDR:	/* Parse as octal number */
	    if (!prm.prm_val || !s_tonum(prm.prm_val, &lval))
		break;
#if 0
	    if (lval < (DVHOST_REG_N<<1) || (lval&037)) {
		fprintf(f, "HOST ADDR must be valid Unibus address\n");
		ret = FALSE;
	    } else
#endif
		hst->hst_dv.dv_addr = lval;
	    continue;
#endif /* KLH10_CPU_KS */
	}
	ret = FALSE;
	fprintf(f, "HOST param \"%s\": ", prm.prm_name);
	if (prm.prm_val)
	    fprintf(f, "bad value syntax: \"%s\"\n", prm.prm_val);
	else
	    fprintf(f, "missing value\n");
    }

    /* Param string all done, do followup checks or cleanup */
#if KLH10_CPU_KS
# if 0
    if (!hst->hst_dv.dv_brlev || !hst->hst_dv.dv_brvec || !hst->hst_dv.dv_addr) {
	fprintf(f, "HOST missing one of BR, VEC, ADDR params\n");
# else
    if (!hst->hst_dv.dv_addr) {
	fprintf(f, "HOST missing ADDR param\n");
# endif
	ret = FALSE;
    }
    /* Set 1st invalid addr */
    hst->hst_dv.dv_aend = hst->hst_dv.dv_addr + (DVHOST_REG_N * 2);
#endif /* KLH10_CPU_KS */

    return ret;
}

/* HOST interface routines to KLH10 */

struct device * dvhost_create(FILE *f, char *s)
{
    register struct host *hst;

    /* Parse string to determine which device to use, config, etc etc
    ** But for now, just allocate sequentially.  Hack.
    */ 
    if (nhosts >= DVHOST_NSUP) {
	fprintf(f, "Too many HOSTs, max: %d\n", DVHOST_NSUP);
	return NULL;
    }
    hst = &dvhost[nhosts++];			/* Pick unused dev */
    memset((char *)hst, 0, sizeof(*hst));	/* Clear it out */

    /* Initialize generic device part of HOST struct */
    iodv_setnull(&hst->hst_dv);		/* Initialize as null device */

#if KLH10_CPU_KS
    /* Operate as new-style Unibus device */
    hst->hst_dv.dv_write = hst_write;	/* Write unibus register */
#else
    /* Operate as old-style IO-bus device */
    hst->hst_dv.dv_cono = hst_cono;
    hst->hst_dv.dv_coni = hst_coni;
    hst->hst_dv.dv_datao = hst_datao;
    hst->hst_dv.dv_datai = hst_datai;
    hst->hst_lhcond = 0535450;	/* SIXBIT/KLH/ */
    hst->hst_cond = DVHOST_CO_STATE | DVHOST_CO_SETAUX | DVHOST_CO_IDLE ;
#endif

    /* Configure from parsed string and remember for init
    */
    if (!hst_conf(f, s, hst))
	return NULL;

    return &hst->hst_dv;
}


#if !KLH10_CPU_KS

/* CONO 18-bit conds out
**	Args D, ERH
** Returns nothing
*/
static insdef_cono(hst_cono)
{
    register struct host *hst = (struct host *)d;
    register uint18 cond = erh;

    if (DVDEBUG(hst))
	fprintf(DVDBF(hst), "[hst_cono: %lo]\r\n", (long)erh);

    if (cond & (DVHOST_CO_CPU | DVHOST_CO_DISK | DVHOST_CO_TAPE |
		DVHOST_CO_NET | DVHOST_CO_STATE))
        lights_status ((cond & DVHOST_CO_CPU) ? 1 : 0,
		       (cond & DVHOST_CO_DISK) ? 1 : 0,
		       (cond & DVHOST_CO_TAPE) ? 1 : 0,
		       (cond & DVHOST_CO_NET) ? 1 : 0,
		       (cond & DVHOST_CO_STATE) ? 1 : 0);
    if (cond & DVHOST_CO_SETAUX)
        lights_pgmaux ((cond >> 9) & 07);

    if (cond & DVHOST_CO_IDLE)
	clk_idle();		/* Invoke CLK_IDLE! */

				/* update host CONI */
    if (cond & DVHOST_CO_STATE)
      hst->hst_cond &= ~(DVHOST_CO_CPU | DVHOST_CO_DISK |
			 DVHOST_CO_TAPE | DVHOST_CO_NET);
    else hst->hst_cond |= (cond & (DVHOST_CO_CPU | DVHOST_CO_DISK |
				   DVHOST_CO_TAPE | DVHOST_CO_NET));
    if (cond & DVHOST_CO_SETAUX) {
      hst->hst_cond &= ~DVHOST_CO_AUX;
      hst->hst_cond |= cond & DVHOST_CO_AUX;
    }
}


/* CONI 36-bit conds in
**	Args D
** Returns condition word
*/
static insdef_coni(hst_coni)
{
    register w10_t w;
    register struct host *hst = (struct host *)d;

    if (DVDEBUG(hst))
	fprintf(DVDBF(hst), "[hst_coni: %lo,,%lo]\r\n",
		(long)hst->hst_lhcond, (long)hst->hst_cond);

    LRHSET(w, hst->hst_lhcond, hst->hst_cond);
    return w;
}

/* DATAO word out
**	Args D, W
** Returns nothing
*/
static insdef_datao(hst_datao)
{
    register struct host *hst = (struct host *)d;

    lights_pgmlites(LHGET(w), RHGET(w));
    if (DVDEBUG(hst))
	fprintf(DVDBF(hst), "[hst_datao: %lo,,%lo]\r\n",
		(long)LHGET(w), (long)RHGET(w));

}

/* DATAI word in
**	Args D
** Returns data word
*/
static insdef_datai(hst_datai)
{
    register struct host *hst = (struct host *)d;
    register w10_t w;

    w = cpu.mr_dsw;		/* Read data switches */
    if (DVDEBUG(hst))
	fprintf(DVDBF(hst), "[hst_datai: %lo,,%lo]\r\n",
		(long)LHGET(w), (long)RHGET(w));
    return w;
}

#endif /* !KLH10_CPU_KS */

#if KLH10_CPU_KS

/* Unibus interface routines */

static void hst_write(struct device *d, uint18 addr, register dvureg_t val)
{
    register struct host *hst = (struct host *)d;

    if (DVDEBUG(hst))
	fprintf(DVDBF(hst), "[hst_write: %lo]\r\n", (long)val);

    if (val == DVHOST_CO_IDLE)
	clk_idle();		/* Invoke CLK_IDLE! */
}
#endif /* KLH10_CPU_KS */

#endif /* KLH10_DEV_HOST */
