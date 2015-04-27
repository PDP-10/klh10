/* DVNI20.C - Emulates NIA20 Network Interface for KL10
*/
/* $Id: dvni20.c,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: dvni20.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*
	This implements a new device subprocess (DP) mechanism.  The
correspondence between the NIA20 state and the DP state is:

	NIA20		DP
	-----		--
	dvni20 setup	dpni20 setup (mem seg ready), no fork.
	Not running	No fork/program.
	Running		DP fork/program running!
	  Disabled	  NI20 code flushes DP input, does no output.
	  Enabled	  NI20 code reads DP input & does output.
*/
#include "klh10.h"

#if !KLH10_DEV_NI20 && CENV_SYS_DECOSF
	/* Stupid gubbish needed to prevent OSF/1 AXP compiler from
	** halting merely because compiled file is empty!
	*/
static int decosfcclossage;
#endif

#if KLH10_DEV_NI20		/* Moby conditional for entire file */

#include <stddef.h>	/* For size_t etc */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>	/* For malloc */ 

#include "kn10def.h"	/* This includes OSD defs */
#include "kn10dev.h"
#include "kn10ops.h"
#include "dvni20.h"
#include "prmstr.h"	/* For parameter parsing */

#if KLH10_DEV_DPNI20	/* Event handling and dev sub-proc stuff! */
# include "dpni20.h"
#endif

