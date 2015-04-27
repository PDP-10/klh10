/* KN10DEV.H - KLH10 Device Definitions
*/
/* $Id: kn10dev.h,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: kn10dev.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/* This file contains definitions needed for all device emulation code,
** whether old-style (I/O instructions) or KS-style (Unibus).
*/

#ifndef KN10DEV_INCLUDED
#define KN10DEV_INCLUDED 1

#ifdef RCSID
 RCSID(kn10dev_h,"$Id: kn10dev.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

#include "klh10.h"
#include "word10.h"
#include <stdio.h>	/* For FILE stream def */ 

#include "dvuba.h"	/* For ref to ubctl struct and unibus stuff */

#if KLH10_DEV_DP
# include "dpsup.h"	/* For device subproc stuff */
#else
	struct dp_s;	/* Fwd decl of anonymous struct */
#endif
	struct dvevent_s;	/* Another anon fwd decl */

/* Basic device functionality needed.

In the future, to allow for the possibility of dynamically loaded
libraries that can't resolve references to the main modules (ie one-way
linking), the main emulator may provide the device with a similar vector
to core functions (e.g. dv_pireq(), etc).

On startup, main calls an initial device function which returns a pointer
to this device structure.  The initial function must be specified
in some external fashion, either by predefined table or config file.

	extern struct device * (*device_create)(
					FILE *out,
					char *configstr);

Devices are allowed (encouraged) to maintain their internal device
information in a larger structure that includes this one as its first
element, thus the same pointer can indicate both sets of data.

Device drivers should be capable of handling more than one device or
unit.  Static variables outside of the device structure should be
avoided.

There are three types of output streams that might concern a driver:
	
  (1) Report stream - for reporting requested information back to user.
	This is assumed to be part of an interactive dialogue, with
	non-raw output, and the stream is provided at the time of the
	call to a function.
	It is not remembered across calls (unless the device wants
	to do so privately for some reason).

  (2) Error stream - for unexpected errors during device and PDP-10
	operation.  For now, the dv_dbf debug stream is used for this
	purpose and should always be open.  This stream is assumed
	to be a raw-mode stream since the console will typically be
	in raw mode and the universally brain-damaged unix tty support
	doesn't allow different modes for different channels.  So
	use \r\n instead of \n.  Redirecting this to a log file will
	include the CRs, but so what.

  (3) Debug stream - for spontaneous trace output during device and PDP-10
	operation when dv_debug is set.  While conceptually distinct,
	this stream is used both for errors and for debug output.

Issues not addressed:
	Interactive stream indication?
	How to specify whether user or debug output is interactive, needs
		CR-LF instead of LF, or is a file?
		Needs CR-LF if raw mode TTY.
	Best to pass stream for each command, or leave set in structure?
		Note dbf needs to be set, cuz can't find it otherwise from
		the places doing debug output.
	Which stream to use for actual error reporting?
*/


#define KN10DEV_VERSION 2	/* Version # of structure */

/* Device structure.
**	All entries here are set by the device, except for those marked [C]
**	which are set by the higher-level CPU or Controller after device has
**	created struct.
*/
#define dv_t struct device	/* Temporary local def */
struct device {

	/* All IO */

    int dv_vers;		/* Device interface version */
    int dv_dflags;		/* Device flags */
    int dv_cflags;		/* [C] CPU flags */
    char *dv_name;		/* [C] Device name (unique) */
    int dv_debug;		/* [C] Bit flags for debugging options */
    FILE *dv_dbf;		/* [C] Debug output stream */
    int dv_pireq;		/* [C/D] NZ = bit for level PI req active on */
    void (*dv_pifun)(dv_t *, int);	/* [C] PI request function */
    int  (*dv_evreg)(dv_t *,		/* [C] Event callback registration */
		     void (*)(dv_t *, struct dvevent_s *),
		     struct dvevent_s *);
    struct dp_s *dv_dpp;		/* Pointer to DP struct, if one */

	/* Controller <-> Drive communication stuff */

    struct device *dv_ctlr;		/* [C] Back-ptr to parent controller */
    int dv_num;				/* [C] Unit # on parent controller */
    void (*dv_attn) (dv_t *, int);	/* [C] Attention request */
    void (*dv_drerr)(dv_t *);		/* [C] Drive or xfer error */
    int  (*dv_iobeg)(dv_t *, int);	/* [C] Xfer start */
    int  (*dv_iobuf)(dv_t *, int, w10_t **);	/* [C] Xfer buffer setup */
    void (*dv_ioend)(dv_t *, int);	/* [C] Xfer end */
    uint32 (*dv_rdreg)(dv_t *, int);		/* Drive register read */
    int    (*dv_wrreg)(dv_t *, int, dvureg_t);	/* Drive register write */

	/* KA/KI/KL ("dev" IO )*/

    w10_t (*dv_pifnwd)(dv_t *);		/* PI: Get function word */
    void  (*dv_cono) (dv_t *, h10_t);	/* CONO 18-bit conds out */
    w10_t (*dv_coni) (dv_t *);		/* CONI 36-bit conds in  */
    void  (*dv_datao)(dv_t *, w10_t);	/* DATAO word out */
    w10_t (*dv_datai)(dv_t *);		/* DATAI word in */

	/* KS ("new" IO) */

    struct ubctl *dv_uba;		/* [C] Unibus Adapter for device */
    dvuadr_t dv_addr;			/* 1st valid Unibus address */
    dvuadr_t dv_aend;			/* 1st invalid Unibus address */
    int	     dv_brlev;			/* BR setting (IRQ level= 4,5,6,7) */
    dvureg_t dv_brvec;			/* BR interrupt vector setting */
    dvureg_t (*dv_pivec)(dv_t *);	/* PI: Get vector */
    dvureg_t (*dv_read) (dv_t *,	/* Read Unibus register */
			 dvuadr_t);
    void     (*dv_write)(dv_t *,	/* Write Unibus register */
			 dvuadr_t, dvureg_t);

	/* Control functions initiated by user/operator, with
	 * possible feedback via stdio stream.
	 */

    int (*dv_bind)  (dv_t *, FILE *,	/* Bind device to controller */
		     dv_t *, int);
    int (*dv_init)  (dv_t *, FILE *);	/* Final post-bind device init */
    int (*dv_exit)  (dv_t *, FILE *);	/* Exit, close, terminate */
    int (*dv_mount) (dv_t *, FILE *,	/* Mount or unmount media */
		     char *, char *);
    int (*dv_help)  (dv_t *, FILE *);	/* Output help info */
    int (*dv_status)(dv_t *, FILE *);	/* Output status info */
    int (*dv_cmd)   (dv_t *, FILE *,	/* General user command to device */
		     char *);

	/* Misc control functions */

    int   (*dv_readin)(dv_t *, FILE *,	/* Readin mode */
		       w10_t, w10_t *, int);
    void  (*dv_powon) (dv_t *);		/* "Power on" */
    void  (*dv_reset) (dv_t *);		/* System reset */
    void  (*dv_powoff)(dv_t *);		/* "Power off" */
};
#undef dv_t	/* Was only temporary */

/* Flags for dv_dflags */
#define DVFL_DEVIO 04	/* [C] Expects old-style I/O instrs */
#define DVFL_UBIO  010	/* [C] Expects new-style I/O instrs (unibus) */
#define DVFL_CTLIO 020	/* [D] Invoked as unit of a controller */
#define DVFL_CTLR  040	/* [D] Controller device */
#define DVFL_NBA   0100	/* [D] Not block addressed (== TYP 2.7) */
#define DVFL_TAPE  0200	/* [D] Tape of some sort   (== TYP 2.6) */
#define DVFL_DISK  0400	/* [D] Random-access drive (== TYP 2.5) */

/* Flags for dv_debug */
#define DVDBF_ON	01	/* Debugging on */
#define DVDBF_DATSHO 	02	/* "Show data" (if flag recognized) */

/* Handy debug macros */
#define DVDEBUG(d) (((struct device *)(d))->dv_debug)
#define DVDBF(d)   (((struct device *)(d))->dv_dbf)

#define insdef_coni(rtn) \
	w10_t rtn(register struct device *d)
#define insdef_datai(rtn) \
	w10_t rtn(register struct device *d)
#define insdef_cono(rtn) \
	void rtn(register struct device *d, register h10_t erh)
#define insdef_datao(rtn) \
	void rtn(register struct device *d, register w10_t w)

/* Exported functions */

extern void dev_init(void);
extern void dev_term(void);
extern int  dev_drvload(FILE *, char *, char *, char *, char *);
extern int  dev_define(FILE *, char *, char *, char *, char *);
extern int  dev_command(FILE *, char *, char *);
extern int  dev_help(FILE *, char *, char *);
extern int  dev_mount(FILE *, char *, char *, char *);
extern int  dev_debug(FILE *, char *, char *, char *);
extern int  dev_boot(FILE *, char *, vaddr_t *);
extern int  dev_show(FILE *, char *, char *);
extern int  dev_status(FILE *, char *, char *);
extern int  dev_waiting(FILE *, char *);
extern int  dev_dpchk_ctl(int);

extern int iodv_version(void);		/* Return current ver of dev struct */
extern int iodv_nullinit(struct device *, int);	/* For initing to null dev */
extern void iodv_setnull(struct device *);	/*  " " " "     (old form) */

/* Device Event types & flags */

enum dveventtype {
	DVEV_CLOCK=1,	/* Periodic clock callout, arg is period in ticks */
	DVEV_TMOUT,	/* One-shot timeout, arg is ticks */
	DVEV_1SIG,	/* Sole signal */
	DVEV_NSIG,	/* Multiplexed signal */
	DVEV_ASIG,	/* Allocated signal */
	DVEV_DPSIG,	/* Dev subproc comm signal */
	DVEV_N		/* #+1 of event types */
};

struct dvevent_s {
    enum dveventtype dvev_type;
    union dvevarg_u {
	int eva_int;
	long eva_long;
	unsigned char *eva_ucp;
	int *eva_ip;
    } dvev_arg;
    union dvevarg_u dvev_arg2;
};

/* INTERNAL definitions, not really for export to device code
*/

#if KLH10_EVHS_INT

extern void dev_evinit(void);
extern void dev_evcheck(void);
extern int  dev_evshow(FILE *, char *, char *);
extern int  dev_evreg(struct device *,
		      void (*)(struct device *, struct dvevent_s *),
		      struct dvevent_s *);


/* Event registration entries */
struct dvevreg_s {
    struct dvevreg_s *dver_next, *dver_prev;	/* List for variousness */
    void (*dver_hdlr)(struct device *, struct dvevent_s *);
    struct device *dver_d;		/* Remember its device */
    struct dvevent_s dver_ev;		/* and args */
};
extern struct dvevreg_s *evregfree;	/* Head of reg entry free list */
extern struct dvevreg_s evregtab[];

struct dvevsig_s {
	osintf_t dves_intf;		/* Bumped when sig seen */
	struct dvevsig_s *dves_next,	/* Chain on list of reg'd signals */
			*dves_prev;
	int dves_sig;			/* Signal # */
	struct dvevreg_s *dves_reglist;	/* List of event hndlrs for this sig */
};
extern struct dvevsig_s *evsiglist;	/* Head of reg'd signal list */
extern struct dvevsig_s evsigtab[];

#endif /* KLH10_EVHS_INT */

#endif /* KN10DEV_INCLUDED */
