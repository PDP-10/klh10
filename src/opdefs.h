/* OPDEFS.H - Define PDP-10 instruction opcodes and declare routines.
*/
/* $Id: opdefs.h,v 2.3 2001/11/10 21:28:59 klh Exp $
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
 * $Log: opdefs.h,v $
 * Revision 2.3  2001/11/10 21:28:59  klh
 * Final 2.0 distribution checkin
 *
 */

#ifndef OPDEFS_INCLUDED
#define OPDEFS_INCLUDED 1

/* See also the RCSID for opcods.h after its first inclusion */
#ifdef RCSID
 RCSID(opdefs_h,"$Id: opdefs.h,v 2.3 2001/11/10 21:28:59 klh Exp $")
#endif

#ifndef EXTDEF
# define EXTDEF extern		/* Default is just to declare vars */
#endif

#define I_N  01000	/* Max # of PDP-10 opcodes */
#define IO_N (16*3)	/* Max # of IO instr opcodes (true max 02000) */
#define IX_N 040	/* Max # of EXTEND opcodes */

/* Opcode flags
**	IF_{A/M}{R/W}{/2/4}/{RI/RI8/RIF}
**		A/M - AC or Mem or {Immediate, Imm-8, Imm-float}
**		R/W - Read or Write (or both) operand
**		/2/4 - 1, 2, or 4 word op/result
*/

/* Mem Operand flags */
#define IF_MFOFF 0	/* Offset of Mem operand type field */
#define IF_MFMASK (017<<IF_MFOFF)
#define IF_M0	(0<<IF_MFOFF)	/* Operand is unused (0 if R&W are 0) */
#define IF_M1	(1<<IF_MFOFF)	/*   "	1 word */
#define IF_M2	(2<<IF_MFOFF)	/*   "	2 words (double) */
#define IF_M4	(3<<IF_MFOFF)	/*   "	4 words (quad) */
#define IF_ME	(4<<IF_MFOFF)	/*   "	E (immediate) */
#define IF_ME8	(5<<IF_MFOFF)	/*   "  signed 8-bit E */
#define IF_MEF	(6<<IF_MFOFF)	/*   "	floating immediate */
#define IF_MEIO	(7<<IF_MFOFF)	/*   "	IO register (Unibus) */
#define IF_MIN  (010<<IF_MFOFF)	/*   "  instruction at E (new PC?) */
#define IF_MR	020		/* Reads Mem */
#define IF_MW	040		/* Writes Mem */

/* AC Operand flags */
#define IF_AFOFF 6	/* Offset of AC operand type field */
#define IF_AFMASK (03<<IF_AFOFF)
#define IF_A0	0		/* AC optype: 0 - 1-wd only if AC fld non-Z */
#define IF_A1	(1<<IF_AFOFF)	/* AC optype: 1 - 1-word */
#define IF_A2	(2<<IF_AFOFF)	/* AC optype: 2 - 2-word (double) */
#define IF_A4	(3<<IF_AFOFF)	/* AC optype: 3 - 4-word (quad) */
#define IF_AR	0400		/* Reads AC */
#define IF_AW	01000		/* Writes AC */

/* General flags */
#define IF_SPEC	02000		/* Special-handling instruction */
#define IF_IO	04000		/* IO-format instruction */
#define IF_JMP	010000		/* May jump */
#define IF_SKP	020000		/* May skip */
#define IF_OPN	040000		/* Append opcode # to name on printout */
#if 0
# define IF_	0100000		/* Max flag (try to keep in 16 bits) */
#endif

/* Common combos */
#define IF_AS	(IF_AR|IF_AW)			/* Modify AC (Self) */
#define IF_MS	(IF_MR|IF_MW)			/* Modify Mem (Self) */
#define IF_XI	(IF_MR|IF_ME)			/* E    -> ? */
#define IF_X1	(IF_MR|IF_M1)			/* c(E) -> ? */
#define IF_1X1	(IF_AR|IF_A1|IF_MR|IF_M1)	/* c(AC) op c(E) -> ? */

#define IF_1X	(IF_AS|IF_A1|IF_MR|IF_M1)	/* c(AC) op c(E) -> c(AC) */
#define IF_1XI	(IF_AS|IF_A1|IF_MR|IF_ME)	/* c(AC) op E    -> c(AC) */
#define IF_1XM	(IF_AR|IF_A1|IF_MS|IF_M1)	/* c(AC) op c(E) -> c(E) */
#define IF_1XB	(IF_AS|IF_A1|IF_MS|IF_M1)	/* c(AC) op c(E) -> c(AC,E) */

#define IF_1XFI	(IF_AS|IF_A1|IF_MR|IF_MEF)	/* c(AC) op f(E,0) -> c(AC) */
#define IF_1XFL	(IF_AS|IF_A2|IF_MR|IF_M1)	/* c(AC) op c(E) -> c(AC2) */