#ifdef RCSID
 RCSID(dvni20_c,"$Id: dvni20.c,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* Ethernet packet definitions */

#define ETHER_ADRSIZ	6	/* # bytes in ethernet address */
#define ETHER_HDRSIZ	14	/* # bytes in header (2 addrs plus type) */
#define ETHER_MTU	1500	/* Max # data bytes in ethernet pkt */
#define ETHER_MIN	(60-ETHER_HDRSIZ)	/* Minimum # data bytes */
#define ETHER_CRCSIZ	4	/* # bytes in trailing CRC */

	/* Ethernet packet offset values */
#define ETHER_PX_DST	0	/* Dest address */
#define ETHER_PX_SRC	6	/* Source address */
#define ETHER_PX_TYP	12	/* Type (high byte first) */
#define ETHER_PX_DAT 	14	/* Data bytes */
	/* CRC comes after data, which is variable-length */

#define NI20_MAXPKTLEN	1520	/* # bytes in max pkt NIA20 handles */
				/* (actually 1519 but round up) */

/* Later move these elsewhere? */
#define w10topa(w) ((paddr_t)W10_U32(w) & MASK22)
#define patow10(w,pa) W10_U32SET((w), (pa))

/* Likewise move to dvdchn.h or something */
struct dvdchn {
    int     dc_eptoff;	/* Offset of channel area in EPT */
    paddr_t dc_clp;	/* DC "PC" - Current Command List Pointer */
    uint18  dc_sts;	/* DC status flag bits (LH) */
    w10_t   dc_ccw;	/* DC current cmd word */
    int     dc_wcnt;	/* DC cmd word count */
    paddr_t dc_buf;	/* DC cmd data buffer pointer */
    int	    dc_hlt;	/* TRUE if halt when current wordcount done */
    int	    dc_rev;	/* TRUE if doing reverse data xfer */
    int     dc_wrt;	/* TRUE if doing device write */
};

void dchn_init(struct dvdchn *dc, int mbcn);
void dchn_clear(struct dvdchn *dc);
int  dchn_ccwget(struct dvdchn *dc);


/* Internal PTT entry */
struct niptt {
    unsigned int ptt_type;	/* 16-bit protocol type */
    paddr_t ptt_freeq;		/* Phys addr of free queue header */
};

/* Internal echo check buffer entry */
struct niecbe {
    int niec_deathtmo;		/* .5-sec ticks left to live */
    unsigned char niec_hdr[ETHER_HDRSIZ];	/* Ether header */
    uint32 niec_digest;		/* Checksum/hash of data */
};

struct ni20 {
    struct device ni_dv;	/* Generic 10 device structure */

    /* NI20-specific vars */
    int ni_no;		/* NI20 Unit number (0-7) (normally 5) */
    uint18 ni_lhcond;	/* LH CONI bits (CSR) */
    uint18 ni_cond;	/* RH CONI bits (CSR) */
    int ni_pia;		/* PIA from CSR bits (vs ni_ppia, from PCB) */
    int ni_pilev;	/* PI level mask (0 if PIA=0) */
    int ni_pivec;	/* PI vector for RH of PI function word */

    int ni_state;	/* NI port state */
# define NI20_STF_RUN  02	/* Running */
# define NI20_STF_ENA  01	/* If Running, 1=Enabled/0=Disabled */
				/*	else 1=Halt/0=Uninitialized */
# define NI20_ST_UNINT  0	/* Uninitialized (powerup, not running) */
# define NI20_ST_HALT   1	/* Halted (normally from error) */
# define NI20_ST_RUN    2	/* Running disabled */
# define NI20_ST_RUNENA 3	/* Running enabled */
    int ni_rar;		/* RAR (RAM Address Register) 13 bits + 1 LH/RH bit */
    int ni_lar;		/* LAR (Last Address Register) 13 bits */
    w10_t ni_ebuf;	/* Ebuf word */

    /* Station info */
    unsigned char ni_ethadr[6];	/* Ethernet addr of this NIA20 port */
    dw10_t ni_dwethadr;	/* Ethernet addr in PDP-10 format */
    int ni_staflgs;	/* Station flags (NI20_RSF_*) */
    int ni_retries;	/* # retries allowed (default 5, T20 sets to 16) */

    /* Misc config info not set elsewhere */
    char *ni_ifnam;	/* Native platform's interface name */
    int ni_dedic;	/* TRUE if interface dedicated (else shared) */
    int ni_decnet;	/* TRUE to filter DECNET packets (if shared) */
    int ni_doarp;	/* TRUE to do ARP hackery (if shared) */
    int ni_backlog;	/* Max # input msgs to queue up in kernel */
    int ni_rdtmo;	/* # secs to timeout on packetfilter reads */
    int ni_lsapf;	/* TRUE to filter on LSAP addr (if shared) */
    int ni_lsap;	/* LSAP src/dst address for above */
    unsigned char ni_ipadr[4];	/* KLH10 IP address to filter (if shared) */
    int32 ni_c3dly;		/* Initial-cmd delay in ticks */
    int32 ni_c3dlyct;		/* Countdown (no delay if 0) */

    /* Cached port control block data */
    paddr_t ni_pcba;	/* Phys addr of PCB */
    vmptr_t ni_pcbvp;	/* Ptr to actual PCB memory */
    int ni_ppia;	/* Port PIA */
    w10_t ni_ivec;	/* Port interrupt vector */
    int ni_upqelen;	/* Unknown Protocol Queue Entry length, in words */

    /* Semi-cached PCB data - PCB entry is re-checked on appropriate cmd */
    vmptr_t ni_pttvp;	/* Ptr to PTT in 10 memory; updated by LDPTT */
    vmptr_t ni_mcatvp;	/* Ptr to MCAT in 10 memory; updated by LDMCAT */
    vmptr_t ni_rcbvp;	/* Ptr to counters in 10 memory; updated by RCCNT */

    struct dvdchn ni_dc;	/* Data Channel vars */

    int ni_istate;	/* Internal state for timeouts etc */
    int ni_cmdqf;	/* TRUE if want to check cmd queue */
    paddr_t ni_qepa;	/* Active queue entry phys addr (needs relinking) */
    paddr_t ni_qhpa;	/* Active queue header phys addr (relink here) */
    int ni_pktinf;	/* TRUE if have packet input waiting */

    int ni_nptts;	/* # of valid entries in PTT */
    int ni_nmcats;	/* # of valid entries in MCAT */
    struct niptt ni_ptt[NI20_NPTT];	/* Internal PTT */
    dw10_t ni_mcat[NI20_NMTT];		/* Internal MCAT - simple addr table */

    uint32 ni_cnts[NI20_RCLEN];

#if KLH10_DEV_DPNI20
    int ni_dpstate;	/* TRUE if dev process has finished its init */
    struct dp_s ni_dp;	/* Handle on dev process */
    char *ni_dpname;	/* Pointer to dev process pathname */
    unsigned char *ni_sbuf;	/* Pointers to shared memory buffers */
    unsigned char *ni_rbuf;
    int ni_rcnt;	/* # chars in received packet input buffer */
    int ni_dpidly;	/* # secs to sleep when starting NI DP */
    int ni_dpdbg;	/* Initial DP debug flag */
#endif

    /* New clock timer stuff */
    struct clkent *ni_chktmr;	/* Timer for periodic run re-checks */
    int ni_docheck;		/* TRUE if timer active */

    /* Ugly echo packet checking, to emulate NI's half-duplex lossage */
    int ni_ecchk;	/* TRUE to check for (and flush) echoed packets */
    int ni_ecblen;	/* Echo check buffer length (# entries) */
    int ni_ectmo;	/* Echo check packet timeout (# half-secs) */
    struct niecbe *ni_ecb;	/* Echo check ring buffer */
    int ni_ecbfuse;		/* Index of first active entry */
    int ni_ecbffree;		/* Index of first free entry */
    struct clkent *ni_ectmr;	/* Timer for periodic buffer reaping */
    int ni_ectact;		/* TRUE if timer active */
};

static int nni20s = 0;
/* static */ struct ni20 dvni20[NI20_NSUP];


static unsigned char ni20_bcadr[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static dw10_t ni20_dwbcadr;

#define NIDEBUG(ni) ((ni)->ni_dv.dv_debug)
#define NIDBF(ni)   ((ni)->ni_dv.dv_dbf)


/* Function predecls */

/* Functions provided to device vector */

static w10_t ni20_pifnwd(struct device *d);
static void  ni20_cono(struct device *d, h10_t erh);
static w10_t ni20_coni(struct device *d);
static void  ni20_datao(struct device *d, w10_t w);
static w10_t ni20_datai(struct device *d);
static int   ni20_init(struct device *d, FILE *of);
static void  ni20_reset(struct device *d);
static void  ni20_powoff(struct device *d);
#if KLH10_DEV_DPNI20
static void  ni20_evhsdon(struct device *d, struct dvevent_s *evp);
static void  ni20_evhrwak(struct device *d, struct dvevent_s *evp);
#endif

/* Completely internal functions */

static int  ni20_conf(FILE *f, char *s, struct ni20 *ni);
static void ni20_clear(struct ni20 *ni);
static void ni20_picheck(struct ni20 *ni);
static void ni20_pi(struct ni20 *ni);
static void ni20_piclr(struct ni20 *ni);
static void ni20_start(struct ni20 *ni);
static void ni20_stop(struct ni20 *ni);
static void ni20_enable(struct ni20 *ni);
static void ni20_disable(struct ni20 *ni);
static void ni20_run(struct ni20 *ni);
static int  ni20_runclk(void *arg);
static void ni_ethtodw(dw10_t *da, unsigned char *ea);
static int  ni20_cmdchk(struct ni20 *ni);
#if KLH10_DEV_DPNI20
static void ni20_iniable(register struct ni20 *ni);
#endif

static int
	nicmd_snddg(struct ni20 *ni, vmptr_t qep),
	nicmd_ldmcat(struct ni20 *ni, vmptr_t qep),
	nicmd_ldptt(struct ni20 *ni, vmptr_t qep),
	nicmd_rccnt(struct ni20 *ni, vmptr_t qep),
	nicmd_wrtpli(struct ni20 *ni, vmptr_t qep),
	nicmd_rdpli(struct ni20 *ni, vmptr_t qep),
	nicmd_rdnsa(struct ni20 *ni, vmptr_t qep),
	nicmd_wrtnsa(struct ni20 *ni, vmptr_t qep),
	nicmd_dgrcv(struct ni20 *ni, unsigned char *ucp, unsigned int blen);

static void ni20_ldptt(struct ni20 *ni);
static void ni20_ldmcat(struct ni20 *ni);
static paddr_t ni_pttfind(struct ni20 *ni, unsigned int ptyp);
static int     ni_ipttfind(struct ni20 *ni, unsigned int ptyp);
static int  ni_qeput(struct ni20 *ni, unsigned int qh, unsigned int qe);
static paddr_t ni_qeget(struct ni20 *ni, unsigned int qh);
static int  ni_qeunget(struct ni20 *ni, unsigned int qh, unsigned int qe);
static int  ni_qecnt(struct ni20 *ni, unsigned int qh);

static int  ni20_ecpclk(void *arg);
static void ni_ecpstore(struct ni20 *ni, unsigned char *ucp, unsigned int len);
static int  ni_ecpcheck(struct ni20 *ni, unsigned char *ucp, unsigned int len);
static uint32 ni_ecpdigest(unsigned char *ucp, int len);

/* Configuration Parameters */

#define DVNI20_PARAMS \
    prmdef(NIP_DBG,"debug"),    /* Initial debug flag */\
    prmdef(NIP_EN, "enaddr"),   /* Ethernet address to use (override) */\
    prmdef(NIP_IFC,"ifc"),      /* Ethernet interface name */\
    prmdef(NIP_BKL,"backlog"),/* Max bklog for rcvd pkts (else sys default) */\
    prmdef(NIP_DED,"dedic"),    /* TRUE= Ifc dedicated (else shared) */\
    prmdef(NIP_IP, "ipaddr"),   /* IP address of KLH10, if shared */\
    prmdef(NIP_DEC,"decnet"),   /* TRUE= if shared, seize DECNET pkts */\
    prmdef(NIP_ARP,"doarp"),    /* TRUE= if shared, do ARP hackery */\
    prmdef(NIP_LSAP,"lsap"),    /* Set= if shared, filter on LSAP pkts */\
    prmdef(NIP_ECCHK,"echochk"), /* TRUE= check for echoed pkts */\
    prmdef(NIP_ECBUF,"echobuf"), /* # echoed pkts to remember */\
    prmdef(NIP_ECTMO,"echotmo"), /* # secs to remember them */\
    prmdef(NIP_C3DLY,"c3dly"),  /* # ticks to use for NI cmd #3 (LDPTT)*/\
    prmdef(NIP_RDTMO,"rdtmo"),  /* # secs to timeout on packetfilter read */\
    prmdef(NIP_DPDLY,"dpdelay"),/* # secs to sleep when starting DP */\
    prmdef(NIP_DPDBG,"dpdebug"),/* Initial DP debug value */\
    prmdef(NIP_DP, "dppath")    /* Device subproc pathname */

enum {
# define prmdef(i,s) i
	DVNI20_PARAMS
# undef prmdef
};

static char *niprmtab[] = {
# define prmdef(i,s) s
	DVNI20_PARAMS
# undef prmdef
	, NULL
};

	/* Local parsing routines */
static int pareth(char *cp, unsigned char *adr);
static int parip(char *cp, unsigned char *adr);

/* NI20_CONF - Parse configuration string and set defaults.
**	At this point, device has just been created, but not yet bound
**	or initialized.
** NOTE that some strings are dynamically allocated!  Someday may want
** to clean them up nicely if config fails or device is uncreated.
*/

static int
ni20_conf(FILE *f, char *s, struct ni20 *ni)
{
    int i, ret = TRUE;
    struct prmstate_s prm;
    char buff[200];
    long lval;

    /* First set defaults for all configurable parameters */
    DVDEBUG(ni) = FALSE;
    ni->ni_ifnam = NULL;
    ni->ni_backlog = 0;
    ni->ni_decnet = FALSE;
    ni->ni_dedic = FALSE;
    ni->ni_doarp = TRUE;
    ni->ni_lsapf = FALSE;
    ni->ni_lsap = 0;
    ni->ni_ecchk = -1;		/* Initially unknown */
    ni->ni_ecblen = 60;
    ni->ni_ectmo = 1*2;
    ni->ni_rdtmo = 0;
    ni->ni_c3dly = 5;		/* Conservative 5-millisec timeout for T10 */
    ni->ni_c3dlyct = 0;
#if KLH10_DEV_DPNI20
    ni->ni_dpname = "dpni20";	/* Pathname of device subproc */
    ni->ni_dpidly = 5;		/* Conservative 5-second timeout for T10/T20 */
    ni->ni_dpdbg = FALSE;
#endif

    prm_init(&prm, buff, sizeof(buff),
		s, strlen(s),
		niprmtab, sizeof(niprmtab[0]));
    while ((i = prm_next(&prm)) != PRMK_DONE) {
	switch (i) {
	case PRMK_NONE:
	    fprintf(f, "Unknown NI20 parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;
	case PRMK_AMBI:
	    fprintf(f, "Ambiguous NI20 parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;
	default:	/* Handle matches not supported */
	    fprintf(f, "Unsupported NI20 parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;

	case NIP_DBG:		/* Parse as true/false boolean or number */
	    if (!prm.prm_val)	/* No arg => default to 1 */
		DVDEBUG(ni) = 1;
	    else if (!s_tobool(prm.prm_val, &DVDEBUG(ni)))
		break;
	    continue;

	case NIP_IP:		/* Parse as IP address: u.u.u.u */
	    if (!prm.prm_val)
		break;
	    if (!parip(prm.prm_val, &ni->ni_ipadr[0]))
		break;
	    continue;

	case NIP_EN:		/* Parse as EN address in hex */
	    if (!prm.prm_val)
		break;
	    if (!pareth(prm.prm_val, &ni->ni_ethadr[0]))
		break;
	    continue;

	case NIP_IFC:		/* Parse as simple string */
	    if (!prm.prm_val)
		break;
	    ni->ni_ifnam = s_dup(prm.prm_val);
	    continue;

	case NIP_BKL:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    ni->ni_backlog = lval;
	    continue;

	case NIP_DED:		/* Parse as true/false boolean */
	    if (!prm.prm_val)
		break;
	    if (!s_tobool(prm.prm_val, &ni->ni_dedic))
		break;
	    continue;

	case NIP_DEC:		/* Parse as true/false boolean */
	    if (!prm.prm_val)
		break;
	    if (!s_tobool(prm.prm_val, &ni->ni_decnet))
		break;
	    continue;

	case NIP_ARP:		/* Parse as true/false boolean or number */
	    if (!prm.prm_val)
		break;
	    if (!s_tobool(prm.prm_val, &ni->ni_doarp))
		break;
	    continue;

	case NIP_LSAP:		/* Parse as number, preferably hex */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    if (lval <= 0xFF) {		/* If only one LSAP byte set, */
		lval |= (lval << 8);	/* double it up for both dest & src */
	    }
	    ni->ni_lsap = lval;
	    ni->ni_lsapf = TRUE;
	    continue;

	case NIP_RDTMO:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    ni->ni_rdtmo = lval;
	    continue;

	case NIP_C3DLY:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    ni->ni_c3dly = lval;
	    continue;

	case NIP_ECCHK:		/* Parse as true/false boolean */
	    if (!prm.prm_val)
		break;
	    if (!s_tobool(prm.prm_val, &ni->ni_ecchk))
		break;
	    continue;

	case NIP_ECBUF:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    ni->ni_ecblen = lval;
	    continue;

	case NIP_ECTMO:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    ni->ni_ectmo = lval * 2;	/* Half-secs! */
	    continue;

	case NIP_DPDLY:		/* Parse as decimal number */
#if KLH10_DEV_DPNI20
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    ni->ni_dpidly = lval;
#endif
	    continue;

	case NIP_DPDBG:		/* Parse as true/false boolean or number */
#if KLH10_DEV_DPNI20
	    if (!prm.prm_val)	/* No arg => default to 1 */
		ni->ni_dpdbg = 1;
	    else if (!s_tobool(prm.prm_val, &(ni->ni_dpdbg)))
		break;
#endif
	    continue;

	case NIP_DP:		/* Parse as simple string */
#if KLH10_DEV_DPNI20
	    if (!prm.prm_val)
		break;
	    ni->ni_dpname = s_dup(prm.prm_val);
#endif
	    continue;
	}
	ret = FALSE;
	fprintf(f, "NI20 param \"%s\": ", prm.prm_name);
	if (prm.prm_val)
	    fprintf(f, "bad value syntax: \"%s\"\n", prm.prm_val);
	else
	    fprintf(f, "missing value\n");
    }

    /* Param string all done, do followup checks */

    /* Unless interface is dedicated, either DECNET or IPADDR *must* be set! */
    if (!ni->ni_dedic
      && !ni->ni_decnet
      && (memcmp(ni->ni_ipadr, "\0\0\0\0", 4) == 0)) {
	fprintf(f,
	    "NI20 param \"decnet\" or \"ipaddr\" must be set for a shared interface\n");
	return FALSE;
    }

    /* If necessary, make a guess as to whether to do echo checking.  Although
    ** setting it TRUE is the safe default for accurate emulation, the overhead
    ** may sometimes be questionable.
    */
    if (ni->ni_ecchk == -1) {		/* If no explicit setting */
	if (ni->ni_dedic		/* Fast if dedicated, so OK */
	  || ni->ni_decnet)		/* Shared DECNET must play safe */
	    ni->ni_ecchk = TRUE;
	else
	    ni->ni_ecchk = FALSE;	/* Shared IP uses OS filtering */
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

/* NI20 interface routines to KLH10 */

/* Address note: It's unclear where ni_ethadr should be initialized.
**	It could be obtained from the host platform's OS, or could
**	be specified in the device config param string.
**
**	For now, init it with a compile-time param, sigh.
*/

struct device *
dvni20_create(FILE *f, char *s)
{
    register struct ni20 *ni;

    /* Parse string to determine which device to use, config, etc etc
    ** But for now, just allocate sequentially.  Hack.
    */ 
    if (nni20s >= NI20_NSUP) {
	fprintf(f, "Too many NI20s, max: %d\n", NI20_NSUP);
	return NULL;
    }
    ni = &dvni20[nni20s++];		/* Pick unused NI20 */
    memset((char *)ni, 0, sizeof(*ni));	/* Clear it out */

    /* Initialize generic device part of NI20 struct */
    iodv_setnull(&ni->ni_dv);		/* Initialize as null device */

    ni->ni_dv.dv_dflags = 0;
    ni->ni_dv.dv_pifnwd = ni20_pifnwd;
    ni->ni_dv.dv_cono = ni20_cono;
    ni->ni_dv.dv_coni = ni20_coni;
    ni->ni_dv.dv_datao = ni20_datao;
    ni->ni_dv.dv_datai = ni20_datai;

    ni->ni_dv.dv_bind = NULL;		/* Not a controller!! */
    ni->ni_dv.dv_init = ni20_init;	/* Set up own post-bind init */
    ni->ni_dv.dv_reset = ni20_reset;	/* System reset (clear stuff) */
    ni->ni_dv.dv_powoff = ni20_powoff;	/* Power-off cleanup */

    if (!ni20_conf(f, s, ni))		/* Do configuration stuff */
	return NULL;

    return &ni->ni_dv;
}


static int
ni20_init(struct device *d,
		   FILE *of)
{
    register struct ni20 *ni = (struct ni20 *)d;
    size_t junk;

    /* Transform device # into NI20 unit # */
    if (DEVRH20(0) <= ni->ni_dv.dv_num && ni->ni_dv.dv_num < DEVRH20(8)) {
	ni->ni_no = (ni->ni_dv.dv_num - DEVRH20(0)) >> 2;
    } else {
	if (of) fprintf(of, "Trying to init NI20 with non-Massbus device #\n");
	return FALSE;
    }
    if (ni->ni_dv.dv_num != NI20_DEV) {
	if (of) fprintf(of, "Initing NI20 with non-standard device #\n");
    }
    dchn_init(&ni->ni_dc, ni->ni_no);	/* Init data channel */

#if 0
    memcpy(ni->ni_ethadr, ni20_ethadr, 6);	/* Initial ROM E/N addr */
#endif
    ni_ethtodw(&ni->ni_dwethadr, ni->ni_ethadr); /* Also in PDP-10 fmt */
    ni_ethtodw(&ni20_dwbcadr, ni20_bcadr);	/* Get bcast into 10 fmt */

    ni->ni_state = NI20_ST_UNINT;	/* Ensure ni20_stop not invoked */

    /* Set up periodic recheck timer (in case queue locked, etc) */
    if (!ni->ni_chktmr)
	ni->ni_chktmr = clk_tmrget(ni20_runclk, (void *)ni,
				CLK_USECS_PER_MSEC);
    clk_tmrquiet(ni->ni_chktmr);	/* Immediately make it quiescent */
    ni->ni_docheck = FALSE;

    /* Initialize gross echo packet checking if necessary */
    if (ni->ni_ecchk && !(ni->ni_dedic) && (ni->ni_ecblen > 0)) {
	/* Echo check for shared interface, must use buffer, ugh! */
	ni->ni_ecb = (struct niecbe *)malloc(ni->ni_ecblen
						* sizeof(struct niecbe));
	if (!(ni->ni_ecb)) {
	    if (of) fprintf(of, "NI20 couldn't alloc echo check buffer\n");
	    return FALSE;
	}
	ni->ni_ecbfuse = ni->ni_ecbffree = 0;
	if (ni->ni_ectmo) {	/* Use slow half-sec clock? */
	    ni->ni_ectmr = clk_tmrget(ni20_ecpclk, (void *)ni,
						(CLK_USECS_PER_SEC/2));
	    clk_tmrquiet(ni->ni_ectmr);	/* Immediately make it quiescent */
	    ni->ni_ectact = FALSE;
	}
    }

#if KLH10_DEV_DPNI20
  {
    register struct dpni20_s *dpc;
    struct dvevent_s ev;

    ni->ni_dpstate = FALSE;
    if (!dp_init(&ni->ni_dp, sizeof(struct dpni20_s),
			DP_XT_MSIG, SIGUSR1, (size_t)1600,	/* in */
			DP_XT_MSIG, SIGUSR1, (size_t)1600)) {	/* out */
	if (of) fprintf(of, "NI20 subproc init failed!\n");
	return FALSE;
    }
    ni->ni_sbuf = dp_xsbuff(&(ni->ni_dp.dp_adr->dpc_todp), &junk);
    ni->ni_rbuf = dp_xrbuff(&(ni->ni_dp.dp_adr->dpc_frdp), &junk);

    ni->ni_dv.dv_dpp = &(ni->ni_dp);	/* Tell CPU where our DP struct is */

    /* Set up NI20-specific part of shared DP memory */
    dpc = (struct dpni20_s *) ni->ni_dp.dp_adr;
    dpc->dpni_dpc.dpc_debug = ni->ni_dpdbg;	/* Init DP debug flag */
    if (cpu.mm_locked)				/* Lock DP mem if CPU is */
	dpc->dpni_dpc.dpc_flags |= DPCF_MEMLOCK;

#if DPNI20_LSAP
    dpc->dpni_ver = DPNI20_VERSION;
    dpc->dpni_attrs = 0;
    if (ni->ni_lsapf)			/* Pass on LSAP value if any */
    {
	dpc->dpni_attrs |= DPNI20F_LSAP;
	dpc->dpni_lsap = ni->ni_lsap;
    }
#endif
    dpc->dpni_backlog = ni->ni_backlog;	/* Pass on backlog value */
    dpc->dpni_dedic = ni->ni_dedic;	/* Pass on dedicated flag */
    dpc->dpni_decnet = ni->ni_decnet;	/* Pass on DECNET flag */
    dpc->dpni_doarp = ni->ni_doarp;	/* Pass on DOARP flag */
    dpc->dpni_rdtmo = ni->ni_rdtmo;	/* Pass on RDTMO value */

    if (ni->ni_ifnam)			/* Pass on interface name if any */
	strncpy(dpc->dpni_ifnam, ni->ni_ifnam, sizeof(dpc->dpni_ifnam)-1);
    else
	dpc->dpni_ifnam[0] = '\0';	/* No specific interface */
    memcpy((char *)dpc->dpni_ip,	/* Set our IP address for filter */
		ni->ni_ipadr, 4);
    memcpy(dpc->dpni_eth,		/* Set EN address if any given */
		ni->ni_ethadr, 6);	/* (all zero if none) */

    /* Register ourselves with main KLH10 loop for DP events */

    ev.dvev_type = DVEV_DPSIG;		/* Event = Device Proc signal */
    ev.dvev_arg.eva_int = SIGUSR1;
    ev.dvev_arg2.eva_ip = &(ni->ni_dp.dp_adr->dpc_todp.dpx_donflg);
    if (!(*ni->ni_dv.dv_evreg)((struct device *)ni, ni20_evhsdon, &ev)) {
	if (of) fprintf(of, "NI20 event reg failed!\n");
	return FALSE;
    }

    ev.dvev_type = DVEV_DPSIG;		/* Event = Device Proc signal */
    ev.dvev_arg.eva_int = SIGUSR1;
    ev.dvev_arg2.eva_ip = &(ni->ni_dp.dp_adr->dpc_frdp.dpx_wakflg);
    if (!(*ni->ni_dv.dv_evreg)((struct device *)ni, ni20_evhrwak, &ev)) {
	if (of) fprintf(of, "NI20 event reg failed!\n");
	return FALSE;
    }
  }
#endif /* KLH10_DEV_DPNI20 */

    ni20_clear(ni);			/* Clear NIA20 */

    return TRUE;
}

static void
ni20_reset(struct device *d)
{
    ni20_clear((struct ni20 *)d);
}


/* NI20_POWOFF - Handle "power-off" which usually means the KLH10 is
**	being shut down.  This is important if using a dev subproc!
*/
static void
ni20_powoff(struct device *d)
{
    register struct ni20 *ni = (struct ni20 *)d;

    ni20_stop(ni);		/* First stop NI cold */
#if KLH10_DEV_DPNI20
    ni->ni_state = NI20_ST_UNINT;
    (*ni->ni_dv.dv_evreg)(	/* Flush all event handlers for device */
		(struct device *)ni,
		NULL,		/* No event handler proc */
		(struct dvevent_s *)NULL);
    dp_term(&(ni->ni_dp), 0);	/* Flush all subproc overhead */
    ni->ni_sbuf = NULL;		/* Clear pointers no longer meaningful */
    ni->ni_rbuf = NULL;
#endif
}

/* PI: Get PI function word
**
**	Not clear which PI takes precedence; the PIA from the PCB or the
**	CSR.  They really should be identical.
**	It appears that the PCB takes precedence, inasmuch as KLNI.MEM says
**	the port cannot do a PI until the PCB PIA is set, and the T20 bootstrap
**	code (and monitor startup) appears to screw up by treating the NI
**	port like a RH20 initially -- if the NI responded to the CSR PI
**	assignment, it would interrupt at RH20 level and generally confuse
**	the software.  What a mess.
**
**	It appears that for T20, an unvectored interrupt is used.
**	Again, not clear if this is because the IVA word of PCB is zero;
**	it isn't even explicitly initialized to zero, it's just left alone!
**
**	To avoid slowing up the KLH10 PI code, we'll compute the correct
**	standard-dispatch vector here and feed it back in the RH so the
**	PI RH20 handling stuff will be suitably faked out; if it is changed
**	to pay attention to the PIFN_STD value, whatever is in the RH will
**	be harmless anyway.
*/

static w10_t
ni20_pifnwd(struct device *d)
{
    register struct ni20 *ni = (struct ni20 *)d;
    register w10_t w;

    LRHSET(w, PIFN_FSTD,	/* Do a standard unvectored interrupt */
		ni->ni_pivec);	/* But provide vector anyway! */
    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_pifnwd: %lo,,%lo]\r\n",
			(long)LHGET(w), (long)RHGET(w));
    return w;
}

/* CONO 18-bit conds out
**	Args D, ERH
** Returns nothing
*/
static insdef_cono(ni20_cono)
{
    register struct ni20 *ni = (struct ni20 *)d;
    register uint18 cond = erh;

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_cono: %lo]\r\n", (long)erh);


    /* B18 - Clear port - assume this stops it too */
    if (cond & NI20CO_CPT) {
	ni20_clear(ni);	
    }
    
    /* Set most but not all RH bits to whatever CONO wants */
    ni->ni_cond &= ~(NI20CO_SEB|NI20CO_LAR|NI20CO_SSC
		 | NI20CO_CQA|NI20CO_DIS|NI20CO_ENA|NI20CO_MRN|NI20CO_PIA);
    ni->ni_cond |= cond
		& (NI20CO_SEB|NI20CO_LAR|NI20CO_SSC
		 | NI20CO_CQA|NI20CO_DIS|NI20CO_ENA|NI20CO_MRN|NI20CO_PIA);

    /* Bits to zap, if specified by CONO */
    if (cond & (NI20CI_EPE|NI20CI_FQE|NI20CI_DME|NI20CI_RQA)) {
	ni->ni_cond &= ~(cond &
		( NI20CI_EPE	/* B24 - Turn off Ebus Parity Error bit */
		| NI20CI_FQE	/* B25 - Turn off Free Queue Error bit */
		| NI20CI_DME	/* B26 - Turn off Data Mover Error bit */
		| NI20CI_RQA));	/* B28 - Turn off Response Queue Avail bit */
    }

    /* B32 - Run.  Keep running if already on, else start running
    **	using PC in RAR; 0 is normal start addr for KLNI port ucode.
    */
    if (cond & NI20CO_MRN) {
	if (!(ni->ni_state & NI20_STF_RUN))
	    ni20_start(ni);	/* Start the NI running */
    } else {
	if (ni->ni_state & NI20_STF_RUN)
	    ni20_stop(ni);	/* Stop the NI */
    }

    /* B30 - Disable.  KL sets this with B32-Run to initialize port.
    */
    if (cond & NI20CO_DIS) {
	if (ni->ni_state == NI20_ST_RUNENA)
	    ni20_disable(ni);		/* This turns on "disable complete" */
	else
	    ni->ni_lhcond |= NI20CI_DCP; /* Turn on "disable complete" */
    } else
	ni->ni_lhcond &= ~NI20CI_DCP;	/* Turn off "disable complete" */

    /* B31 - Enable.  KL sets this after initialized.
    **	Must evidently stay on in all CONOs to keep port working.
    */
    if (cond & NI20CO_ENA) {
	/* Only has effect if running and disabled */
	if (ni->ni_state == NI20_ST_RUN)
	    ni20_enable(ni);		/* Turns on "enable complete" */
	else
	    ni->ni_lhcond |= NI20CI_ECP;	/* Turn on "enable complete" */
    } else
	ni->ni_lhcond &= ~NI20CI_ECP;	/* Turn off "enable complete" */


    /* B33-35 - PI channel assignment (0 = none) */
    ni->ni_pia = cond & NI20CO_PIA;
    if (ni->ni_pia == ni->ni_ppia) {	/* If consistent, enable PI */
	ni->ni_pilev = (1 << (7- ni->ni_pia)) & 0177; /* 0 if PIA=0 */
    } else
	ni->ni_pilev = 0;		/* Else disable PI */

    if (ni->ni_dv.dv_pireq && (ni->ni_dv.dv_pireq != ni->ni_pilev)) {
	/* Changed PIA while PI outstanding; flush it, let picheck re-req */
	ni20_piclr(ni);			/* Clear it. */
    }

    if (cond & NI20CO_CQA) {	/* KL saying cmds now available? */
	if (ni->ni_state & NI20_STF_RUN) {
	    ni->ni_cmdqf = TRUE;
	    ni20_run(ni);	/* Check command queue for KL input! */
	}
    }

    /* Check for any changes to PI status */
    ni20_picheck(ni);
}

/* CONI 36-bit conds in
**	Args D
** Returns condition word
*/
static insdef_coni(ni20_coni)
{
    register w10_t w;
    register struct ni20 *ni = (struct ni20 *)d;

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_coni: %lo,,%lo]\r\n",
		(long)ni->ni_lhcond, (long)ni->ni_cond);

    LRHSET(w, ni->ni_lhcond, ni->ni_cond);

    /* KLNI.MEM p.73 claims that B26 (DME) is cleared by reading the
    ** CSR.  Dunno if this is necessary, but why not.
    */
    ni->ni_cond &= ~NI20CI_DME;

    return w;
}

/* DATAO word out
**	Args D, W
** Returns nothing
*/
static insdef_datao(ni20_datao)
{
    register struct ni20 *ni = (struct ni20 *)d;

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_datao: %lo,,%lo]\r\n",
		(long)LHGET(w), (long)RHGET(w));

    if (ni->ni_cond & NI20CO_SEB) {
	ni->ni_ebuf = w;	/* Write EBUF word, for whatever it's worth */
	return;
    }

    if (LHGET(w) & NI20DO_LRA) {	/* Load RAR? */
	ni->ni_rar = (LHGET(w) & (NI20DO_RAR | NI20DO_MSB)) >> 4;
    } else {
	/* Deposit micro-halfword into loc specified by RAR. */
	/* For now, nothing. */
    }
}

