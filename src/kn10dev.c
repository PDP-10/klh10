/* KN10DEV.C - KLH10 Generic Device Support
*/
/* $Id: kn10dev.c,v 2.4 2001/11/10 21:28:59 klh Exp $
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
 * $Log: kn10dev.c,v $
 * Revision 2.4  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*
** This file contains generic device support code, including a "null device"
** definition that is used to help initialize other devices.
*/

#include <stdio.h>	/* For stderr */
#include <ctype.h>
#include <string.h>
#include <stdlib.h>	/* For malloc/free */

#include "klh10.h"
#include "osdsup.h"
#include "kn10def.h"
#include "kn10dev.h"
#include "kn10ops.h"
#include "prmstr.h"

#if KLH10_CPU_KS && KLH10_DEV_TM03
# include "dvtm03.h"	/* For setting up FECOM_BOOTP with magtape params */
#endif

#ifdef RCSID
 RCSID(kn10dev_c,"$Id: kn10dev.c,v 2.4 2001/11/10 21:28:59 klh Exp $")
#endif

/* Imported functions */
extern void pi_devupd(void);

/* NULL device routines, for handling non-existent device. */

static void  dvnull_v(struct device *d) { }
static int   dvnull_i(struct device *d) { return 0; }
static w10_t dvnull_w(struct device *d)
			{ register w10_t w; op10m_setz(w); return w; }
static int   dvnull_ifs(struct device *d, FILE *f, char *s)
			{ return 0; }
static int   dvnull_ifss(struct device *d, FILE *f, char *s, char *s2)
			{ return 0; }
static int   dvnull_if(struct device *d, FILE *f)
			{ return 1; }
static dvureg_t dvnull_16(struct device *d)
			{ return 0; }
static dvureg_t dvnull_rd(struct device *d, dvuadr_t a)
			{ return 0; }
static void   dvnull_wr(struct device *d, dvuadr_t a, dvureg_t u)
			{ }

static void   dvnull_attn(struct device *d, int i)
			{ }
static int    dvnull_iobeg(struct device *d, int i)
			{ return 0; }
static int    dvnull_iobuf(struct device *d, int wc, w10_t **awp)
			{ return 0; }
static void   dvnull_ioend(struct device *d, int i)
			{ }
static uint32 dvnull_rrg(struct device *d, int r)
			{ return -1; }
static int    dvnull_wrg(struct device *d, int r, dvureg_t v)
			{ return 0; }
static int    dvnull_evreg(struct device *d,
			   void (*rtn)(struct device *, struct dvevent_s *),
			   struct dvevent_s *ev)
			{ return 0; }
static int    dvnull_rim(struct device *d, FILE *f,
			 w10_t w, w10_t *wp, int cnt)
			{ return 0; }
			 

/* Handle non-existent dev-IO instruction dispatch.
** CONO and DATAO do nothing.
**	(DATAO memory read reference already made by dispatch code)
** CONI and DATAI both read 0
**	(Memory write ref will be made by dispatch code)
*/
static insdef_cono( dvnull_cono)  { }
static insdef_datao(dvnull_datao) { }
static insdef_coni( dvnull_coni)  { register w10_t w; op10m_setz(w); return w;}
static insdef_datai(dvnull_datai) { register w10_t w; op10m_setz(w); return w;}

/* Set up null (non-ex) device vector.
**	Until vector stabilizes, easier to init it dynamically than statically.
*/
void
iodv_setnull(register struct device *d)
{
    d->dv_vers   = KN10DEV_VERSION;	/* Device interface version */
    d->dv_dflags = 0;		/* Device flags */
    d->dv_cflags = 0;		/* [C] CPU flags */
    d->dv_name   = " NULDEV ";	/* [C] Device name (unique) */
    d->dv_debug  = 0;		/* [C] Bit flags for debugging options */
    d->dv_dbf    = stderr;	/* [C] Debug output stream */
    d->dv_pireq  = 0;		/* [C/D] NZ = bit for level PI req active on */
    d->dv_pifun = dvnull_attn;	/* [C] PI request function */
    d->dv_evreg = dvnull_evreg;	/* [C] Event callback reg function */
    d->dv_dpp   = NULL;		/* Pointer to DP struct, NULL cuz none */

    d->dv_ctlr = NULL;		/* [C] Back-pointer to parent controller */
    d->dv_num = 0;		/* [C] Unit number on parent controller */
    d->dv_attn = dvnull_attn;	/* [C] Attention request */
    d->dv_drerr = dvnull_v;	/* [C] Drive or xfer error */
    d->dv_iobeg = dvnull_iobeg;	/* [C] Xfer start */
    d->dv_iobuf = dvnull_iobuf;	/* [C] Xfer buffer setup */
    d->dv_ioend = dvnull_ioend;	/* [C] Xfer end */
    d->dv_rdreg = dvnull_rrg;	/* Drive register read */
    d->dv_wrreg = dvnull_wrg;	/* Drive register write */

	/* KA/KI/KL ("dev" IO )*/

    d->dv_pifnwd = dvnull_w;	/* PI: Get function word */
    d->dv_cono   = dvnull_cono;  /* CONO 18-bit conds out */
    d->dv_coni   = dvnull_coni;  /* CONI 36-bit conds in  */
    d->dv_datao  = dvnull_datao; /* DATAO word out */
    d->dv_datai  = dvnull_datai; /* DATAI word in */

	/* KS ("new" IO) */

    d->dv_uba  = NULL;		/* Unibus adapter */
    d->dv_addr  = 0;		/* 1st valid Unibus address */
    d->dv_aend  = 0;		/* Last valid Unibus address */
    d->dv_brlev = 0;		/* BR setting */
    d->dv_brvec = 0;		/* BR interrupt vector setting */
    d->dv_pivec = dvnull_16;	/* PI: Get vector */
    d->dv_read  = dvnull_rd;	/* Read Unibus register */
    d->dv_write = dvnull_wr;	/* Write Unibus register */

    d->dv_bind   = NULL;	/* Bind device to controller if non-NULL */
    d->dv_init   = dvnull_if;	/* Final post-bind device init */
    d->dv_exit   = dvnull_if;	/* Exit, close, terminate */
    d->dv_cmd    = dvnull_ifs;	/* User command to device */
    d->dv_mount  = dvnull_ifss;	/* Mount or unmount media */
    d->dv_help   = dvnull_if;	/* Output help info for user */
    d->dv_status = dvnull_if;	/* Output status info for user */

    d->dv_readin = dvnull_rim;	/* Readin mode */
    d->dv_powon  = dvnull_v;	/* "Power on" */
    d->dv_reset  = dvnull_v;	/* System reset */
    d->dv_powoff = dvnull_v;	/* "Power off" */
}


/* External routine in case want to know what CPU thinks version is
*/
int
iodv_version(void)
{
    return KN10DEV_VERSION;
}

/* IODV_NULLINIT - Initialize as null device, check version #
*/
int
iodv_nullinit(register struct device *d,
	      int version)
{
    if (version != KN10DEV_VERSION)
	return FALSE;
    iodv_setnull(d);		/* Sets dv_vers to KN10DEV_VERSION */
    return TRUE;
}


/* Set max # of registered devices.
**	Table can actually be dynamic, but for now let's keep it simple.
*/
#ifndef KLH10_DEVMAX
# define KLH10_DEVMAX 20
#endif

/* Warning - using arrays for the two tables below is good for
** keyword lookup and general searching.  However, it is bad if
** unloading or undefining is allowed, because the array must then
** be compacted and things like dev_drv adjusted to point into the
** right place.  Ugh.  If dynamic unloading/undefining is desired,
** a linked list is probably best, with a separate keyword table
** for parsing purposes.
**
** Don't free up static entries.
*/


/* Device Driver/Emulator Module - information for both static (link-time)
** and extern (dynamic link, run-time) drivers.
**	drv_path is NULL if driver is static.
**	drv_init is NULL if driver isn't loaded yet (but shouldn't happen)
*/
struct dvdrv_s {
	char *drv_name;		/* Name (used after "static" keyword) */
	struct device *(*drv_init)(FILE *,	/* Addr of dev config rtn */
				   char *);
	char *drv_path;		/* Path, if dynamically loadable */
	char *drv_isym;		/* Symbol, if " ", for device config rtn */
	char *drv_desc;		/* Description for help printout */
	osdll_t drv_dllhdl;	/* Handle for library, if loaded */
};


