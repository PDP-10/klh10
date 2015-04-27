/* DVUBA.C - KLH10 Emulation of KS10 Unibus Adapters
*/
/* $Id: dvuba.c,v 2.4 2002/05/21 09:44:52 klh Exp $
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
 * $Log: dvuba.c,v $
 * Revision 2.4  2002/05/21 09:44:52  klh
 * Complain about bad unibus refs only if ub_debug set
 *
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

/*
**	Provides interface between I/O instructions and UNIBUS devices,
**	including the unibus adapters as devices in their own right.
**
**	Note that unibus addresses and register values are different in
**	the UBA world than they are for "normal" unibus devices.
**	In particular, for the UBA code:
**		Addresses: 18 bits (RH of IO-address)	(18 for real devs)
**		Registers: 32 bits on read; 18 on write (16 for real devs)
*/

#include "klh10.h"

#if !KLH10_CPU_KS && CENV_SYS_DECOSF
	/* Stupid gubbish needed to prevent OSF/1 AXP compiler from
	** halting merely because compiled file is empty!
	*/
static int decosfcclossage;
#endif

#if KLH10_CPU_KS	/* Moby conditional for entire file */

#include <stdio.h>	/* For stderr if buggy */

#include "kn10def.h"
#include "kn10ops.h"
#include "kn10dev.h"
#include "dvuba.h"

#ifdef RCSID
 RCSID(dvuba_c,"$Id: dvuba.c,v 2.4 2002/05/21 09:44:52 klh Exp $")
#endif

/* Imported functions */
extern void pi_devupd(void);

/* Pre-declarations */
static void ubasta_write(struct ubctl *, h10_t);
static uint32 ubapag_read(struct ubctl *, dvuadr_t);


#define PILEV_UB1REQ cpu.pi.pilev_ub1req
#define PILEV_UB3REQ cpu.pi.pilev_ub3req

/* Unibus Adapter data structures.
**	Leave external so CPU can access the PI request status.
**	Easier debugging, too.
*/
struct ubctl dvub1, dvub3;
int ub_debug;		/* Not a full debug trace, just prints warnings */

static void ub_pifun(struct device *, int);

static uint32 ub0_read(dvuadr_t);	/* Special rtns for controller #0 */
static void   ub0_write(dvuadr_t, h10_t);


/* DVUB_INIT - Initialize unibus code & data & devices
*/
void
dvub_init(void)
{
    memset((char *)&dvub1, 0, sizeof(dvub1));
    memset((char *)&dvub3, 0, sizeof(dvub3));

    dvub1.ubn = 1;
    dvub3.ubn = 3;

    /* Remember place to deposit our PI requests with CPU.  This is
    ** something of a hack, but will do for now.
    */
    dvub1.ubpireqs = &cpu.pi.pilev_ub1req;
    dvub3.ubpireqs = &cpu.pi.pilev_ub3req;
}


int
dvub_add(FILE *of,
	 register struct device *d,
	 int ubn)
{
    register struct ubctl *ub;
    register int i;

    switch (ubn) {
    case 1: ub = &dvub1; break;
    case 3: ub = &dvub3; break;
    default:
	fprintf(of, "Cannot bind device to unibus #%d - only 1 and 3 allowed.\n",
			ubn);
	return FALSE;
    }

    if (ub->ubndevs >= KLH10_DEVUBA_MAX) {
	fprintf(of, "Too many unibus #%d devices (limit is %d)\n",
			ubn, KLH10_DEVUBA_MAX);
	return FALSE;
    }

    /* Verify that device registers don't conflict with an existing device */
    for (i = 0; i < ub->ubndevs; ++i) {
	register struct device *d2 = ub->ubdevs[i];

	/* For each existing dev, see if 1st addr comes after new dev's last,
	 * or last addr comes before new dev's first.
	 */
	if ((d->dv_aend <= d2->dv_addr)		/* Comes after? */
	 || (d->dv_addr >= d2->dv_aend)) {	/* Comes before? */
	    continue;		/* OK, keep going */
	}

	/* Overlap -- complain and fail */
	fprintf(of, "Cannot bind device - Unibus register overlap:\r\n\
    Device %-6s %06lo-%06lo\r\n\
    Device %-6s %06lo-%06lo\r\n",
		d->dv_name,  (long)d->dv_addr,  (long)d->dv_aend,
		d2->dv_name, (long)d2->dv_addr, (long)d2->dv_aend);
	return FALSE;
    }

    d->dv_uba = ub;		/* Point to right unibus controller */
    d->dv_pifun = ub_pifun;	/* Use this to handle PI reqs */

    i = ub->ubndevs;
    ub->ubdevs[i+1] = NULL;	/* Set fence in array */
    ub->ubdevs[i] = d;
    ub->ubdvadrs[i*2] = d->dv_addr;
    ub->ubdvadrs[(i*2)+1] = d->dv_aend;
    ub->ubndevs++;
    return TRUE;
}