/* DATAI word in
**	Args D
** Returns data word
*/
static insdef_datai(ni20_datai)
{
    register struct ni20 *ni = (struct ni20 *)d;
    register w10_t w;

    if (ni->ni_cond & NI20CO_SEB) {	/* Read EBUF word? */
	w = ni->ni_ebuf;

    } else if (ni->ni_cond & NI20CO_LAR) {	/* Select LAR? */
	LRHSET(w, (0400000 | (ni->ni_lar << 5) | 07),
		H10MASK);
    } else {

	/* Read LH or RH of word addressed by RAR.
	** Perhaps eventually this will actually read back what DATAOs have
	** written, just for kicks.  For now, merely test for the addresses
	** holding the ucode version numbers.
	*/
	switch (ni->ni_rar) {
	case (NI20_UA_VER<<1)+1:
	    LRHSET(w, 0, ((NI20_VERMAJ)<<12) | ((NI20_VERMIN<<6)));
	    break;
	case (NI20_UA_EDT<<1)+1:
	    LRHSET(w, 0, (NI20_EDITNO<<6));
	    break;
	default:
	    op10m_setz(w);
	    break;
	}
    }

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_datai: %lo,,%lo]\r\n",
		(long)LHGET(w), (long)RHGET(w));
    return w;
}

/*
NI20 BEHAVIOR NOTES:

	On powerup, NI20 isn't running, CRAM is garbage.  CRAM is 4096
	words of 60 bits.

	T20 starts SYSTEM:KNILDR.EXE which does explicit I/O instrs to
	load the CRAM.  NI isn't started.

	NI20 is later started up by T20 by setting CO_DIS and CO_MRN while
	RAR is 0.  Apparently, when proc state is changed from stopped to
	running, it starts at RAR, which amounts to PC.

	Starting at 0 causes it to do a channel transfer; monitor must have
	set up a 3-word transfer where the 3 words are
		<phys addr of PCB>
		<PIA assignment>
		<Interrupt vector assignment>

	The "disable-done" flag is set when all's ready.  T20 waits for 5 sec.

	"Enable" transitions to actual processing while running.
	"Enable-done" ACKs this.  T20 waits for 5 sec.

My guess as to the reason for specifying the PIA twice (once in the CONO
bits, again in the PCB/initial-3-words) is as follows:
	CONO PIA applies to interrupting the processor whenever any
	"interrupt-KL" bits are set in the CSR.

	The PCB PIA applies to any functions that the port wants to carry
	out using non-standard API function words; specifically, INC/DEC
	of queue locks.

So it would be possible for the CONO bits to be turned off while still
allowing the port to function.


	DISABLING/STOPPING: When T20 stops the NI it first disables the
port and waits for "disable complete" to show up (with a 5 sec timeout)
before actually halting it by turning off the CO_MRN bit.
	Now, T20 exhibits a possible bug whereby it only has one queue
entry for each of the RDNSA, LDPTT, and LDMCAT commands, which it forgets
about if they are put on the command queue and never make it back to the
response queue.  If the NI is stopped while they're on the command queue,
it can *never* be properly restarted because the T20 code will never find
those entries again and thus never re-initialize properly (symptom is
that first CO_CQA->cmdchk given on startup will find an empty cmd queue).
	So, disabling the port should probably try hard to clear
up the command queue in any way possible -- simply drop the SND DGMS and
execute the rest.  (Obviously if the response Q is locked, this can't be
done, but try anyway).


QUEUE LENGTH notes:

	It doesn't appear as if the queue entry length field is actually
used by the NIA20, or at least not the way the spec describes it.  The
only place where it seems relevant is when the NIA20 builds a DGRECV
entry, and the value of that field is set by the T20 driver to the
length in bytes of the data portion of the first BSD!  See next note.

RECEIVE notes:

	The KLNI.MEM spec is extremely unclear on how receive datagrams
are put together.

	The protocol type queues have entries built with their RDPBA
field set pointing to a BSD.  The size of this buffer seems to be
the same as that in the (misused) queue header QHLEN field.
	The BDSBA and BDSLN fields are also set for the first BSD.  It
appears that only one BSD is ever set up, and the NIA20 is expected to
dump its data into that one BSD.  So, it looks like the NIA20 must:
	(1) Find the appropriate protocol queue (does it really use
	"unknown" if there is none?)
	(2) Pluck an entry off that queue, examine its RD_PBA field
	to find the BSD header address.
	(3) Use the BSD's BD_SBA and BD_SLN fields to dump the data
	portion of the ethernet packet.
	(4) Fill out RD_DA1, RD_SA1, RD_SIZ, RD_PTY as per spec.
	(5) Hand to driver (link into response queue).

On receive the driver always expects to see BSD format and doesn't
check the flag byte.  The fields read by the T20 driver are:
	RDDA1, RDSA1	- passed on to portal user
	RDSIZ		- always has 4 subtracted (for CRC)
	RDPTY		- ptcl type as received off wire (T20 driver swaps
			bytes before passing on to portal user)
	CMERC		- To see if error

If portal defined to be using padding, driver checks 1st 2 bytes of data
buffer to pluck out "data length" field (bytes swapped) and skip the
portal-user BP over it.  This code doesn't even bother to track down the
pointer to the BSD header, it simply references the BSD's SBA directly
because it knows where it's located in the DGRECV entry.

	A few other driver-defined RD fields are used which the NIA20
	doesn't know about.  These identify a portal (RDPID), virtual
	address of its BSD buffer (RDVBA)

Packet length field (RD_SIZ) is always set to actual dgm length plus
4, even though buffer itself is never overfilled (presumably this generates
an error of type 31, "queue length violation").


LOOPBACK NOTES:

	The KLNI is effectively never allowed to receive any packets that
it transmits!!! (These are called, by the way, "echo" packets.)

	This happens because the KLNI only has one set of CRC logic,
shared between receive and transmit.  Any attempt to generate CRC for
an outgoing packet will cause a CRC error when that packet is received
by the KLNI.  When the KLNI detects an incoming CRC it checks whether
the monitor wants to accept CRC-error packets or not; if yes, it is put
on the unknown-PTT queue.  If not, it is discarded without notification.
Neither TOPS-10 nor TOPS-20 appear to ever use this capability, although
I'm not completely sure.
	The workaround for this CRC lossage is for the monitor to
compute the CRC itself, append it to the data, and set the
NI20_CMF_CRC flag in the SEND DGM command which means "CRC included";
then the KLNI doesn't do an outgoing CRC and can apply its logic to
the incoming CRC.
	However, neither TOPS-10 nor TOPS-20 ever set this flag or
generate their own CRC.  Thus, neither expect to ever see datagrams
that they send out themselves!

	Emulating this behavior with a dedicated interface is easy
enough -- just check the source address of all received packets and
throw away the ones that match our own.  Doing this with a shared
interface is rather more difficult if the hardware or OS allows
internal loopback (normally a good thing).  Note that there could
be an unknown number of packets buffered in both directions
between the NI emulation and the actual hardware.

One possible method:
    When enabled,
	- Keep a small ring buffer of outgoing possible-echo packets.
		Save header & a digest/checksum for matching up.
	- When a packet is received, check its source.  If us, then
	    scan ring buffer for a match.  If found, flush.
	- Note that the only totally foolproof matchup method is to keep
		around the complete packet until it's no longer needed.
	- To determine when "no longer needed", use periodic timer to
		reap buffer (remember range to flush at each tick).

An enterprising variation would be to send out some initial
self-addressed or broadcast packets and see if they were received or
not.  If not, no echo checking is necessary and it can be turned off.

MCAT notes:

	The T20 driver does its own check of multi-cast datagrams it
receives, as if it doesn't trust the port to do the filtering properly!
Bizarre.

*/

/* NI20 internal routines (internal registers etc) */

/* NI20_CLEAR - Clear NI port
**	If device subproc is running, stop and kill it via ni20_stop.
*/
static void
ni20_clear(register struct ni20 *ni)
{
    if (ni->ni_dv.dv_pireq)		/* If PI request outstanding, */
	ni20_piclr(ni);			/* clear it. */

    /* Stop and flush any in-progress xfer? */

    /* Need to figure out just which bits get cleared. */
    if (ni->ni_state & NI20_STF_RUN)
	ni20_stop(ni);			/* Stop NI if running */
    ni->ni_state = NI20_ST_UNINT;	/* Force known state - RESET */

    ni->ni_lhcond = NI20CI_PPT	/* Reset LH CONI bits - say NI Port present */
			| NI20CI_PID;	/* Say port type ID = 7 */
    ni->ni_cond = 0;		/* Reset all RH CONI bits */
    ni->ni_pia = 0;
    ni->ni_rar = 0;		/* Clear RAR */
    ni->ni_lar = 0;		/* May as well do the rest... */
    op10m_setz(ni->ni_ebuf);

    /* ni_ethadr is at bind time to a default value, then we hope it's
    ** later set by ni20_iniable().
    */
    ni->ni_staflgs = 0;		/* Station flags (NI20_RSF_*) */
    ni->ni_retries = 5;		/* # retries (default 5, T20 sets to 16) */

    ni->ni_pcba = 0;		/* Phys addr of PCB */
    ni->ni_pcbvp = NULL;	/* Ptr to actual PCB memory */
    ni->ni_ppia = 0;		/* Port PIA */
    op10m_setz(ni->ni_ivec);	/* Port interrupt vector */
    ni->ni_upqelen = 0;		/* Unknown Ptcl Queue Entry len, in words */
    ni->ni_pttvp = NULL;	/* Ptr to PTT in 10 memory */
    ni->ni_mcatvp = NULL;	/* Ptr to MCAT in 10 memory */
    ni->ni_rcbvp = NULL;	/* Ptr to counters in 10 memory */

    dchn_clear(&ni->ni_dc);	/* Data Channel vars */

    ni->ni_istate = 0;		/* Internal state for timeouts etc */
    ni->ni_cmdqf = FALSE;	/* TRUE if want to check cmd queue */
    ni->ni_qepa = 0;		/* Active queue entry (needs relinking) */
    ni->ni_qhpa = 0;		/* Active queue header (relink here) */
    ni->ni_pktinf = FALSE;	/* TRUE if have packet input waiting */

    ni->ni_nptts = 0;		/* # of valid entries in PTT */
    ni->ni_nmcats = 0;		/* # of valid entries in MCAT */

    memset((char *)(ni->ni_cnts), 0, sizeof(ni->ni_cnts));

    clk_tmrquiet(ni->ni_chktmr);	/* Force timer to be quiescent */
    ni->ni_docheck = FALSE;
}

/* NI20_PICHECK - Check NI20 conditions to see if PI should be attempted.
*/
static void
ni20_picheck(register struct ni20 *ni)
{
    /* If any possible interrupt bits are set */
    if ((ni->ni_lhcond & (NI20CI_CPE | NI20CI_MBE))	/* LH err bits */
      || (ni->ni_cond & (NI20CI_EPE | NI20CI_DME	/* RH err bits */
		| NI20CI_FQE | NI20CI_RQA))) {		/* RH norm int bits */

	ni20_pi(ni);
	return;
    }

    /* Here, shouldn't be requesting PI, so if our request bit is set,
    ** turn it off.
    */
    if (ni->ni_dv.dv_pireq) {		/* If set while shouldn't be, */
	ni20_piclr(ni);			/* Clear it! */
    }
}


/* NI20_PI - trigger PI for selected NI20.
**	This could perhaps be an inline macro, but for now
**	having it as a function helps debug.
*/
#if 1
static int savedebug = 0;
#endif

static void
ni20_pi(register struct ni20 *ni)
{
    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_pi: %o]", ni->ni_pilev);

    if (ni->ni_pilev			/* If have non-zero PIA */
      && !(ni->ni_dv.dv_pireq)) {	/* and not already asking for PI */
	(*ni->ni_dv.dv_pifun)(&ni->ni_dv, ni->ni_pilev); /* then do it! */
#if 1
	if (NIDEBUG(ni)) {
	    savedebug = cpu.mr_debug;
	    cpu.mr_debug = 1;		/* Temp hack - get more info */
	}
#endif

    }
}

/* NI20_PICLR - Clear PI for selected NI20.
**	This would normally be inline code, but want it a fn for debugging.
*/
static void
ni20_piclr(register struct ni20 *ni)
{
    if (NIDEBUG(ni)) {
	fprintf(NIDBF(ni), "[ni20_piclr:%o]", ni->ni_dv.dv_pireq);
#if 1
	cpu.mr_debug = savedebug;	/* Temp hack - restore flag */
#endif
    }

    (*ni->ni_dv.dv_pifun)(&ni->ni_dv, 0);
}

static void
ni20_cperr(register struct ni20 *ni,
		       int err)	/* 12-bit PC to indicate specific error */
{
    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_cperr: %o]", err);

    ni->ni_lar = err;			/* Set LAR to error-code PC */
    ni->ni_lhcond |= NI20CI_CPE;	/* Say CRAM Parity Error */
    ni20_stop(ni);			/* Stop NI */
    ni20_pi(ni);			/* Invoke PI */
}

/* Misc auxiliaries for shuffling data */

/* Convert ethernet addr from 6-byte array to PDP-10 doubleword format
*/
static void
ni_ethtodw(dw10_t *da, register unsigned char *ea)
{
    register dw10_t d;

    LRHSET(d.w[0], ((ea[0]&0377)<<10) | ((ea[1]&0377)<<2) | ((ea[2]>>6)&03),
		((ea[2]&077)<<12) | ((ea[3]&0377)<<4));
    LRHSET(d.w[1], ((ea[4]&0377)<<10) | ((ea[5]&0377)<<2), 0);
    *da = d;
}

/* Convert ethernet addr from PDP-10 doubleword format to 6-byte array
*/
static void
ni_dwtoeth(register unsigned char *ea, register dw10_t d)
{
    ea[0] = LHGET(d.w[0])>>10;
    ea[1] = (LHGET(d.w[0])>>2) & 0377;
    ea[2] = ((LHGET(d.w[0])<<6) | (RHGET(d.w[0])>>12)) & 0377;
    ea[3] = (RHGET(d.w[0])>>4) & 0377;
    ea[4] = LHGET(d.w[1])>>10;
    ea[5] = (LHGET(d.w[1])>>2) & 0377;
}