/* Device Definition - binds 10's idea of devices to a specific driver.
**	There is one such entry for every instance of a device.
*/ 
struct dvdef_s {
    char *dev_name;		/* Defined name of device */
    int dev_flags;		/* Various */
    struct dvdef_s *dev_ctl;	/* Pointer to controller dev, if one */
    int dev_num;		/* Device # or unibus # or drive unit # */
    struct device *dev_dv;	/* Pointer to active device structure */
    struct dvdrv_s *dev_drv;	/* Pointer to selected driver code */
};
int dvdefcnt = 0;
struct dvdef_s dvdeftab[KLH10_DEVMAX+1];

#define DVDF_EXTERN 01		/* Device is dynamically loaded library */
#define DVDF_STATIC 02		/* Device is statically linked module */
#define DVDF_DEVIO  04		/* Device expects <ins> <dev>,E I/O instrs */
#define DVDF_UBIO   010		/* Device expects KS10 Unibus I/O instrs */
#define DVDF_CTLIO  020		/* Device invoked as unit of a controller */

/* Predefined device number mnemonics
**	These can be provided as the <10dev> spec in a device definition.
**	Combine these with opcdvnam[] later.
*/
struct dvnum_s {
	char *dvn_name;
	int dvn_num;
};
static struct dvnum_s dvnumtab[] = {

	{ "apr", IODV_APR<<2 },	/* Processor */
	{ "pi",  IODV_PI <<2 },	/* PI system */
	{ "pag", IODV_PAG<<2 },	/* KI+: Pager */
	{ "cca", IODV_CCA<<2 },	/* KL: Cache */
	{ "tim", IODV_TIM<<2 },	/* KL: Timers, KS: Read various */
	{ "mtr", IODV_MTR<<2 },	/* KL: Meters, KS: Write ditto */

		/* For fun */
	{ "clk", IODV_CLK<<2 },	/* DKN10 clock (KA/KI) */
	{ "ptp", IODV_PTP<<2 },	/* Paper-Tape Punch */
	{ "ptr", IODV_PTR<<2 },	/* Paper-Tape Reader */
	{ "tty", IODV_TTY<<2 },	/* Console TTY */
	{ "dis", IODV_DIS<<2 },	/* Ye 340 display */
	{ NULL,    0 }
};


/* Without good macros, C forward declarations suck...
*/
#if KLH10_DEV_DTE
extern struct device *dvdte_create(FILE *f, char *s);
#endif
#if KLH10_DEV_RH20
extern struct device *dvrh20_create(FILE *f, char *s);
#endif
#if KLH10_DEV_RPXX
extern struct device *dvrp_create(FILE *f, char *s);
#endif
#if KLH10_DEV_TM03
extern struct device *dvtm03_create(FILE *f, char *s);
#endif
#if KLH10_DEV_NI20
extern struct device *dvni20_create(FILE *f, char *s);
#endif
#if KLH10_DEV_HOST
extern struct device *dvhost_create(FILE *f, char *s);
#endif
#if KLH10_DEV_RH11
extern struct device *dvrh11_create(FILE *f, char *s);
#endif
#if KLH10_DEV_LHDH
extern struct device *dvlhdh_create(FILE *f, char *s);
#endif
#if KLH10_DEV_DZ11
extern struct device *dvdz11_init(FILE *f, char *s);
#endif
#if KLH10_DEV_CH11
extern struct device *dvch11_init(FILE *f, char *s);
#endif

/* Table binding static device driver modules with their names
*/
static struct dvdrv_s dvdrvtab[KLH10_DEVMAX+1] = {
#if KLH10_DEV_DTE
    { "dte", dvdte_create, NULL, NULL, "DTE 10-11 Interface (dev)" },
#endif
#if KLH10_DEV_RH20
    { "rh20", dvrh20_create, NULL, NULL, "RH20 Massbus Controller (MB dev)" },
#endif
#if KLH10_DEV_RPXX
    { "rp",   dvrp_create,   NULL, NULL, "RP/RM Disk Drive (unit)" },
#endif
#if KLH10_DEV_TM03
    /* Keep "tm02" as a backward-compatible alias for "tm03" */
    { "tm02", dvtm03_create, NULL, NULL, "TM03 Tape Drive (unit)" },
    { "tm03", dvtm03_create, NULL, NULL, "TM03 Tape Drive (unit)" },
#endif
#if KLH10_DEV_NI20
    { "ni20", dvni20_create, NULL, NULL, "NIA20 Ethernet Port (MB dev)" },
#endif
#if KLH10_DEV_HOST
    { "host", dvhost_create, NULL, NULL, "HOST native access (dev or Unibus)" },
#endif
#if KLH10_DEV_RH11
    { "rh11", dvrh11_create, NULL, NULL, "RH11 Controller (Unibus)" },
#endif
#if KLH10_DEV_LHDH
    { "lhdh", dvlhdh_create, NULL, NULL, "LHDH IMP Interface (Unibus)" },
#endif
#if KLH10_DEV_DZ11
    { "dz11", dvdz11_init, NULL, NULL, "DZ11 dummy (Unibus)" },
#endif
#if KLH10_DEV_CH11
    { "ch11", dvch11_init, NULL, NULL, "Chaosnet dummy (Unibus)" },
#endif
    { NULL, NULL, NULL }
};

#if !KLH10_CPU_KS

/* General Dev-IO support - "IOB" for I/O Bus.
**	Eventually move this page elsewhere, either into dviob.c or
**	perhaps inio.c?
*/

struct device dvnull;		/* Null device - default vector */

/* Actual binding of dev-IO devices!
*/
struct device *devtab[128];

/* Sub-binding tables.  On the KL, all devices ultimately belong to one
** of the following three groups: RH20, DTE, or DIA.
*/
#if KLH10_CPU_KL
# define NRH20 8
# define DEVRH20(n) (0540+((n)<<2))	/* Cvt 0-7 into RH20 devs 540-574 */
struct device *devrh20[8+1];	/* Up to 8 RH20 channels, in order of binding
				** (NOT indexed by device number!)
				** Plus one terminating null pointer.
				** Code assumes all entries null initially!
				*/
# define NDTE20 4
# define DEVDTE20(n) (0200+((n)<<2))
struct device *devdte20[4+1];	/* 4 DTE20s, devs 200-214 */
				/* Same format as devrh20 table */

/* Not clear if the DIA20 is a distinct device or merely a transparent
** hookup to a traditional IO bus to support all other random device #s.
*/
struct device *devdia20;	/* One DIA20 for random devs */
#endif /* KL */


static void
dviob_init(void)
{
    iodv_setnull(&dvnull);	/* Initialize null device vector */
    {
	register int i;
	for (i = 0; i < 128; i++)
	    devtab[i] = &dvnull;
    }
}

#if KLH10_CPU_KL

static void
rh20pifun(register struct device *d, int lev)
{
    if (lev) {				/* Requesting PI on level? */
	d->dv_pireq = lev;		/* Force new level for device */
	if (cpu.pi.pilev_rhreq & lev)	/* Already being requested? */
	    return;			/* Yes, don't yell at CPU again */
	cpu.pi.pilev_rhreq |= lev;
    } else {
	if (!(lev = d->dv_pireq))	/* Turning off -- dev already off? */
	    return;			/* Yep, nothing to do */
	d->dv_pireq = 0;		/* Force level off for device */
	if (!(cpu.pi.pilev_rhreq & lev)) /* RHs already turned off? */
	    return;			/* Yes, don't yell at CPU again */

	/* Ugh, generic RH PI indicator is turned on.  Need to re-assess all
	** RHs to see if safe to turn off bit.  Do this by simply summing
	** all RH request bits.
	** In a multiproc environment this scan is a critical section.
	*/
	{
	    register struct device **adv;

	    cpu.pi.pilev_rhreq = 0;
	    for (adv = devrh20; *adv; ++adv)
		cpu.pi.pilev_rhreq |= (*adv)->dv_pireq;
	}
    }
    pi_devupd();			/* Update main PI state */
}

static void
dte20pifun(register struct device *d, int lev)
{
    if (lev) {				/* Requesting PI on level? */
	d->dv_pireq = lev;		/* Force new level for device */
	if (cpu.pi.pilev_dtereq & lev)	/* Already being requested? */
	    return;			/* Yes, don't yell at CPU again */
	cpu.pi.pilev_dtereq |= lev;
    } else {
	if (!(lev = d->dv_pireq))	/* Turning off -- dev already off? */
	    return;			/* Yep, nothing to do */
	d->dv_pireq = 0;		/* Force level off for device */
	if (!(cpu.pi.pilev_dtereq & lev)) /* DTEs already turned off? */
	    return;			/* Yes, don't yell at CPU again */

	/* Ugh, generic DTE PI indicator is turned on.  Need to re-assess all
	** DTEs to see if safe to turn off bit.  Do this by simply summing
	** all DTE request bits.
	** In a multiproc environment this scan is a critical section.
	*/
	{
	    register struct device **adv;

	    cpu.pi.pilev_dtereq = 0;
	    for (adv = devdte20; *adv; ++adv)
		cpu.pi.pilev_dtereq |= (*adv)->dv_pireq;
	}
    }
    pi_devupd();			/* Update main PI state */
}

