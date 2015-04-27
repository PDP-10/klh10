/* INIO.C - I/O Instructions (Ouch!!)
*/
/* $Id: inio.c,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: inio.c,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#include "klh10.h"
#include "kn10def.h"
#include "kn10ops.h"
#include "kn10dev.h"
#include "dvuba.h"

#ifdef RCSID
 RCSID(inio_c,"$Id: inio.c,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

/* See CODING.TXT for guidelines to coding instruction routines. */

/*
	"IO Instruction" theoretically includes all 07xx opcodes, although
with newer processors (KL and KS) it is possible to have essentially
normal instructions with opcodes in the 07xx range.  In the KLH10 the
following terms will hold:
	dev-IO instruction - one interpreted as CONI, DATAI, etc.
		Functions handling these instructions are given only a
		device handle as argument.
	IO instruction - above, plus 07xx instructions either interpreted as
		applying to "internal" devices, or further dispatched by
		by their AC field.
		Functions handling these instructions are given only E as
		argument.

	But all opcodes of 07xx are subject to the IO instruction
restrictions:
    (1) User mode illegal unless UIO set (else does MUUO trap)
    (2) On KI & KL, dev-style IO instrs are ok in user mode for devices 740 up.
	This corresponds to opcodes of 774-777 inclusive.
	(on KA, need UIO; on KS, illegal->MUUO)

Internal devices:			External
	KA10: 2 (APR, PI)		(CLK, PTR, ...)
	KI10: 3 (APR, PI, PAG)		(CLK, PTR, ...)
	KL10: 6 (APR, PI, PAG, CCA, TIM, MTR)
	KS10: no dev-style, all "internal"

External devices:
	4 basic functions: DATAI, DATAO, CONI, CONO.
	For nonexistent external devices,
		DATAI and CONI read zero,
		DATAO and CONO are no-ops.

On the KS10,
	all "IO instrs" not specifically defined are executed as MUUOs.
	However, note CONSZ/CONSO APR/PI are defined even though the PRM
	doesn't mention them.

SPECIAL NOTE for the KL10:
	PRM p.3-2 states:  "CAUTION"
		"All IO instructions in this chapter are for internal devices
		(E bus functions).  An address given by such an instruction
		for storing a result is always interpreted as global in the
		section containing the instruction.  Hence data or conditions
		in cannot be stored in an AC unless the instruction is in
		section 0 or 1."
	[Does this apply to external devices as well?  Assume not.]
	[What about the E for DATAO dev,E ??  Assume not affected by this,
	since data is going OUT, not in -- normal CPU mem ref fetch, then given
	to IO system.  I'm imagining a model where the "IO system", not the
	uprocessor itself, is being given responsibility for storing input
	data, and is crippled in its ability to do so.]


     Dev-IO op	KS10	ITSKS	KL10	KI10	KA10
700 AC
    0	BI APR,	APRID	==	==	?	?
    1	DI APR,	-UUO-	==	DI_APR	DI_APR	==
    2	BO APR,	-UUO-	==	WRFIL	?	?
    3	DO APR,	-UUO-	==	DO_APR	DO_APR	DO_APR
    4	CO APR,	WRAPR	==	==	==	==
    5	CI APR,	RDAPR	==	==	==	==
    6	SZ APR,	 ==	==	==	==	==
    7	SO APR,	 ==	==	==	==	==
    10	BI PI,	-UUO-	==	RDERA	?	?
    11	DI PI,	-UUO-	==	==	?	?	; KL: stats option
    12	BO PI,	-UUO-	==	SBDIAG	?	?
    13	DO PI,	-UUO-	==	==	DO_PI	?	; KL: stats option
    14	CO PI,	WRPI	==	==	==	==
    15	CI PI,	RDPI	==	==	==	==
    16	SZ PI,	 ==	==	==	==	==
    17	SO PI,	 ==	==	==	==	==
701 AC						<end KA devs>
    0	BI PAG,	-UUO-	CLRCSH	-UUO-	?
    1	DI PAG,	RDUBR	==	DI_PAG	DI_PAG
    2	BO PAG,	CLRPT	==	==	?
    3	DO PAG,	WRUBR	==	DO_PAG	DO_PAG
    4	CO PAG,	WREBR	==	CO_PAG	CO_PAG
    5	CI PAG,	RDEBR	==	CI_PAG	CI_PAG
    6	SZ PAG,	-UUO-	==	SZ PAG,	==
    7	SO PAG,	-UUO-	==	SO PAG,	==
    10	BI CCA,	-UUO-	==	"SWP"	<end KI devs>
    11	DI CCA, -UUO-	RDPCST	SWPIA
    12	BO CCA,	-UUO-	==	SWPVA
    13	DO CCA,	-UUO-	WRPCST	SWPUA
    14	CO CCA,	-UUO-	==	"SWP"
    15	CI CCA,	-UUO-	==	SWPIO
    16	SZ CCA,	-UUO-	==	SWPVO
    17	SO CCA,	-UUO-	==	SWPUO
702 AC
    0	BI TIM,	RDSPB	SDBR1	RDPERF
    1	DI TIM,	RDCSB	SDBR2	RDTIME
    2	BO TIM,	RDPUR	SDBR3	WRPAE
    3	DO TIM,	RDCSTM	SDBR4	-MUUO-*		[* PRM 3-54 fn.39]
    4	CO TIM,	RDTIM	==	CO_TIM
    5	CI TIM,	RDINT	==	CI_TIM
    6	SZ TIM,	RDHSB	==	SZ TIM,
    7	SO TIM,	-UUO-	SPM	SO TIM,
    10	BI MTR,	WRSPB	LDBR1	RDMACT
    11	DI MTR, WRCSB	LDBR2	RDEACT
    12	BO MTR,	WRPUR	LDBR3	-MUUO-*		[* PRM 3-54 fn.39]
    13	DO MTR,	WRHSB	LDBR4	-MUUO-*		[* PRM 3-54 fn.39 corrected]
    14	CO MTR,	WRTIM	==	WRTIME
    15	CI MTR,	WRINT	==	CI_MTR
    16	SZ MTR,	WRHSB	==	SZ MTR,
    17	SO MTR,	-UUO-	LPMR	SO MTR,
				<end KL devs>
703 *	-	-UUO-	==
704 *	-	UMOVE	==
705 *	-	UMOVEM	==
706 *	-	-UUO-	==
707 *	-	-UUO-	==
710
	DO PTR,				DO_PTR (Special KI hack)
710-715 * -	IO	==
716,717	* -	BLT	==
720-725	* -	IO	==
726-777	* -	-UUO-	==
*/