#define IF_1S	(IF_AW|IF_A1|IF_MR|IF_M1)	/* op  c(E) -> c(AC) */
#define IF_1SI	(IF_AW|IF_A1|IF_ME)		/* op    E  -> c(AC) */
#define IF_1SM	(IF_AR|IF_A1|IF_MW|IF_M1)	/* op c(AC) -> c(E) */
#define IF_1SS	(IF_AW|IF_A0|IF_MS|IF_M1)	/* op  c(E) -> c(E) */
						/*  (if AC!=0, -> c(AC) too) */
#define IF_1SB	(IF_AW|IF_A1|IF_MS|IF_M1)	/* op  c(E) -> c(AC), c(E) */

#define IF_1C	(IF_AW|IF_A1)			/* op const -> c(AC) */
#define IF_1CM	(IF_MW|IF_M1)			/* op const -> c(E) */
#define IF_1CB	(IF_MW|IF_M1)			/* op const -> c(AC),c(E) */

#define IF_2X	(IF_AS|IF_A2|IF_MR|IF_M2)	/* c(AC2) op c(E2) -> c(AC2) */
#define IF_2S	(IF_AW|IF_A2|IF_MR|IF_M2)	/* op  c(E2) -> c(AC2) */
#define IF_2SM	(IF_AR|IF_A2|IF_MW|IF_M2)	/* op c(AC2) -> c(E2) */

#define IF_NOP	(IF_A1)		/* NOP must have some flag set to avoid
				** use of default flags, so use this one
				** without an AC R/W flag. */

/* Normal instruction defs */

    /* None - the 9-bit opcode is used, represented as octal */


/* EXTEND instruction defs */

/* Macro to define value of an IX_<name> EXTEND instruction opcode */
#define IXOP(op) ((op)|01000)		/* Make up an extended opcode */
#define IXIDX(xop) ((xop)&0777)		/* Cvt IX_x opcode to dispatch index */


/* IO instruction defs */

/* Macros to define value of an IO_<name> IO instruction opcode.
**    IOINOP (IO INternal OP) defines it as AC-dispatched from a normal op.
**    IOEXOP (IO EXternal OP) defines it as an "<dev-IO> <dev>," instruction.
**
** Either can generate the same value, the difference is only one of
** representation.  Both use a max of 15 bits, so no special type is needed.
*/
#define IOINOP(op, ac) (((op)<<6)|((ac)<<2))	/* Use normal op plus AC */
#define IOEXOP(iox, dev) \
	((0700<<6)|((iox)<<2)|((dev)<<5))	/* Use IOX_ plus ext device */

#define IOIDX(io) (((io)>>2)&01777)	/* Cvt IO_x opcode to dispatch index */

/* Indices for Dev-IO instructions */
enum {
	IOX_BLKI=0,	/* IOEXOP(IOX_BLKI, 0) == IOINOP(0700,0) == 070000 */
	IOX_DATAI,	/* IOEXOP(IOX_DATAI,0) == IOINOP(0700,1) == 070004 */
	IOX_BLKO,	/* IOEXOP(IOX_BLKO, 0) == IOINOP(0700,2) == 070010 */
	IOX_DATAO,	/* IOEXOP(IOX_DATAO,0) == IOINOP(0700,3) == 070014 */
	IOX_CONO,	/* IOEXOP(IOX_CONO, 0) == IOINOP(0700,4) == 070020 */
	IOX_CONI,	/* IOEXOP(IOX_CONI, 0) == IOINOP(0700,5) == 070024 */
	IOX_CONSZ,	/* IOEXOP(IOX_CONSZ,0) == IOINOP(0700,6) == 070030 */
	IOX_CONSO,	/* IOEXOP(IOX_CONSO,0) == IOINOP(0700,7) == 070034 */
	IOX_N
};

extern char *opcionam[IOX_N];	/* Names for dev-style IO instrs */
extern int   opcioflg[IOX_N];	/* IF_ flags for ditto */


/* Conventional values for PDP-10 I/O devices,
**	provided here so they can be used with IOEXOP.
*/
				/* Internal "devices" */
#define IODV_APR  (000>>2)	/* Processor */
#define IODV_PI	  (004>>2)	/* PI system */
#define IODV_PAG  (010>>2)	/* KI+: Pager */
#define IODV_CCA  (014>>2)	/* KL: Cache */
#define IODV_TIM  (020>>2)	/* KL: Timers, KS: Read various */
#define IODV_MTR  (024>>2)	/* KL: Meters, KS: Write ditto */

				/* External devices - old-style IO */
#define IODV_CLK  (0070>>2)	/* DKN10 clock (KA/KI) */
#define IODV_PTP  (0100>>2)	/* Paper-Tape Punch */
#define IODV_PTR  (0104>>2)	/* Paper-Tape Reader */
#define IODV_TTY  (0120>>2)	/* Console TTY */
#define IODV_DIS  (0130>>2)	/* Ye 340 display */