#endif /* KL */


static int
dviob_add(FILE *of,			/* Error output */
	  register struct device *d,	/* Device (post-define, pre-init) */
	  int num)			/* IO Bus Device # to bind it to */
{
    register int i;

    if (devtab[num>>2] != &dvnull) {
	if (of)
	    fprintf(of, "Cannot bind to IO device %o: already in use\n", num);
	return FALSE;
    }

#if KLH10_CPU_KL
	/* Do special hackery on KL for Massbus and DTE devs */
    if (DEVRH20(0) <= num && num < DEVRH20(8)) {
	for (i = 0; i < NRH20; ++i) {
	    if (!devrh20[i] || (devrh20[i] == d))
		break;			/* Found entry, stop loop */
	}
	if (i >= NRH20) {
	    if (of)
		fprintf(of, "Cannot bind RH20 %o: No slots left?!\n", num);
	    return FALSE;
	}
	d->dv_pifun = rh20pifun;
	devrh20[i] = d;
    } else if (DEVDTE20(0) <= num && num < DEVDTE20(4)) {
	for (i = 0; i < NDTE20; ++i) {
	    if (!devdte20[i] || (devdte20[i] == d))
		break;			/* Found entry, stop loop */
	}
	if (i >= NDTE20) {
	    if (of)
		fprintf(of, "Cannot bind DTE20 %o: No slots left?!\n", num);
	    return FALSE;
	}
	d->dv_pifun = dte20pifun;
	devdte20[i] = d;
    }
#endif /* KL */

    devtab[num>>2] = d;		/* Bind it! */
    d->dv_num = num;		/* Remember device # bound to */

    return TRUE;
}

#endif	/* !KS */

/* DEV_INIT
**	Initializes all device support.  Called from main startup code.
*/
void
dev_init(void)
{
    dvdefcnt = 0;
    memset((char *)dvdeftab, 0, sizeof(dvdeftab));

#if KLH10_EVHS_INT
    dev_evinit();
#endif

#if KLH10_CPU_KS
    dvub_init();	/* New-style Unibus devices */
#else
    dviob_init();	/* Old-style I/O bus devices */
#endif
}

/* DEV_TERM - Terminate device support.
**	Called from main shutdown code.
**
**	Doesn't bother cleaning up data structures; merely powers off all
**	devices in case they're using system resources that need to be
**	reclaimed.
*/
void
dev_term(void)
{
    register struct dvdef_s *df;

    /* Make two passes through the device definition table; once
    ** to turn off all controller subdevices, and then to turn off
    ** all others (controllers and otherwise).
    ** This ensures that the subdevice being powered off can count on
    ** its controller still being present.
    */
    for (df = dvdeftab; df < &dvdeftab[KLH10_DEVMAX]; ++df) {
	if (df->dev_name && (df->dev_flags & DVDF_CTLIO)) {
	    (*df->dev_dv->dv_powoff)(df->dev_dv);	/* Turn it off */
	}
    }

    /* Second pass, turn off everything that's NOT a controller subdev */
    for (df = dvdeftab; df < &dvdeftab[KLH10_DEVMAX]; ++df) {
	if (df->dev_name && !(df->dev_flags & DVDF_CTLIO)) {
	    (*df->dev_dv->dv_powoff)(df->dev_dv);	/* Turn it off */
	}
    }
}

/* DEV_LOOKUP - Look up device definition
*/
static struct dvdef_s *
dev_lookup(char *name)
{
    return (struct dvdef_s *)
		s_fkeylookup(name, (void *)dvdeftab, sizeof(struct dvdef_s));
}

/* DEV_DRVLOOKUP - Look up device driver
*/
static struct dvdrv_s *
dev_drvlookup(char *name)
{
    return (struct dvdrv_s *)
		s_fkeylookup(name, (void *)dvdrvtab, sizeof(struct dvdrv_s));
}

/* DEV_NUMLOOKUP - Look up device number
*/
static int
dev_numlookup(char *name)
{
    struct dvnum_s *dn;

    dn = (struct dvnum_s *)
		s_fkeylookup(name, (void *)dvnumtab, sizeof(*dn));
    return (dn ? dn->dvn_num : -1);
}


/* Auxiliaries for dev_define. */

static struct dvdef_s *
dev_make(void)
{
    register struct dvdef_s *dp;

    if (dvdefcnt >= KLH10_DEVMAX-1) {
	/* Print error msg? */
	return NULL;
    }
    for (dp = dvdeftab; dp < &dvdeftab[KLH10_DEVMAX]; ++dp)
	if (! dp->dev_name) {
	    memset((char *)dp, 0, sizeof(*dp));	/* Ensure entry cleared */
	    ++dvdefcnt;
	    return dp;			/* Won */
	}
    return NULL;
}

static void
dev_unmake(struct dvdef_s *dp)
{
    if (dp->dev_name) {
	free(dp->dev_name);
	dp->dev_name = NULL;
    }
    --dvdefcnt;
}


/* Auxiliaries for dev_drvload. */

static struct dvdrv_s *
dev_drvmake(void)
{
    register struct dvdrv_s *dp;

    /* Scan entire table to find first free entry */
    for (dp = dvdrvtab; dp < &dvdrvtab[KLH10_DEVMAX]; ++dp)
	if (! dp->drv_name) {
	    memset((char *)dp, 0, sizeof(*dp));	/* Ensure entry cleared */
	    return dp;				/* Won */
	}
    return NULL;
}

static void
dev_drvunmake(struct dvdrv_s *dp)
{
    if (dp->drv_path || dp->drv_isym) {
	if (dp->drv_name)
	    free(dp->drv_name);
	if (dp->drv_path)
	    free(dp->drv_path);
	if (dp->drv_isym)
	    free(dp->drv_isym);
	if (dp->drv_desc)
	    free(dp->drv_desc);
	memset((char *)dp, 0, sizeof(*dp));
    }
}

/* DEV_LOAD - Load a dynamic/shared library as a device driver,
**	and defines its name for later use in configuration.
** <name>  - Arbitrary device driver name.
** <path>  - Pathname for library in native OS.
** <isym>  - "Entry point" symbol (init/config routine)
*/

int
dev_drvload(FILE *of, char *name, char *path, char *isym, char *args)
{
    struct dvdrv_s *dp, ddef;

    if (!(dp = dev_drvmake())) {
	if (of)
	    fprintf(of, "Too many drivers, cannot load another\n");
	return FALSE;
    }

    /* Check out 1st arg (driver name) for pre-existence */
    if (dev_drvlookup(name)) {
	if (of)
	    fprintf(of, "Device \"%s\" already exists, cannot reload\n", name);
	dev_drvunmake(dp);
	return FALSE;
    }

    /* Set up rest of entry.  Do this first because that's easier to back
    ** out of, if load fails, than the reverse (i.e. unloading the driver!)
    */
    if ( !(dp->drv_name = s_dup(name))
      || !(dp->drv_path = s_dup(path))
      || !(dp->drv_isym = s_dup(isym))
      || !(dp->drv_desc = s_dup(args))) {	/* Rest of line is comment */
	if (of)
	    fprintf(of, "Cannot malloc space for dvdrv entry\n");
	dev_drvunmake(dp);
	return FALSE;
    }

    /* Attempt to load up library.  This is very OS dependent.
    ** For now, bundle the symbol lookup in there too because OS may not
    ** be able to do it separately.
    */
    if (!os_dlload(of, ddef.drv_path, &(dp->drv_dllhdl),
			ddef.drv_isym, (void **)&(dp->drv_init))) {
	dev_drvunmake(dp);		/* If fail, error already reported */
	return FALSE;
    }

    return TRUE;			/* Yow!  Yow!*/
}