/* Instruction dispatch:

The entire range of IO instructions (7xx) is treated merely as a bunch
of normal opcodes initially.  Once vectored, there are three
possibilities:

	(1) General PDP-10 instruction (e.g. PMOVE).
		No special handling other than user-IOT checking.
		Function defined with insdef().  3 args: OP, AC, E.
	(2) AC-dispatch internal device instruction.
		Does a second dispatch after combining AC with opcode.
		Function defined with ioinsdef().  1 arg: E.
	(3) Dev-IO instruction.
		i_iodisp() puts opcode & AC back together to decipher as
		an dev-IO instruction, and executes it for the given device
		as found in devtab[device-#].
		This dispatches through the device vector using one of 4
		functions (cono, coni, datao, datai).  See the device structure
		definition in "kn10dev.h" for their exact prototypes.
*/

#if ! KLH10_CPU_KS	/* All machines except KS */

enum {	/* Low 3 bits of AC in Dev-IO instr are the IO opcode dispatch */
	IDV_BLKI=0, IDV_DATAI,
	IDV_BLKO,   IDV_DATAO,
	IDV_CONO,   IDV_CONI,
	IDV_CONSZ,  IDV_CONSO
};
#endif /* !KS */

#if ! KLH10_CPU_KS	/* All machines except KS */