/* Copy byte array into PDP-10 words
*/
static void
ni_8stows(register vmptr_t vp,
	  register unsigned char *ucp,
	  register unsigned int bcnt)
{
    for (; bcnt >= 4; bcnt -= 4, ucp += 4, vp = vm_padd(vp,1)) {
	vm_psetxwd(vp,
		((ucp[0]&0377)<<10) | ((ucp[1]&0377)<<2) | ((ucp[2]>>6)&03),
		((ucp[2]&077)<<12) | ((ucp[3]&0377)<<4));
    }
    if (bcnt) {
	switch (bcnt) {
	case 3:
	    vm_psetxwd(vp,
		((ucp[0]&0377)<<10) | ((ucp[1]&0377)<<2) | ((ucp[2]>>6)&03),
		((ucp[2]&077)<<12));
	    break;
	case 2:
	    vm_psetxwd(vp,
		((ucp[0]&0377)<<10) | ((ucp[1]&0377)<<2),
		0);
	    break;
	case 1:
	    vm_psetxwd(vp,
		((ucp[0]&0377)<<10),
		0);
	    break;
	}
    }
}

/* Copy PDP-10 words into byte array
*/
static void
ni_wsto8s(register unsigned char *ucp,
	  register vmptr_t vp,
	  register unsigned int bcnt)
{
    register w10_t w;

    for (; bcnt >= 4; bcnt -= 4, vp = vm_padd(vp,1)) {
	w = vm_pget(vp);
	*ucp++ = LHGET(w)>>10;
	*ucp++ = (LHGET(w)>>2) & 0377;
	*ucp++ = ((LHGET(w)<<6) | (RHGET(w)>>12)) & 0377;
	*ucp++ = (RHGET(w)>>4) & 0377;
    }
    if (bcnt) {
	w = vm_pget(vp);
	switch (bcnt) {
	case 3:
	    ucp[2] = ((LHGET(w)<<6) | (RHGET(w)>>12)) & 0377;
	case 2:
	    ucp[1] = (LHGET(w)>>2) & 0377;
	case 1:
	    ucp[0] = LHGET(w)>>10;
	}
    }
}

/* NI20_START - Starts NI running
**	If start address is 0, assumes running normal program.
**	Otherwise, fails with an error.
*/
static void
ni20_start(register struct ni20 *ni)
{
    register w10_t w;
    register vmptr_t vp;
    register int res;

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_start: RAR %lo",(long)ni->ni_rar);

    if (ni->ni_rar != 0) {
	/* Handle abnormal start address by giving error */
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), " (bad)]\r\n");
	ni20_cperr(ni, NI20_CPE_INTERR);	/* Say "internal error" */
	return;
    }

    /* Normal start; gobble initial data from channel setup.
    ** Should be 3 words.
    */
    ni->ni_dc.dc_sts = CSW_CLRSET;	/* Clear channel status */
    ni->ni_dc.dc_wrt = 0;		/* Want to read, not write */
    if (!dchn_ccwget(&ni->ni_dc)) {
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), ", ccwget failed]\r\n");
	ni20_cperr(ni, NI20_CPE_CHNERR);	/* Say "channel error" */
	return;
    }
    if (ni->ni_dc.dc_wcnt < 3) {	/* Want 3 words */
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), ", ccnt=%d bad]\r\n", (int)ni->ni_dc.dc_wcnt);
	ni20_cperr(ni, NI20_CPE_CHNERR);	/* What else to do? */
	return;
    }

    /* Gobble the 3 words:
    **		0: <phys addr of Port Control Block (PCB)
    **		1: <PIA for port internal API fns>
    **		2: <Interrupt vector (shd be 0)>
    */
    vp = vm_physmap(ni->ni_dc.dc_buf);
    ni->ni_ppia = vm_pgetrh(vp+1) & 07;	/* Get port's PIA from wd 1 */
    ni->ni_ivec = vm_pget(vp+2);	/* Get port's intvec from wd 2 */
    if (op10m_skipn(ni->ni_ivec)) {
	/* Warn if this is ever non-zero, but keep going. */
	fprintf(NIDBF(ni), "[ni20_start: PCB IVEC non-zero: %lo,,%lo]\r\n",
			(long)LHGET(ni->ni_ivec), (long)RHGET(ni->ni_ivec));
    }
    ni->ni_pivec = EPT_PI0 + (2 * ni->ni_ppia);	/* See ni20_pifnwd */
    if (ni->ni_pia == ni->ni_ppia) {		/* If consistent, enable PI */
	ni->ni_pilev = (1 << (7- ni->ni_pia)) & 0177; /* 0 if PIA=0 */
    } else
	ni->ni_pilev = 0;			/* Else disable PI */

    w = vm_pget(vp);			/* Get word 0 */
    ni->ni_pcba = ((((paddr_t)LHGET(w))<<18) | RHGET(w)) & MASK22;
    ni->ni_pcbvp = vm_physmap(ni->ni_pcba);	/* Get ptr to PCB */

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), ", pcba=%lo pia=%o ivec=%lo,,%lo",
			(long)ni->ni_pcba, ni->ni_pia,
			(long)LHGET(ni->ni_ivec), (long)RHGET(ni->ni_ivec));

    /* Now have pointer to PCB, gobble up its contents into internal vars
    ** if necessary.  Following the KLNI.MEM spec, 3 things are definitely
    ** cached: Unknown ptcl QE length, PTT addr, MCAT addr.
    */

    /* KLNI.MEM appears to be wrong.  TOPS-10 expects to be able to
    ** set the PTT and possibly the MCAT and RCB pointers later.
    ** So for now, these pointers are cleared, and set from PCB only
    ** when the appropriate command is given.
    **
    ** The KNI ucode appears to cache them when enabled.  It also
    ** reads the MCAT and PTT at that time, as well.
    */
#if 0
    ni->ni_upqelen = vm_pgetrh(vm_padd(ni->ni_pcbvp, NI20_PB_UQL));

    w = vm_pget(vm_padd(ni->ni_pcbvp, NI20_PB_PTT));
    ni->ni_pttvp = vm_physmap(((((paddr_t)LHGET(w))<<18)|RHGET(w)) & MASK22);
    w = vm_pget(vm_padd(ni->ni_pcbvp, NI20_PB_MTT));
    ni->ni_mcatvp = vm_physmap(((((paddr_t)LHGET(w))<<18)|RHGET(w)) & MASK22);
    w = vm_pget(vm_padd(ni->ni_pcbvp, NI20_PB_RCB));
    ni->ni_rcbvp = vm_physmap(((((paddr_t)LHGET(w))<<18)|RHGET(w)) & MASK22);
#else
    ni->ni_pttvp = NULL;
    ni->ni_mcatvp = NULL;
    ni->ni_rcbvp = NULL;
#endif


    /* Start subproc here, or wait for enabling? */
#if KLH10_DEV_DPNI20
    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), " - starting DP \"%s\"...",
				ni->ni_dpname);

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
    res = dp_start(&ni->ni_dp, ni->ni_dpname);
    clk_resume();			/* Resume internal clock if one */

    if (!res) {
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), " failed!]\r\n");
	else
	    fprintf(NIDBF(ni), "[ni20_start: Start of DP \"%s\" failed!]\r\n",
				ni->ni_dpname);
	ni20_cperr(ni, NI20_CPE_SLFTST);	/* Startup self-test failed */
	return;
    }
    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), " started!]\r\n");

    /* Hack!  Because TOPS-10 and to a lesser extent TOPS-20 are unduly
    ** sensitive to the length of time needed for the NI to come up (both
    ** have a 5-second timeout), sometimes a total block of the CPU is
    ** necessary in order to give the dpni20 subproc enough time to come up
    ** up successfully.
    */
    if (ni->ni_dpidly)
	os_sleep(ni->ni_dpidly);	/* Sleep for this many seconds */
#else
    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "]\r\n");
#endif

    /* Set state to "running", assume disabled.
    */
    ni->ni_state = NI20_ST_RUN;		/* Running disabled */
}

/* NI20_STOP - Stops NI
*/
static void
ni20_stop(register struct ni20 *ni)
{
    ni->ni_cond &= ~NI20CO_MRN;		/* Ensure run bit off */
    if (!(ni->ni_state & NI20_STF_RUN))	/* If not running, */
	return;				/* that's all. */

    ni->ni_state = NI20_ST_HALT;	/* Say stopped */

    /* Stop stuff... */
#if KLH10_DEV_DPNI20
    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_stop: stopping...");

    if (ni->ni_dp.dp_chpid) {
	dp_stop(&ni->ni_dp, 1);	/* Say to kill and wait 1 sec for synch */
    }

    ni->ni_dpstate = FALSE;	/* No longer there and ready */
    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), " stopped]\r\n");
#endif
}

/* NI20_ENABLE - Enables running NI to start handling packets
*/
static void
ni20_enable(register struct ni20 *ni)
{
    register w10_t w;

    /* Reset delay for initial RDNSA command, if any.  For T10. */
    ni->ni_c3dlyct = ni->ni_c3dly;

    /* Start subproc now if not already there */
#if KLH10_DEV_DPNI20
    /* If subproc hasn't finished initializing yet, wait for it before
    ** changing state.  When DPNI_INIT is received, ni20_able() will be
    ** invoked to carry out state change.
    */
    if (!ni->ni_dpstate) {
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), "[ni20_enable: waiting for DP]");
	return;
    } else
#endif
    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_enable:]");


    /* It turns out that contrary to the KLNI.MEM spec, the actual
    ** NI ucode caches this info when it's enabled, not when the
    ** NI starts running.
    ** Cached info is:
    **	NI20_PB_UQL - length of unknown protocol queue entry (for what?)
    **	NI20_PB_PTT - PTT address (and does initial LDPTT, with no response)
    **	NI20_PB_MTT - MCAT address (and does initial LDMCAT, with no resp)
    **	NI20_PB_LAD - addr of channel logout word 1 (for what? not used)
    **	NI20_PB_RCB - RCB address
    */
    ni->ni_upqelen = vm_pgetrh(vm_padd(ni->ni_pcbvp, NI20_PB_UQL));

    w = vm_pget(vm_padd(ni->ni_pcbvp, NI20_PB_PTT));
    if (op10m_skipe(w))
	fprintf(NIDBF(ni), "[ni20_enable: WARNING: zero PTT ptr]");
    ni->ni_pttvp = vm_physmap(((((paddr_t)LHGET(w))<<18)|RHGET(w)) & MASK22);
    ni20_ldptt(ni);		/* Load PTT */

    w = vm_pget(vm_padd(ni->ni_pcbvp, NI20_PB_MTT));
    if (op10m_skipe(w))
	fprintf(NIDBF(ni), "[ni20_enable: WARNING: zero MCAT ptr]");
    ni->ni_mcatvp = vm_physmap(((((paddr_t)LHGET(w))<<18)|RHGET(w)) & MASK22);
    ni20_ldmcat(ni);		/* Load MCAT, may generate DP cmd */

    w = vm_pget(vm_padd(ni->ni_pcbvp, NI20_PB_RCB));
    if (op10m_skipe(w))
	fprintf(NIDBF(ni), "[ni20_enable: WARNING: zero RCB ptr]");
    ni->ni_rcbvp = vm_physmap(((((paddr_t)LHGET(w))<<18)|RHGET(w)) & MASK22);


    /* All's cached, finished with enable */

    ni->ni_state = NI20_ST_RUNENA;	/* Say running enabled */
    ni->ni_lhcond |= NI20CI_ECP;	/* Set "enable complete" */
}

/* NI20_DISABLE - Tells running NI to stop handling packets
*/
static void
ni20_disable(register struct ni20 *ni)
{
    if (ni->ni_state != NI20_ST_RUNENA)
	return;

#if KLH10_DEV_DPNI20
    /* If subproc hasn't finished initializing yet, wait for it before
    ** changing state.  When DPNI_INIT is received, ni20_able() will be
    ** invoked to carry out state change.
    */
    if (!ni->ni_dpstate) {
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), "[ni20_disable: waiting for DP]");
	return;
    } else
#endif
    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_disable:Beg]");

    /* Want to stop receiving dgms while still processing command
    ** queue, so as to get it all flushed.
    */
    ni->ni_state = NI20_ST_RUN;		/* Say running disabled, to stop inp */

    /* Copy check from CONO */
    if (ni->ni_cond & NI20CO_CQA)	/* KL saying cmds now available? */
	ni->ni_cmdqf = TRUE;

    ni20_run(ni);			/* Hope this all finishes */

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_disable:End]\r\n");

    ni->ni_lhcond |= NI20CI_DCP;	/* Set "disable complete" */
}

#if KLH10_DEV_DPNI20
/* NI20_INIABLE - Invoked when DPNI_INIT is received from dev subproc,
**	indicating that it's ready for action.
**	Here we check current CONI flags to see if 10 is asking for
**	the NI20 to finish becoming enabled or disabled, and finish it.
*/
static void
ni20_iniable(register struct ni20 *ni)
{
    register struct dpni20_s *dpni = (struct dpni20_s *) ni->ni_dp.dp_adr;

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_iniable!]");

    /* DP now initialized, fetch any state we're waiting for. */
    ni->ni_dpstate = TRUE;		/* DP is now initialized! */
    memcpy(ni->ni_ethadr, dpni->dpni_eth, 6);	/* Get true ethernet addr! */
    ni_ethtodw(&ni->ni_dwethadr, ni->ni_ethadr); /* also in PDP-10 fmt */
    /* Maybe later copy back dpni_ifnam also? */

    /* Now check CONI flags, see what to do */
    if (ni->ni_cond & NI20CO_MRN) {	/* Wants us to be running */
	if ((ni->ni_cond & NI20CO_DIS)		/* Wants to be disabled */
	  && !(ni->ni_lhcond & NI20CI_DCP)) {	/* and not complete yet */
	    ni20_disable(ni);			/* then disable it! */
	} else if ((ni->ni_cond & NI20CO_ENA)	/* Wants to be enabled */
	  && !(ni->ni_lhcond & NI20CI_ECP)) {	/* and not complete yet */
	    ni20_enable(ni);			/* then enable it! */
	} else
	    return;
	ni20_picheck(ni);	/* If either change happened, check PI */
    }
}
#endif /* KLH10_DEV_DPNI20 */

/* NI20_CMDCHK - Checks for input on the command queue, and executes
	whatever's there.  Does as much as it can without blocking.

	Question - delink 1st command, or leave on queue until action
is commited?  Latter allows continuing from aborts since next time
will just restart same command.

	Note that the real NI20 locks a queue only when taking an
entry off or putting it on.  In between, while being processed, the
queues are unlocked, the entry is in limbo, and nothing (as far as I
know) points to it!

	Problem - what happens if the appropriate free queue is
already locked??  Rather than trying to preserve state and wait for
next wakeup, find out beforehand which queue to put packet on and then
punt if it's locked, before doing any other processing.

	Rule for determining where the entry is relinked is:
(1) If a response is requested, *OR* there was an error in processing the
	command, it goes at the end of the Response Queue.
(2) Otherwise:
	If command was SNDDG and the protocol type exists in the PTT, it
		is linked onto the end of the appropriate free queue.
	Otherwise (not SNDDG, or unknown type), it is linked onto the
		end of the Unknown Protocol Type free queue.

This is complicated enough (and, for the case of errors, unpredictable
enough) that perhaps it would be best after all to have a way of remembering
a "waiting-to-relink-entry" state.

*/

/* Macro to build error status byte for returning from nicmd_* functions */
#define ni_errbyte(sri,err) (((sri) ? (NI20_CMF_SRI>>10) : 0) \
		| (((err)&(NI20_CMF_ERR>>11))<<1) | (NI20_CMF_ERF>>10))

static int
ni20_cmdchk(register struct ni20 *ni)
{
    register paddr_t qh, qe;
    register vmptr_t qep;
    register w10_t w;
    register int i;

#if 0
    /* Check for possible delay, to help T10.  Ugh. */
    if (ni->ni_inidlyct > 0) {	/* Finished with initial delay? */
	/* Nope, still doing delay */

	ni->ni_inidlyct--;
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni),
		    "[ni20_cmdchk: dly=%ld]", (long) ni->ni_inidlyct);
	/* Set up timer */
	if (!ni->ni_docheck) {		/* Try again later */
	    ni->ni_docheck = TRUE;		/* Say timer active */
	    clk_tmractiv(ni->ni_chktmr);	/* Activate timer */
	}
	return 0;		/* Return incomplete */
    }
#endif

    qh = ni->ni_pcba + NI20_PB_CQI;
    qe = ni_qeget(ni, qh);	/* Get entry from command queue */

    if (!qe)
	return 0;		/* Already locked, punt for now */
    if (qe == 1) {		/* If nothing there, */
	ni->ni_cmdqf = FALSE;	/* tell top-level no more */
	return 1;		/* and pretend success.. */
    }

    /* Have entry, with lock still seized. */
#if 0
    op10m_seto(w);
    vm_pset(vm_physmap(qh + NI20_QH_IWD), w);	/* Set -1 to unlock */