/* DEV_DEFINE - Do initial device configuration
**	Defines the device name and does its initial configuration setup.
**
**	<name> - Name to identify device
**	<sdev> - A PDP-10 device number (dev-IO),
**		or predefined keyword (dev-IO) such as CTY,
**		or UB<n> - A KS10 Unibus Adapter (n = 1 or 3)
**	<sdrv> - Name of a driver module, either builtin or dynamic.
**	<args> - Optional args to dev's definition routine (rest of line)
**
** If the <sdev> and <sdrv> args check out, the following steps are done:
**	(1) The driver module's "define" routine is called to obtain a
**		device struct for it.
**	(2) The device is bound either to a controller or directly to the 10.
**		This is done by invoking the appropriate bind function.
**		For the 10 this depends on the bus involved.
**		For controllers, this is the dv_bind vector.
**	(3) If bind succeeds, the device init is completed by invoking its
**		dv_init vector.
*/
int
dev_define(FILE *of, char *name, char *sdev, char *sdrv, char *args)
{
    char *cp;
    register int dnum, flags = 0;
    long lnum;
    register struct dvdef_s *dp;
    struct dvdrv_s *drv;
    struct dvdef_s *ctldef = NULL;	/* Possible controller */

    /* Perhaps check here to make sure there's room for another device def
    ** before going farther, but no big deal.
    */

    /* Check out device name for pre-existence */
    if (dev_lookup(name)) {
	if (of)
	    fprintf(of, "Device \"%s\" already exists, cannot redefine\n",
			name);
	return FALSE;
    }

    /* Parse 10-device binding specification.
    ** Syntax is a bit peculiar, depends on # of components.
    ** If 2, 1st must be an existing device name and the 2nd a plain number.
    */
    if (cp = strchr(sdev, '.')) {
	char dnbuf[100];
	int i = cp - sdev;

	if (i >= (sizeof(dnbuf)-1)) {
	    if (of)
		fprintf(of, "Device binding \"%s\" too long.\n", sdev);
	    return FALSE;
	}
	memcpy(dnbuf, sdev, i);
	dnbuf[i] = '\0';
	if (!(ctldef = dev_lookup(dnbuf))) {
	    if (of)
		fprintf(of, "Unknown device \"%s\" in binding \"%s\".\n",
				dnbuf, sdev);
	    return FALSE;
	}
	if (!ctldef->dev_dv->dv_bind) {
	    if (of)
		fprintf(of, "Device \"%s\" cannot accept bindings.\n", dnbuf);
	    return FALSE;
	}
	/* Have valid controller spec!  Now get unit # for it... */
	if (!s_tonum(++cp, &lnum)) {	/* Parse rest as unit number */
	    if (of)
		fprintf(of, "Unit binding must be a number (\"%s\" invalid).",
			cp);
	    return FALSE;
	}
	dnum = (int)lnum;
	flags |= DVDF_CTLIO;

    } else {
	cp = sdev;
	if (isdigit(*cp)) {
	    if (s_tonum(cp, &lnum))	/* Parse as device number */
		flags |= DVDF_DEVIO;
	    dnum = (int)lnum;
	} else if (*cp == 'u' && cp[1] == 'b') {
	    if (s_tonum(cp+2, &lnum))	/* Parse as unibus controller # */
		flags |= DVDF_UBIO;
	    dnum = (int)lnum;
	} else {			/* Parse as known-10-device name */
	    if (-1 != (dnum = dev_numlookup(cp)))	/* -1 is error */
		flags |= DVDF_DEVIO;
	}
	if (!(flags & (DVDF_DEVIO|DVDF_UBIO))) {
	    if (of)
		fprintf(of, "Device num spec \"%s\" not recognized.\n", cp);
	    return FALSE;
	}
    }

    /* Do a few sanity checks on the resulting number */
    if (flags & DVDF_UBIO) {
	/* Check out for new-style KS10 unibus IO */
#if KLH10_CPU_KS
	if (dnum != 1 && dnum != 3) {
	    if (of)
		fprintf(of, "Bad unibus #: %#.0o - only 1 or 3 supported\n",
			dnum);
	    return FALSE;
	}
#else
	if (of)
	    fprintf(of, "Unibus devices not supported on non-KS10\n");
	return FALSE;
#endif
    } else if (flags & DVDF_DEVIO) {
	/* Check out for old-style device IO */
#if !KLH10_CPU_KS
	if ((dnum & 03) || (dnum > 0774)) {
	    if (of)
		fprintf(of, "Bad dev number %#.0o, must be in {0,4,10,...,774}\n",
			dnum);
	    return FALSE;
	}
	if (dnum <= (IODV_MTR<<2)) {
	    if (of)
		fprintf(of, "Device %#.0o is internal, cannot be redefined\n",
			dnum);
	    return FALSE;
	}
#else
	if (of)
	    fprintf(of, "Old-style IO devices not supported on KS10\n");
	return FALSE;
#endif /* KS */
    }

    /* Parse driver keyword */
    if (!(drv = dev_drvlookup(sdrv))) {
	if (of)
	    fprintf(of, "Unknown device driver \"%s\"\n", sdrv);
	return FALSE;
    }

    /* Have everything set up!  Now put together real device definition. */
    if (!(dp = dev_make())) {
	if (of)
	    fprintf(of, "Out of room for more device defs\n");
	return FALSE;
    }
    if (!(dp->dev_name = s_dup(name))) {	/* Finally make it valid */
	if (of)
	    fprintf(of, "Cannot malloc copy of device name\n");
	dev_unmake(dp);
	return FALSE;
    }
    dp->dev_ctl = ctldef;
    dp->dev_num = dnum;
    dp->dev_drv = drv;
    dp->dev_flags = flags;

    /* Invoke the driver's init-config routine on the rest of the arg line! */
    if ((dp->dev_dv = (*(drv->drv_init))(of, args)) == NULL) {
	if (of)
	    fprintf(of, "Device init failed\n");
	/* Maybe need to flush device? */
	dev_unmake(dp);		/* Flush entry, free stg */
	return FALSE;
    }
    dp->dev_dv->dv_name = dp->dev_name;	/* Won, set name prior to bind */
#if KLH10_EVHS_INT
    dp->dev_dv->dv_evreg = dev_evreg;	/* Allow dev to register for events */
#endif

    /* Now bind device to 10-side */
#if KLH10_CPU_KS
    if (dp->dev_flags & DVDF_UBIO) {
	/* Bind to specified unibus.  This call reports its own errors.
	*/
	dp->dev_dv->dv_dflags |= DVFL_UBIO;
	if (!dvub_add(of, dp->dev_dv, dp->dev_num)) {
	    dev_unmake(dp);
	    return FALSE;
	}
    }
#endif
#if !KLH10_CPU_KS
    if (dp->dev_flags & DVDF_DEVIO) {
	/* Bind to specified PDP-10 device #.  Reports own errors.
	*/
	dp->dev_dv->dv_dflags |= DVFL_DEVIO;
	if (!dviob_add(of, dp->dev_dv, dp->dev_num)) {
	    dev_unmake(dp);
	    return FALSE;
	}
    }
#endif /* !KS */

    /* Ensure device agrees as to whether it's a controlled unit */
    if (((dp->dev_flags & DVDF_CTLIO)==0)
	!= ((dp->dev_dv->dv_dflags & DVFL_CTLIO)==0)) {
	if (of)
	    fprintf(of,
		    "Ctlr/unit mismatch?  Device %s %s to be controlled.\n",
		    dp->dev_name,
		    (dp->dev_dv->dv_dflags & DVFL_CTLIO)
		    ? "wants" : "refuses");
	dev_unmake(dp);
	return FALSE;
    }
    if (dp->dev_flags & DVDF_CTLIO) {
	/* Bind to specified controller.  Have already checked for
	** existence of dv_bind vector.
	*/
	dp->dev_dv->dv_ctlr = ctldef->dev_dv;	/* Set up backpointer */
	dp->dev_dv->dv_num = dp->dev_num;	/* Set up dev unit # */
	if (0 == (*ctldef->dev_dv->dv_bind)(ctldef->dev_dv, of,
					dp->dev_dv, dp->dev_num)) {
	    if (of)
		fprintf(of, "Cannot bind to %s unit %o: already in use?\n",
			ctldef->dev_name, dp->dev_num);
	    dev_unmake(dp);
	    return FALSE;
	}
    }


    /* Binding succeeded, now do final init for device */
    if (0 == (*dp->dev_dv->dv_init)(dp->dev_dv, of)) {
	if (of)
	    fprintf(of, "Final init of device \"%s\" failed!\n", dp->dev_name);
	dev_unmake(dp);
	return FALSE;
    }

    return TRUE;
}

/* Various device command functions
*/

/* Handy auxiliary for most of the commands
*/
static struct device *
devverify(register FILE *of,
	  register char *dstr)
{
    struct dvdef_s *def;

    if (!dstr) {
	if (of)
	    fprintf(of, "No device specified\n");
	return NULL;
    }
    if (!(def = dev_lookup(dstr))) {
	if (of)
	    fprintf(of, "Unknown device \"%s\"\n", dstr);
	return NULL;
    }

    /* Exercise a little paranoia for now */
    if (!def->dev_dv) {
	if (of)
	    fprintf(of, "Internal error!  Null vector for dev_dv !!\n");
	return NULL;
    }
    return def->dev_dv;
}

