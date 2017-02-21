/* DVCH11.C - Emulates CH11 Chaosnet interface for KS10
*/
/* $Id: dvch11.c,v 2.3 2001/11/10 21:28:59 klh Exp $
*/
/*  Copyright © 2005 Björn Victor and Kenneth L. Harrenstien
**  All Rights Reserved
**
**  This file may become part of the KLH10 Distribution.  Use, modification, and
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
 * $Log: dvch11.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*	NOT just a dummy!
**
** See MIT AI Memo 628, "Chaosnet", by David A. Moon (esp. chapter 7) for documentation.
** Note: chapter 7 ("Hardware Programming Documentation") is not completely correct,
** and should be cross-referenced with the ITS source (SYSTEM;CHAOS).
** E.g. the "timer interrupt enable" bit is "transmit busy", and the
** r/w-only definitions of CSR bits is sometimes wrong (e.g. "transmit
** done").
**
** Based on dvlhdh.c, with dpchudp.c based on dpimp.c.
** (Some things may still be irrelevant inheritage from dvlhdh, and could be cleaned up...)
*/

#include "klh10.h"

#if !KLH10_DEV_CH11 && CENV_SYS_DECOSF
	/* Stupid gubbish needed to prevent OSF/1 AXP compiler from
	** halting merely because compiled file is empty!
	*/
static int decosfcclossage;
#endif

#if KLH10_DEV_CH11	/* Moby conditional for entire file */

#include <stdio.h>
#include <string.h>

#include "kn10def.h"
#include "kn10dev.h"
#include "prmstr.h"	/* For parameter parsing */
#include "dvuba.h"
#include "dvch11.h"

#include "dpchudp.h"

#ifndef KLH10_CH11_USE_GETHOSTBYNAME
# define KLH10_CH11_USE_GETHOSTBYNAME 1
#endif

#if KLH10_CH11_USE_GETHOSTBYNAME
# include <netdb.h>
#endif

#define CHUDPBUFSIZ (DPCHUDP_MAXLEN+500)	/* Plenty of slop */


#ifndef CH11_NSUP
# define CH11_NSUP 1		/* Only one device supported */
#endif

#ifndef CH11_CHIP_MAX
# define CH11_CHIP_MAX 20	/* max Chaos/IP mappings - see DPCHUDP_CHIP_MAX */
#endif

#if CH11_CHIP_MAX > DPCHUDP_CHIP_MAX
# error "DPCHUDP_CHIP_MAX must be at least CH11_CHIP_MAX"
#endif

#define REG(u) ((u)->ch_reg)

/* Chaos/IP mapping entry - see dpchudp_chip */
struct ch_chip {
    unsigned int ch_chip_chaddr; /* Chaos address */
    struct in_addr ch_chip_ipaddr; /* IP address */
  in_port_t ch_chip_ipport;	/* IP port */
};

struct ch11 {
    struct device ch_dv;	/* Generic 10 device structure */

    /* CH11-specific vars */

    /* CH11 internal register (only one) */
    dvureg_t ch_reg;	/* Storage for register (16 bits) */
    unsigned short ch_lost;	/* # msgs rcved with rcv bfr full */

    /* I/O signalling flags */
    int ch_ipireq;	/* Input  side doing PI request */
    int ch_opireq;	/* Output side doing PI request */
    int ch_inactf;	/* TRUE if input can be done */
    int ch_outactf;	/* TRUE if output can be done */
    unsigned char *ch_iptr;	/* Pointer to input data */
    unsigned char *ch_optr;	/* Pointer to output data */

    /* Misc config info not set elsewhere */
    char *ch_ifnam;	/* Native platform's interface name */
    int ch_dedic;	/* TRUE if interface dedicated (else shared) */
    int ch_backlog;	/* Max # input msgs to queue up in kernel */
    unsigned int ch_myaddr;	/* My Chaos address */
  in_port_t ch_chudp_port;	/* CHUDP port to use */

    /* DP stuff */
    char *ch_dpname;	/* Pointer to dev process pathname */
    int ch_dpidly;	/* # secs to sleep when starting DP */
    int ch_dpdbg;	/* Initial DP debug flag */

    int ch_dpstate;	/* TRUE if dev process has finished its init */
    struct dp_s ch_dp;	/* Handle on dev process */
    unsigned char *ch_sbuf;	/* Pointers to shared memory buffers */
    unsigned char *ch_rbuf;
    int ch_rcnt;	/* # chars in received packet input buffer */

  /* Chaos/IP mapping */
  int ch_chip_tlen;		/* table length */
  struct ch_chip ch_chip_tbl[CH11_CHIP_MAX];
};

static int nch11s = 0;
struct ch11 dvch11[CH11_NSUP];
			/* Can be static, but left external for debugging */

/* Function predecls */

static int ch11_conf(FILE *f, char *s, struct ch11 *ch);
static int ch11_init(struct device *d, FILE *of);
static int ch11_cmd(register struct device *d, FILE *of, char *cmd);
static dvureg_t ch11_pivec(struct device *d);
static dvureg_t ch11_read(struct device *d, uint18 addr);
static void ch11_write(struct device *d, uint18 addr, dvureg_t val);
static void ch11_clear(struct device *d);
static void ch11_powoff(struct device *d);

static void ch_clear(struct ch11 *ch);
static void ch_oint(struct ch11 *ch);
static void ch_iint(struct ch11 *ch);
static void ch_igo(struct ch11 *ch);
static void ch_ogo(struct ch11 *ch);
static void ch_idone(struct ch11 *ch);
static void ch_odone(struct ch11 *ch);
static void showpkt(FILE *f, char *id, unsigned char *buf, int cnt);

	/* Virtual CHUDP low-level stuff */
/* static */
int  chudp_init(struct ch11 *ch, FILE *of);
static int  chudp_start(struct ch11 *ch);
static void chudp_stop(struct ch11 *ch);
static void chudp_kill(struct ch11 *ch);
static int  chudp_incheck(struct ch11 *ch);
static void chudp_inxfer(struct ch11 *ch);
static int  chudp_outxfer(struct ch11 *ch);

/* Configuration Parameters */

#define DVCH11_PARAMS \
    prmdef(CH11P_DBG, "debug"),	/* Initial debug value */\
    prmdef(CH11P_BR,  "br"),	/* BR priority */\
    prmdef(CH11P_VEC, "vec"),	/* Interrupt vector */\
    prmdef(CH11P_ADDR,"addr"),	/* Unibus address */\