#endif

    /* DANGER!  At this point queue entry isn't linked anywhere!
    ** Have to be sure that --nothing-- prevents us from either linking it back
    ** in, or setting it up in ni_qep and ni_qhp for ditto.
    */
    qep = vm_physmap(qe);
    w = vm_pget(qep + NI20_CM_CMD);	/* Get command opcode word */
    i = ((LHGET(w) & 03)<<6) | ((RHGET(w)>>12) & 077);	/* Get cmd */

    if (NIDEBUG(ni)) {
	fprintf(NIDBF(ni), "[ni20_cmdchk: cmd=%o wd=%lo,,%lo qe=%lo]\r\n",
		i, (long)LHGET(w), (long)RHGET(w), (long)qe);
    }

    qh = ni->ni_pcba + NI20_PB_UQI;	/* Unk-Ptcl is default relink queue */
    switch (i) {
    case NI20_OP_SND:		/* Send Datagram */
	i = nicmd_snddg(ni, qep);
	/* Do special stuff to find correct relink queue */
	if (i == 0) {
	    qh = ni_pttfind(ni,
		(int)vm_pgetrh(vm_padd(qep, NI20_CM_SNPTY)) & NI20_SNF_PTY);
	    if (!qh)
		qh = ni->ni_pcba + NI20_PB_UQI;	/* Unk-Ptcl is default */
	}
	break;
    case NI20_OP_LDM:		/* Load Multicast Address Table */
	i = nicmd_ldmcat(ni, qep);
	break;
    case NI20_OP_LDP:		/* Load Protocol Type Table */
#if 1
	/* Check for possible delay, to help T10.  Ugh.
	** ANF-10 has a 50-instruction window when starting up
	** (D8EONC between the calls to ETHSER and D8EOOF) during which
	** completion of the LDPTT sent by the ETHSER NU.OPN call will 
	** cause bad things to happen -- D8EOOF is invoked before it's
	** supposed to.  Bleah.
	*/
	while (ni->ni_c3dly) {		/* Doing delay for LDPTT? */
	    if (--(ni->ni_c3dlyct) < 0) {
		/* If counted out, succeed and reset counter */
		ni->ni_c3dlyct = ni->ni_c3dly;
		break;
	    }
	    /* Sigh, must delay. */
	    if (NIDEBUG(ni))
		fprintf(NIDBF(ni),
			"[ni20_ldptt: dly=%ld]", (long) ni->ni_c3dlyct);
	    /* Ensure timer set up */
	    if (!ni->ni_docheck) {		/* Try again later */
		ni->ni_docheck = TRUE;		/* Say timer active */
		clk_tmractiv(ni->ni_chktmr);	/* Activate timer */
	    }
	    /* Put command back at head of command queue!! */
	    (void) ni_qeunget(ni, 
			(ni->ni_pcba + NI20_PB_CQI), qe);
	    return 0;		/* Return incomplete */
	}
#endif
	i = nicmd_ldptt(ni, qep);
	break;
    case NI20_OP_RCC:		/* Read and Clear Counters */
	i = nicmd_rccnt(ni, qep);
	break;
#if 0
    case NI20_OP_RCV:		/* Datagram Received */
#endif
    case NI20_OP_WPL:		/* Write PLI */
	i = nicmd_wrtpli(ni, qep);
	break;
    case NI20_OP_RPL:		/* Read PLI */
	i = nicmd_rdpli(ni, qep);
	break;
    case NI20_OP_RSI:		/* Read Station Information */
	i = nicmd_rdnsa(ni, qep);
	break;
    case NI20_OP_WSI:		/* Write Station Information */
	i = nicmd_wrtnsa(ni, qep);
	break;
    default:
	fprintf(NIDBF(ni), "[ni20_cmdchk: Illop=%o wd=%lo,,%lo qe=%lo]\r\n",
		i, (long)LHGET(w), (long)RHGET(w), (long)qe);
	i = ni_errbyte(0, NI20_ERR_URC);	/* Unrecognized cmd */
	break;
    }

    /* Command done, see where to link it back.
    ** If error, or NI20_CMF_RSP flag set, always put on Response queue.
    ** Otherwise, use default queue (normally Unknown-Ptcl-Type except
    ** for SNDDG which may change it)
    */
    op10m_tlz(w, NI20_CMF_STSB);	/* Clear status byte */
    if (i)				/* Returning error status? */
	op10m_tlo(w, ((h10_t)i) << 10);	/* Set error */
    vm_pset(qep + NI20_CM_CMD, w);	/* Store word back */

    if (i || op10m_tlnn(w, NI20_CMF_RSP))	/* If err or Resp requested */
	qh = ni->ni_pcba + NI20_PB_RQI;	/* Force use of Response queue */

    return ni_qeput(ni, qh, qe);	/* Link to end of right queue! */
}

/* NICMD_SNDDG - Send Datagram
**	Note on protocol type: the 16-bit value in the SNDDG entry has
**	its two bytes in low-high order, rather than network wire order
**	(which has high byte first).  Sigh.
*/


#if !KLH10_DEV_NPNI20
  unsigned int ni20_slen;		/* # bytes */
  unsigned char ni20_sbuf[NI20_MAXPKTLEN];
#endif

static int
nicmd_snddg(register struct ni20 *ni, register vmptr_t qep)
{
    register unsigned char *ucp;
    int lhcmd;			/* LH of cmd word (flags) */
    unsigned int tlen;		/* Data length */
    unsigned int ptyp;		/* Protocol type */
    dw10_t dest;
    register w10_t w;

    /* Get entry's vitals into our locals */
    lhcmd = vm_pgetlh(vm_padd(qep, NI20_CM_CMD));	/* Get flags */
    tlen = vm_pgetrh(vm_padd(qep, NI20_CM_SNTXL)) & NI20_SNF_TXL;
    w = vm_pget(vm_padd(qep, NI20_CM_SNPTY));
    ptyp = ((LHGET(w)&03)<<14) | (RHGET(w)>>4);		/* PDP10 ptcl typ */
    dest = vm_pgetd(vm_padd(qep, NI20_CM_SNHAD));	/* Dest addr */

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[nicmd_snddg: tl=%d pt=0x%x dst=%lo,%lo,%lo]\r\n",
		tlen, ptyp,
		(long)LHGET(dest.w[0]), (long)RHGET(dest.w[0]),
		(long)LHGET(dest.w[1]));

    /* Do simple length checks here before attempting any copying. */
    if ((tlen < ETHER_MIN) && !(lhcmd & NI20_CMF_PAD)) {
	/* Frame Too Short, and padding not requested, so fail. */
	return ni_errbyte(1, NI20_ERR_FTS);	/* Note 1=err on xmit */
    }
    if (tlen > (ETHER_MTU-2)) {			/* Possible Too-Long error? */
	if ((tlen > ETHER_MTU)			/* Check for non-pad fail */
	  || (lhcmd & NI20_CMF_PAD)) {		/* Check for pad fail */

	    /* Frame Too Long */
	    return ni_errbyte(1, NI20_ERR_FTL);	/* Note 1=err on xmit */
	}
    }

#if KLH10_DEV_DPNI20
    /* Set up raw packet buffer using DP comm area.
    ** Should have already verified that sending is possible, but check;
    ** if can't, pretend data overrun error (which may actually only apply
    ** to input, oh well).
    */
    if (!dp_xstest(&(ni->ni_dp.dp_adr->dpc_todp))) {
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), "[nicmd_snddg: DP overrun!]");
	return ni_errbyte(1, NI20_ERR_DOV);
    }
    ucp = ni->ni_sbuf;

#else
    /* Set up raw packet buffer.  For now, simply clobber static area
    ** for easier debugging.
    ** Later, need to make sure we have one available before unlinking
    ** a SNDDG queue entry!
    */
    if (ni->ni_pktinf) {
	/* For now, if input loopback packet already there, pretend
	** data overrun error.  Sigh!
	*/
	return ni_errbyte(1, NI20_ERR_DOV);
    }
    ucp = ni20_sbuf;
#endif /* ! KLH10_DEV_DPNI20 */

    /* Set up header */
    ni_dwtoeth(ucp + ETHER_PX_DST, dest);	/* First goes dest addr */
    memcpy(ucp + ETHER_PX_SRC, ni->ni_ethadr, ETHER_ADRSIZ);	/* then src */
    ucp[ETHER_PX_TYP]   = (ptyp & 0377);	/* ptyp in wrong order */
    ucp[ETHER_PX_TYP+1] = ((ptyp>>8) & 0377);	/* So swap here */
    ucp += ETHER_PX_DAT;			/* Finally point to data */

    if (lhcmd & NI20_CMF_PAD) {		/* If doing padding... */
	*ucp++ = (tlen & 0377);		/* prepend 2 length bytes!  Low 1st */
	*ucp++ = (tlen >> 8) & 0377;	/* High 2nd */
    }

    if (lhcmd & NI20_CMF_BSD) {
	/* BSD format, must track down and copy buffers */
	register uint32 totlen = 0;

	w = vm_pget(vm_padd(qep, NI20_CM_SNBBA));	/* BSD base addr */
	/* KLNI.MEM says ptr field is 24 bits, but we'll use 22 for now */
	op10m_tlz(w, 0777760);				/* Leave low 22 bits */
	while (op10m_skipn(w)) {
	    register unsigned int blen;
	    register vmptr_t vp;

	    vp = vm_physmap(w10topa(w));	/* Point to BSD */
	    blen = vm_pgetrh(vm_padd(vp, NI20_BD_SLN)) & NI20_BDF_SLN;
	    if (blen) {
		if ((totlen += blen) > tlen)	/* Check for excessive len */
		    break;
		w = vm_pget(vm_padd(vp, NI20_BD_HDR));	/* Get wd with SBA */
		ni_wsto8s(ucp, vm_physmap(w10topa(w) & MASK22), blen);
		ucp += blen;
	    }
	    w = vm_pget(vm_padd(vp, NI20_BD_NXA));
	    /* KLNI.MEM says ptr field is 24 bits, but we'll use 22 for now */
	    op10m_tlz(w, 0777760);		/* Leave low 22 bits */
	}

	/* Done with buffer-copy loop, see if resulting total is right */
	if (totlen != tlen) {
	    /* Ugh, buffer length error!  Fail; later versions of code
	    ** may need to free up the packet buffer, when it stops
	    **  being static.
	    */
	    return ni_errbyte(1, NI20_ERR_BLV);	/* Note 1=xmit */
	}

    } else {
	/* Non-BSD format, data immediately follows in entry */
	ni_wsto8s(ucp, vm_padd(qep, NI20_CM_SNDTA), tlen);
	ucp += tlen;
    }

    /* One more check of pad flag */
    if (lhcmd & NI20_CMF_PAD) {
	tlen += 2;			/* Account for prepended len bytes */
	if (tlen < ETHER_MIN) {
	    memset(ucp, 0, ETHER_MIN - tlen);	/* Add zero padding */
	    tlen = ETHER_MIN;		/* Out to minimum data length */
	}
    }

    /* Packet buffer all set!
    ** Do magic to get it transmitted.
    */
    tlen += ETHER_HDRSIZ;		/* Send entire packet */
    ni->ni_cnts[NI20_RC_BX] += tlen;	/* Update # bytes and frames */
    ni->ni_cnts[NI20_RC_FX]++;
    if (op10m_tlnn(dest.w[0], 02000)) {	/* Check B7 - multicast bit */
	/* Gross kludge!  TOPS-20 always subtracts the # of multicasts it
	** sends from the NI20_RC_RF counter, apparently because the NI port
	** always incurs a receive-failure increment every time a multicast
	** packet is transmitted.  This is probably due to the CRC error
	** on self-addressed packets.
	** So, to keep users from gasping when they see negative
	** "receive failure" counts (using the NCP or IPHOST utilities),
	** this hack emulates the NI lossage.
	*/
	ni->ni_cnts[NI20_RC_RF]++;	/* Increment "receive failure" cnt */
    }

    /* One last thing -- ugly check for possible echo packet! */
    if (ni->ni_ecchk && ni->ni_ecblen	/* Doing echo-check buffering? */
      && (op10m_tlnn(dest.w[0], 02000)		/* If multicast, or to */
	  || ( op10m_came(ni->ni_dwethadr.w[0], dest.w[0])	/* self */
	    && op10m_came(ni->ni_dwethadr.w[1], dest.w[1])))) {
	ni_ecpstore(ni, 		/* Remember the packet! */
#if KLH10_DEV_DPNI20
		ni->ni_sbuf,
#else
		ni20_sbuf,
#endif
		tlen);
    }

#if KLH10_DEV_DPNI20
    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[nicmd_snddg: DP send: %d]", tlen);
    dp_xsend(&(ni->ni_dp.dp_adr->dpc_todp), DPNI_SPKT, (size_t)tlen);
#else
    /* For now, set up something to pretend received it on loopback?? */

    ni20_slen = tlen;
    ni->ni_pktinf = TRUE;

    if (!ni->ni_docheck) {
	ni->ni_docheck = TRUE;		/* Say to poll for input */
	clk_tmractiv(ni->ni_chktmr);	/* Activate timer */
    }
#endif /* ! KLH10_DEV_DPNI20 */

    return 0;		/* Success... */

}

/* Other misc commands from 10 */

/* NICMD_LDMCAT - Multicast Address Table
*/
static int
nicmd_ldmcat(register struct ni20 *ni, register vmptr_t qep)
{
    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[nicmd_ldmcat:]");
    ni20_ldmcat(ni);		/* Load MCAT table */
    return 0;			/* No errors */
}

static void
ni20_ldmcat(register struct ni20 *ni)
{
    register vmptr_t vp;
    register int i, n;

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_ldmcat: ");

    /* Set up to scan MCAT table from 10's memory, using previously
    ** cached MCAT pointer set at ENABLE time.  See ni20_enable().
    */
    if (!(vp = ni->ni_mcatvp))
	panic("ni20_ldmcat: null MCAT ptr");
    n = 0;
    for (i = 0; i < NI20_NMTT; ++i, vp = vm_padd(vp, NI20_MT_LEN)) {
	register dw10_t d;

	d = vm_pgetd(vp);
	if (op10m_trnn(d.w[1], NI20_MTF_ENA)) {
	    op10m_trz(d.w[0], 017);	/* Clear internal MBZ parts */
	    op10m_tlz(d.w[1], 03);
	    RHSET(d.w[1], 0);
	    ni->ni_mcat[n] = d;		/* Store in internal table */
	    if (NIDEBUG(ni))
		fprintf(NIDBF(ni), "(%d:%lo,%lo,%lo)", n,
			(long)LHGET(d.w[0]),
			(long)RHGET(d.w[0]),
			(long)LHGET(d.w[1]));
	    ++n;			/* Remember # we stored */
	}
    }
    ni->ni_nmcats = n;
    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), " = %d of %d entries enabled]\r\n", n, i);

#if KLH10_DEV_DPNI20
    /* Now attempt to tell the DP subproc about this new MCAT table, in case
    ** it's OK to clobber the hardware.
    ** Note that DP is always informed even if there are no entries, because
    ** something might have been deleted.  DP must figure out which by
    ** remembering previous state of table.  Sigh.
    */
  {
    register struct dpni20_s *dpni = (struct dpni20_s *) ni->ni_dp.dp_adr;

    if (n > DPNI_MCAT_SIZ)
	n = DPNI_MCAT_SIZ;
    for (i = 0; i < n; i++) {
	ni_dwtoeth(dpni->dpni_mcat[i], ni->ni_mcat[i]);
    }
    dpni->dpni_nmcats = n;

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_ldmcat: DP cmd SETMCAT]\r\n");
    dp_xsend(&(ni->ni_dp.dp_adr->dpc_todp), DPNI_SETMCAT, (size_t)0);

  }
#endif
}



/* NICMD_LDPTT - Protocol Type Table
**
** Note on protocol type:
**	It appears this is kept in low-high byte order, which is the
**	reverse of network order (which has high byte first).
**	For consistency, keep the 16-bit value in its PDP10 format.
*/
static int
nicmd_ldptt(register struct ni20 *ni, register vmptr_t qep)
{
    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[nicmd_ldptt:]");

    ni20_ldptt(ni);		/* Load PTT */
    return 0;			/* No errors */
}

static void
ni20_ldptt(register struct ni20 *ni)
{
    register vmptr_t vp;
    register int i, n;

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_ldptt: ");

    /* Set up to scan PTT table from 10's memory, using previously
    ** cached PTT pointer set at ENABLE time.  See ni20_enable().
    */
    if (!(vp = ni->ni_pttvp))
	panic("ni20_ldptt: null PTT ptr");

    n = 0;
    for (i = 0; i < NI20_NPTT; ++i, vp = vm_padd(vp, NI20_PT_LEN)) {
	register dw10_t d;

	d = vm_pgetd(vp);
	if (op10m_tlnn(d.w[0], NI20_PTF_ENA)) {
	    ni->ni_ptt[n].ptt_type =	/* Set type in internal table */
		((LHGET(d.w[0]) & 03)<<14) | (RHGET(d.w[0])>>4);
	    ni->ni_ptt[n].ptt_freeq = w10topa(d.w[1]);
	    if (NIDEBUG(ni))
		fprintf(NIDBF(ni), "(%d:0x%x,%lo)",
			n, ni->ni_ptt[n].ptt_type,
			(long)ni->ni_ptt[n].ptt_freeq);

	    ++n;			/* Remember # we stored */
	}
    }
    ni->ni_nptts = n;
    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), " = %d of %d entries enabled]\r\n", n, i);
}