/* Similar auxiliary for anything that wants to hack DPs
**	Parse "<dev>" - just like devverify, existence of DP is irrelevant.
**			*adp is set to NULL.
** also parse "<dev>.dp" - Sets *adp to DP pointer; returns NULL if *adp is
**			NULL (meaning no DP exists, altho user is trying to
**			refer to it).
*/
static struct device *
devdpverify(register FILE *of,
	    register char *dstr,
	    struct dp_s **adp)
{
    register int i;
    register char *cp;
    char tmpstr[100];
    register struct device *d;

    *adp = NULL;

    /* See if attempting reference to DP of a device */
    if (dstr && (cp = strrchr(dstr, '.'))
      && (s_match(cp, ".dp") == 2)
      && ((i = cp-dstr) < sizeof(tmpstr))) {

	/* Yes, identify device string.  Copy so needn't mung arg */
	memcpy(tmpstr, dstr, i);
	tmpstr[i] = '\0';
	if (!(d = devverify(of, tmpstr)))
	    return NULL;
	if (!(*adp = d->dv_dpp)) {
	    if (of)
		fprintf(of, "Device \"%s\" has no DP\n", d->dv_name);
	    return NULL;
	}
	return d;			/* DP won! */
    } else
	return devverify(of, dstr);	/* No ".dp" spec, do normal stuff */
}


/* DEV_COMMAND - Generic device command
**    Syntax:	dev <name> <commandline>
**
** Invokes device's command parsing and execution, which can be anything
** the device wants to do with the line.  Most should follow certain
** conventions, however:
**	dev <name> help		- Show what this device understands
**	dev <name> status	- Show status in appropriate form
**	dev <name> show [param] [...]		- Show specific device vars
**	dev <name> set  [param=val] [...]	- Set them
*/
int
dev_command(FILE *of,
	    char *dstr,
	    char *args)
{
    register struct device *d;

    if (!(d = devverify(of, dstr)))
	return FALSE;
    if (!d->dv_cmd) {			/* Exercise more paranoia */
	if (of)
	    fprintf(of, "Internal error!  Null vector for dv_cmd\n");
	return FALSE;
    }
    return (*(d->dv_cmd))(d, of, args);		/* Invoke it! */
}


int
dev_help(FILE *of,
	    char *dstr,
	    char *args)
{
    register struct device *d;

    if (!dstr || strcmp(dstr, "?")==0) {
	/* Show general help */

	return TRUE;
    }
    if (!(d = devverify(of, dstr)))
	return FALSE;
    if (!d->dv_help) {			/* Exercise more paranoia */
	if (of)
	    fprintf(of, "Internal error!  Null vector for dv_help !!\n");
	return FALSE;
    }

    return (*(d->dv_help))(d, of);		/* Invoke it! */
}

int
dev_status(FILE *of,
	    char *dstr,
	    char *args)
{
    register struct device *d;

    if (!dstr || strcmp(dstr, "?")==0) {
	/* Show general status */
	return TRUE;
    }
    if (!(d = devverify(of, dstr)))
	return FALSE;
    if (!d->dv_status) {		/* Exercise more paranoia */
	if (of)
	    fprintf(of, "Internal error!  Null vector for dv_status !!\n");
	return FALSE;
    }

    return (*(d->dv_status))(d, of);		/* Invoke it! */
}

int
dev_mount(FILE *of,
	  char *dstr,
	  char *path,
	  char *args)
{
    register struct device *d;

    if (!(d = devverify(of, dstr)))
	return FALSE;
    if (!d->dv_mount) {			/* Exercise more paranoia */
	if (of)
	    fprintf(of, "Internal error!  Null vector for dv_mount !!\n");
	return FALSE;
    }

    return (*(d->dv_mount))(d, of, path, args);		/* Invoke it! */
}


int
dev_debug(FILE *of,
	  char *dstr,
	  char *sval,
	  char *args)
{
    long dbgval;
    register struct device *d;
    struct dp_s *dp;

    if (!dstr || !*dstr || (*dstr == '?')) {
	/* Show debug values for all known devices */
	struct dvdef_s *def;

	fprintf(of, "  Device debug values:\n");
	for (def = dvdeftab; def->dev_name; ++def) {
#if KLH10_DEV_DP
	    fprintf(of, "  %6s    = %d\n", def->dev_name,
						def->dev_dv->dv_debug);
	    if (dp = def->dev_dv->dv_dpp) {
		fprintf(of, "  %6s.dp = ", def->dev_name);
	        if (dp->dp_adr)
		    fprintf(of, "%d\n", dp->dp_adr->dpc_debug);
		else
		    fprintf(of, "??\n");
	    }
#else
	    fprintf(of, "  %6s = %d\n", def->dev_name, def->dev_dv->dv_debug);
#endif
	}
	return TRUE;
    }

    if (!(d = devdpverify(of, dstr, &dp)))
	return FALSE;

    /* Parse debug value */
    if (!sval || !*sval) {
	if (of) {
	    fprintf(of, "  Debug value for %s = ", dstr);
#if KLH10_DEV_DP
	    if (dp) {
		if (dp->dp_adr)
		    fprintf(of, "%d\n", dp->dp_adr->dpc_debug);
		else
		    fprintf(of, "??\n");
	    } else
#endif
		fprintf(of, "%d\n", d->dv_debug);

	}
	return FALSE;
    }

    if (!s_tonum(sval, &dbgval)) {
	if (of)
	    fprintf(of, "Bad syntax - \"%s\" not a number\n", sval);
	return FALSE;
    }

    /* Won, set it!! */
#if KLH10_DEV_DP
    if (dp) {
	if (dp->dp_adr) {
	    dp->dp_adr->dpc_debug = dbgval;
	    return TRUE;
	}
	if (of)
	    fprintf(of, "DP not active, cannot set debug value\n");
	return FALSE;
    }
#endif
    d->dv_debug = dbgval;
    return TRUE;
}



int
dev_show(FILE *of,
	 char *dstr,
	 char *args)
{
    struct dvdef_s *def;
    struct dvdrv_s *drv;

    if (!dstr || strcmp(dstr, "?")==0) {
	/* Show general config status, etc */
	if (!of) return TRUE;

	/* Show all defined drivers */
	fprintf(of, "Known device drivers:\n");
	fprintf(of, "    Name    Linkage     EntryAddr     \"Description\"\n");
	for (drv = dvdrvtab; drv->drv_name; ++drv) {
	    fprintf(of, "  %6s (%s) 0x%lX ", drv->drv_name,
		(drv->drv_path ? "extern-dll" : "static-lib"),
		(long)(drv->drv_init));
	    if (drv->drv_path)
		fprintf(of, "{path=%s isym=%s}", drv->drv_path, drv->drv_isym);
	    fprintf(of, "  \"%s\"\n", drv->drv_desc);
	}

	/* Show all bound devices */
	fprintf(of, "\nDefined devices:\n");
	fprintf(of, "   DevID   Dev#   Driver  StructAddr\n");
	for (def = dvdeftab; def->dev_name; ++def) {
	    fprintf(of, "  %6s ", def->dev_name);
	    if (def->dev_flags & DVDF_UBIO) {
		struct device *dv = def->dev_dv;
		fprintf(of,
		    "   UB%d   %-6s 0x%lX addr=%#.0o:%#.0o br=%d vec=%#.0o\n",
			def->dev_num, def->dev_drv->drv_name,
			(unsigned long)dv,
			dv->dv_addr, dv->dv_aend, dv->dv_brlev,
			dv->dv_brvec);
	    } else if (def->dev_flags & DVDF_DEVIO) {
		fprintf(of,
		    "%6o   %-6s 0x%lX\n",
			def->dev_num, def->dev_drv->drv_name,
			(unsigned long)def->dev_dv);
	    } else if (def->dev_flags & DVDF_CTLIO) {
		fprintf(of,
		    "%6s.%o %-6s 0x%lX\n",
			def->dev_ctl->dev_name,
			def->dev_num, def->dev_drv->drv_name,
			(unsigned long)def->dev_dv);
	    } else
		fprintf(of, "\?\?\?\n");
	}

	/* List known device number mnemonics? */

	return TRUE;
    }

    return FALSE;
#if 0
    if (!(def = dev_lookup(dstr))) {
	if (of)
	    fprintf(of, "Unknown device driver \"%s\"\n", dstr);
	return FALSE;
    }

    /* Exercise a little paranoia for now */
    if (!def->dev_dv || !def->dev_dv->dv_help) {
	if (of)
	    fprintf(of, "Internal error!  Null vector for %s!!\n",
			def->dev_dv ? "dev_dv" : "dev_dv->dv_help");
	return FALSE;
    }

    /* Found it, now invoke it */
    return (*(def->dev_dv->dv_help))(def->dev_dv, of);
#endif
}