/* "Dev" IO instr dispatch
**	Does dispatch of traditional IO instructions by device,
**	as opposed to internal-device dispatch (i_io###disp) which uses
**	AC field as a subvector.
**
** Note that input instructions (BLKI,DATAI,CONI) need to be particularly
** careful about resolving their memory ref *prior* to doing I/O or the
** data will be lost if a page fault happens.
*/
extern struct device *devtab[128];	/* From kn10dev.c */

insdef(i_diodisp)
{
    register struct device *dev;
    register vmptr_t vp;
    register w10_t w;

    /* Don't allow users to hack IO instrs unless User-IOT is set */
    if ((PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)
#if KLH10_CPU_KI || KLH10_CPU_KL	/* On KI+KL, devs >= 740 are OK */
	&& (op < 0774))
      || (!PCFTEST(PCF_USR) && PCFTEST(PCF_PUB)	/* Supv mode illegal */
#endif
	  ))
	return i_muuo(op, ac, e);

    /* Find device vector */
    dev = devtab[((op&077)<<1) | ((ac>>3)&01)];
    switch (ac & 07) {

    /* Handle BLKI and BLKO.  Note special use of First-Part-Done flag
    ** similar to that for byte pointers, in case a page fault happens
    ** during the actual data transfer.  The PRM does not explicitly mention
    ** this use, but that seems to be what real machines do.
    ** The BLKI/O pointer needs to be updated so the PDP-10 pagefail
    ** handler can figure out what's going on if it wants to.
    */
    case IDV_BLKI:
    case IDV_BLKO:		/* For now, share BLKI code.  Later can
				** duplicate it if desired for speed. */
	vp = vm_modmap(e);
	w = vm_pget(vp);	/* Get AOBJN pointer */

	if (!PCFTEST(PCF_FPD)) {	/* Unless pointer already inc'd, */
#if KLH10_CPU_KA
	    w10_t tmp = W10XWD(1,1);	/* KA adds 1,,1 */
	    op10m_add(w, tmp);
#else
	    LRHSET(w, (LHGET(w)+1)&H10MASK, (RHGET(w)+1)&H10MASK);
#endif
	    vm_pset(vp, w);		/* Store back inc'd pointer */
	}

	/* Now generate local-section address from RH of pointer */
#if KLH10_EXTADR
	/* Confirm later that this code is correct.
	** Default section # is that of the pointer; should it be PC?
	*/
#endif
	/* Make local address in E out of pointer RH */
	va_lmake(e, va_sect(e), RHGET(w));
	if (ac == IDV_BLKI) {
	    vp = vm_modmap(e);			/* See if page fault first */
	    vm_pset(vp, (*(dev->dv_datai))(dev));	/* Do DATAI */
	} else
	    (*(dev->dv_datao))(dev, vm_read(e));	/* Do DATAO */

	if (LHGET(w))		/* Skip return unless LH 0 */
	    return PCINC_2;
	break;
    case IDV_DATAI:
	vp = vm_modmap(e);			/* Check access first */
	vm_pset(vp, (*(dev->dv_datai))(dev));
	break;
    case IDV_DATAO:
	(*(dev->dv_datao))(dev, vm_read(e));
	break;
    case IDV_CONO:
	(*(dev->dv_cono))(dev, (h10_t)va_insect(e));
	break;
    case IDV_CONI:
	vp = vm_modmap(e);			/* Check access first */
	vm_pset(vp, (*(dev->dv_coni))(dev));
	break;
    case IDV_CONSZ:
	w = (*(dev->dv_coni))(dev);		/* Do CONI even if E=0 */
#if 1
	if (!(va_insect(e) & RHGET(w)))
#else	/* See IO_SZ_APR for comments on this */
	if (va_insect(e) && !(va_insect(e) & RHGET(w)))
#endif
	    return PCINC_2;			/* Skip return */
	break;
    case IDV_CONSO:
	w = (*(dev->dv_coni))(dev);		/* Do CONI even if E=0 */
	if (RHGET(w) & va_insect(e))
	    return PCINC_2;			/* Skip return */
	break;
    }
    return PCINC_1;
}