/* NICMD_RCCNT - Read and Clear Counters
*/
static int
nicmd_rccnt(register struct ni20 *ni, register vmptr_t qep)
{
    register vmptr_t vp;
    register w10_t w;
    register int i;
    h10_t cmdlh = vm_pgetlh(vm_padd(qep, NI20_CM_CMD));

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[nicmd_rccnt:%s%s]\r\n",
			(cmdlh & NI20_CMF_RSP) ? " Read"  : "",
			(cmdlh & NI20_CMF_CLR) ? " Clear" : "" );

    /* KLNI.MEM claims counters are returned as part of response entry.
    ** However, PCB has pointer to a buffer of counters and other
    ** clues indicate that is where counts are returned.
    ** For now, go with T20 and update buffer, not response entry.
    */
    /* Also, note that the RCB pointer is cached at the same time as
    ** the PTT and MCAT pointers; see ni20_enable().
    */
    if (!(vp = ni->ni_rcbvp))
	panic("nicmd_rccnt: null RCB ptr");

    /* I wonder if the counts are only copied if CMF_RSP is given,
    ** or always?  Assuming always, and to RCB buffer...
    */
#if 0
    /* If wants a response, copy current counters */
    if (cmdlh & NI20_CMF_RSP)		/* Copy counters into response */
	for (vp = vm_padd(qep, NI20_CM_CMD+1),
#else
    for (vp = ni->ni_rcbvp,		/* Copy counters into RCB */
#endif
      i = 0; i < NI20_RCLEN; ++i, vp = vm_padd(vp, 1)) {
	LRHSET(w, (ni->ni_cnts[i] >> H10BITS)&H10MASK,
			(ni->ni_cnts[i]&H10MASK));
	vm_pset(vp, w);
    }


    /* If then wants to clear them, do so... */
    if (cmdlh & NI20_CMF_CLR) {
	memset((char *)(ni->ni_cnts), 0, sizeof(ni->ni_cnts));
    }

    return 0;			/* No errors */
}

/* NICMD_WRTPLI - Write PLI
*/
static int
nicmd_wrtpli(register struct ni20 *ni, register vmptr_t qep)
{
    fprintf(NIDBF(ni), "[nicmd_wrtpli: unimplem]\r\n");
    return ni_errbyte(0, NI20_ERR_INT);	/* Internal error */
}

/* NICMD_RDPLI - Read PLI
*/
static int
nicmd_rdpli(register struct ni20 *ni, register vmptr_t qep)
{
    fprintf(NIDBF(ni), "[nicmd_rdpli: unimplem]\r\n");
    return ni_errbyte(0, NI20_ERR_INT);	/* Internal error */
}

/* NICMD_RDNSA - Read Station Information
**	Note that the "version number" here is actually
**	the "edit number".  T20 ignores the RDNSA information, getting
**	its version/edit info directly from a CRAM read; T10 however
**	diligently checks the RDNSA result.
*/
static int
nicmd_rdnsa(register struct ni20 *ni, register vmptr_t qep)
{
    if (NIDEBUG(ni))
	fprintf(NIDBF(ni),
		"[nicmd_rdnsa: en=%lo,%lo,%lo f=%o ucv=%d #m=%d #p=%d]\r\n",
			(long)LHGET(ni->ni_dwethadr.w[0]),
			(long)RHGET(ni->ni_dwethadr.w[0]),
			(long)LHGET(ni->ni_dwethadr.w[1]),
			ni->ni_staflgs,
			NI20_EDITNO, NI20_NMTT, NI20_NPTT);

    if (vm_pgetlh(vm_padd(qep, NI20_CM_CMD)) & NI20_CMF_RSP) {
	register vmptr_t vp = vm_padd(qep, NI20_CM_CMD+1);
	register w10_t w;

	vm_psetd(vp, ni->ni_dwethadr);	/* Return our ethernet addr */
	LRHSET(w, 0, ni->ni_staflgs);
	vm_pset(vm_padd(vp, 2), w);
	LRHSET(w, (NI20_EDITNO>>6)&03,
		((NI20_EDITNO<<12)|(NI20_NMTT<<6)|NI20_NPTT) & H10MASK);
	vm_pset(vm_padd(vp, 3), w);
    }

    return 0;		/* No errors */
}

/* NICMD_WRTNSA - Write Station Information
*/
static int
nicmd_wrtnsa(register struct ni20 *ni, register vmptr_t qep)
{
    dw10_t newadr;
    int newflgs, newtries;
    register vmptr_t vp = vm_padd(qep, NI20_CM_CMD+1);

    newadr = vm_pgetd(vp);		/* Get desired ethernet addr */
    newflgs = vm_pgetrh(vm_padd(vp, 2)) & NI20_RSF_FLGMSK;
    newtries = vm_pgetrh(vm_padd(vp, 3)) & NI20_WSF_RTY;

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni),
		"[nicmd_wrtnsa: en=%lo,%lo,%lo f=%o t=%d]\r\n",
			(long)LHGET(newadr.w[0]),
			(long)RHGET(newadr.w[0]),
			(long)LHGET(newadr.w[1]),
			newflgs, newtries);

    /* Now attempt to set things according to furnished parameters */

    ni->ni_retries = newtries;	/* Basically ignore this parameter */

    if (newflgs & NI20_RSF_PMC) {
	fprintf(NIDBF(ni),
		"[ni20: Ignoring attempt to set promisc-multicast]\r\n");
	newflgs &= ~NI20_RSF_PMC;
    }
    if (newflgs & NI20_RSF_PRM) {
	fprintf(NIDBF(ni),
		"[ni20: Ignoring attempt to set promiscuous mode]\r\n");
	newflgs &= ~NI20_RSF_PRM;
    }
    ni->ni_staflgs = newflgs;

    /* Try to change ethernet address, but only if it's really changing. */
    if ( op10m_camn(ni->ni_dwethadr.w[0], newadr.w[0])
      || op10m_camn(ni->ni_dwethadr.w[1], newadr.w[1])) {
#if KLH10_DEV_DPNI20
	register struct dpni20_s *dpni = (struct dpni20_s *) ni->ni_dp.dp_adr;

	ni_dwtoeth(dpni->dpni_rqeth, newadr);	/* Set up new-address arg */
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), "[nicmd_wrtnsa: DP cmd SETETH]\r\n");
	dp_xsend(&(ni->ni_dp.dp_adr->dpc_todp), DPNI_SETETH, (size_t)0);
#else
	fprintf(NIDBF(ni),
		"[ni20: Ignoring attempt to set ethernet addr]\r\n");
#endif
    }
    return 0;		/* No errors */
}

/* Process received datagram.
**	Three possible outcomes:
**	1  - Flush dgm, keep going (all's been handled)
**	0  - Keep dgm, block & wait (qeget is blocked)
**	-1 - Flush dgm, block & wait (qeput is blocked)
**
** Note on protocol type:
**	It appears that the PDP10 16-bit value is in low-high byte order
**	rather than the network wire order of high byte first.
**	So the received type needs its bytes swapped before it can be
**	looked up.
*/
#define DGRCV_WONFLS	1
#define DGRCV_BLK	0
#define DGRCV_BLKFLS	-1

static int
nicmd_dgrcv(register struct ni20 *ni,
	    register unsigned char *ucp, /* Pointer to received datagram */
	    unsigned int blen)		/* # bytes in datagram */
{
    register vmptr_t qep, qhp;
    register w10_t w;
    paddr_t qh, qe;
    int lhcmd;			/* LH of cmd word (flags) */
    int ukptf;			/* Set TRUE if type unknown, not in PTT */
    unsigned int ptyp;		/* Protocol type */
    dw10_t ethadr;

    if (blen < ETHER_HDRSIZ) {
	/* Increment some error counter */
	ni->ni_cnts[NI20_RC_RF]++;	/* Receive failure */
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), "[nicmd_dgrcv: drop, len=%d < hdr]\r\n", blen);
	/* Throw packet away */
	return DGRCV_WONFLS;		/* Pretend success */
    }

    /* Now carry out received packet filtering per [KLNI.MEM 6.6]
    ** Eventually this should largely be done by the host platform's OS
    ** filtering.
    ** Note that we check for echo packets, if necessary for the particular
    ** hardware or config we're using, before doing anything else.  See
    ** explanation on comment page above titled "NI20 Behavior Notes".
    */
    ni_ethtodw(&ethadr, &ucp[ETHER_PX_DST]);	/* Get dest */
    if (ni->ni_ecchk			/* Must apply echo check? */
     && (memcmp(ni->ni_ethadr,		/* Yep, see if source addr is us */
		 &ucp[ETHER_PX_SRC], 6)==0)
     &&  (ni->ni_dedic				/* Yep, so if dedicated ifc */
       || ni_ecpcheck(ni, ucp, blen))) {	/* or we remember sending */
	if (NIDEBUG(ni))			/* then flush it! */
	    fprintf(NIDBF(ni),
		"[nicmd_dgrcv: filtered echo pkt: %lo,%lo,%lo]\r\n",
		(long)LHGET(ethadr.w[0]), (long)RHGET(ethadr.w[0]),
		(long)LHGET(ethadr.w[1]) );
	/* Throw packet away */
	return DGRCV_WONFLS;		/* Pretend success */
    }

    blen -= ETHER_HDRSIZ;	/* Now OK to get # bytes actual ether data */

    if (op10m_tlnn(ethadr.w[0], 02000)) {	/* Check B7 - multicast bit */
	/* Multicast packet.
	** Update count here for simplicity - but note count will be wrong
	** if we have to block later for lack of a receive queue.
	*/
	ni->ni_cnts[NI20_RC_MCB] += blen;	/* Update # bytes, frames */
	ni->ni_cnts[NI20_RC_MCF]++;
	if (ni->ni_staflgs & (NI20_RSF_PRM|NI20_RSF_PMC))
	    ;	/* Won - promiscuous */
	else if (op10m_came(ni20_dwbcadr.w[0], ethadr.w[0])	/* Bcast? */
	      && op10m_came(ni20_dwbcadr.w[1], ethadr.w[1]))
	    ;	/* Won - all-ones broadcast */
	else {
	    /* Grovel through MCAT table filtering */
	    register int i = ni->ni_nmcats;
	    while (--i >= 0) {
		if ( op10m_came(ni->ni_mcat[i].w[0], ethadr.w[0])
		  && op10m_came(ni->ni_mcat[i].w[1], ethadr.w[1])) {
		    break;
		}
	    }
	    if (i < 0) {
		/* Multicast packet failed */
		/* Bump some counter?  Nothing looks promising. */
		if (NIDEBUG(ni))
		    fprintf(NIDBF(ni),
			"[nicmd_dgrcv: MC filtered: %lo,%lo,%lo]\r\n",
			(long)LHGET(ethadr.w[0]), (long)RHGET(ethadr.w[0]),
			(long)LHGET(ethadr.w[1]) );
		/* Throw packet away */
		return DGRCV_WONFLS;		/* Pretend success */
	    }
	    ;	/* Won - enabled in MCAT */
	}
    } else {
	/* Normal packet, see if addressed to us (or if we're promiscuous) */
	if ( op10m_came(ni->ni_dwethadr.w[0], ethadr.w[0])
	  && op10m_came(ni->ni_dwethadr.w[1], ethadr.w[1]))
	    ;	/* Won - addressed to us */
	else if (ni->ni_staflgs & NI20_RSF_PMC)
	    ;	/* Won - promiscuous */
	else {
	    /* Non-multicast packet failed */
	    /* Bump some counter? */
	    if (NIDEBUG(ni))
		fprintf(NIDBF(ni),
			"[nicmd_dgrcv: filtered: %lo,%lo,%lo]\r\n",
			(long)LHGET(ethadr.w[0]), (long)RHGET(ethadr.w[0]),
			(long)LHGET(ethadr.w[1]) );
	    /* Throw packet away */
	    return DGRCV_WONFLS;		/* Pretend success */
	}
    }


    /* First find protocol type to see if a queue entry exists.
    ** In order to match up with the PTT, and for insertion into the DGRCV
    ** entry, the 2 type bytes must be kept in low-high order, even though
    ** this is the opposite of both the on-wire and PDP-10 order!  The
    ** monitor NI20 driver reswaps the bytes into high-low when it reads
    ** the DGRCV entry... sigh.
    */
    lhcmd = 0;
    ukptf = FALSE;
    ptyp = (ucp[ETHER_PX_TYP+1]<<8) | ucp[ETHER_PX_TYP];
    qh = ni_pttfind(ni, ptyp);
    if (!qh) {
	/* Should we turn it into error 12 (Unrec ptcl type, NI20_ERR_UPT)
	** if no match in PTT?
	** A: NO - in fact the NI20 ucode never generates
	** an error of this type!
	*/
	qh = ni->ni_pcba + NI20_PB_UQI;	/* Unk-Ptcl is default */
	ukptf = TRUE;			/* Remember it's unknown */
    }
    qhp = vm_physmap(qh);

    /* Now see if can grab a queue entry */
    if (!(qe = ni_qeget(ni, qh))) {
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), "[nicmd_dgrcv: pt=0x%x Q locked]\r\n", ptyp);

	return DGRCV_BLK;		/* Locked, so block and wait */
    }

    /* See if anything actually there */
    if (qe == 1) {
	if (!ukptf) {				/* If ptcl was enabled, */
	    ni->ni_cond |= NI20CI_FQE;		/* Trigger FreeQueueError */
	    ni20_pi(ni);

	    /* Here is where we should update the appropriate count for
	    ** this protocol type... it's enabled, but the free queue is empty.
	    */
	    ni->ni_cnts[NI20_RC_D01 + ni_ipttfind(ni, ptyp)]++;

	} else {
	    /* Ptcl not enabled and no unknown-ptcl queue for it */
	    ni->ni_cnts[NI20_RC_DUN]++;
	}
	/* Throw packet away */
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), "[nicmd_dgrcv: drop, pt=0x%x%s freeq empty]\r\n",
		ptyp, (lhcmd ? " (unk)" : ""));
	return DGRCV_WONFLS;			/* Pretend success */
    }
#if 0
    op10m_seto(w);
    vm_pset(qhp + NI20_QH_IWD, w);		/* Set -1 to unlock */
#endif
    /* DANGER!  At this point queue entry isn't linked anywhere!
    ** Have to be sure that --nothing-- prevents us from either linking it back
    ** in, or setting it up in ni_qep and ni_qhp for ditto.
    */

    /* Got entry, fill it out! */
    qep = vm_physmap(qe);
    LRHSET(w, 0, blen + ETHER_CRCSIZ);
    vm_pset(vm_padd(qep, NI20_CM_RDSIZ), w);		/* 16-bit RJ len */
    vm_psetd(vm_padd(qep, NI20_CM_RDDHA), ethadr);	/* Already have dest */
    ni_ethtodw(&ethadr, &ucp[ETHER_PX_SRC]);		/* Get source */
    vm_psetd(vm_padd(qep, NI20_CM_RDSHA), ethadr);
    LRHSET(w, (ptyp>>14) & 03, (ptyp&MASK14)<<4);	/* pt still swapped */
    vm_pset(vm_padd(qep, NI20_CM_RDPTY), w);		/* 16-bit type */

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[nicmd_dgrcv: tl=%d pt=0x%x src=%lo,%lo,%lo]\r\n",
		blen, ptyp,
		(long)LHGET(ethadr.w[0]), (long)RHGET(ethadr.w[0]),
		(long)LHGET(ethadr.w[1]));

    /* Now find BSD header address, which driver should have set up for
    ** us.  Gotta take it on faith here...
    */
    w = vm_pget(vm_padd(qep, NI20_CM_RDPBA));
    if (op10m_skipe(w)) {
	/* Boy are we gonna lose */
	fprintf(NIDBF(ni), "[nicmd_dgrcv: zero BSD!!!]\r\n");
	lhcmd = ((h10_t)ni_errbyte(0, NI20_ERR_QLV)) << 10;
	/* Some kind of error response return here */

    } else {
	register vmptr_t bdp;
	register unsigned int sln;

	bdp = vm_physmap(w10topa(w));
	sln = vm_pgetrh(vm_padd(bdp, NI20_BD_SLN)) & NI20_BDF_SLN;
	if (sln < blen) {
	    /* Say queue length violation (not enough room in buffer) */
	    lhcmd = ((h10_t)ni_errbyte(0, NI20_ERR_QLV)) << 10;
	} else
	    sln = blen;

	/* Copy the data! */
	ni_8stows(vm_physmap(MASK22 & w10topa(
				vm_pget(vm_padd(bdp, NI20_BD_HDR)))),
		&ucp[ETHER_PX_DAT], sln);
    }

    /* Queue entry (almost) all done!
    ** Always link it onto response queue.
    */
    LRHSET(w, lhcmd, ((NI20_OP_RCV&077)<<12));	/* May contain error */
    vm_pset(vm_padd(qep, NI20_CM_CMD), w);

    return ni_qeput(ni, ni->ni_pcba + NI20_PB_RQI, qe)
		? DGRCV_WONFLS : DGRCV_BLKFLS;		/* Link in, return */
}