/* Device boot code */

/*

The device boot code doesn't go in any really obvious place as it can
combine CPU instructions, device invocations, and FE knowledge of
where bootstraps are located, the latter being the most devious.

For now, put it here.

*/

#define FE_BOOTLOC 01000
#define FE_BOOTLEN 512

/* DEV_BOOT - Read bootstrap from device using "read-in mode"
**
*/
int
dev_boot(FILE *of,
	 char *dstr,
	 vaddr_t *bootsa)
{
    register struct device *d, *ctlr;
    register w10_t w;
    register vmptr_t vp;
    int res;

    /* XXX: Later use first controller if no dev specified? */

    if (!(d = devverify(of, dstr)))
	return FALSE;

    /* Determine device type.
       XXX: If it's a controller, use first live device on it.
    */
    if (d->dv_dflags & DVFL_CTLR) {
	if (of) fprintf(of, "Cannot boot from a controller\n");
	return FALSE;
    } else
	ctlr = d->dv_ctlr;

    /* Have device, now determine whether dealing with random-access
       block mode device, or tape device.
    */
#if KLH10_CPU_KS
    /* Must set up device variables in FECOM area so boot code knows
       where it came from.
    */
    if (ctlr) {
	W10_XSET(w, ctlr->dv_uba->ubn, ctlr->dv_addr);
	vm_pset(vm_physmap(FECOM_RHBAS), w);	/* RH11 base address */
	W10_U32SET(w, d->dv_num);
	vm_pset(vm_physmap(FECOM_QNUM), w);	/* RH11 unit number */
	W10_U32SET(w, (TM_D16<<8) | (TM_FCD<<4) | 0);
	vm_pset(vm_physmap(FECOM_BOOTP), w);	/* Tape OFTC (dens & slave) */
    }

    W10_U32SET(w, 1);		/* Address: one file or one sector in */
    vp = vm_physmap(FE_BOOTLOC);	/* Read into here */
    if (d->dv_dflags & DVFL_TAPE) {
	/* A boot tape is set up so the first file is microcode, and
	   the second is a PDP-10 boot image.  The first record of that
	   boot (normally 512 wds) is read into loc 1000 and the 10
	   started there.
	*/
	if (of) fprintf(of, "Reading 1st record of 2nd file ... ");
	res = (*d->dv_readin)(d, of, w, (w10_t *)vp, FE_BOOTLEN);
	if (!res)
	    return FALSE;
	if (of) fprintf(of, "OK\nRead in %ld words at %lo\n",
			(long)res, (long)FE_BOOTLOC);
	*bootsa = FE_BOOTLOC;

    } else if (d->dv_dflags & DVFL_DISK) {
	/* KS disk boot is messy.
	   First reads sector #1 (each sector 128 words) 
	   and verifies first word is 'HOM   '.
	   Then reads sector #010 and does same verification.
	   If OK, takes disk address from loc +103 in that block,
	   reads it, gets another disk address from loc +4,
	   and finally uses that to read the first 512 words (4
	   sectors) of the "real" 10 boot.  Jumps to 1000 to start.
	 */
	w10_t w6hom = W10_XINIT(0505755,0);	/* sixbit /HOM   / */
	
	if (of) fprintf(of, "Reading HOM sector 1 ... ");
	if (!(res = (*d->dv_readin)(d, of, w, (w10_t *)vp, 128)))
	    return FALSE;
	if (op10m_camn(vm_pget(vp), w6hom)) {
	    if (of) fprintf(of, "Bad HOM check, val = %lo,,%lo\n",
			    1, (long)LHGET(w), (long)RHGET(w));
	    return FALSE;
	}
	if (of) fprintf(of, "OK\nReading HOM sector 010 ...");
	W10_U32SET(w, 010);
	if (!(res = (*d->dv_readin)(d, of, w, (w10_t *)vp, 128)))
	    return FALSE;
	if (op10m_camn(vm_pget(vp), w6hom)) {
	    if (of) fprintf(of, "Bad HOM check, val = %lo,,%lo\n",
			    010, (long)LHGET(w), (long)RHGET(w));
	    return FALSE;
	}
	w = vm_pget(vm_physmap(FE_BOOTLOC+0103));	/* Get ptrs pag addr */
	if (op10m_skipn(w)) {
	    if (of) fprintf(of, "Bad ptr pag addr from HOM blk = %lo,,%lo\n",
			    (long)LHGET(w), (long)RHGET(w));
	    return FALSE;
	}
	if (of) fprintf(of, "OK\nReading pointers sector at %lo,,%lo ...",
	       (long)LHGET(w), (long)RHGET(w));
	if (!(res = (*d->dv_readin)(d, of, w, (w10_t *)vp, 128)))
	    return FALSE;
	w = vm_pget(vm_physmap(FE_BOOTLOC+04));	/* Get boot code addr */
	if (op10m_skipn(w)) {
	    if (of) fprintf(of, "Bad bootcode addr from ptr blk = %lo,,%lo\n",
			    (long)LHGET(w), (long)RHGET(w));
	    return FALSE;
	}

	/* Finally!  Note we now read in a full page (4 sectors) */
	if (of) fprintf(of, "OK\nReading 4 boot sectors at %lo,,%lo ...",
			(long)LHGET(w), (long)RHGET(w));
	if (!(res = (*d->dv_readin)(d, of, w, (w10_t *)vp, 512)))
	    return FALSE;

	if (of) fprintf(of, "OK\nRead in %ld words at %lo\n",
			(long)res, (long)FE_BOOTLOC);
	*bootsa = FE_BOOTLOC;

    }

#elif KLH10_CPU_KA || KLH10_CPU_KI
    /* Do one DATAI into loc 0, then treat it as
       a BLKI pointer.  When done, KI jumps to last word,
       KA executes it.
    */
    {
	int32 cnt;
#if KLH10_CPU_KA
	w10_t tmp = W10XWD(1,1);	/* KA adds 1,,1 */
#endif
	w = (*d->dv_datai)(d);
	
	if ((cnt = LHGET(w)) & H10SIGN)
	    cnt = -(cnt | ~((int32)MASK18));
	vp = vm_xrwmap(0);		/* Will be in AC so map it */

	if (of) fprintf(of, "Read BLKI=%lo,,%lo (%ld words)\n"
			LHGET(w), RHGET(w), (long)cnt);
	vm_pset(vp, w);

	/* Now do BLKI loop.  Always inputs at least one word */
	for (cnt = 1; ; ++cnt) {
	    /* Get word and store it.
	       Re-map each time since may cross AC or address bounds
	    */
	    vm_pset(vm_xrwmap(RHGET(w)), (*d->dv_datai)(d));
#if KLH10_CPU_KA
	    op10m_add(w, tmp);		/* KA adds 1,,1 */
#else					/* KI increments independently */
	    LRHSET(w, (LHGET(w)+1)&H10MASK, (RHGET(w)+1)&H10MASK);
#endif
	    vm_pset(vp, w);		/* Store back inc'd pointer */
	    if (LHGET(w) == 0)
		break;
	}
	if (of) fprintf(of, "Readin complete, %ld words\n", (long)cnt);
	*bootsa = (vaddr_t)RHGET(w);
    }

#else
    if (0) ;
#endif
    else {
	if (of) fprintf(of, "%s not supported for boot\n", d->dv_name);
	return FALSE;
    }
    return TRUE;
}


/* First stab at support for device subprocs -- cooperates with DPSUP code */

#if KLH10_EVHS_INT