#define IODV_N	 (01000>>2)	/* Max # of IO devices (128) */

extern char *opcdvnam[IODV_N];		/* Names for IO devices */

/* Declare instruction routines */

/* Instruction routine definition macros.
**	For consistency and the ability to globally control all instruction
**	routine setups, these macros must ALWAYS be used when defining
**	instruction execution routines.
**   insdef - for normal instructions
**  xinsdef - for EXTEND instructions (takes additional E1 arg)
** ioinsdef - for new-IO instructions (dispatched by AC; only E arg provided)
**	All instruction routines must return one of
**		PCINC_0 (jumped), PCINC_1 (normal), or PCINC_2 (skipped).
*/
#define   insdef(rtn) pcinc_t rtn(int op, int ac, register vaddr_t e)
#define  xinsdef(rtn) pcinc_t rtn(int xop, int ac, vaddr_t e0, vaddr_t e1)
#define ioinsdef(rtn) pcinc_t rtn(register vaddr_t e)

typedef pcinc_t (*opfp_t)(int, int, vaddr_t);		/* Normal instr */
typedef pcinc_t (*opxfp_t)(int, int, vaddr_t, vaddr_t);	/* Extended instr */
typedef pcinc_t (*opiofp_t)(vaddr_t);			/* I/O instr */

extern pcinc_t		/* For now, bypass include ordering probs */
	ix_undef(int, int, vaddr_t, vaddr_t),
	i_diodisp(int, int, vaddr_t)
#  define  idef(i, nam, en, rtn, fl) , rtn(int, int, vaddr_t)
#  define ixdef(i, nam, en, rtn, fl) , rtn(int, int, vaddr_t, vaddr_t)
#  define iodef(i, nam, en, rtn, fl) , rtn(vaddr_t)
#  include "opcods.h"
#  undef  idef
#  undef ixdef
#  undef iodef
	;

/* Define I_xxx enums so can easily refer to specific opcodes */
enum opcode {
	I_0 = 0
#  define  idef(i, nam, en, rtn, fl) , en=i
#  define iodef(i, nam, en, rtn, fl) , en=i
#  define ixdef(i, nam, en, rtn, fl) , en=i
#  include "opcods.h"
#  undef  idef
#  undef iodef
#  undef ixdef
};

extern struct opdef {		/* Master table */
	char *opstr;
	int opval, opflg;
	opfp_t oprtn;		/* Function pointer */
} opclist[];

/* Declare tables that will hold opcode info, indexed by I_xxx enum.
** These are initialized at runtime from the master opclist.
** Note that the normal i_xxx dispatch table now lives in "cpu" to
** help promote locality.
*/
#if 0
EXTDEF opfp_t opdisp[I_N];	/* I_xxx Routine dispatch table */
#endif
EXTDEF opxfp_t  opcxrtn[IX_N];	/* IX_xxx routine dispatch */
EXTDEF opiofp_t opciortn[IO_N];	/* IO_xxx routine dispatch */

EXTDEF struct opdef		/* Pointers to full definitions */
	*opcptr[I_N],		/*  back in master list */
	*opcxptr[IX_N],
	*opcioptr[IO_N];

extern int op_init(void);	/* Function to initialize runtime tables */

/* RCSID expressed here on behalf of opcods.h which cannot do it itself.
 */
#if defined(OPCODS_RCSID) && defined(RCSID)
       OPCODS_RCSID
#endif

/* Macro for invoking normal instruction */
#define op_xct(op, ac, e) \
	(*cpu.opdisp[(int)(op)])((int)(op), (int)(ac), (vaddr_t)(e))

/* Instruction word format defs - here for now */

/* Define fields of instruction word */
#define IW_OP ((h10_t)0777<<9)	/* LH: Opcode */
#define IW_AC (AC_MASK<<5)	/* LH: AC field */
#define IW_I  020		/* LH: Indirect bit */
#define IW_X  AC_MASK		/* LH: Index register AC */
#define IW_Y  H10MASK		/* RH: Y address */

#define IW_EI 0200000		/* LH: EFIW Indirect bit */
#define IW_EX 0170000		/* LH: EFIW Index register */
#define IW_EY MASK30		/* LH+RH: EFIW Y address */

/* Get various fields of instruction word */
#define iw_op(w) (LHGET(w)>>9)		/* Get opcode, assumes word is OK */
#define iw_ac(w) ((LHGET(w)>>5)&AC_MASK)
#define iw_i(w)  (LHGET(w)&IW_I)	/* Note result not right-justified! */
#define iw_x(w)  (LHGET(w)&IW_X)
#define iw_y(w)  RHGET(w)

#define iw_ei(w) (LHGET(w)&IW_EI)	/* Note result not right-justified! */
#define iw_ex(w) ((LHGET(w)>>12)&AC_MASK)
/*define iw_ey(w) */

#endif /* ifndef OPDEFS_INCLUDED */