/* UB_PIFUN - Called from devices with BR # (int level) to assert.
**	If nonzero, maps BR level to PI level and triggers PI.
**	If zero, turns off PI request.
**		Sorta painful cuz must check all other devices to be sure
**		it's OK to turn off the corresponding PI level request.
*/

static void
ub_pifun(register struct device *d,
	 int br)
{
    register struct ubctl *ub = d->dv_uba;	/* Get right UBA for dev */
    register int lev;
    register struct device **dp;

    switch (br) {
    case 0:			/* Turn off request for this device */
	d->dv_pireq = 0;	/* Turn off for specific device */

	/* Now propagate turnoff up the chain */

	/* Find new outstanding level, if any.
	** This scan is a bit ugly.
	*/
	lev = 0;
	for (dp = ub->ubdevs; *dp; ++dp)	/* Check all known devs */
	    lev |= (*dp)->dv_pireq;

	if (lev == *(ub->ubpireqs))	/* Compare new level with current */
	    return;			/* No change, do nothing */
	*(ub->ubpireqs) = lev;		/* Changed!  Set new */
	pi_devupd();			/* Update main PI state */
	return;

    case 4:
    case 5:
	lev = ub->ublolev;	/* BRs 4 and 5 are mapped to "low" PI */
	break;
    case 6:
    case 7:
	lev = ub->ubhilev;	/* BRs 6 and 7 are mapped to "high" PI */
	break;
    default:
	panic("ub_pireq: unknown BR level: %d", br);
    }

    /* If broke out of switch, triggering interrupt at level "lev" */
    if (!lev)			/* UBA may have no interrupt mapping */
	return;

    d->dv_pireq = lev;		/* Set PI request for specific device */
    if (*(ub->ubpireqs) & lev)	/* This level PI already requested? */
	return;			/* Yep, needn't yell at CPU again */

    *(ub->ubpireqs) |= lev;	/* Nope, tell CPU about new PI! */
    pi_devupd();		/* Update main PI state */
}


/* UB_DEVFIND - Look up device given its controller and unibus address.
**	This is a slow but sure crock for now; it can be done much
**	more cleverly with a hash table or even a sorted binary-searched
**	order at runtime when devices are configured.  Later...
*/
static struct device *
ub_devfind(register struct ubctl *ub,
	   register dvuadr_t addr)
{
    register int i = 0, n = ub->ubndevs;
    register dvuadr_t *ua = ub->ubdvadrs;

    for (; i < n; ua += 2, ++i) {
      if (ua[0] <= addr && addr < ua[1])
	return ub->ubdevs[i];
    }
    return NULL;		/* None found */
}

/* Auxiliaries */

#define ubasta_read(ub) ((uint32)((ub)->ubsta))
#define ubapag_write(ub,addr,val) ((ub)->ubpmap[(addr) - UB_UBAPAG] = (val))

static void
ub_badctl(char *fn, w10_t ioaddr, int bflag)
{
    if (ub_debug)
	fprintf(stderr, "%s: Bad UB ctlr in IO addr: %lo,,%lo\r\n",
		fn, (long)LHGET(ioaddr), (long)RHGET(ioaddr));
    pag_iofail(((paddr_t)LHGET(ioaddr))<<18 | RHGET(ioaddr), bflag);
}