/* Event callback registration:

	The mechanism for organizing asynchronous events will rely on
having handlers register themselves with DEV_EVREG.  The various types
of events understood are defined in kn10dev.h as DVEV_xxx values.
	For now, event handlers are *only* invoked atomically between
instructions, as part of INSBRK processing.  They are never invoked
at "OS interrupt level", i.e. as part of an OS signal handler.  Such
signal handlers merely set the appropriate flags so that INSBRK will
know that the respective event happened.
	If handlers generate any PI interrupts, those will take effect
before any instruction is executed.

Some notes on specific events:

    DVEV_1SIG - Single signal specified by eva_int.
	Event handler wants to monopolize this signal -- registration fails
	if anyone else tries to register for it.
	When signal happens, its handler is always invoked.

    DVEV_NSIG - Multiplexed signal specified by eva_int.
	Event handler is willing to share this signal.  
	When signal happens, *all* registered handlers are invoked, since
	without additional hackery there's no way to know which one was
	intended to receive the signal.
	This is rather inefficient and at the top of the list.

    DVEV_ASIG - Allocated signal.  No arg; signal value is returned.
	This type asks the registrar to find an unused single signal
	if possible, or a multiplexed one if not.

    DVEV_DPSIG - Device subProcess signal specified by eva_int.
	Pointer to flag check word specified by 2nd arg's eva_ip.
	Like DVEV_NSIG but handler only invoked if flag is non-zero
	(flag is cleared just prior to invoking handler).

    DVEV_CLOCK - Periodic clock callout.  Time in eva_int is # msec.
	The time will be converted to some # of internal clock ticks
	by rounding to the nearest tick (but no less than 1).

    DVEV_TIMOUT - Clock timeout.  Time in eva_int is # msec.
	Same as DVEV_CLOCK except routine is de-registered just before
	the callback is made.

*/


#ifndef SIGMAX
# ifndef NSIG
#  define SIGMAX 32	/* Probable guess */
# else
#  define SIGMAX NSIG
# endif
#endif

#ifndef KLH10_EVHS_MAX
# define KLH10_EVHS_MAX (KLH10_DEVMAX+10)
#endif

struct dvevsig_s *evsiglist;
struct dvevsig_s evsigtab[SIGMAX];		/* Registered signals */

struct dvevreg_s *evregfree;
struct dvevreg_s evregtab[KLH10_EVHS_MAX];	/* Registered handlers */

static void dev_evunreg(struct device *);

/* DEV_EVINIT - Initialize event stuff
*/
void
dev_evinit(void)
{
    register struct dvevreg_s *evr;

    INTF_INIT(cpu.intf_evsig);
    evsiglist = NULL;
    memset((char *)evsigtab, 0, sizeof(evsigtab));
    memset((char *)evregtab, 0, sizeof(evregtab));

    /* Set up handler entry freelist */
    evregfree = evregtab;
    for (evr = evregfree; evr < &evregtab[KLH10_EVHS_MAX-1]; ++evr)
	evr->dver_next = evr+1;
    evr->dver_next = NULL;
}

static void
dev_sighan(int sig)
{
    if (0 < sig && sig < SIGMAX) {		/* Paranoia */
	INTF_SET(evsigtab[sig].dves_intf);	/* Say this signal seen */
	INTF_SET(cpu.intf_evsig);		/* Say some signal seen */
	INSBRKSET();				/* Interrupt instr loop */
    }
}

/* DEV_EVCHECK - Called at INSBRK to process any valid events
*/

void
dev_evcheck(void)
{
    register struct dvevsig_s *evs;
    register struct dvevreg_s *evr;

    /* Check all signals to see which ones went off */
    for (evs = evsiglist; evs; evs = evs->dves_next) {
	if (INTF_TEST(evs->dves_intf)) {
	    INTF_ACTBEG(evs->dves_intf);

	    /* Process all handlers registered for this signal */
	    for (evr = evs->dves_reglist; evr; evr = evr->dver_next) {
		if (*(evr->dver_ev.dvev_arg2.eva_ip)) {
		    /* If this handler's flag shows it really does want
		    ** the call, invoke handler after clearing flag!
		    */
		    *(evr->dver_ev.dvev_arg2.eva_ip) = 0;
		    (*(evr->dver_hdlr))(evr->dver_d, &(evr->dver_ev));
		}
	    }

	    INTF_ACTEND(evs->dves_intf);
	}
    }
}

/* DEV_EVREG - Called by devices through device vector.
**	Registers a callback routine for handling a specific event.
**	If any suitable event happens, routine will be invoked atomically
**	between instructions (as part of INSBRK processing).  It should
**	be defined as:
**		callback_rtn(struct device *d, int evtyp, int evarg)
**
*/
int
dev_evreg(register struct device *d,
	  void (*rtn)(struct device *, struct dvevent_s *),
	  register struct dvevent_s *evp)
{
    register struct dvevreg_s *evr;
    register struct dvevsig_s *evs;
    int sig;
    ossigset_t oldmask, allmask;

    /* First ensure we aren't interrupted while messing with table */
    os_sigfillset(&allmask);
    (void) os_sigsetmask(&allmask, &oldmask);

    /* Check if unregistering device and handle if so */
    if (rtn == NULL) {
	dev_evunreg(d);		/* Flush it */
	(void) os_sigsetmask(&oldmask, (ossigset_t *)NULL);
	return TRUE;
    }

    /* Find new entry in evreg table */
    if (evr = evregfree) {		/* Pluck from freelist */
	evregfree = evr->dver_next;	/* Freelist is one-way, no prev */
    } else {
	fprintf(stderr, "[dev_evreg: out of table entries!]\r\n");
	(void) os_sigsetmask(&oldmask, (ossigset_t *)NULL);
	return 0;
    }

    evr->dver_hdlr = rtn;		/* Remember callback rtn */
    evr->dver_d = d;			/* Remember device */
    evr->dver_ev = *evp;		/* Remember event & args */

    switch (evp->dvev_type) {

    case DVEV_DPSIG:
	/* Ensure 2 args look sensible */
	sig = evp->dvev_arg.eva_int;
	if (sig <= 0 || SIGMAX <= sig) {
	    fprintf(stderr, "[dev_evreg: Bad signal: %d]\r\n", sig);
	    break;		/* Fail... */
	}
	if (! evp->dvev_arg2.eva_ip) {
	    fprintf(stderr, "[dev_evreg: Bad DPSIG pointer]\r\n");
	    break;		/* Fail... */
	}
	*(evp->dvev_arg2.eva_ip) = 0;	/* Clear request flag */

	/* If signal not already registered, add it */
	evs = &evsigtab[sig];
	if (evs->dves_sig == 0) {

	    /* New, see if can install OS signal handler */
	    if (osux_signal(sig, dev_sighan) == -1) {
		fprintf(stderr, "[dev_evreg: Can't handle sig %d - %s]\r\n",
				 sig, os_strerror(-1));
		break;			/* Fail... */
	    }

	    /* Yup, proceed with registration! */
	    evs->dves_sig = sig;
	    INTF_INIT(evs->dves_intf);
	    evs->dves_reglist = NULL;
	    evs->dves_prev = NULL;
	    if (evs->dves_next = evsiglist)	/* Add to head of list */
	        evsiglist->dves_prev = evs;
	    evsiglist = evs;
	}

	/* Now add handler to list for this signal */
	evr->dver_prev = NULL;
	if (evr->dver_next = evs->dves_reglist)	/* Add to head of list */
	    evr->dver_next->dver_prev = evr;
	evs->dves_reglist = evr;

	(void) os_sigsetmask(&oldmask, (ossigset_t *)NULL);
	return TRUE;

    default:
	/* Can't handle any other event type, break out to fail */
	break;
    }

    /* Failure if get here */

    /* Put event entry back on freelist */
    evr->dver_next = evregfree;		/* Freelist is one-way, no prev */
    evregfree = evr;
    (void) os_sigsetmask(&oldmask, (ossigset_t *)NULL);
    return 0;
}

/* DEV_EVUNREG - De-registers specified device completely.
**	Currently only used when device is powering off, so no need
**	to distinguish between events/handlers for a device.
** Note that interrupts are suspended by caller (dev_evreg).
*/
static void
dev_evunreg(register struct device *d)
{
    register struct dvevreg_s *evr, *nextevr;
    register struct dvevsig_s *evs, *nextevs;

    /* Scan all signals handled */
    for (evs = evsiglist; evs; evs = nextevs) {
	nextevs = evs->dves_next;

	/* Scan all handlers registered for this signal */
	for (evr = evs->dves_reglist; evr; evr = nextevr) {
	    nextevr = evr->dver_next;
	    if (evr->dver_d == d) {	/* Matching device? */
		/* Take it off list */
		if (evr->dver_prev)
		    evr->dver_prev->dver_next = nextevr;
		else
		    evs->dves_reglist = evr->dver_next;	/* Fix up head */
		if (nextevr)
		    nextevr->dver_prev = evr->dver_prev;

		/* Put on freelist */
		evr->dver_next = evregfree;	/* Flist is one-way, no prev */
		evregfree = evr;
	    }
	}

	/* Handler scan done, now see if anything left for signal to do */
	if (evs->dves_reglist == NULL) {
	    /* All gone, so turn off handling of signal.
	    ** This is probably superfluous, but tidy is as tidy do.
	    ** Ignore errors since some signals may be un-ignorable.
	    */
	    osux_signal(evs->dves_sig, SIG_IGN);
	    
	    /* Take it off list */
	    if (evs->dves_prev)
		evs->dves_prev->dves_next = nextevs;
	    else
		evsiglist = evs->dves_next;	/* Fix up head */
	    if (nextevs)
		nextevs->dves_prev = evs->dves_prev;

	    /* Now say it's free (uses indexed table, no freelist) */
	    evs->dves_sig = 0;		/* No signal in effect */
	}
    }
}