\
    prmdef(CH11P_IFC,"ifc"),      /* Ethernet interface name */\
    prmdef(CH11P_BKL,"backlog"), /* Max bklog for rcvd pkts (else sys deflt) */\
    prmdef(CH11P_DED,"dedic"),    /* TRUE= Ifc dedicated (else shared) */\
    prmdef(CH11P_DPDLY,"dpdelay"),/* # secs to sleep when starting DP */\
    prmdef(CH11P_DPDBG,"dpdebug"), /* Initial DP debug value */\
    prmdef(CH11P_DP, "dppath"),    /* Device subproc pathname */	\
\
    prmdef(CH11P_MYADDR, "myaddr"),/* My Chaosnet address */\
    prmdef(CH11P_CHUPORT, "chudpport"),	/* CHUDP port to use */\
    prmdef(CH11P_CHIP, "chip") /* Chaos/IP mapping */


enum {
# define prmdef(i,s) i
	DVCH11_PARAMS
# undef prmdef
};

static char *ch11prmtab[] = {
# define prmdef(i,s) s
	DVCH11_PARAMS
# undef prmdef
	, NULL
};

static int parip(char *cp, unsigned char *adr);

/* CH11_CONF - Parse configuration string and set defaults.
**	At this point, device has just been created, but not yet bound
**	or initialized.
** NOTE that some strings are dynamically allocated!  Someday may want
** to clean them up nicely if config fails or device is uncreated.
*/