#endif	/* !KS */

/* PDP-10 opcodes of 7xx come here if there is no "normal" instruction
** defined for them.  This is where the secondary dispatch takes place,
** based either on their AC field (for internal-device IO instrs) or
** on their device code.
**	KA only has device dispatch.
**	KI and KL have both.
**	KS has only AC-field dispatch.
*/

/* Define subvector dispatcher.  One of these is defined for each
** normal-op opcode that is AC-dispatched.  They could all be combined
** into the "generic" dispatcher, but having them separate retains the
** information we already have by virtue of the primary dispatch (ie 9-bit
** opcode value) and lets us avoid a reference and shift/mask of the "op"
** argument and thus is a tiny bit faster.
*/

#define iosubvec_define(name, opcod) \
    insdef(name)					\
    {							\
	/* Don't allow users to hack IO instrs unless User-IOT is set */\
	if ((!PCFTEST(PCF_USR) || PCFTEST(PCF_UIO))	\
	  && opciortn[(((opcod)&077)<<4)+ac])		\
	    return (*opciortn[(((opcod)&077)<<4)+ac])(e);	\
	else return i_muuo(op, ac, e);			\
    }

#if 0
iosubvec_define(i_iodisp, op)	/* Generic dispatcher for AC-IO instrs */
#endif /* 0 */			/* Not needed but saved for posterity */

iosubvec_define(i_io700disp, 0700)
iosubvec_define(i_io701disp, 0701)
iosubvec_define(i_io702disp, 0702)


#if KLH10_SYS_ITS && KLH10_CPU_KS

/* ITS I/O instructions.
*/

/* IORDI (0710) - Read Unibus #3 word at E into AC
*/
insdef(i_iordi)
{
    register uint32 reg;

    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    reg = uba_read(&dvub3, (dvuadr_t)va_insect(e));
    ac_setlrh(ac, (reg>>18), reg & H10MASK);
    return PCINC_1;
}

/* IORDQ (0711) - Read Unibus #1 word at E into AC
*/
insdef(i_iordq)
{
    register uint32 reg;

    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    reg = uba_read(&dvub1, (dvuadr_t)va_insect(e));
    ac_setlrh(ac, (reg>>18), reg & H10MASK);
    return PCINC_1;
}

/* IOWRI (0714) - Write Unibus #3 word from AC into E
*/
insdef(i_iowri)
{
    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    uba_write(&dvub3, (dvuadr_t)va_insect(e), ac_getrh(ac));
    return PCINC_1;
}

/* IOWRQ (0715) - Write Unibus #1 word from AC into E
*/
insdef(i_iowrq)
{
    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    uba_write(&dvub1, (dvuadr_t)va_insect(e), ac_getrh(ac));
    return PCINC_1;
}

/* IORD (0712) - Read Unibus word into AC from IO addr in c(E)
*/
insdef(i_iord)
{
    register uint32 reg;

    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    reg = ub_read(vm_read(e));
    ac_setlrh(ac, (reg>>18), reg & H10MASK);
    return PCINC_1;
}

/* IOWR (0713) - Write Unibus word from AC to IO addr in c(E)
*/
insdef(i_iowr)
{
    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    ub_write(vm_read(e), ac_getrh(ac));
    return PCINC_1;
}


/* Byte-mode IO is only used three times in all of ITS:
**	all in TS3TTY for the DZ-11.
**		IORDBI B,%DZRTC(C)
**		IOWRBI A,%DZRTD(C)
**		IOWRBI B,%DZRTC(C)
*/

/* IORDBI (0720) - Read Unibus #3 byte at E into AC
*/
insdef(i_iordbi)
{
    register uint32 reg;

    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    reg = va_isodd(e)
		? (uba_read(&dvub3, (dvuadr_t)va_insect(e) & ~01) >> 8)
		: uba_read(&dvub3, (dvuadr_t)va_insect(e));
    ac_setlrh(ac, 0, reg & 0377);
    return PCINC_1;
}