/* DEV_EVSHOW - Show status of event registration data
**	Maybe should lock out interrupts while doing this...
*/
int
dev_evshow(FILE *of,
	   char *dstr,
	   char *args)
{
    register struct dvevreg_s *evr;
    register struct dvevsig_s *evs;
    register int i;
    struct dvdef_s *def;

#if 0	/* For now, ignore any args */
    if (!dstr || strcmp(dstr, "?")==0)
	/* Show general config status, etc */

#endif
    if (of) {
	fprintf(of, "Outstanding event flag: %lo\n", (long)cpu.intf_evsig);

	/* Show all registered event handlers */
	fprintf(of, "Registered event handlers:\n");

	for (evs = evsiglist; evs; evs = evs->dves_next) {

	    /* Show signal event and stuff registered for it */
	    fprintf(of, "  Signal: Sig %d, intf=%ld\n",
			evs->dves_sig, (long)evs->dves_intf);

	    /* Show all handlers registered for this signal */
	    for (evr = evs->dves_reglist; evr; evr = evr->dver_next) {
		fprintf(of, "    Hdlr: 0x%lx (", (long)(evr->dver_hdlr));

		/* Try to identify device */
		for (def = dvdeftab; def->dev_name; ++def) {
		    if (def->dev_dv == evr->dver_d)
			break;
		}
		if (def->dev_name)
		    fprintf(of, "dev \"%s\"", def->dev_name);
		else
		    fprintf(of, "unknowndev 0x%lx",
						(unsigned long)(evr->dver_d));

		/* Now show its event args in general */
		fprintf(of, ", ev{%o, 0x%lx, 0x%lx})",
			i = evr->dver_ev.dvev_type,
			evr->dver_ev.dvev_arg.eva_long,
			evr->dver_ev.dvev_arg2.eva_long);

		/* Try to show event-specific stuff */
		if (i == DVEV_DPSIG) {
		    fprintf(of, " DPSIG %d, Flag=%o",
			evr->dver_ev.dvev_arg.eva_int,
			*(evr->dver_ev.dvev_arg2.eva_ip));
		}
		fputc('\n', of);
	    }
	}
    }
    return TRUE;
}

#endif /* KLH10_EVHS_INT */

/* Support for asynch device attention during FE command loop

   The problem arises when using DPs for devices with mountable media.
   When a user does a device (normally tape) mount or dismount at the
   command loop, the command is relayed on to the DP, but the acknowledgement
   of the response is normally handled by the CPU processing loop (see
   apr_check().)  Without an explicit call to dev_evcheck these devices
   would appear to remain busy until the CPU is continued again.

   Solution: 
	If any command is executed that might require DP waiting, do a check
	and if waiting for any DPs, turn hackery on if config/switch permits.

	If hackery enabled, during cmd input wait enable SIGALRM with
	a sigaction sa_flag of SA_RESTART so as not to interfere with
	any pending input.  All known systems now support that.

	On each SIGALRM, call dev_evcheck(), then return quietly.

	Between each command, attempt to turn hackery off if no more
	DPs are waiting.

	If calling apr_run, force hackery off.

   Plus "devwait" added for benefit of command scripts:

	devwait [dev] [n]
		Wait for device DPs to become ready:
			no args - all, indefinitely
			[dev] - that dev only
			[n] - for N seconds

	Also implement dev_status call to return actual status when desired,
	eliminating the crock of using a null arg to devmount.
 */

static int dev_dpwaiting(void);

/* Signal handler for command-level periodic check
*/
#if KLH10_DEV_DP
static void
dev_dpchk_sighan(int sig)
{
#if KLH10_EVHS_INT
    dev_evcheck();
#endif
}
#endif /* KLH10_DEV_DP */

/* DEV_DPCHK_CTL - Turn dev-dp checking on or off.
**
** Intended use:
**  After every command that affects devices:
**	chkflag = dev_dpchk_ctl(TRUE);	// Start checking if necessary
**  Between entry of each command and its execution:
**	if (chkflag)			// If on, try to turn checking off
**	    chkflag = dev_dpchk_ctl(TRUE);
**  Before calling apr_run to proceed PDP-10:
**	chkflag = dev_dpchk_ctl(FALSE);	// Turn checking off
*/
int
dev_dpchk_ctl(int dochk)		/* Arg TRUE to do a check */
{
#if KLH10_DEV_DP
    static int ischecking = 0;
    static ostimer_t savestate;
    int ndps;

    if (dochk) {
	ndps = dev_dpwaiting();		/* Find # devs waiting */
	if (ndps && !ischecking) {
	    /* Devs waiting and not checking, so turn checking on */
	    os_timer(OS_ITIMER_REAL, dev_dpchk_sighan,
		     1000000,	/* One sec's worth of usec */
		     &savestate);
	    ischecking = TRUE;
	}
    } else
	ndps = 0;

    if (!ndps && ischecking) {
	/* No devs but we're checking, so turn checking off */
	ischecking = FALSE;
	os_timer_restore(&savestate);
    }
    return ischecking;
#else
    return FALSE;
#endif /* !KLH10_DEV_DP */
}


/* DEV_DPWAITING - Return some indication of the number of DPs that are
   being waited for, i.e. which are not ready to accept new commands.

   Returns:
	>=1 if there are any DPs waiting
	-1 if there are any DPs which required a call to dev_evcheck().
	0 if there are neither - can stop checking!

   Could either step thru all devs, or go through event reg list.
   Latter probably a little faster.
*/
static int
dev_dpwaiting(void)
{
#if KLH10_EVHS_INT
    register struct dvevsig_s *evs;
    register struct dvevreg_s *evr;
    register int cnt = 0;

    /* Our DP registry starts with a list of signals; check them all,
       and check all handlers registered for each signal.
    */
    for (evs = evsiglist; evs; evs = evs->dves_next) {
	for (evr = evs->dves_reglist; evr; evr = evr->dver_next) {
	    /* If the handler's flag indicates it's waiting to be processed
	       by dev_evcheck(), call it immediately and return with
	       an indication that this was done.
	       No re-check is performed (even though it might indicate
	       that everything's clear) in order to avoid infinite loops for
	       a misbehaving device.	       
	    */
	    if (*(evr->dver_ev.dvev_arg2.eva_ip)) {
		dev_evcheck();
		return -1;
	    }

	    /* Count as "waiting" any registered device for which
	       its comm area says "cannot send".  Keep going in case
	       we later find a device waiting to be processed as above.
	    */
	    if (!dp_xstest(dp_dpxto(evr->dver_d->dv_dpp)))
		++cnt;
	}
    }
    return cnt;
#elif KLH10_DEV_DP
    register struct dvdef_s *df;

    /* Grovel through entire device definition table.
    ** This would be a good place to have a list of just DP devices.
    */
    for (df = dvdeftab; df < &dvdeftab[KLH10_DEVMAX]; ++df) {
	if (df->dev_name && df->dev_dv->dv_dpp) {
	    /* Count as "waiting" any device having a DP for which
	       its comm area says "cannot send".
	    */
	    if (!dp_xstest(dp_dpxto(df->dev_dv->dv_dpp)))
		return 1;
	}
    }
    return 0;
#else
    return 0;
#endif
}

/* DEV_WAITING - returns TRUE if a device is waiting for its DP to respond.
 */
int
dev_waiting(FILE *of, char *dstr)
{
#if KLH10_DEV_DP
    register struct device *d;
    struct dp_s *dp;

    if (!dstr || !*dstr || (*dstr == '*')) {
	/* Check all devices */
	return (dev_dpwaiting() ? TRUE : FALSE);
    }
    if (!(d = devdpverify(of, dstr, &dp)) || !dp)
	return FALSE;	/* No such dev, or has no DP */

    /* Check out specific DP.  First do complete event check to remove
       possibility that it might be ready but its event needs handling.
       (and if nothing's waiting, can skip specific check).
     */
    if (dev_dpwaiting()		/* If any DPs still waiting, */
	&& !dp_xstest(dp_dpxto(dp)))	/* check this one. */
	    return TRUE;	/* DP not ready, still waiting */
#endif /* KLH10_DEV_DP */
    return FALSE;
}