static void
ub_badaddr(char *fn, dvuadr_t uaddr, int bflag)
{
    if (ub_debug)
	fprintf(stderr, "%s: Bad unibus IO addr: %lo\r\n", fn, (long)uaddr);
    pag_iofail((paddr_t)uaddr, bflag);
}

/* UB_READ - Called by KS10 IO instructions to read the Unibus.
** UB_WRITE - Ditto to write
*/
uint32
ub_read(register w10_t ioaddr)
{
    switch (LHGET(ioaddr)) {		/* Check controller # */
	case 0: return ub0_read((dvuadr_t)RHGET(ioaddr));
	case 1: return uba_read(&dvub1, (dvuadr_t)RHGET(ioaddr));
	case 3: return uba_read(&dvub3, (dvuadr_t)RHGET(ioaddr));
    }
    ub_badctl("ub_read", ioaddr, FALSE);
    return 0;		/* Never returns */
}

uint32
ub_bread(register w10_t ioaddr)
{
    register dvuadr_t uaddr;
    register uint32 reg;

    uaddr = RHGET(ioaddr) & ~01;	/* Get address w/o low bit */
    switch (LHGET(ioaddr)) {		/* Check controller # */
	case 0: reg = ub0_read(uaddr);	break;
	case 1: reg = uba_read(&dvub1, uaddr);	break;
	case 3: reg = uba_read(&dvub3, uaddr);	break;
	default:
	    ub_badctl("ub_bread", ioaddr, TRUE);
	    /* Never returns */
    }
    if (RHGET(ioaddr) & 01)
	reg = reg >> 8;
    return reg & 0377;
}

void
ub_write(register w10_t ioaddr,
	 h10_t val)
{
    switch (LHGET(ioaddr)) {		/* Check controller # */
	case 0: ub0_write((dvuadr_t)RHGET(ioaddr), val);	break;
	case 1: uba_write(&dvub1, (dvuadr_t)RHGET(ioaddr), val);	break;
	case 3: uba_write(&dvub3, (dvuadr_t)RHGET(ioaddr), val);	break;
	default:
	    ub_badctl("ub_write", ioaddr, FALSE);
	    /* Never returns */
    }
}

void
ub_bwrite(register w10_t ioaddr,
	  register h10_t val)
{
    register dvuadr_t uaddr;
    register uint32 mask;

    if ((uaddr = RHGET(ioaddr)) & 01) {
	uaddr &= ~01;
	mask = 0377 << 8;
	val <<= 8;
    } else {
	mask = 0377;
    }
    val &= mask;

    switch (LHGET(ioaddr)) {		/* Check controller # */
	case 0: ub0_write(uaddr, (ub0_read(uaddr) & ~mask) | val);	break;
	case 1: uba_write(&dvub1, uaddr,
			(uba_read(&dvub1, uaddr) & ~mask) | val);	break;
	case 3: uba_write(&dvub3, uaddr,
			(uba_read(&dvub3, uaddr) & ~mask) | val);	break;
	default:
	    ub_badctl("ub_bwrite", ioaddr, TRUE);
	    /* Never returns */
    }
}

static uint32
ub0_read(dvuadr_t addr)
{
    switch (addr) {
    case UB_KSECCS:		/* Memory Status Register */
	return 0;		/* Ignore writes, return 0 on reads */
    }
    ub_badaddr("ub0_read", addr, FALSE);
    return 0;			/* Never returns */
}

static void
ub0_write(dvuadr_t addr,
	  h10_t val)
{
    switch (addr) {
    case UB_KSECCS:		/* Memory Status Register */
	return;			/* Ignore writes */
    }
    ub_badaddr("ub0_write", addr, FALSE);
    return;			/* Never returns */
}