static int
ch11_conf(FILE *f, char *s, struct ch11 *ch)
{
    int i, ret = TRUE;
    struct prmstate_s prm;
    char buff[200];
    long lval;

    /* First set defaults for all configurable parameters
	Unfortunately there's currently no way to access the UBA # that
	we're gonna be bound to, otherwise could set up defaults.  Later
	fix this by giving ch11_create() a ptr to an arg structure, etc.
     */
    DVDEBUG(ch) = FALSE;
    ch->ch_ifnam = NULL;
    ch->ch_backlog = 0;
    ch->ch_dedic = FALSE;
    ch->ch_dpidly = 0;
    ch->ch_dpdbg = FALSE;
    ch->ch_dpname = "dpchudp";		/* Pathname of device subproc */
    ch->ch_chip_tlen = 0;
    ch->ch_chudp_port = CHUDP_PORT;

    prm_init(&prm, buff, sizeof(buff),
		s, strlen(s),
		ch11prmtab, sizeof(ch11prmtab[0]));
    while ((i = prm_next(&prm)) != PRMK_DONE) {
	switch (i) {
	case PRMK_NONE:
	    fprintf(f, "Unknown CH11 parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;
	case PRMK_AMBI:
	    fprintf(f, "Ambiguous CH11 parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;
	default:	/* Handle matches not supported */
	    fprintf(f, "Unsupported CH11 parameter \"%s\"\n", prm.prm_name);
	    ret = FALSE;
	    continue;

	case CH11P_DBG:		/* Parse as true/false boolean or number */
	    if (!prm.prm_val)	/* No arg => default to 1 */
		DVDEBUG(ch) = 1;
	    else if (!s_tobool(prm.prm_val, &DVDEBUG(ch)))
		break;
	    continue;

	case CH11P_BR:		/* Parse as octal number */
	    if (!prm.prm_val || !s_tonum(prm.prm_val, &lval))
		break;
	    if (lval < 4 || lval > 7) {
		fprintf(f, "CH11 BR must be one of 4,5,6,7\n");
		ret = FALSE;
	    } else
		ch->ch_dv.dv_brlev = lval;
	    continue;

	case CH11P_VEC:		/* Parse as octal number */
	    if (!prm.prm_val || !s_tonum(prm.prm_val, &lval))
		break;
	    if (lval < 4 || lval > 0400 || (lval&03)) {
		fprintf(f, "CH11 VEC must be valid multiple of 4\n");
		ret = FALSE;
	    } else
		ch->ch_dv.dv_brvec = lval;
	    continue;

	case CH11P_ADDR:	/* Parse as octal number */
	    if (!prm.prm_val || !s_tonum(prm.prm_val, &lval))
		break;
	    if (lval < UB_CH11 || (lval&037)) {
		fprintf(f, "CH11 ADDR must be valid Unibus address\n");
		ret = FALSE;
	    } else
		ch->ch_dv.dv_addr = lval;
	    continue;

	case CH11P_MYADDR:	/* Parse as octal number */
	  if (!prm.prm_val || !s_tonum(prm.prm_val, &lval))
	    break;
	  if ((lval < 1) || (lval >= 0xffff)) {
	    fprintf(f, "CH11 MYADDR must be a valid Chaosnet address\n");
	    ret = FALSE;
	  } else
	    ch->ch_myaddr = lval;
	  continue;

	case CH11P_CHUPORT:
	  if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
	    break;
	  if ((lval < 1) || (lval >= 0xffff)) {
	    fprintf(f, "CH11 CHUDPPORT must be a valid UDP port\n");
	    break;
	  } else
	    ch->ch_chudp_port = lval;
	  continue;

	case CH11P_CHIP:
	  if (ch->ch_chip_tlen >= CH11_CHIP_MAX) {
	    fprintf(f,"CH11 Chaos/IP table full\n");
	    break;
	  }
	  if (!prm.prm_val)
	    break;
	  {
	    char *c, *s = index(prm.prm_val, '/');
	    if (s == NULL)
	      break;
	    else {
	      unsigned long cha;
	      unsigned char ipa[IP_ADRSIZ];
	      struct hostent *he;
	      in_port_t ipp = CHUDP_PORT;
	      struct ch_chip *chip;
	      int idx;
	      *(s++) = '\0';	/* separate chaos from ip */
	      if (!s_tonum(prm.prm_val, (long*)&cha)) {
		*(--s) = '/';	/* failed to parse chaos, put back slash */
		break;		/* and complain */
	      }
	      if ((c = index(s,':')) != NULL) {
		long x;
		if (s_todnum(c+1,&x) && (x > 0) && (x < 0xffff)) {
		  ipp = x;
		} else {
		  fprintf(f,"CH11 Chaos/IP mapping port number invalid");
		  *(s-1) = '/';
		  break;
		}
		*c = '\0';	/* zap for IP parsing */
	      }
#if KLH10_CH11_USE_GETHOSTBYNAME
	      if ((he = gethostbyname(s)) == NULL)
#else
	      if (!parip(s, &ipa[0])) 
#endif
		{
		  *(--s) = '/';	/* put back slash */
		  if (c)
		    *c = ':';	/* and colon */
		  break;		/* and complain */
		}
#if KLH10_CH11_USE_GETHOSTBYNAME
	      if ((he->h_addrtype != AF_INET) || (he->h_length != IP_ADRSIZ)) {
		fprintf(stderr,"CH11 CHIP spec found non-IPv4 address");
		break;
	      }
#endif
	      idx = (ch->ch_chip_tlen); /* new index */
	      ch->ch_chip_tbl[idx].ch_chip_chaddr = cha;
	      ch->ch_chip_tbl[idx].ch_chip_ipport = ipp;
#if KLH10_CH11_USE_GETHOSTBYNAME
	      memcpy(&ch->ch_chip_tbl[idx].ch_chip_ipaddr, he->h_addr, IP_ADRSIZ);
#else
	      memcpy(&ch->ch_chip_tbl[idx].ch_chip_ipaddr, &ipa[0], IP_ADRSIZ);
#endif
	      ch->ch_chip_tlen = idx+1; /* update length */
	    }
	    continue;
	  }
	case CH11P_IFC:		/* Parse as simple string */
	    if (!prm.prm_val)
		break;
	    ch->ch_ifnam = s_dup(prm.prm_val);
	    continue;

	case CH11P_BKL:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    ch->ch_backlog = lval;
	    continue;

	case CH11P_DED:		/* Parse as true/false boolean */
	    if (!prm.prm_val)
		break;
	    if (!s_tobool(prm.prm_val, &ch->ch_dedic))
		break;
	    continue;

	case CH11P_DPDLY:		/* Parse as decimal number */
	    if (!prm.prm_val || !s_todnum(prm.prm_val, &lval))
		break;
	    ch->ch_dpidly = lval;
	    continue;

	case CH11P_DPDBG:		/* Parse as true/false boolean or number */
	    if (!prm.prm_val)	/* No arg => default to 1 */
		ch->ch_dpdbg = 1;
	    else if (!s_tobool(prm.prm_val, &(ch->ch_dpdbg)))
		break;
	    continue;

	case CH11P_DP:		/* Parse as simple string */
	    if (!prm.prm_val)
		break;
	    ch->ch_dpname = s_dup(prm.prm_val);
	    continue;

	}
	ret = FALSE;
	fprintf(f, "CH11 param \"%s\": ", prm.prm_name);
	if (prm.prm_val)
	    fprintf(f, "bad value syntax: \"%s\"\n", prm.prm_val);
	else
	    fprintf(f, "missing value\n");
    }

    /* Param string all done, do followup checks or cleanup */
    if (!ch->ch_dv.dv_brlev || !ch->ch_dv.dv_brvec || !ch->ch_dv.dv_addr) {
	fprintf(f, "CH11 missing one of BR, VEC, ADDR params\n");
	ret = FALSE;
    }
    if (ch->ch_dedic || ch->ch_backlog || ch->ch_dpidly || ch->ch_ifnam)
      fprintf(f, "CH11 params \"dedic\", \"backlog\", \"dpdelay\", \"ifc\" not supported, ignoring");
    /* Set 1st invalid addr */
    ch->ch_dv.dv_aend = ch->ch_dv.dv_addr + (UB_CH11END-UB_CH11);

    /* MYADDR must always be set! */
    if (ch->ch_myaddr == 0) {
	fprintf(f,
	    "CH11 param \"myaddr\" must be set\n");
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


/* CH11 interface routines to KLH10 */

struct device *
dvch11_create(FILE *f, char *s)
{
    register struct ch11 *ch;

    /* Allocate a CH11 device structure */
    if (nch11s >= CH11_NSUP) {
	fprintf(f, "Too many CH11s, max: %d\n", CH11_NSUP);
	return NULL;
    }
    ch = &dvch11[nch11s++];		/* Pick unused CH11 */

    /* Various initialization stuff */
    memset((char *)ch, 0, sizeof(*ch));

    iodv_setnull(&ch->ch_dv);		/* Init as null device */

    ch->ch_dv.dv_init   = ch11_init;	/* Set up own post-bind init */
    ch->ch_dv.dv_reset  = ch11_clear;	/* System reset (clear stuff) */
    ch->ch_dv.dv_powoff = ch11_powoff;	/* Power-off cleanup */
    ch->ch_dv.dv_cmd = ch11_cmd; /* General user command to device */

    /* Unibus stuff */
    ch->ch_dv.dv_pivec = ch11_pivec;	/* Return PI vector */
    ch->ch_dv.dv_read  = ch11_read;	/* Read unibus register */
    ch->ch_dv.dv_write = ch11_write;	/* Write unibus register */

    /* Configure from parsed string and remember for init
    */
    if (!ch11_conf(f, s, ch))
	return NULL;

    return &ch->ch_dv;
}


static void
ch11_cmd_dpdebug(struct ch11 *ch, FILE *of, int val)
{
  struct dpchudp_s *dpc = (struct dpchudp_s *)ch->ch_dp.dp_adr;
  fprintf(of,"Old value: %d.  New value: %d.\n", dpc->dpchudp_dpc.dpc_debug, val);
  dpc->dpchudp_dpc.dpc_debug = val;
}

static void
ch11_cmd_chiptable(struct ch11 *ch, FILE *of)
{
  int n, i;
  unsigned char *ip;
  char ipa[4*4];
  struct tm *ltime;
  char last[128];
  struct dpchudp_chip *chip;
  struct dpchudp_s *dpc = (struct dpchudp_s *)ch->ch_dp.dp_adr;
  if (!dpc) {
    fprintf(of,"Can't find DP!\n");
    return;
  }
  n = dpc->dpchudp_chip_tlen;
  fprintf(of,"Currently %d entries in Chaos/IP table\n",n);
  if (n > 0) {
    fprintf(of,"Chaos   IP               Port    Last received\n");
    for (i = 0; i < n; i++) {
      chip = &dpc->dpchudp_chip_tbl[i];
      ip = (unsigned char *)&chip->dpchudp_chip_ipaddr.s_addr;
      sprintf(ipa,"%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
      if (chip->dpchudp_chip_lastrcvd != 0) {
	ltime = localtime(&chip->dpchudp_chip_lastrcvd);
	strftime(last, sizeof(last), "%Y-%m-%d %T", ltime);
      } else
	strcpy(last,"[static]");
      fprintf(of,"%6o  %-15s  %d.  %s\n",
	      chip->dpchudp_chip_chaddr,
	      ipa,
	      chip->dpchudp_chip_ipport,
	      last);
    }
  }
}

static void
ch11_cmd_status(struct ch11 *ch, FILE *of)
{
  fprintf(of,"My CHAOS address: 0%o, CHUDP port: %d.\n", ch->ch_myaddr, ch->ch_chudp_port);
  fprintf(of,"Status register: 0%o\n", REG(ch));
  if (REG(ch) & CH_BSY)
    fprintf(of, "  Transmit busy\n");
  if (REG(ch) & CH_LUP)
    fprintf(of, "  Loopback\n");
  if (REG(ch) & CH_SPY)
    fprintf(of, "  Spy (promiscuous)\n");
  if (REG(ch) & CH_REN)
    fprintf(of, "  Receive enabled\n");
  if (REG(ch) & CH_TEN)
    fprintf(of, "  Transmit enabled\n");
  if (REG(ch) & CH_TAB)
    fprintf(of, "  Transmit aborted\n");
  if (REG(ch) & CH_TDN)
    fprintf(of, "  Transmit done\n");
  if (REG(ch) & CH_RDN)
    fprintf(of, "  Receive done\n");
  if (REG(ch) & CH_ERR)
    fprintf(of, "  CRC error\n");
  fprintf(of, "  Lost count: %d\n", ch->ch_lost);
  fprintf(of,"PI request: input %d, output: %d\n", ch->ch_ipireq, ch->ch_opireq);
  fprintf(of,"Input possible: %d, Output possible: %d\n", ch->ch_inactf, ch->ch_outactf);
  fprintf(of, "DP status: %d\n", ch->ch_dpstate);
  fprintf(of, "DP Rtest: %d, Stest: %d\n",
	  (int)dp_xrtest(dp_dpxfr(&ch->ch_dp)), (int)dp_xstest(dp_dpxfr(&ch->ch_dp)));
  fprintf(of, "DP rcmd: %d, rcnt: %d\n",
	  (int)dp_xrcmd(dp_dpxfr(&ch->ch_dp)), (int)dp_xrcnt(dp_dpxfr(&ch->ch_dp)));
  fprintf(of,"Input buffer: ");
  if (ch->ch_iptr)
    fprintf(of,"%d chars\n",ch->ch_iptr - ch->ch_rbuf);
  else
    fprintf(of,"none\n");
  fprintf(of,"Output buffer: ");
  if (ch->ch_optr)
    fprintf(of,"%d chars\n",ch->ch_optr - ch->ch_sbuf);
  else
    fprintf(of,"none\n");
  fprintf(of,"Receive count: %d\n", ch->ch_rcnt);  
}

static int
ch11_cmd(register struct device *d, FILE *of, char *cmd)
{
  register struct ch11 *ch = (struct ch11 *)d;

  if (*cmd)
    while (*cmd == ' ')
      cmd++;
  if (*cmd && (strcmp(cmd,"chiptable") == 0)) 
    ch11_cmd_chiptable(ch, of);
  else if (*cmd && (strcmp(cmd,"status") == 0))
    ch11_cmd_status(ch, of);
  else if (*cmd && strncmp(cmd,"dpdebug",strlen("dpdebug")) == 0) {
    int val = 0;
    cmd += strlen("dpdebug");
    while (*cmd == ' ')
      cmd++;
    if (s_tobool(cmd, &val))
      ch11_cmd_dpdebug(ch, of, val);
    else
      fprintf(of,"Couldn't grok argument: \"%s\"\n", cmd);
  }
  else if (*cmd) {
      fprintf(of,"Unknown command \"%s\"\n", cmd);
      fprintf(of,"Commands:\n \"chiptable\" to show the Chaos/IP table\n \"status\" to show device status\n \"dpdebug x\" to set dpchudp debug level to x\n");
  } else
    /* No command, do both */
    ch11_cmd_status(ch, of);
    ch11_cmd_chiptable(ch, of);
  return TRUE;
}

static dvureg_t
ch11_pivec(register struct device *d)
{
    (*d->dv_pifun)(d, 0);	/* Turn off interrupt request */
    return d->dv_brvec;		/* Return vector to use */
}

static int
ch11_init(struct device *d, FILE *of)
{
    register struct ch11 *ch = (struct ch11 *)d;

    if (!chudp_init(ch, of))
	return FALSE;
    ch_clear(ch);
    return TRUE;
}

/* CH11_POWOFF - Handle "power-off" which usually means the KLH10 is
**	being shut down.  This is important if using a dev subproc!
*/
static void
ch11_powoff(struct device *d)
{
    chudp_kill((struct ch11 *)d);
}


static void
ch11_clear(register struct device *d)
{
    ch_clear((struct ch11 *)d);
}

/* CH_CLEAR - clear device */
static void
ch_iint_off(register struct ch11 *ch)
{
    if (ch->ch_ipireq) {
	ch->ch_ipireq = 0;	/* Clear any interrupt request */
	if (ch->ch_opireq == 0)	/* if no output interrupt requested */
	  (*ch->ch_dv.dv_pifun)(&ch->ch_dv, 0);
    }
}
static void
ch_oint_off(register struct ch11 *ch)
{
    if (ch->ch_opireq) {
	ch->ch_opireq = 0;	/* Clear any interrupt request */
	if (ch->ch_ipireq == 0)	/* if no input interrupt requested */
	  (*ch->ch_dv.dv_pifun)(&ch->ch_dv, 0);
    }
}

static void
ch_iclear(register struct ch11 *ch)
{
  ch_iint_off(ch);
  if (ch->ch_rbuf)
    ch->ch_iptr = ch->ch_rbuf + DPCHUDP_DATAOFFSET;
  else
    ch->ch_iptr = NULL;
  ch->ch_rcnt = -1;
  ch->ch_lost = 0;		/* When receiver is re-enabled (write 1 into %CARDN)
				   the count is then cleared. */
  ch->ch_inactf = TRUE;		/* OK to receive */
  REG(ch) &= ~(CH_ERR|CH_RDN);		/* No checksum error, no pkt here */
}

static void
ch_oclear(register struct ch11 *ch)
{
  ch_oint_off(ch);
  if (ch->ch_sbuf)
    ch->ch_optr = ch->ch_sbuf + DPCHUDP_DATAOFFSET;
  else
    ch->ch_optr = NULL;
  REG(ch) &= ~CH_TAB;		/* Not aborted */
  if (ch->ch_outactf)		/* OK to transmit? */
    REG(ch) |= CH_TDN;		/* Ready to send (again) */
}
  
static void
ch_clear(register struct ch11 *ch)
{
    chudp_stop(ch);		/* Kill CHUDP process, ready line going down */

    ch->ch_outactf = TRUE;	/* Initialise to true */

    ch_iclear(ch);
    ch_oclear(ch);
}

static unsigned int
ch_checksum(const unsigned char *addr, int count)
{
  /* RFC1071 */
  /* Compute Internet Checksum for "count" bytes
   *         beginning at location "addr".
   */
  register long sum = 0;

  while( count > 1 )  {
    /*  This is the inner loop */
    sum += *(addr)<<8 | *(addr+1);
    addr += 2;
    count -= 2;
  }

  /*  Add left-over byte, if any */
  if( count > 0 )
    sum += * (unsigned char *) addr;

  /*  Fold 32-bit sum to 16 bits */
  while (sum>>16)
    sum = (sum & 0xffff) + (sum >> 16);

  return (~sum) & 0xffff;
}

static dvureg_t
ch11_read(struct device *d, register uint18 addr)
{
  register struct ch11 *ch = (struct ch11 *)d;
  dvureg_t val;

  if (DVDEBUG(ch) > 4)
    fprintf(DVDBF(ch), "[CH11 Read %#o]\r\n", addr);

  if (ch->ch_dp.dp_chpid == 0)	/* if DP process is not running, start it */
    chudp_start(ch);

  switch (addr) {
/*  case UB_CHWBF:		/* Write buffer (write only) = CHMYN */
/*     break; */
  case UB_CHMYN:		/* My chaos address (read only) */
    val = ch->ch_myaddr;
    break;
  case UB_CHCSR:		/* Command status reg */
    val = ch->ch_reg;
    val &= ~(CH_LOS);		/* clear lost field */
    val |= (ch->ch_lost & 017) << 9; /* insert current counter */
    break;
  case UB_CHRBF:		/* Read buffer (read only) */
    /* The first word read will be filled to the left
       to make the message recieved a multiple of 16 bits.
       #### (Assume this is managed by ITS, until found wrong.)
       */
    if (ch->ch_rcnt > 0) {
      val = *(ch->ch_iptr++) << 8;
      val |= *(ch->ch_iptr++);
      if ((ch->ch_iptr - ch->ch_rbuf) >= (ch->ch_rcnt + DPCHUDP_DATAOFFSET)) {
	if (DVDEBUG(ch) > 4)
	  fprintf(DVDBF(ch), "[CH11 reading last word, clearing RDN]\r\n");
	ch->ch_rcnt = -1;	/* read last word */
	REG(ch) &= ~CH_RDN;	/* Done receiving? */
      }
      break;
    }
    val = 0;
    break;
  case UB_CHRBC:		/* Receive bit counter (read only) */
    if (ch->ch_rcnt > 0)
      val = (dvureg_t)ch->ch_rcnt*8-1; /* byte count */
    else
      val = (dvureg_t)07777;
    break;
  case UB_CHXMT:		/* Initiate transmit (read only) */
    /* last 16b in buffer is dest addr;
       add source addr and checksum, and send
       */
    if (ch->ch_optr) {		/* #### range check too */
      int cks, len;
      /* Dest addr already in data, checksum at end */
      len = (ch->ch_optr - ch->ch_sbuf)-DPCHUDP_DATAOFFSET+2;
      /* Add source */
      *(ch->ch_optr++) = (ch->ch_myaddr>>8);
      *(ch->ch_optr++) = (ch->ch_myaddr & 0xff);
      /* Make checksum */
      cks = ch_checksum(&ch->ch_sbuf[DPCHUDP_DATAOFFSET],len);
      *(ch->ch_optr++) = cks >> 8;
      *(ch->ch_optr++) = cks & 0xff;

      chudp_outxfer(ch);		/* Send it to DP */
    } else
      panic("ch11_read: no output pointer available at CHXMT");
    val = (dvureg_t) ch->ch_myaddr;
    break;
  default:
    panic("ch11_read: Unknown register %lo", (long)addr);
  }
  if (DVDEBUG(ch) > 4)
    fprintf(DVDBF(ch), "[CH11 Read %#o => %o]\r\n", addr, val);
  return val;
}

/* Write CH11 registers.
*/
static void
ch11_write(struct device *d, uint18 addr, register dvureg_t val)
{
  register struct ch11 *ch = (struct ch11 *)d;

  val &= MASK16;
  if (DVDEBUG(ch) > 4)
    fprintf(DVDBF(ch), "[CH11 Write %#o <= %#lo]\r\n", addr, (long)val);

  if (ch->ch_dp.dp_chpid == 0)	/* if DP process is not running, start it */
    chudp_start(ch);

  switch (addr) {
/*  case UB_CHMYN:		/* My chaos address (read only) = CHWBF */
  case UB_CHRBF:		/* Read buffer (read only) */
  case UB_CHRBC:		/* Receive bit counter (read only) */
  case UB_CHXMT:		/* Initiate transmit (read only) */
    panic("ch11_write: read-only address %lo", (long)addr);
    break;
  case UB_CHWBF:		/* Write buffer (write only) */
    /* AIM628: TDN cleared when a word is written into the outgoingpacket buffer */
    REG(ch) &= ~CH_TDN;
    if (ch->ch_optr) {		/* #### range check too */
      *(ch->ch_optr++) = val>>8;	/* write two bytes */
      *(ch->ch_optr++) = val&0xff;
    } else {
      panic("ch11_write: no output pointer available at CHWBF");
    }
    return;
  case UB_CHCSR:		/* Command status reg */
    if (val & (CH_BSY | CH_TAB | CH_LOS | CH_ERR)) {
      panic("ch11_write: writing read-only bits to CSR: %lo", (long)val);
      break;
    }
    if (val & (CH_LUP | CH_SPY)) {
      if (DVDEBUG(ch))
	fprintf(DVDBF(ch), "[CH11: loopback/spy being set - NYI]\r\n");
      /* AIM628:
	 LUP: the cable and transmitter are not used and the interface
	 is looped back to itself.  This is for maintenance.
	 SPY: the interface will receive all packets regardless of
	 their destination.  This is for maintenance and network
	 monitoring.
      */
      /* LUP could possibly be usefully implemented, but SPY not, over UDP. */
      val &= ~(CH_LUP | CH_SPY);
    }
    if (val & CH_RST) {		/* I/O reset */
      ch_clear(ch);
      if (DVDEBUG(ch) > 4)
	fprintf(DVDBF(ch),"[CH11 new CSR contents %#lo]\r\n", (long)REG(ch));
      return;
    }
    if (val & CH_RCL) {		/* Clear and enable the receiver, it can now gobble another msg */
      /* AIM628: clears RDN and enables rcvr to receive another pkt */
      /* AIM628: resets the lost count to 0 */
      val &= ~(CH_RCL|CH_RDN);
      /* reset buffer, bit count etc */
      ch_iclear(ch);
    }
    if (val & CH_TCL) {		/* Clear the transmitter, making it ready */
      /* AIM628: stops transmitter and sets TDN */
      /* #### do we need to "stop transmitter" (dpchudp)? */
      val &= ~CH_TCL;
      val |= CH_TDN;
      /* Done below */
/*       ch_oclear(ch); */
    }
    if (val & CH_RDN) {		/* RCV DONE */
      val &= ~CH_RDN;
      ch_iclear(ch);		/* same as RCL? */
    }

    REG(ch) |= val;		/* save bits */

    if (val & CH_TDN)		/* Transmit Done. Set when transmitter is done */
      ch_oclear(ch);		/* same effect as TCL? (cf SYSTEM;CHAOS, CHXBK7+4) */

    /* AIM628: when both xDN and xEN are set, the computer is interrupted */
    if ((val & CH_TEN) && (val & CH_REN)) {
      /* try to be clever */
      if (dp_xstest(dp_dpxfr(&ch->ch_dp))) {
	ch_ogo(ch);		/* sender's turn, process output first */
	ch_igo(ch);
      } else {
	ch_igo(ch);		/* receiver's turn */
	ch_ogo(ch);
      }
    } else if (val & CH_TEN) {
      REG(ch) &= ~CH_REN;
      ch_iint_off(ch);
      ch_ogo(ch);
    } else if (val & CH_REN) {
      REG(ch) &= ~CH_TEN;
      ch_oint_off(ch);
      ch_igo(ch);
    } else {
      REG(ch) &= ~(CH_REN|CH_TEN);
      ch_iint_off(ch);
      ch_oint_off(ch);
    }

    if (DVDEBUG(ch) > 4)
      fprintf(DVDBF(ch),"[CH11 new CSR contents %#lo]\r\n", (long)REG(ch));

    return;
  default:
    panic("ch11_write: Unknown address %lo", (long)addr);
  }
}

/* Generate CH11 output interrupt */

static void
ch_oint(register struct ch11 *ch)
{
    if (REG(ch) & CH_TEN) {
	if (DVDEBUG(ch))
	    fprintf(DVDBF(ch), "[CH11: output int]\r\n");
	ch->ch_opireq = TRUE;
	(*ch->ch_dv.dv_pifun)(&ch->ch_dv,	/* Put up interrupt */
				(int)ch->ch_dv.dv_brlev);
    }
}

/* Generate CH11 input interrupt */

static void
ch_iint(register struct ch11 *ch)
{
    if (REG(ch) & CH_REN) {
	if (DVDEBUG(ch))
	    fprintf(DVDBF(ch), "[CH11: input int]\r\n");
	ch->ch_ipireq = TRUE;
	(*ch->ch_dv.dv_pifun)(&ch->ch_dv,	/* Put up interrupt */
				(int)ch->ch_dv.dv_brlev);
    }
}

/* Activate input side - allow CHUDP input to be received and processed.
*/
static void
ch_igo(register struct ch11 *ch)
{
    ch->ch_inactf = TRUE;		/* OK to start reading input! */

    if (chudp_incheck(ch)) {		/* Do initial check for input */
	chudp_inxfer(ch);			/* Have input!  Go snarf it! */
	ch_idone(ch);			/* Finish up CH input done */
    }
}

/* CH input done - called to finish up CHUDP input
*/
static void
ch_idone(register struct ch11 *ch)
{
    ch->ch_inactf = FALSE;	/* don't read another just yet */

    ch_iint(ch);			/* Send input interrupt! */
}

static int
ch_outcheck(register struct ch11 *ch)
{
  /* check if there's some output to send */
  return ch->ch_outactf && (((ch->ch_optr - ch->ch_sbuf) - DPCHUDP_DATAOFFSET) > 0);
}

/* Activate output side - send a message to the CHUDP.
** If can't do it because an outbound message is already in progress,
** complain and cause an error.
*/
static void
ch_ogo(register struct ch11 *ch)
{
  if (ch_outcheck(ch)) {
    if (!chudp_outxfer(ch)) {
      /* Couldn't output, so abort and done immediately */
      REG(ch) |= (CH_TAB|CH_TDN); /* #### */
	if (DVDEBUG(ch))
	    fprintf(DVDBF(ch), "[CH11 out err - overrun]\r\n");
    }
    ch_oint(ch);
    return;
    /* DPCHUDP will call ch_evhsdon() when ready for more output. */
  } else {
    ch_oint(ch);		/* ITS seems to need this? */
    return;
  }
}

/* CH output done - called to finish up CHUDP output
*/
static void
ch_odone(register struct ch11 *ch)
{
    REG(ch) |= CH_TDN;		/* Note it's done */
    ch->ch_outactf = TRUE;	/* OK to send another */
    ch_oint(ch);			/* Send output interrupt! */
}

/* VIRTUAL CHUDP ROUTINES
*/

/* Utility routine */

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


static void ch_evhrwak(struct device *d, struct dvevent_s *evp);
static void ch_evhsdon(struct device *d, struct dvevent_s *evp);

/* static */
int
chudp_init(register struct ch11 *ch, FILE *of)
{
    register struct dpchudp_s *dpc;
    struct dvevent_s ev;
    size_t junk;

    ch->ch_dpstate = FALSE;
    if (!dp_init(&ch->ch_dp, sizeof(struct dpchudp_s),
			DP_XT_MSIG, SIGUSR1, (size_t)CHUDPBUFSIZ,	   /* in */
			DP_XT_MSIG, SIGUSR1, (size_t)CHUDPBUFSIZ)) { /* out */
	if (of) fprintf(of, "CHUDP subproc init failed!\n");
	return FALSE;
    }
    ch->ch_sbuf = dp_xsbuff(&(ch->ch_dp.dp_adr->dpc_todp), &junk);
    ch->ch_rbuf = dp_xrbuff(&(ch->ch_dp.dp_adr->dpc_frdp), &junk);

    ch->ch_dv.dv_dpp = &(ch->ch_dp);	/* Tell CPU where our DP struct is */

    /* Set up DPCHUDP-specific part of shared DP memory */
    dpc = (struct dpchudp_s *) ch->ch_dp.dp_adr;
    dpc->dpchudp_dpc.dpc_debug = ch->ch_dpdbg;	/* Init DP debug flag */
    if (cpu.mm_locked)				/* Lock DP mem if CPU is */
	dpc->dpchudp_dpc.dpc_flags |= DPCF_MEMLOCK;

    dpc->dpchudp_ver = DPCHUDP_VERSION;
    dpc->dpchudp_attrs = 0;

    dpc->dpchudp_backlog = ch->ch_backlog;	/* Pass on backlog value */
    dpc->dpchudp_dedic = ch->ch_dedic;	/* Pass on dedicated flag */

    if (ch->ch_ifnam)			/* Pass on interface name if any */
	strncpy(dpc->dpchudp_ifnam, ch->ch_ifnam, sizeof(dpc->dpchudp_ifnam)-1);
    else
	dpc->dpchudp_ifnam[0] = '\0';	/* No specific interface */

    dpc->dpchudp_myaddr = ch->ch_myaddr; /* Set our Chaos address */
    dpc->dpchudp_port = ch->ch_chudp_port;
    
    /* copy chip table */
    for (junk = 0; junk < ch->ch_chip_tlen; junk++) {
      memset(&dpc->dpchudp_chip_tbl[junk], 0, sizeof(struct dpchudp_chip));
      dpc->dpchudp_chip_tbl[junk].dpchudp_chip_chaddr = ch->ch_chip_tbl[junk].ch_chip_chaddr;
      dpc->dpchudp_chip_tbl[junk].dpchudp_chip_ipport = ch->ch_chip_tbl[junk].ch_chip_ipport;
      memcpy(&dpc->dpchudp_chip_tbl[junk].dpchudp_chip_ipaddr,
	     &ch->ch_chip_tbl[junk].ch_chip_ipaddr, sizeof(struct in_addr));
    }
    dpc->dpchudp_chip_tlen = ch->ch_chip_tlen;

    /* Register ourselves with main KLH10 loop for DP events */

    ev.dvev_type = DVEV_DPSIG;		/* Event = Device Proc signal */
    ev.dvev_arg.eva_int = SIGUSR1;
    ev.dvev_arg2.eva_ip = &(ch->ch_dp.dp_adr->dpc_todp.dpx_donflg);
    if (!(*ch->ch_dv.dv_evreg)((struct device *)ch, ch_evhsdon, &ev)) {
	if (of) fprintf(of, "CHUDP event reg failed!\n");
	return FALSE;
    }

    ev.dvev_type = DVEV_DPSIG;		/* Event = Device Proc signal */
    ev.dvev_arg.eva_int = SIGUSR1;
    ev.dvev_arg2.eva_ip = &(ch->ch_dp.dp_adr->dpc_frdp.dpx_wakflg);
    if (!(*ch->ch_dv.dv_evreg)((struct device *)ch, ch_evhrwak, &ev)) {
	if (of) fprintf(of, "CHUDP event reg failed!\n");
	return FALSE;
    }
    return TRUE;
}

static int
chudp_start(register struct ch11 *ch)
{
    register int res;

    if (DVDEBUG(ch))
	fprintf(DVDBF(ch), "[chudp_start: starting DP \"%s\"...",
				ch->ch_dpname);

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
    res = dp_start(&ch->ch_dp, ch->ch_dpname);
    clk_resume();			/* Resume internal clock if one */

    if (!res) {
	if (DVDEBUG(ch))
	    fprintf(DVDBF(ch), " failed!]\r\n");
	else
	    fprintf(DVDBF(ch), "[chudp_start: Start of DP \"%s\" failed!]\r\n",
				ch->ch_dpname);
	return FALSE;
    }
    if (DVDEBUG(ch))
	fprintf(DVDBF(ch), " started!]\r\n");

    REG(ch) |= CH_REN;
    ch_iint(ch);		/* Kick start */

    return TRUE;
}

/* CHUDP_STOP - Stops CHUDP and drops Host Ready by killing CHUDP subproc,
**	but allow restarting.
*/
static void
chudp_stop(register struct ch11 *ch)
{
    if (DVDEBUG(ch))
	fprintf(DVDBF(ch), "[CHUDP: stopping...");

    dp_stop(&ch->ch_dp, 1);	/* Say to kill and wait 1 sec for synch */

    ch->ch_dpstate = FALSE;	/* No longer there and ready */
    if (DVDEBUG(ch))
	fprintf(DVDBF(ch), " stopped]\r\n");
}


/* CHUDP_KILL - Kill CHUDP process permanently, no restart.
*/
static void
chudp_kill(register struct ch11 *ch)
{
    if (DVDEBUG(ch))
	fprintf(DVDBF(ch), "[CHUDP kill]\r\n");

    ch->ch_dpstate = FALSE;
    (*ch->ch_dv.dv_evreg)(	/* Flush all event handlers for device */
		(struct device *)ch,
		NULLPROC,
		(struct dvevent_s *)NULL);
    dp_term(&(ch->ch_dp), 0);	/* Flush all subproc overhead */
    ch->ch_sbuf = NULL;		/* Clear pointers no longer meaningful */
    ch->ch_rbuf = NULL;
}


/* CH_EVHRWAK - Invoked by INSBRK event handling when
**	signal detected from DP saying "wake up"; the DP is sending
**	us an input packet.
*/
static void
ch_evhrwak(struct device *d, struct dvevent_s *evp)
{
    register struct ch11 *ch = (struct ch11 *)d;

    if (DVDEBUG(ch))
	fprintf(DVDBF(ch), "[CH11 input wakeup: %d]",
				(int)dp_xrtest(dp_dpxfr(&ch->ch_dp)));

    /* Always check CHUDP input in order to process any non-data messages
    ** regardless of whether CH is actively reading,
    ** then invoke general CH check to do data transfer if OK.
    */
    if (chudp_incheck(ch)) {
      if (!ch->ch_inactf) {
	ch->ch_lost++;		/* received, but busy */
	dp_xrdone(dp_dpxfr(&ch->ch_dp)); /* ack to DP! */
      } else {
	chudp_inxfer(ch);			/* Have input!  Go snarf it! */
	ch_idone(ch);			/* Finish up CH input done */
      }
    }
}

static void
ch_evhsdon(struct device *d, struct dvevent_s *evp)
{
    register struct ch11 *ch = (struct ch11 *)d;
    register struct dpx_s *dpx = dp_dpxto(&ch->ch_dp);

    if (DVDEBUG(ch))
	fprintf(DVDBF(ch), "[ch_evhsdon: %d]", (int)dp_xstest(dpx));

    if (dp_xstest(dpx)) {	/* Verify message is done */
	ch_odone(ch);		/* Say CH output done */
    }
}


/* Start CHUDP output.
*/
static int
chudp_outxfer(register struct ch11 *ch)
{
    register int cnt;
    register struct dpx_s *dpx = dp_dpxto(&ch->ch_dp);

    /* Make sure we can output message and fail if not */
    if (!dp_xstest(dpx)) {
      if (DVDEBUG(ch))
	fprintf(DVDBF(ch), "[CHUDP: DP out blocked]\r\n");
      return 0;
    }

    ch->ch_outactf = FALSE;	/* Don't send another just yet */

    /* Output xfer requested! */
    register struct dpchudp_s *dpc = (struct dpchudp_s *) ch->ch_dp.dp_adr;
#if 1 /* #### Debug */
      int chlen = ((ch->ch_sbuf[DPCHUDP_DATAOFFSET+2] & 0xf) << 4) | ch->ch_sbuf[DPCHUDP_DATAOFFSET+3];
#endif
    cnt = (ch->ch_optr - ch->ch_sbuf) - DPCHUDP_DATAOFFSET;
#if 1 /* #### Debug */
    if ((cnt % 2) == 1)
      fprintf(stderr,"\r\n[CH11 sending odd number of bytes (%d. data len %d.)]\r\n",
	      cnt, chlen);
    if ((chlen + CHAOS_HEADERSIZE + CHAOS_HW_TRAILERSIZE) > cnt)
      fprintf(stderr,"\r\n[CH11 sending less than packet: sending %d, expected %d (Chaos data len %d)]\r\n",
	      cnt, CHAOS_HEADERSIZE + chlen + CHAOS_HW_TRAILERSIZE, chlen);
#endif
    if (DVDEBUG(ch) & DVDBF_DATSHO)	/* Show data? */
      showpkt(DVDBF(ch), "PKTOUT", ch->ch_sbuf + DPCHUDP_DATAOFFSET, cnt);

    dpc->dpchudp_outoff = DPCHUDP_DATAOFFSET;

    REG(ch) &= ~(CH_TAB|CH_TDN); /* Not done yet, not aborted */

    dp_xsend(dpx, DPCHUDP_SPKT, (size_t)cnt + DPCHUDP_DATAOFFSET);

    if (DVDEBUG(ch))
      fprintf(DVDBF(ch), "[CHUDP: Out %d]\r\n", cnt);

    return 1;
}

static int
chudp_incheck(register struct ch11 *ch)
{
    register struct dpx_s *dpx = dp_dpxfr(&ch->ch_dp);

    if (dp_xrtest(dpx)) {	/* Verify there's a message for us */
	switch (dp_xrcmd(dpx)) {
	case DPCHUDP_INIT:
	    ch->ch_dpstate = TRUE;
	    dp_xrdone(dpx);		/* ACK it */
	    return 0;			/* No actual input */

	case DPCHUDP_RPKT:		/* Input packet ready! */
	    if (DVDEBUG(ch))
		fprintf(DVDBF(ch), "[CHUDP: inbuf %ld]\r\n",
			    (long) dp_xrcnt(dpx));
	    return 1;

	default:
	    if (DVDEBUG(ch))
		fprintf(DVDBF(ch), "[CHUDP: R %d flushed]", dp_xrcmd(dpx));
	    dp_xrdone(dpx);			/* just ACK it */
	    return 0;
	}
    }

    return 0;
}


/* CHUDP_INXFER - CHUDP Input.
**	For time being, don't worry about partial transfers (sigh),
**	which might have left part of a previous message still lying
**	around waiting for the next read request.
*/
static void
chudp_inxfer(register struct ch11 *ch)
{
  register int err, cnt, cks;
    register struct dpx_s *dpx = dp_dpxfr(&ch->ch_dp);
    register struct dpchudp_s *dpc = (struct dpchudp_s *) ch->ch_dp.dp_adr;
    register unsigned char *pp;

    /* Assume this is ONLY called after verification by chudp_incheck that
    ** an input message is actually ready.
    */
    cnt = dp_xrcnt(dpx);

    /* Adjust for possible offset */
/*     cnt -= dpc->dpchudp_inoff; */
    pp = ch->ch_rbuf + dpc->dpchudp_inoff;

    if (DVDEBUG(ch))
	fprintf(DVDBF(ch), "[CHUDP: In %d]\r\n", cnt);
    if (DVDEBUG(ch) & DVDBF_DATSHO)	/* Show data? */
	showpkt(DVDBF(ch), "PKTIN ", pp, cnt);

    /* Message already read in! */
    if (cnt & 01) {		/* If msg len not multiple of 2, pad out */
      pp[cnt+1] = '\0';
    }

    ch->ch_rcnt = cnt;
    ch->ch_iptr = pp;

    /* check hw trailer: dest, checksum */
    if ((((pp[cnt-6]<<8) | pp[cnt-5]) != ch->ch_myaddr)) {
      if (DVDEBUG(ch))
	fprintf(DVDBF(ch), "[CHUDP: not for my address: destination %o]\r\n",
		((pp[cnt-6]<<8) | pp[cnt-5]));
    } else {
      cks = ch_checksum(pp,cnt);
      if (cks != 0) {
	if (DVDEBUG(ch))
	  fprintf(DVDBF(ch), "[CHUDP: bad checksum 0x%x]\r\n", cks);
#if 1 /* 0 for testing */
	REG(ch) |= CH_ERR;
#endif
      }
      REG(ch) |= CH_RDN;		/* Note it's done! */
    }
    ch->ch_inactf = TRUE;	/* OK to read another */
    dp_xrdone(dpx);		/* Done, can now ACK */
}

#endif /* KLH10_DEV_CH11 */