/* IOWRBI (0724) - Write Unibus #3 byte from AC into E
**	Note that a word is first read, then written!
*/
insdef(i_iowrbi)
{
    register h10_t mask, val;
    register dvuadr_t uadr;

    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    val = ac_getrh(ac) & 0377;
    if (va_isodd(e)) { mask = ~(0377 << 8); val <<= 8; }
    else mask = ~0377;
    uadr = va_insect(e) & ~(dvuadr_t)01;
    uba_write(&dvub3, uadr, (uba_read(&dvub3, uadr) & mask) | val);
    return PCINC_1;
}

/* IORDBQ (0721) - Read Unibus #1 byte at E into AC
*/
insdef(i_iordbq)
{
    register uint32 reg;

    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    reg = va_isodd(e)
		? (uba_read(&dvub1, (dvuadr_t)va_insect(e) & ~01) >> 8)
		: uba_read(&dvub1, (dvuadr_t)va_insect(e));
    ac_setlrh(ac, 0, reg & 0377);
    return PCINC_1;
}

/* IOWRBQ (0725) - Write Unibus #1 byte from AC into E
**	Note that a word is first read, then written!
*/
insdef(i_iowrbq)
{
    register h10_t mask, val;
    register dvuadr_t uadr;

    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    val = ac_getrh(ac) & 0377;
    if (va_isodd(e)) { mask = ~(0377 << 8); val <<= 8; }
    else mask = ~0377;
    uadr = va_insect(e) & ~(dvuadr_t)01;
    uba_write(&dvub1, uadr, (uba_read(&dvub1, uadr) & mask) | val);
    return PCINC_1;
}

/* IORDB (0722) - Read Unibus byte into AC from IO addr in c(E)
*/
insdef(i_iordb)
{
    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    ac_setlrh(ac, 0, ub_bread(vm_read(e)));
    return PCINC_1;
}

/* IOWRB (0723) - Write Unibus byte from AC to IO addr in c(E)
*/
insdef(i_iowrb)
{
    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    ub_bwrite(vm_read(e), ac_getrh(ac));
    return PCINC_1;
}
#endif /* ITS && KS */

#if (KLH10_SYS_T10 || KLH10_SYS_T20) && KLH10_CPU_KS

/* DEC I/O instructions.
*/

/* IOEACALC - Auxiliary to painfully re-calculate E for IO instructions.
**	Assumes instruction is at PC or at end of an XCT chain starting
**	at PC.  This will not work for IO instructions executed as
**	trap or interrupt instructions, nor PXCT operands!  All of which
**	are probably undefined anyway.
*/
static w10_t ioeacalc()
{
    register vaddr_t ea;
    register w10_t w;

    /* Assume that our execution started at PC.  This will be untrue
    ** only if we're being executed as a trap or interrupt instruction.
    ** XCT is allowed, but PXCT is fortunately illegal.
    */
    ea = PC_VADDR;
    for (;;) {
	w = vm_fetch(ea);		/* Get instr */
	if ((LHGET(w) >> 9) != I_XCT)
	    break;
	ea = ea_calc(w);		/* Is XCT, track chain */
    }

    /* Now have retrieved original instruction word, re-compute
    ** its E using special algorithm.
    */
    if (iw_i(w)) {			/* Indirection? */
	va_lmake(ea, 0,
	    (RHGET(w) + (iw_x(w)	/* Indirection & indexing? */
	    		? ac_getrh(iw_x(w))
			: 0)) & H10MASK);
	w = vm_fetch(ea);		/* Get contents of word pointed to */
    } else {
	if (iw_x(w)) {			/* Indexing? */
	    register h10_t y;
	    y = RHGET(w);		/* Yes, remember Y */
	    w = ac_get(iw_x(w));	/* Get the X reg */
	    if (op10m_skipge(w))	/* If its LH is positive, */
		op10m_addi(w, y);	/*	add Y into whole word */
	    else			/* Else neg, just get local X+Y */
		LRHSET(w, 0, (RHGET(w)+y)&H10MASK);
	} else
	    LHSET(w, 0);	/* No I or X, just return Y */
    }
    return w;		/* Done, return whole-word address */
}