static paddr_t
ni_pttfind(register struct ni20 *ni, register unsigned int ptyp)
{
    register struct niptt *ptt;
    register int i;

    /* KLNI.MEM claims PTT should be provided in order of increasing
    ** protocol type, to speed up elimination of unknown types.  However,
    ** this appears not to be the case for T20 at least, so we need to
    ** check all entries.
    ** ALSO: The free queue pointer points to the FLINK word, not to
    ** the interlock word!  Hence the adjustment to the return value so
    ** it's consistent with the other Queue Header pointers.
    */

    for (ptt = ni->ni_ptt, i = ni->ni_nptts; i > 0; --i, ++ptt) {
	if (ptyp == ptt->ptt_type)	/* If same type, */
	    return ptt->ptt_freeq-1;	/* won, return its queue! */
    }
    return 0;
}

/* Same as above routine but returns index instead, for benefit of
** code that wants to update counters.  Hope this doesn't happen often.
** Returns -1 if not found; this becomes "unknown".
*/
static int
ni_ipttfind(register struct ni20 *ni, register unsigned int ptyp)
{
    register struct niptt *ptt;
    register int i;

    for (ptt = ni->ni_ptt, i = 0; i < ni->ni_nptts; ++i, ++ptt) {
	if (ptyp == ptt->ptt_type)	/* If same type, */
	    return i;			/* won, return its index */
    }
    return -1;
}

#if 0 /* Not used */

static void
ni_errclr(unsigned int qe)
{
    register vmptr_t qep = vm_physmap(qe + NI20_CM_CMD);
    register w10_t w = vm_pget(qep);

    op10m_tlz(w, NI20_CMF_STSB);	/* Clear status byte */
    vm_pset(qep, w);			/* Store word back */
}

static void
ni_errset(unsigned int qe, int err)
{
    register vmptr_t qep = vm_physmap(qe);
    register w10_t w = vm_pget(qep + NI20_CM_CMD);

    /* Set up response packet */
    op10m_tlz(w, NI20_CMF_STSB);	/* Clear status byte */
    op10m_tlo(w, (((h10_t)err)<<10) | NI20_CMF_ERF);	/* Set error */
    vm_pset(qep + NI20_CM_CMD, w);	/* Store word back */
}

/* NI_ERRCMD - Generate error response for command.
**	Stuffs error info into command word of entry, and links it
**	into the Response queue.
*/
static int
ni_errcmd(register struct ni20 *ni, unsigned int qe, int err)
{
    ni_errset(qe, err);			/* Set up response packet */
    return ni_qeput(ni, ni->ni_pcba + NI20_PB_RQI, qe);
					/* Link on ResponseQ */
}
#endif /* 0 - unused */

/* NI_QEGET - Get queue entry from front of queue.
**	Three possible outcomes:
**	0 - queue was locked, must block and wait.
**	1 - queue not locked, but was empty.
**	QE - queue now locked, entry plucked off.
*/
static paddr_t
ni_qeget(register struct ni20 *ni,
	 register unsigned int qh)
{
    register vmptr_t qhp, qep, qnp;
    register w10_t w;
    register paddr_t qe;

    qhp = vm_physmap(qh);

    /* First attempt to get lock - fail if can't */
    if (op10m_skipge(vm_pget(vm_padd(qhp, NI20_QH_IWD)))) {
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), "[ni_qeget: Q=%lo locked]", (long)qh);
	return 0;			/* Already locked, forget it */
    }

    /* Hurray, it's unlocked.  Don't actually waste time locking it here,
    ** since the fact we're running means the CPU isn't.
    ** If the KLH10 goes multi-threaded this will have to change, of course.
    */
#if 0
    op10m_setz(w);
    vm_pset(vm_padd(qhp, NI20_QH_IWD), w);	/* Set 0 to lock */
#endif

    /* Get phys addr of 1st queue entry */
    qe = w10topa(vm_pget(vm_padd(qhp, NI20_QH_FLI)));

    /* See if anything actually there */
    if (qe == (qh + NI20_QH_FLI)) {
#if 0
	op10m_seto(w);
	vm_pset(qhp + NI20_QH_IWD, w);		/* Set -1 to unlock */
#endif
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), "[ni_qeget: Q=%lo empty]", (long)qh);

	return 1;				/* and return bad value */
    }

    /* Get ptr to first queue entry and unlink from queue
    **	NE = c(QE+FLI)		Get ptr to next entry
    **	NE backlink = QH+FLI	Set it up to point back at QH
    **	QH forwlink = NE	Point QH to it
    */

    /* A little paranoia never hurt anyone.  Make sure return value
    ** conventions can't be confused with a real (bad) queue entry address!
    */
    if (qe == 0 || qe == 1) {
	fprintf(NIDBF(ni), "[ni_qeget: QH=%lo QE=%lo Bad?!]\r\n",
			(long)qh, (long)qe);
#if 0
	op10m_seto(w);
	vm_pset(qhp + NI20_QH_IWD, w);		/* Set -1 to unlock */
#endif
	return 1;
    }
    qep = vm_physmap(qe);
    w = vm_pget(vm_padd(qep, NI20_QE_FLI));	/* Get NE = wd addr of next */
    qnp = vm_physmap(w10topa(w));		/* Make it a ptr */
    vm_pset(vm_padd(qhp, NI20_QH_FLI), w);	/* Fix up QH fwd link */
    patow10(w, qh + NI20_QH_FLI);		/* QH+FLI */
    vm_pset(vm_padd(qnp, NI20_QE_BLI), w);	/* Set NE backlink */

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni_qeget: Q=%lo E=%lo #=%d]", (long)qh, (long)qe,
			ni_qecnt(ni, qh));

    return qe;
}

#if 1 /* TOPS-10 CROCK */

/* NI_QEUNGET - Link entry to *head* of a queue.
**	Dies if queue is locked by -10.
**	If the KLH10 goes multi-threaded this will need to change.
*/
static int
ni_qeunget(register struct ni20 *ni,
		      register unsigned int qh,	/* Phys addr */
		      register unsigned int qe)	/* Phys addr */
{
    register vmptr_t qhp = vm_physmap(qh);
    register vmptr_t qep, qtp;
    register w10_t w;			/* Phys addr in word */

    /* Verify that can lock it */
    if (op10m_skipge(vm_pget(qhp + NI20_QH_IWD))) {
	fprintf(NIDBF(ni), "[ni_qeunget: internal error, Q=%lo E=%lo locked]",
			(long)qh, (long)qe);
	return 0;
    }

    /* Hurray, it's unlocked.  Don't actually waste time locking it here,
    ** since the fact we're running means the CPU isn't.
    ** If the KLH10 goes multi-threaded this will have to change, of course.
    */
#if 0
    op10m_setz(w);
    vm_pset(qhp + NI20_QH_IWD, w);	/* Set 0 to lock */
#endif

    /* First set up queue entry, then link in.
    **
    **	QE forwlink = QH+FLI
	    **	QE backlink = QH+FLI
    **	TE = c(QH+BLI)	Tail entry (is QH if Q empty)
	    **	HE = c(QH+FLI)	Head entry (is QH if Q empty)
    **	QE backlink = TE
	    **	QE forwlink = HE
    **	TE forwlink = QE
	    **	HE backlink = QE
    **	QH backlink = QE
	    **	QH forwlink = QE
    */
    patow10(w, qh + NI20_QH_FLI);	/* Get phys addr of QH as an entry */
    qep = vm_physmap(qe);
    vm_pset(qep + NI20_QE_BLI, w);	/* Set QE backlink -> QH */
    w = vm_pget(qhp + NI20_QH_FLI);	/* Get HE = wd addr of head entry */
    vm_pset(qep + NI20_QE_FLI, w);	/* Set QE forwlink -> head entry */
    qtp = vm_physmap(w10topa(w));	/* Get ptr to HE */
    patow10(w, qe + NI20_QE_FLI);	/* Get QE = wd addr of new entry */
    vm_pset(qtp + NI20_QE_BLI, w);	/* Set HE backlink -> QE */
    vm_pset(qhp + NI20_QH_FLI, w);	/* Set QH forwlink -> QE */

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni_qeunget: Q=%lo E=%lo #=%d]",
		    (long)qh, (long)qe, ni_qecnt(ni, qh));


    /* Now could unlock the queue, if it was locked to begin with. */
#if 0
    op10m_seto(w);
    vm_pset(qhp + NI20_QH_IWD, w);	/* Set -1 to unlock */
#endif

    return 1;				/* Success! */
}
#endif /* T10 crock */

/* NI_QEPUT - Link entry to end of a queue.
**	If locked, remembers attempt by setting state vars
**	ni_qhpa and ni_qepa, so next time NI20 is run, will re-try the link.
*/
static int
ni_qeput(register struct ni20 *ni,
	 register unsigned int qh,	/* Phys addr */
	 register unsigned int qe)	/* Phys addr */
{
    register vmptr_t qhp = vm_physmap(qh);
    register vmptr_t qep, qtp;
    register w10_t w;			/* Phys addr in word */

    /* See if can lock it */
    if (op10m_skipge(vm_pget(qhp + NI20_QH_IWD))) {
	/* Ugh, already locked, so remember for re-try. */
	ni->ni_qhpa = qh;
	ni->ni_qepa = qe;
	if (!ni->ni_docheck) {			/* Poll again later */
	    ni->ni_docheck = TRUE;		/* Say to poll for input */
	    clk_tmractiv(ni->ni_chktmr);	/* Activate timer */
	}
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), "[ni_qeput: Q=%lo E=%lo locked]",
			(long)qh, (long)qe);
	return 0;
    }

    /* Hurray, it's unlocked.  Don't actually waste time locking it here,
    ** since the fact we're running means the CPU isn't.
    ** If the KLH10 goes multi-threaded this will have to change, of course.
    */
#if 0
    op10m_setz(w);
    vm_pset(qhp + NI20_QH_IWD, w);	/* Set 0 to lock */
#endif

    /* First set up queue entry, then link in.
    **
    **	QE forwlink = QH+FLI
    **	TE = c(QH+BLI)	Tail entry (is QH if Q empty)
    **	QE backlink = TE
    **	TE forwlink = QE
    **	QH backlink = QE
    */
    patow10(w, qh + NI20_QH_FLI);	/* Get phys addr of QH as an entry */
    qep = vm_physmap(qe);
    vm_pset(qep + NI20_QE_FLI, w);	/* Set QE forwlink -> QH */
    w = vm_pget(qhp + NI20_QH_BLI);	/* Get TE = wd addr of tail entry */
    vm_pset(qep + NI20_QE_BLI, w);	/* Set QE backlink -> tail entry */
    qtp = vm_physmap(w10topa(w));	/* Get ptr to TE */
    patow10(w, qe + NI20_QE_FLI);	/* Get QE = wd addr of new entry */
    vm_pset(qtp + NI20_QE_FLI, w);	/* Set TE forwlink -> QE */
    vm_pset(qhp + NI20_QH_BLI, w);	/* Set QH backlink -> QE */

    /* Linked, now check to see whether to set CSR Resp-Queue-Avail bit */
    if ((qtp == vm_padd(qhp, NI20_QH_FLI))	/* If Q was empty before */
      && (qh == (ni->ni_pcba + NI20_PB_RQI))) {	/* and Q is Response Q */

	ni->ni_cond |= NI20CI_RQA;	/* Say response Q available! */
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), "[ni_qeput: Q=%lo E=%lo #=1 Set_RQA]",
			(long)qh, (long)qe);
	ni20_pi(ni);

    } else
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), "[ni_qeput: Q=%lo E=%lo #=%d]",
			(long)qh, (long)qe, ni_qecnt(ni, qh));


    /* Now could unlock the queue, if it was locked to begin with. */
#if 0
    op10m_seto(w);
    vm_pset(qhp + NI20_QH_IWD, w);	/* Set -1 to unlock */
#endif

    return 1;				/* Success! */
}

/* NI_QECNT - debugging subroutine to return # of entries on a queue.
**	Subject to horrible lossage if 10's queue list is screwed up!
*/
static int
ni_qecnt(register struct ni20 *ni,
	 register unsigned int qh)	/* Phys addr of QH */
{
    register paddr_t qe;
    register w10_t w;			/* Phys addr in word */
    register int cnt;

    qh += NI20_QH_FLI;		/* Point to forward link in QH */
    qe = qh;
    for (cnt = 0; ++cnt < 1000;) {
	/* Check here to see if QE is valid phys addr?? */

	w = vm_pget(vm_physmap(qe + NI20_QE_FLI));	/* Get link wd */
	qe = w10topa(w);	/* Cvt to physaddr value */
	if (!qe)
	    return -1;		/* Error return, zero pointer */
	if (qe == qh)
	    return cnt;		/* Win return, points back to header */
    }
    return -2;			/* Error return, infinite loop */
}

#if KLH10_DEV_DPNI20

/* NI20_EVHSDON - Invoked by INSBRK event handling when
**	signal detected from DP saying "done" in response to something
**	we sent it.
**	Basically this means the DP should be ready to accept another
**	output packet.
*/
static void
ni20_evhsdon(struct device *d,
	     register struct dvevent_s *evp)
{
    register struct ni20 *ni = (struct ni20 *)d;

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_evhsdon: %d]",
		(int)dp_xstest(&(ni->ni_dp.dp_adr->dpc_todp)));

    /* Do possible DP command post-processing */
    switch (dp_xrcmd(&(ni->ni_dp.dp_adr->dpc_todp))) {
    case DPNI_SETETH:		/* Ethernet addr maybe changed! */
      {
	register struct dpni20_s *dpni =
		    (struct dpni20_s *) ni->ni_dp.dp_adr;
	memcpy(ni->ni_ethadr, dpni->dpni_eth, 6);	/* Get true addr */
	ni_ethtodw(&ni->ni_dwethadr, ni->ni_ethadr); /* also in 10 fmt */
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), "[ni20_evhsdon: New EN=%lo,%lo,%lo]\r\n",
		    (long)LHGET(ni->ni_dwethadr.w[0]),
		    (long)RHGET(ni->ni_dwethadr.w[0]),
		    (long)LHGET(ni->ni_dwethadr.w[1]));
      }
	break;
    }
    ni20_run(ni);		/* Always do generic run check */
}
#endif /* KLH10_DEV_DPNI20 */

#if KLH10_DEV_DPNI20

/* NI20_EVHRWAK - Invoked by INSBRK event handling when
**	signal detected from DP saying "wake up"; the DP is sending
**	us an input packet.
*/
static void
ni20_evhrwak(struct device *d,
	     register struct dvevent_s *evp)
{
    register struct ni20 *ni = (struct ni20 *)d;
    register struct dpx_s *dpx = &(ni->ni_dp.dp_adr->dpc_frdp);

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_evhrwak: %d]", (int)dp_xrtest(dpx));

    if (dp_xrtest(dpx)) {	/* Verify there's a message for us */
	switch (dp_xrcmd(dpx)) {
	case DPNI_INIT:
	    ni20_iniable(ni);		/* Do initial disable/enable */
	    dp_xrdone(dpx);		/* and ACK it */
	    break;

	/* Kludge note: add an extra 4 bytes into "bytes received" counter
	** for each datagram received, since the real NI apparently includes
	** the 4 CRC bytes in its count, and TOPS-20 subtracts 4 per datagram
	** from the count in order to "correct" it before giving it to the
	** user (via NI%).  Another example of emulating hardware bogosity.
	*/
	case DPNI_RPKT:
	    ni->ni_cnts[NI20_RC_BR] += (ni->ni_rcnt = dp_xrcnt(dpx)) + 4;
	    ni->ni_cnts[NI20_RC_FR]++;		/* Update # bytes & frames */
	    if (ni->ni_state == NI20_ST_RUNENA) { /* If running enabled */
		/* ni->ni_rbuf = ;  Already set up */
		ni->ni_pktinf = TRUE;
		ni20_run(ni);			/* Go process it */
	    } else {
	default:
		if (NIDEBUG(ni))
		    fprintf(NIDBF(ni), "[ni20_evhrwak: R flushed]");
		dp_xrdone(dpx);			/* Else just ACK it */
		ni->ni_pktinf = FALSE;		/* Ensure flushed */
	    }
	    break;
	}
    }
}
#endif /* KLH10_DEV_DPNI20 */

/* NI20_RUNCLK - invoked by clock timeout code to "run" the NI20
**	if needed.
*/
static int
ni20_runclk(void *arg)
{
    register struct ni20 *ni = (struct ni20 *)arg;

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_runclk:]");
    if (ni->ni_docheck)
	ni20_run(ni);
    return ni->ni_docheck ? CLKEVH_RET_REPEAT	/* Repeat again later */
		: CLKEVH_RET_QUIET;		/* No recheck */
}