uint32
uba_read(register struct ubctl *ub,
	 dvuadr_t addr)
{
    register struct device *d;

    if (d = ub_devfind(ub, addr))
	return (uint32) (*(d->dv_read))(d, addr);

    switch (addr) {
    case UB_UBASTA:		/* Unibus Status Register */
	return ubasta_read(ub);

    case UB_UBAMNT:		/* Unibus Maintenance Register */
	return 0;		/* Ignore writes, return 0 on read */
    }

    if (UB_UBAPAG <= addr && addr < UB_UBAEND) {
	return ubapag_read(ub, addr);
    }
    ub_badaddr("uba_read", addr, FALSE);
    return 0;		/* Never returns */
}

void
uba_write(register struct ubctl *ub,
	  register dvuadr_t addr,
	  h10_t val)			/* UBA itself can take 18 bits */
{
    register struct device *d;

    if (d = ub_devfind(ub, addr)) {
	(*(d->dv_write))(d, addr, (dvureg_t)val);
	return;
    }

    switch (addr) {
    case UB_UBASTA:		/* Unibus Status Register */
	ubasta_write(ub, val);
	return;

    case UB_UBAMNT:		/* Unibus Maintenance Register */
	return;			/* Ignore writes, return 0 on read */
    }

    if (UB_UBAPAG <= addr && addr < UB_UBAEND) {
	ubapag_write(ub, addr, val);
	return;
    }
    ub_badaddr("uba_write", addr, FALSE);
}

/* Write status reg.
**	Note corresponding ubasta_read() is a macro.
*/
static void
ubasta_write(register struct ubctl *ub,
	     register h10_t val)
{
    register h10_t clrbits, setbits;

    /* Find bits to clear */
    clrbits = (val & (UBA_BTIM|UBA_BBAD|UBA_BPAR|UBA_BNXD)) | UBA_BPWR
		| (UBA_BDXF|UBA_BINI|UBA_BPIH|UBA_BPIL);

    /* Find bits to set */
    setbits = (val & (UBA_BDXF|UBA_BINI|UBA_BPIH|UBA_BPIL));
    ub->ubsta = (ub->ubsta & ~clrbits) | setbits;
    if (ub->ubsta & UBA_BINI) {
	/* Do something to initialize this unibus?
	** Note: ITS source says can't both init and set PI levels,
	**	hence the clearing done here for sake of emulation.
	*/
	val = (ub->ubsta &= ~(UBA_BINI|UBA_BPIH|UBA_BPIL));
    }
    ub->ubhilev = pilev_bits[(val & UBA_BPIH)>>3];
    ub->ublolev = pilev_bits[val & UBA_BPIL];
}

/* Read paging regs.
** Note corresponding ubapag_write() is a macro
*/
static uint32
ubapag_read(register struct ubctl *ub,
	    dvuadr_t addr)
{
    register uint32 wval;
    register uint18 ret;

    /* Reading, must get bits in screwy format */
    ret = ub->ubpmap[addr - UB_UBAPAG];
    wval = ((ret & (UBA_QRPW|UBA_Q16B|UBA_QFST|UBA_QVAL)) >> 5)
		| UBA_PPVL		/* Parity always valid */
		| ((ret & UBA_QPAG) >> 9);
    wval = (wval << 18) | (((ret & UBA_QPAG) << 9) & H10MASK);
    return wval;
}

/* UBn_PIVGET - Called by PI system when responding to a PI request.
**	Given a PI level mask (1 bit set),
**	returns vector address for the "closest" device currently requesting
**	an interrupt on that level.  If none, returns 0.
*/
paddr_t
ub1_pivget(int lev)
{
    register struct device **dp = dvub1.ubdevs;

    /* Return first device wanting attention on given level. */
    for (; *dp; dp++)
	if (lev == (*dp)->dv_pireq)
	    return (paddr_t)(*(*dp)->dv_pivec)(*dp);
    return 0;
}

paddr_t
ub3_pivget(int lev)
{
    register struct device **dp = dvub3.ubdevs;

    /* Return first device wanting attention on given level. */
    for (; *dp; dp++)
	if (lev == (*dp)->dv_pireq)
	    return (paddr_t)(*(*dp)->dv_pivec)(*dp);
    return 0;
}

#endif /* KLH10_CPU_KS */