/* TIOE (0710) - Test IO Equal
*/
insdef(i_tioe)
{
    register w10_t w;

    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    w = ac_get(ac);
    return ((((uint32)LHGET(w)<<18) | RHGET(w)) & ub_read(ioeacalc()) )
		? PCINC_1 : PCINC_2;	/* Skip if no AC bits are set */
}

/* TION (0711) - Test IO Not Equal
*/
insdef(i_tion)
{
    register w10_t w;

    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    w = ac_get(ac);
    return ((((uint32)LHGET(w)<<18) | RHGET(w)) & ub_read(ioeacalc()) )
		? PCINC_2 : PCINC_1;	/* Skip if some AC bits are set */
}

/* RDIO (0712) - Read IO
*/
insdef(i_rdio)
{
    register uint32 reg;

    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);

    /* Compute IO address and make read request */
    reg = ub_read(ioeacalc());		/* Make the read request */
    ac_setlrh(ac, (reg>>18), reg & H10MASK);
    return PCINC_1;
}

/* WRIO (0713) - Write IO
*/
insdef(i_wrio)
{
    register w10_t w;

    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);

    /* Fetch IO address and make write request */
    w = ac_get(ac);
    ub_write(ioeacalc(), /* (LHGET(w)<<18)| */ RHGET(w));
    return PCINC_1;
}

/* BSIO (0714) - Bit Set IO
**	Only deals with low 16 bits.
*/
insdef(i_bsio)
{
    register w10_t ioaddr;
    
    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    ioaddr = ioeacalc();
    ub_write(ioaddr, (h10_t)(ub_read(ioaddr) | (ac_getrh(ac) & MASK16)));
    return PCINC_1;
}

/* BCIO (0715) - Bit Clear IO
**	Only deals with low 16 bits.
*/
insdef(i_bcio)
{
    register w10_t ioaddr;
    
    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    ioaddr = ioeacalc();
    ub_write(ioaddr, (h10_t)(ub_read(ioaddr) & ~(ac_getrh(ac) & MASK16)));
    return PCINC_1;
}


/* TIOEB (0720) - Test IO Equal, Byte
*/
insdef(i_tioeb)
{
    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    return (ac_getrh(ac) & ub_bread(ioeacalc())) ? PCINC_1 : PCINC_2;
}

/* TIONB (0721) - Test IO Not Equal, Byte
*/
insdef(i_tionb)
{
    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    return (ac_getrh(ac) & ub_bread(ioeacalc())) ? PCINC_2 : PCINC_1;
}

/* RDIOB (0722) - Read IO Byte
*/
insdef(i_rdiob)
{
    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);

    /* Fetch IO address and make read request */
    ac_setlrh(ac, 0, ub_bread(ioeacalc()));
    return PCINC_1;
}

/* WRIOB (0723) - Write IO Byte
*/
insdef(i_wriob)
{
    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);

    /* Fetch IO address and make write request */
    ub_bwrite(ioeacalc(), ac_getrh(ac));
    return PCINC_1;
}

/* BSIOB (0724) - Bit Set IO Byte
*/
insdef(i_bsiob)
{
    register w10_t ioaddr;
    
    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    ioaddr = ioeacalc();
    ub_bwrite(ioaddr, (h10_t)(ub_bread(ioaddr) | (ac_getrh(ac) & 0377)));
    return PCINC_1;
}

/* BCIOB (0725) - Bit Clear IO Byte
*/
insdef(i_bciob)
{
    register w10_t ioaddr;
    
    if (PCFTEST(PCF_USR) && !PCFTEST(PCF_UIO)) return i_muuo(op, ac, e);
    ioaddr = ioeacalc();
    ub_bwrite(ioaddr, (h10_t)(ub_bread(ioaddr) & ~(ac_getrh(ac) & 0377)));
    return PCINC_1;
}

#endif /* (T10 || T20) && KS */