/* NI20_RUN - Invoked whenever the NI20 needs to "run".
**	Synchronously, invoked by clock timeout via ni20_rundef().
**	Asynchronously, invoked by insbreak event handling.
*/
static void
ni20_run(register struct ni20 *ni)
{
    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_run:]");

    for (;;) {
	/* See if waiting to relink a queue entry */
	if (ni->ni_qhpa && ni->ni_qepa) {
	    if (!ni_qeput(ni, ni->ni_qhpa, ni->ni_qepa))
		break;				/* Still couldn't do it */
	    ni->ni_qhpa = ni->ni_qepa = 0;	/* Done, clear state */
	}

	/* Always give priority to incoming packets. */
	while (ni->ni_pktinf) {
	    int res;
#if KLH10_DEV_DPNI20
	    res = nicmd_dgrcv(ni, ni->ni_rbuf, ni->ni_rcnt);
#else
	    res = nicmd_dgrcv(ni, &ni20_sbuf[0], ni20_slen);
#endif
	    if (res == DGRCV_WONFLS || res == DGRCV_BLKFLS) {
		ni->ni_pktinf = FALSE;
#if KLH10_DEV_DPNI20
		/* Tell DP that we're done with what it sent us */
		if (NIDEBUG(ni))
		    fprintf(NIDBF(ni), "[ni20_run: R done]");

		dp_xrdone(&(ni->ni_dp.dp_adr->dpc_frdp));
#endif
	    }
	    if (res != DGRCV_WONFLS)
		break;			/* Must block, so stop now */
	}

	/* Then check for outgoing or command packets */
	if (ni->ni_cmdqf) {
#if KLH10_DEV_DPNI20
	    /* Before taking anything off cmd queue, ensure we can send
	    ** an ethernet datagram.  Yes, this is kludgy; should only
	    ** block on actual sending, not on commands in general.
	    */
	    if (!dp_xstest(&(ni->ni_dp.dp_adr->dpc_todp)))
		break;
#endif
	    if (!ni20_cmdchk(ni))
		break;
	} else {
	    ni->ni_docheck = FALSE;	/* Nothing left to do */
	    if (NIDEBUG(ni))
		fprintf(NIDBF(ni), "[ni20_run: Done]");
	    return;
	}

    }

    /* Now what?  Re-schedule self? */
    if (!ni->ni_docheck) {
	ni->ni_docheck = TRUE;		/* Say to check again later */
	clk_tmractiv(ni->ni_chktmr);	/* Activate timer */
    }
    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_run: resched]");
}

/* Echo Packet Check grossness.
    See comment page with note on "Loopback" for explanation of all this.
*/

/* Remember datagram for later checking.
*/
static void
ni_ecpstore(register struct ni20 *ni,
	    register unsigned char *ucp, /* Ptr to received dgm */
	    unsigned int len)		/* # bytes in datagram */
{
    register struct niecbe *be;

    /* Always grab next "free" in ring.  This will clobber oldest entry
    ** if buffer is full.
    */
    if (!(be = ni->ni_ecb))
	return;			/* Sanity check */
    be += ni->ni_ecbffree;	/* Point to first free */

    /* Stuff data into entry */
    memcpy(be->niec_hdr, ucp, sizeof(be->niec_hdr));	/* Remember header */
    be->niec_digest = ni_ecpdigest(ucp + sizeof(be->niec_hdr),
				len - sizeof(be->niec_hdr));
    be->niec_deathtmo = ni->ni_ectmo;

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni_ecpstore: added #%d]", ni->ni_ecbffree);

    /* Now bump index */
    if (++(ni->ni_ecbffree) >= ni->ni_ecblen)
	ni->ni_ecbffree = 0;		/* Wrap to start of ring buffer */
    if (ni->ni_ecbffree == ni->ni_ecbfuse) {
	/* We've used up last buffer slot!  Flush oldest one in order to
	    always maintain a gap of one.
	*/
	if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), "[ni_ecpstore: buffer overflow, flushed #%d]",
			ni->ni_ecbfuse);
	if (++(ni->ni_ecbfuse) >= ni->ni_ecblen)
	    ni->ni_ecbfuse = 0;
    }

    /* Tickle timer if necessary */
    if (!(ni->ni_ectact) && ni->ni_ectmr) {
	clk_tmractiv(ni->ni_ectmr);	/* Activate timer */
	ni->ni_ectact = TRUE;
    }
}

/* Check to see if datagram is a true echo packet; i.e. we remember
    having sent it recently.
*/
static int
ni_ecpcheck(register struct ni20 *ni,
	    register unsigned char *ucp,	/* Ptr to received dgm */
	    unsigned int len)			/* # bytes in datagram */
{
    register struct niecbe *be;
    register int i;
    register uint32 digest;
    register int cnt = 0;		/* For debugging */

    if (!(be = ni->ni_ecb)		/* Do nothing if no buffer */
      || (ni->ni_ecbfuse == ni->ni_ecbffree))	/* or nothing active */
	return FALSE;
    digest = ni_ecpdigest(ucp + sizeof(be->niec_hdr),
				len - sizeof(be->niec_hdr));
    for (be += (i = ni->ni_ecbfuse); i != ni->ni_ecbffree; ++cnt) {
	if ((be->niec_digest == digest)
	  && (memcmp(be->niec_hdr, ucp, sizeof(be->niec_hdr))==0)) {
	    /* MATCHED!!!
		Flush not only this entry but any others prior to it, on
		assumption that packet ordering will be preserved.
	    */
	    ni->ni_ecbfuse =		/* Set new active start pos */
		((++i < ni->ni_ecblen) ? i : 0);
	    if (NIDEBUG(ni))
		fprintf(NIDBF(ni),
			"[ni_ecpcheck: echo-flushed %ld up to #%d]",
			(long) cnt, i);
	    return TRUE;
	}
	if (++i < ni->ni_ecblen)	/* Bump to next entry */
	    ++be;
	else				/* Wrap in ring buf */
	    i = 0, be = ni->ni_ecb;
    }

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni_ecpcheck: no match in %ld]", (long) cnt);

    return FALSE;		/* Not an echoed packet */
}

/* Grind datagram data and return a digest/checksum value
*/
static uint32
ni_ecpdigest(register unsigned char *ucp,	/* Ptr to received dgm */
			   register int len)	/* # bytes in datagram */
{
    register uint32 digest = 0;

    while (--len >= 0)
	digest = (digest<<1) + (digest>>31) + *ucp++;

    return digest;
}

/* NI20_ECPCLK - invoked by clock timeout code to reap the echo-check buffer.
*/
static int
ni20_ecpclk(void *arg)
{
    register struct ni20 *ni = (struct ni20 *)arg;

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_ecpclk:]");

    if (ni->ni_ectact) {
	register int i;
	register int cnt, dcnt;		/* For debugging */

	/* Scan starting at first active entry and flush those that
	** have timed out.  This is gross, but simpler than maintaining
	** delta values for now.
	*/
	cnt = dcnt = 0;
	for (i = ni->ni_ecbfuse; i != ni->ni_ecbffree; ++cnt) {
	    if (--(ni->ni_ecb[i].niec_deathtmo) <= 0) {
		if (++i >= ni->ni_ecblen)	/* Bump to next entry */
		    i = 0;
		ni->ni_ecbfuse = i;	/* Set new active start pos */
		++dcnt;
		continue;
	    }
	    if (++i >= ni->ni_ecblen)	/* Bump to next entry */
		i = 0;
	}
	if (dcnt) {
	    if (NIDEBUG(ni))
		fprintf(NIDBF(ni),
			"[ni_ecptmo: flushed %d of %d, left #%d-#%d]",
			dcnt, cnt, ni->ni_ecbfuse, ni->ni_ecbffree);
	}
	if (ni->ni_ecbfuse == ni->ni_ecbffree) {
	    ni->ni_ectact = FALSE;		/* No more, silence timer */
	}
    }
    return ni->ni_ectact ? CLKEVH_RET_REPEAT	/* Repeat again later */
		: CLKEVH_RET_QUIET;		/* No recheck */
}

#if 0
/* NI20_IOBEG - Called by drive to set up data channel just prior to
**	starting an I/O xfer.
** Returns 0 if failure; drive should stop immediately and not
**	invoke rhdv_iobuf, but must still call rhdv_ioend.
** Else returns # of block units (sectors or records) to do I/O for.
*/
int
ni20_iobeg(struct device *drv, int wflg)
{
    register struct ni20 *ni = (struct ni20 *)(drv->dv_ctlr);

    ni->ni_dcsts = CSW_CLRSET;		/* Clear channel status */
    ni->ni_dcwrt = wflg;		/* Remember if writing */
    if (!nidc_ccwget(ni)) {
	ni->ni_cond |= NI20CI_CE	/* Say Channel Error */
		| NI20CI_CMD;		/* and Command Done */
	ni20_pi(ni);			/* Try to trigger PI */
	return 0;			/* Tell drive we failed */
    }

    return ni->ni_bcnt;
}


/* NI20_IOBUF - Called by drive to query data channel to find source
**	or dest buffer for I/O xfer.  Can be called repeatedly for one xfer.
**	Caller must provide # of words used from the last call (0 if none).
**	In particular, final transfer MUST invoke this to tell the channel
**	how many words were actually used.
**
**	If returns 0, no data or space left.
**	If returns +, OK, *avp NULL if skipping, else vmptr to mem.
**	If returns -, error.
*/
int
ni20_iobuf(struct device *drv,
	   register int wc,	/* Max 11 bits of word count, so int OK */
	   vmptr_t *avp)
{
    register struct ni20 *ni = (struct ni20 *)(drv->dv_ctlr);

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_iobuf: %d. => ", wc);

    if (!ni->ni_dcwcnt) {	/* Nothing left */
        if (NIDEBUG(ni))
	    fprintf(NIDBF(ni), "0]\r\n");
	return 0;
    }

    if (wc) {
	/* If drive is updating our IO xfer status, do it. */
	if ((ni->ni_dcwcnt -= wc) < 0)	/* Update remaining wd cnt */
	    panic("ni20_iobuf: drive overran chan!");

	/* Update buffer pointer.  Note check of reverse xfer */
	if (ni->ni_dcbuf)
	    ni->ni_dcbuf += (ni->ni_dcrev ? -wc : wc);

	/* See if word count ran out */
	if (ni->ni_dcwcnt == 0) {
	    /* Yep, was this the last cmd (halt or last xfer?)
	    ** If not, try to get another data xfer CCW.
	    */
	    if (ni->ni_dchlt || !nidc_ccwget(ni)) {
	        if (NIDEBUG(ni))
		    fprintf(NIDBF(ni), "0]\r\n");
		return 0;		/* Done, or no more */
	    }
	}
    }

    /* Here, we have a positive count, so return that. */
    wc = ni->ni_dcwcnt;

    /* Also return addr of buffer.  But there are a few special cases
    ** to check for, sigh...
    */
    if (ni->ni_dcbuf)
	*avp = vm_physmap(ni->ni_dcbuf);
    else {			/* Ugh! Special fill or skip hack. */
	if (ni->ni_dcwrt) {	/* If writing, use fill wds from EPT */
	    *avp = vm_physmap(cpu.mr_ebraddr + EPT_CB0);
	    if (wc > 4)
		wc = 4;
	} else {
	    *avp = NULL;	/* Tell drive to skip input */
	}
    }
    if (ni->ni_dcrev) {		/* If reverse xfer, do 1 wd at time, sigh */
	wc = 1;
    }

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "%d. (%lo)]\r\n", wc, (long)(ni->ni_dcbuf));

    return wc;
}



/* NI20_IOEND - Called by drive when I/O finished, terminate data channel
**	I/O xfer.  Assumes that ni20_iobuf has been called to update
**	status after each transfer or sub-transfer, so channel word count
**	is accurate reflection of data xfer.
** State may be one of:
**	Error - either in drive, or from data channel.  Assumption is
**		that error has set all appropriate error bits (specifically
**		Channel Error if channel status must be stored), and 
**		triggered PI as well.
**
**	Drive stopped early due to no more DC buffer space.  Indicated by
**		non-zero returned block count.
**	Drive stopped normally, block count 0.
**
**	Checks to see whether to set Short/Long WC Error.
**	Stores channel status if requested by PTCR bit NI20DO_SES.
**
**	May initiate next NI20 data transfer.
*/
void
ni20_ioend(register struct device *drv,
	   int bc)		/* Drive's final block count */
{
    register struct ni20 *ni = (struct ni20 *)(drv->dv_ctlr);

    if (NIDEBUG(ni))
	fprintf(NIDBF(ni), "[ni20_ioend: %d.]\r\n", bc);


    /* Update final IO xfer status */
    if (bc || ni->ni_dcwcnt) {
	/* Drive still has stuff it wanted to do.
	** Set channel Short WC if WC==0, or Long WC if WC != 0,
	** and NI20 Chan Err.
	*/
	ni->ni_dcsts |= (ni->ni_dcwcnt ? CSW_LWCE : CSW_SWCE);
	ni->ni_cond |= NI20CI_CE;
    }

    /* See whether to store channel status */
    if ((NI20REG(ni, NI20_PTCR) & NI20DO_SES)	/* If wanted state stored */
      || (ni->ni_cond & NI20CI_CE)) {		/* or any kind of chan error */
	register vmptr_t vp;
	register w10_t w;

	vp = vm_physmap(cpu.mr_ebraddr + ni->ni_eptoff);

	/* Store status bits and current cmd list ptr */
	if (ni->ni_dcwcnt)
	    ni->ni_dcsts |= CSW_NOWC0;
	LRHSET(w, ni->ni_dcsts | ((ni->ni_clp >> H10BITS)&CSW_HIADR),
		ni->ni_clp & H10MASK);
	vm_pset(vp+NI20_EPT_CST(0), w);

	/* Reconstruct current CCW and store it */
	LRHSET(w, (LHGET(ni->ni_dcccw)&CCW_OP)
		| (ni->ni_dcwcnt<<4) | ((ni->ni_dcbuf>>H10BITS)&CCW_ADR),
			ni->ni_dcbuf & H10MASK);
	vm_pset(vp+NI20_EPT_CCW(0), w);
    }

    /* Now say command done, whatever happened. */
    ni->ni_cond &= ~NI20CI_PCRF;	/* Primary regs now free */
    ni->ni_cond |= NI20CI_CMD;
    ni20_pi(ni);			/* Attempt triggering PI */

    /* Now if no errors, check for secondary TCR and initiate it? */
}
#endif /* 0 */

/* Massbus Data Channel routines.
**
*/

/* DCHN_INIT - Called when setting up internal data structures
*/
void
dchn_init(register struct dvdchn *dc,
	  int mbcn)	/* Massbus controller # (0-7) */
{
    dc->dc_eptoff = mbcn * 4;	/* Offset of EPT area */
    dchn_clear(dc);
}

/* DCHN_CLEAR - Called to clear data channel while KL running
*/
void
dchn_clear(register struct dvdchn *dc)
{
    /* Reset channel PC to point at 1st wd of channel area in EPT */
    dc->dc_clp = dc->dc_eptoff + cpu.mr_ebraddr;

    dc->dc_sts = CSW_CLRSET;		/* Clear status */
    LRHSET(dc->dc_ccw, 0, 0);
    dc->dc_wcnt = 0;
    dc->dc_buf = 0;
    dc->dc_hlt = TRUE;
}

/* DCHN_CCWGET - called by device when drive wants to start xfer or needs more
**	data/buffer space from channel.
*/
int
dchn_ccwget(register struct dvdchn *dc)
{
    register w10_t w;
    register paddr_t pa;
    register int wc;

    /* Grovel down cmd list until hit valid xfer cmd, use that to set up */
    for (;;) {
	/* Fetch current cmd word */
	w = vm_pget(vm_physmap(dc->dc_clp));	/* Get cmd word */
	pa = w10topa(w);
	wc = (LHGET(w) & CCW_CNT) >> (22-18);

	switch (LHGET(w) & CCW_OP) {
	case CCW_HLT:
	    /* Use addr as CLP for next call, and do stuff to halt.
	    ** Should this cause a "Short Word Count" channel error, or
	    ** something else since transfer was never started?
	    ** For now, pretend zero word count, fail as if short WC.
	    */
	    dc->dc_clp = pa;	/* Set new "PC" */
	    dc->dc_buf = pa;	/* Also set here in case status stored */
	    dc->dc_ccw = w;	/* Remember current cmd */
	    dc->dc_wcnt = 0;
	    dc->dc_hlt = TRUE;
	    dc->dc_rev = 0;
	    return 0;

	case CCW_JMP:
	    dc->dc_clp = pa;	/* Set up CLP and loop to effect jump */
	    continue;

	case CCW_XFR:		/* Forward transfer, no halt */
	    dc->dc_hlt = 0;
	    dc->dc_rev = 0;
	    break;

	case CCW_XFR|CCW_XHLT:	/* Forward transfer, halt */
	    dc->dc_hlt = TRUE;
	    dc->dc_rev = 0;
	    break;

	case CCW_XFR|CCW_XREV|CCW_XHLT:
	    dc->dc_hlt = 0;
	    dc->dc_rev = TRUE;
	    break;

	case CCW_XFR|CCW_XREV:
	    dc->dc_hlt = TRUE;
	    dc->dc_rev = TRUE;
	    break;

	default:
	    /* Perhaps assert Channel Error (RH20CI_CE) here? */
	    panic("dchn_ccwget: Bad CCW op %lo", (long)(LHGET(w) & CCW_OP));
	    return 0;
	}

	dc->dc_ccw = w;		/* Remember current cmd */
	dc->dc_wcnt = wc;	/* Set up word count */
	dc->dc_buf = pa;	/* Set up address */
	dc->dc_clp = (dc->dc_clp + 1) & MASK22;	/* Bump "PC" */
	return 1;
    }
}

#endif /* KLH10_DEV_NI20 */
