// ksim: an 8080 simulator
// Copyright 2011, 2014 Eric Smith <spacewar@gmail.com>

// ksim was written with the intent of having an accurate 8080 simulator
// for reference. Maximum simulation performance is not a goal of ksim,
// and there is little performance optimization. There are many available
// Z80 simulators, but few for the 8080, and even fewer that simulate the
// processor flags correctly. While it is certainly possible that there
// may be some remaining bugs in ksim, it does pass several published
// 8080 exerciser programs, including one which tests the processor flags
// fairly extensively:
//    http://www.idb.me.uk/sunhillow/8080.html

// This version of ksim does not include any interrupts, and has only
// rudimentary I/O support, which is only intended as an example. Console
// I/O is supported via stdin and stdout, using input port 0 for input
// status, input port 1 for character input, and output port 0 for character
// output. It is assumed that stdin and stdout are a tty device, which is
// put into raw mode. Because of this use of raw mode, the interrupt
// character (usually Control-C) will not stop the program, so use of
// another shell to send a TERM signal (typically by use of the "kill"
// command) is required if the simulated program does not halt.

// LICENSE:

// This program is free software: you can redistribute it and/or modify
// it under the terms of version 3 of the GNU General Public License as
// published by the Free Software Foundation.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.


#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <termios.h>
#include <unistd.h>

// The following definition is used to suppress warnings about unused
// function parameters, but might not work with compilers other than
// GCC.  It can either be defined differently for those compilers, or
// declared with an empty defnition.
#define UNUSED __attribute__((unused))

char *progname;

void usage (FILE *f)
{
  fprintf (f, "usage: %s [options...] <bootstrap>\n", progname);
  fprintf (f, "options:\n");
  fprintf (f, "  -b  bootstrap is a binary file\n");
  fprintf (f, "  -h  bootstrap is an Intel hex file\n");
  fprintf (f, "  -t <tracefile>   trace instruction execution\n");
  fprintf (f, "  --bdos  simulate a tiny subset of CP/M BDOS\n");
}

// generate fatal error message to stderr, doesn't return
void fatal (int ret, char *format, ...) __attribute__ ((noreturn));

void fatal (int ret, char *format, ...)
{
  va_list ap;

  if (format)
    {
      fprintf (stderr, "fatal error: ");
      va_start (ap, format);
      vfprintf (stderr, format, ap);
      va_end (ap);
    }
  if (ret == EX_USAGE)
    usage (stderr);
  exit (ret);
}


FILE *trace_f = NULL;

struct termios tty_cooked;
struct termios tty_raw;

void console_init (void)
{
  tcgetattr (STDIN_FILENO, & tty_cooked);
  tcgetattr (STDIN_FILENO, & tty_raw);
  tty_raw.c_iflag = 0;
  tty_raw.c_oflag = 0;
  tty_raw.c_lflag = 0;
  tty_raw.c_cc [VTIME] = 0;
  tty_raw.c_cc [VMIN] = 1;
  tcsetattr (STDIN_FILENO, TCSAFLUSH, & tty_raw);
}

void console_cleanup (void)
{
  tcsetattr (STDIN_FILENO, TCSAFLUSH, & tty_cooked);
}

bool bdos_sim = false;


static bool even_parity [0x100];

uint64_t cycle_count;

uint16_t pc;
uint16_t reg_pair [4];
uint8_t reg_a;

bool halted;
bool interrupt_enabled;

uint8_t flag_sp_data;
bool flag_z;
bool flag_ac;
bool flag_cy;

#define SET_FLAG_SZP(data) do { flag_sp_data = data; flag_z = ((data) == 0); } while (0)
#define FLAG_Z  (flag_z)
#define FLAG_S  (flag_sp_data >> 7)
#define FLAG_P  (even_parity [flag_sp_data])
#define FLAG_AC (flag_ac)
#define FLAG_CY (flag_cy)

// On 8085, flag bit 1 is two's complement overflow for 8-bit and 16-bit
// arithmetic operations

// On 8085, flag bit 5 is UI, which is underflow indication for DCX,
// and overflow indication for INX
//     UI = O1 x O2 + O1 x R + O2 x R
// where O1 = sign of operand 1
//       O2 = sign of operand 2
//       R = sign of result
// For subtraction and comparisons, replace O2 with not O2.

void set_f (uint8_t f_value)
{
  flag_sp_data = (f_value & 0x80) | (((f_value >> 5) ^ ~f_value) & 0x04);

  // S P  flag_sp_data
  // - -  ------------
  // 0 0   00000100
  // 0 1   00000000
  // 1 0   10000000
  // 1 1   10000100

  flag_z  = (f_value >> 6) & 1;
  flag_ac = (f_value >> 4) & 1;
  flag_cy = f_value & 1;
}

uint8_t get_f (void)
{
  return ((FLAG_S << 7) | (FLAG_Z << 6) | (FLAG_AC << 4) |
	  (FLAG_P << 2) | (1 << 1)      | (FLAG_CY << 0));
}

#define REG_PAIR_BC (reg_pair [0])
#define REG_PAIR_DE (reg_pair [1])
#define REG_PAIR_HL (reg_pair [2])
#define REG_SP      (reg_pair [3])

// The REG_PAIR macro handles access to reg pairs based on a two
// bit index from an opcode.
#define REG_PAIR(x) (reg_pair [x])

#define REG_IDX_B 0
#define REG_IDX_C 1
#define REG_IDX_D 2
#define REG_IDX_E 3
#define REG_IDX_H 4
#define REG_IDX_L 5
#define REG_IDX_M 6
#define REG_IDX_A 7

// The REG macro handles access to A, B, C, D, E, H, L, based on a
// three bit index from an opcode.  It does NOT handle "M" (memory)!
// $$$ The ^1 needs to be adjusted for endianness
#define REG(x) (*((x == REG_IDX_A) ? (& reg_a) : (((uint8_t *)(& reg_pair)) + (x^1))))

#define REG_B REG(REG_IDX_B)
#define REG_C REG(REG_IDX_C)
#define REG_D REG(REG_IDX_D)
#define REG_E REG(REG_IDX_E)
#define REG_H REG(REG_IDX_H)
#define REG_L REG(REG_IDX_L)
#define REG_A REG(REG_IDX_A)



// static const char *reg_name [8] = { "B", "C", "D", "E", "H", "L", "M", "A" };

// static const char *rp_name [4] = { "BC", "DE", "HL", "SP" };



uint8_t mem [0x10000];

static inline uint8_t mem_read (uint16_t addr)
{
  return mem [addr];
}

static inline void mem_write (uint16_t addr, uint8_t data)
{
  mem [addr] = data;  // $$$ may need to change if there is any ROM,
                      // memory-mapped I/O, or nonexistent memory
}

static bool eval_cond (int cond)
{
  switch (cond)
    {
    case 0: // NZ not zero
      return ! FLAG_Z;
    case 1: // Z zero
      return FLAG_Z;
    case 2: // NC not carry
      return ! FLAG_CY;
    case 3: // C carry
      return FLAG_CY;
    case 4: // PO parity odd
      return ! FLAG_P;
    case 5: // PE parity even
      return FLAG_P;
    case 6: // P positive
      return ! FLAG_S;
    case 7: // M minus
      return FLAG_S;
    }
  return false;  // should never get here
}


static void illegal_op (uint8_t opcode)
{
  // $$$
  halted = 1;
  printf ("illegal opcode %04o at pc=%04x\n", opcode, pc);
}

#define DISK_BUF   0xfbfa  /* 16-bit */
#define DISK_SECT  0xfbfc
#define DISK_CYL   0xfbfd  /* 16-bit */
#define DISK_DRIVE 0xfbff  /* 1=A, 2=B */

#define MAX_DISK_DRIVE 2
FILE *disk [MAX_DISK_DRIVE];

static void op_disk_io (uint8_t opcode UNUSED)
{
  uint8_t byte2 = mem_read ((pc) & 0xffff);
  uint8_t byte3 = mem_read ((pc+1) & 0xffff);
  if ((byte2 != 0xed) || ((byte3 & 0xfe) != 0x02))
    {
      halted = 1;
      printf ("bad inst ED %02x %02x\n", byte2, byte3);
      return;
    }
  bool write = byte3 == 0x03;
  uint16_t buf_addr = mem_read (DISK_BUF) | (mem_read (DISK_BUF + 1) << 8);
  uint8_t  sector = mem_read (DISK_SECT);
  uint16_t cylinder = mem_read (DISK_CYL) | (mem_read (DISK_CYL + 1) << 8);
  uint8_t  drive = mem_read (DISK_DRIVE);
  if ((drive < 1) || (drive > MAX_DISK_DRIVE))
    {
      halted = 1;
      printf ("bad disk drive number %d\n", drive);
      return;
    }
  drive--;

#if 0
  printf ("disk %s  drive %u cyl %5u sect %3u buffer %04x\r\n",
	  write ? "write" : "read ",
	  drive, cylinder, sector, buf_addr);
#endif
  if (! disk [drive])
    {
      char *fn = drive ? "a.img" : "b.img";
      disk [drive] = fopen (fn, "rb+");
      if (! disk [drive])
	{
	  halted = 1;
	  printf ("can't open image file '%s'\n", fn);
	  return;
	}
    }
  fseek (disk [drive], ((cylinder * 128) + sector) * 128, SEEK_SET);
  if (write)
    fwrite (mem + buf_addr,  // ptr
	    128,             // size
	    1,               // nmemb
	    disk [drive]);
  else
    fread  (mem + buf_addr,  // ptr
	    128,             // size
	    1,               // nmemb
	    disk [drive]);
  REG_A = 0x00;  // success
  pc += 2;
}

// ------------------------------------------------------------
// Data Transfer Group
// Flags not affected by any instruction in this group
// ------------------------------------------------------------

static void op_MOV (uint8_t opcode)
{
  // no flags affected
  uint8_t data;
  int dst = (opcode >> 3) & 7;
  int src = opcode & 7;
  if (src == REG_IDX_M)
    data = mem_read (REG_PAIR_HL);
  else
    data = REG (src);
  if (dst == REG_IDX_M)
    mem_write (REG_PAIR_HL, data);
  else
    REG (dst) = data;
}

static void op_MVI (uint8_t opcode)
{
  // no flags affected
  uint8_t data;
  int op543 = (opcode >> 3) & 7;
  data = mem_read (pc++);
  if (op543 == REG_IDX_M)
    mem_write (REG_PAIR_HL, data);
  else
    REG (op543) = data;
}

static void op_LXI (uint8_t opcode)
{
  // no flags affected
  uint16_t data;
  int op54 = (opcode >> 4) & 3;
  data = mem_read (pc++);
  data |= (mem_read (pc++) << 8);
  REG_PAIR (op54) = data;
}

static void op_LDA (uint8_t opcode UNUSED)
{
  // no flags affected
  uint16_t addr;
  addr = mem_read (pc++);
  addr |= (mem_read (pc++) << 8);
  REG_A = mem_read (addr);
}

static void op_STA (uint8_t opcode UNUSED)
{
  // no flags affected
  uint16_t addr;
  addr = mem_read (pc++);
  addr |= (mem_read (pc++) << 8);
  mem_write (addr, REG_A);
}

static void op_LHLD (uint8_t opcode UNUSED)
{
  // no flags affected
  uint16_t addr;
  addr = mem_read (pc++);
  addr |= (mem_read (pc++) << 8);
  REG_L = mem_read (addr++);
  REG_H = mem_read (addr);
}

static void op_SHLD (uint8_t opcode UNUSED)
{
  // no flags affected
  uint16_t addr;
  addr = mem_read (pc++);
  addr |= (mem_read (pc++) << 8);
  mem_write (addr++, REG_L);
  mem_write (addr,   REG_H);
}

static void op_LDAX (uint8_t opcode)
{
  // no flags affected
  int op54 = (opcode >> 4) & 3;
  REG_A = mem_read (REG_PAIR (op54));
}

static void op_STAX (uint8_t opcode)
{
  // no flags affected
  int op54 = (opcode >> 4) & 3;
  mem_write (REG_PAIR (op54), REG_A);
}

static void op_XCHG (uint8_t opcode UNUSED)
{
  // no flags affected
  uint16_t temp;
  temp = REG_PAIR_DE;
  REG_PAIR_DE = REG_PAIR_HL;
  REG_PAIR_HL = temp;
}

// ------------------------------------------------------------
// Arithmetic Group
// Most instructions affect Z, S, P, CY, AC by standard rules
// ------------------------------------------------------------

static void op_ADD (uint8_t opcode)
{
  // affects Z, S, P, CY, AC
  uint8_t data;
  uint16_t sum;
  int src = opcode & 7;
  if (src == REG_IDX_M)
    data = mem_read (REG_PAIR_HL);
  else
    data = REG (src);
  FLAG_AC = ((REG_A & 0x0f) + (data & 0x0f)) >> 4;
  sum = REG_A + data;
  FLAG_CY = sum >> 8;
  REG_A = sum & 0xff;
  SET_FLAG_SZP (REG_A);
}

static void op_ADI (uint8_t opcode UNUSED)
{
  // affects Z, S, P, CY, AC
  uint8_t data;
  uint16_t sum;
  data = mem_read (pc++);
  FLAG_AC = ((REG_A & 0x0f) + (data & 0x0f)) >> 4;
  sum = REG_A + data;
  FLAG_CY = sum >> 8;
  REG_A = sum & 0xff;
  SET_FLAG_SZP (REG_A);
}

static void op_ADC (uint8_t opcode)
{
  // affects Z, S, P, CY, AC
  uint8_t data;
  uint16_t sum;
  int src = opcode & 7;
  if (src == REG_IDX_M)
    data = mem_read (REG_PAIR_HL);
  else
    data = REG (src);
  FLAG_AC = (((REG_A & 0x0f) + (data & 0x0f) + FLAG_CY) >> 4) & 1;
  sum = REG_A + data + FLAG_CY;
  FLAG_CY = sum >> 8;
  REG_A = sum & 0xff;
  SET_FLAG_SZP (REG_A);
}

static void op_ACI (uint8_t opcode UNUSED)
{
  // affects Z, S, P, CY, AC
  uint8_t data;
  uint16_t sum;
  data = mem_read (pc++);
  FLAG_AC = (((REG_A & 0x0f) + (data & 0x0f) + FLAG_CY) >> 4) & 1;
  sum = REG_A + data + FLAG_CY;
  FLAG_CY = sum >> 8;
  REG_A = sum & 0xff;
  SET_FLAG_SZP (REG_A);
}

static void op_SUB (uint8_t opcode)
{
  // affects Z, S, P, CY, AC
  uint8_t data;
  uint16_t sum;
  int src = opcode & 7;
  if (src == REG_IDX_M)
    data = mem_read (REG_PAIR_HL);
  else
    data = REG (src);
  data ^= 0xff;
  FLAG_AC = (((REG_A & 0x0f) + (data & 0x0f) + 1) >> 4) & 1;
  sum = REG_A + data + 1;
  FLAG_CY = (sum >> 8) ^ 1;
  REG_A = sum & 0xff;
  SET_FLAG_SZP (REG_A);
}

static void op_SUI (uint8_t opcode UNUSED)
{
  // affects Z, S, P, CY, AC
  uint8_t data;
  uint16_t sum;
  data = mem_read (pc++);
  data ^= 0xff;
  FLAG_AC = (((REG_A & 0x0f) + (data & 0x0f) + 1) >> 4) & 1;
  sum = REG_A + data + 1;
  FLAG_CY = (sum >> 8) ^ 1;
  REG_A = sum & 0xff;
  SET_FLAG_SZP (REG_A);
}

static void op_SBB (uint8_t opcode)
{
  // affects Z, S, P, CY, AC
  uint8_t data;
  uint16_t sum;
  int src = opcode & 7;
  if (src == REG_IDX_M)
    data = mem_read (REG_PAIR_HL);
  else
    data = REG (src);
  data ^= 0xff;
  FLAG_AC = (((REG_A & 0x0f) + (data & 0x0f) + (FLAG_CY ^ 1)) >> 4) & 1;
  sum = REG_A + data + (FLAG_CY ^ 1);
  FLAG_CY = (sum >> 8) ^ 1;
  REG_A = sum & 0xff;
  SET_FLAG_SZP (REG_A);
}

static void op_SBI (uint8_t opcode UNUSED)
{
  // affects Z, S, P, CY, AC
  uint8_t data;
  uint16_t sum;
  data = mem_read (pc++);
  data ^= 0xff;
  FLAG_AC = (((REG_A & 0x0f) + (data & 0x0f) + (FLAG_CY ^ 1)) >> 4) & 1;
  sum = REG_A + data + (FLAG_CY ^ 1);
  FLAG_CY = (sum >> 8) ^ 1;
  REG_A = sum & 0xff;
  SET_FLAG_SZP (REG_A);
}

static void op_INR (uint8_t opcode)
{
  // affects Z, S, P, AC -- DOES NOT affect CY
  int reg = (opcode >> 3) & 7;
  uint8_t data;

  // We could do this more efficiently if we didn't need to set AC.
  // Does the real 8080 actually do that?
  if (reg == REG_IDX_M)
    data = mem_read (REG_PAIR_HL);
  else
    data = REG (reg);
  FLAG_AC = (((data & 0x0f) + 1) >> 4) & 1;
  data++;
  if (reg == REG_IDX_M)
    mem_write (REG_PAIR_HL, data);
  else
    REG (reg) = data;

  SET_FLAG_SZP (data);
}

static void op_DCR (uint8_t opcode)
{
  // affects Z, S, P, AC -- DOES NOT affect CY
  int reg = (opcode >> 3) & 7;
  uint8_t data;

  // We could do this more efficiently if we didn't need to set AC.
  // Does the real 8080 actually do that?
  if (reg == REG_IDX_M)
    data = mem_read (REG_PAIR_HL);
  else
    data = REG (reg);
  FLAG_AC = (((data & 0x0f) + 0xf) >> 4) & 1;
  data--;
  if (reg == REG_IDX_M)
    mem_write (REG_PAIR_HL, data);
  else
    REG (reg) = data;

  SET_FLAG_SZP (data);
}

static void op_INX (uint8_t opcode)
{
  // no flags affected
  int op54 = (opcode >> 4) & 3;
  REG_PAIR (op54)++;
}

static void op_DCX (uint8_t opcode)
{
  // no flags affected
  int op54 = (opcode >> 4) & 3;
  REG_PAIR (op54)--;
}

static void op_DAD (uint8_t opcode)
{
  // affects CY flag ONLY
  int op54 = (opcode >> 4) & 3;
  uint32_t sum;
  sum = REG_PAIR_HL + REG_PAIR (op54);
  REG_PAIR_HL = sum & 0xffff;
  FLAG_CY = (sum >> 16) & 1;
}

static void op_DAA (uint8_t opcode UNUSED)
{
  // affects Z, S, P, CY, AC
  uint8_t adjust = 0x00;
  if (((REG_A & 0xf) > 9) || FLAG_AC)
    adjust = 0x06;
  if ((((REG_A >> 4) > 9) || FLAG_CY) ||
      (((REG_A >> 4) == 9) && (FLAG_CY || ((REG_A & 0xf) > 9))))
    adjust |= 0x60;

  FLAG_AC = (REG_A & 0xf) >= 0xa;

  if ((((REG_A >> 4) >= 9) && ((REG_A & 0xf) >= 0xa)) ||
      ((REG_A >> 4) >= 0xa))
    FLAG_CY = 1;

  REG_A += adjust;
  SET_FLAG_SZP (REG_A);
}

// "Intel 8080 Microcomputer Systems Users Manual" of Sept. 1975
// classifies CMP and CPI as logical instructions!

static void op_CMP (uint8_t opcode)
{
  // affects Z, S, P, CY, AC
  // Z set if A=r
  // CY set if A<r
  // $$$ I've assumed that CMP is just SUB without writing result
  // to accumulator. Better test that!
  uint8_t data;
  uint16_t sum;
  int src = opcode & 7;
  if (src == REG_IDX_M)
    data = mem_read (REG_PAIR_HL);
  else
    data = REG (src);
  data ^= 0xff;
  FLAG_AC = (((REG_A & 0x0f) + (data & 0x0f) + 1) >> 4) & 1;
  sum = REG_A + data + 1;
  FLAG_CY = (sum >> 8) ^ 1;
  SET_FLAG_SZP (sum & 0xff);
}

static void op_CPI (uint8_t opcode UNUSED)
{
  // affects Z, S, P, CY, AC
  // Z set if A=r
  // CY set if A<r
  // $$$ I've assumed that CPI is just SUI without writing result
  // to accumulator. Better test that!
  uint8_t data;
  uint16_t sum;
  data = mem_read (pc++);
  data ^= 0xff;
  FLAG_AC = (((REG_A & 0x0f) + (data & 0x0f) + 1) >> 4) & 1;
  sum = REG_A + data + 1;
  FLAG_CY = (sum >> 8) ^ 1;
  SET_FLAG_SZP (sum & 0xff);
}

// ------------------------------------------------------------
// Logical Group
// Most instructions affect Z, S, P, CY, AC by standard rules
// ------------------------------------------------------------

static void op_ANA (uint8_t opcode)
{
  // affects Z, S, P, CY, AC - CY is cleared
  // 8080: AC set to the logical OR of bits 3 of the operands
  // 8085: AC set to 1
  uint8_t data;
  int src = opcode & 7;
  if (src == REG_IDX_M)
    data = mem_read (REG_PAIR_HL);
  else
    data = REG (src);
  FLAG_AC = ((REG_A | data) >> 3) & 1;  // 8080 only, 8085 sets to 1
  REG_A &= data;
  FLAG_CY = 0;
  SET_FLAG_SZP (REG_A);
}

static void op_ANI (uint8_t opcode UNUSED)
{
  // affects Z, S, P, CY, AC - CY is cleared
  // 8080: AC set to the logical OR of bits 3 of the operands
  // 8085: AC set to 1
  uint8_t data;
  data = mem_read (pc++);
  FLAG_AC = ((REG_A | data) >> 3) & 1;  // 8080 only, 8085 sets to 1
  REG_A &= data;
  FLAG_CY = 0;
  SET_FLAG_SZP (REG_A);
}

static void op_XRA (uint8_t opcode)
{
  // affects Z, S, P, CY, AC - CY and AC are cleared
  uint8_t data;
  int src = opcode & 7;
  if (src == REG_IDX_M)
    data = mem_read (REG_PAIR_HL);
  else
    data = REG (src);
  REG_A ^= data;
  FLAG_CY = 0;
  FLAG_AC = 0;
  SET_FLAG_SZP (REG_A);
}

static void op_XRI (uint8_t opcode UNUSED)
{
  // affects Z, S, P, CY, AC - CY and AC are cleared
  uint8_t data;
  data = mem_read (pc++);
  REG_A ^= data;
  FLAG_CY = 0;
  FLAG_AC = 0;
  SET_FLAG_SZP (REG_A);
}

static void op_ORA (uint8_t opcode)
{
  // affects Z, S, P, CY, AC - CY and AC are cleared
  uint8_t data;
  int src = opcode & 7;
  if (src == REG_IDX_M)
    data = mem_read (REG_PAIR_HL);
  else
    data = REG (src);
  REG_A |= data;
  FLAG_CY = 0;
  FLAG_AC = 0;
  SET_FLAG_SZP (REG_A);
}

static void op_ORI (uint8_t opcode UNUSED)
{
  // affects Z, S, P, CY, AC - CY and AC are cleared
  uint8_t data;
  data = mem_read (pc++);
  REG_A |= data;
  FLAG_CY = 0;
  FLAG_AC = 0;
  SET_FLAG_SZP (REG_A);
}

static void op_RLC (uint8_t opcode UNUSED)
{
  // affects CY flag ONLY
  REG_A = (REG_A << 1) | (REG_A >> 7);
  FLAG_CY = REG_A & 0x01;
}

static void op_RRC (uint8_t opcode UNUSED)
{
  // affects CY flag ONLY
  REG_A = (REG_A >> 1) | (REG_A << 7);
  FLAG_CY = (REG_A >> 7) & 1;
}

static void op_RAL (uint8_t opcode UNUSED)
{
  // affects CY flag ONLY
  uint16_t data;
  data = (REG_A << 1) | FLAG_CY;
  REG_A = data;
  FLAG_CY = (data >> 8) & 1;
}

static void op_RAR (uint8_t opcode UNUSED)
{
  // affects CY flag ONLY
  uint16_t data;
  data = (FLAG_CY << 8) | REG_A;
  REG_A = data >> 1;
  FLAG_CY = data & 1;
}

static void op_CMA (uint8_t opcode UNUSED)
{
  // no flags affected
  REG_A ^= 0xff;
}

static void op_CMC (uint8_t opcode UNUSED)
{
  // affects CY flag ONLY
  FLAG_CY ^= 1;
}

static void op_STC (uint8_t opcode UNUSED)
{
  // affects CY flag ONLY
  FLAG_CY = 1;
}

// ------------------------------------------------------------
// Branch Group
// Flags not affected by any instruction in this group
// ------------------------------------------------------------

static void op_JMP (uint8_t opcode UNUSED)
{
  // no flags affected
  uint16_t addr;
  addr = mem_read (pc++);
  addr |= (mem_read (pc) << 8);
  if (bdos_sim && (addr == 0x0000))
    {
      printf ("jump to CP/M warm boot from pc=%04x\n", (pc - 2) & 0xffff);
      halted = true;
    }
  else
    pc = addr;
}

static void op_Jcond (uint8_t opcode)
{
  // no flags affected
  int cond = (opcode >> 3) & 7;
  uint16_t addr;
  addr = mem_read (pc++);
  addr |= (mem_read (pc++) << 8);
  if (eval_cond (cond))
    pc = addr;
}

static void bdos_call (void)
{
  uint8_t c;
  uint16_t addr;
  switch (REG_C)
    {
    case 0x02:  // output character in A
      c = REG_A;
      write (STDOUT_FILENO, & c, 1);
      break;
    case 0x09:  // output message pointed to by DE
      for (addr = REG_PAIR_DE; (c = mem_read (addr)) != '$'; addr++)
	write (STDOUT_FILENO, & c, 1);
      break;
    default:
      printf ("unrecognized BDOS function %02x pc=%04x\n", REG_C, pc);
      printf ("BC=%04x\n", REG_PAIR_BC);
      printf ("B=%02x\n", REG_B);
      printf ("C=%02x\n", REG_C);
      halted = true;
    }
}

static void op_CALL (uint8_t opcode UNUSED)
{
  // no flags affected
  uint16_t addr;
  addr = mem_read (pc++);
  addr |= (mem_read (pc++) << 8);
  if (bdos_sim && (addr == 0x0005))
    bdos_call ();
  else
    {
      mem_write (--REG_SP, pc >> 8);
      mem_write (--REG_SP, pc & 0xff);
      pc = addr;
    }
}

static void op_Ccond (uint8_t opcode)
{
  // no flags affected
  int cond = (opcode >> 3) & 7;
  uint16_t addr;
  addr = mem_read (pc++);
  addr |= (mem_read (pc++) << 8);
  if (eval_cond (cond))
    {
      mem_write (--REG_SP, pc >> 8);
      mem_write (--REG_SP, pc & 0xff);
      pc = addr;
      cycle_count += 6;
    }
}

static void op_RET (uint8_t opcode UNUSED)
{
  // no flags affected
  pc = mem_read (REG_SP++);
  pc |= (mem_read (REG_SP++) << 8);
}

static void op_Rcond (uint8_t opcode)
{
  // no flags affected
  int cond = (opcode >> 3) & 7;
  if (eval_cond (cond))
    {
      pc = mem_read (REG_SP++);
      pc |= (mem_read (REG_SP++) << 8);
      cycle_count += 6;
    }
}

static void op_RST (uint8_t opcode)
{
  // no flags affected
  mem_write (--REG_SP, pc >> 8);
  mem_write (--REG_SP, pc & 0xff);
  pc = opcode & 070;
}

static void op_PCHL (uint8_t opcode UNUSED)
{
  // no flags affected
  pc = REG_PAIR_HL;
}

// ------------------------------------------------------------
// Stack, I/O and Machine Control Group
// Unless otherwise specified, flags are not affected by any
// instruction in this group
// ------------------------------------------------------------

static void op_PUSH (uint8_t opcode)
{
  // no flags affected
  int rp = (opcode >> 4) & 3;
  uint16_t data;
  if (rp == 3)  // PSW
    data = (REG_A << 8) | get_f ();
  else
    data = REG_PAIR (rp);
  mem_write (--REG_SP, data >> 8);
  mem_write (--REG_SP, data & 0xff);
}

static void op_POP (uint8_t opcode)
{
  // POP BC, DE, HL: no flags affected
  // POP PSW: all flags affected
  int rp = (opcode >> 4) & 3;
  uint16_t data;
  data = mem_read (REG_SP++);
  data |= (mem_read (REG_SP++) << 8);
  if (rp == 3)  // PSW
    {
      REG_A = data >> 8;
      set_f (data & 0xff);
    }
  else
    REG_PAIR (rp) = data;
}

static void op_XTHL (uint8_t opcode UNUSED)
{
  // no flags affected
  uint16_t data;
  data = mem_read (REG_SP++);
  data |= (mem_read (REG_SP) << 8);
  mem_write (REG_SP--, REG_H);
  mem_write (REG_SP, REG_L);
  REG_PAIR_HL = data;
}

static void op_SPHL (uint8_t opcode UNUSED)
{
  // no flags affected
  REG_SP = REG_PAIR_HL;
}

static void op_IN (uint8_t opcode UNUSED)
{
  // no flags affected
  uint8_t port;
  uint8_t c;
  fd_set read_fds, write_fds, except_fds;
  int status;
  struct timeval timeout;
  port = mem_read (pc++);
  switch (port)
    {
    case 0x00:
      // read key pressed status
      FD_ZERO (& read_fds);
      FD_ZERO (& write_fds);
      FD_ZERO (& except_fds);
      FD_SET (STDIN_FILENO, & read_fds);
      timeout.tv_sec = 0;
      timeout.tv_usec = 0;
      status = select (1, & read_fds, & write_fds, & except_fds, & timeout);
      if (status < 0)
	fatal (EX_IOERR, "select() error %d", status);
      REG_A = FD_ISSET (STDIN_FILENO, & read_fds);
      break;
    case 0x01:
      // read key
      read (STDIN_FILENO, & c, 1);
      REG_A = c;
      break;
    case 0xf8:  // Sol-20 UART status port
      REG_A = 0x80;  // transmit buffer empty
      break;
    }
}

static void op_OUT (uint8_t opcode UNUSED)
{
  // no flags affected
  // $$$
  uint8_t port;
  uint8_t c;
  c = REG_A;
  port = mem_read (pc++);
  switch (port)
    {
    case 0x00:
    case 0xf9:  // Sol-20 UART data port
      write (STDOUT_FILENO, & c, 1);
      break;
    }
}

static void op_EI (uint8_t opcode UNUSED)
{
  // no flags affected
  interrupt_enabled = true;
}

static void op_DI (uint8_t opcode UNUSED)
{
  // no flags affected
  interrupt_enabled = false;
}

static void op_HLT (uint8_t opcode UNUSED)
{
  // no flags affected
  halted = true;
  printf ("halt at pc=%04x\n", pc);
}

static void op_NOP (uint8_t opcode UNUSED)
{
  // no flags affected
  // do nothing
}


typedef enum
{
  MODE_IMP,
  MODE_IMM1,
  MODE_IMM2,
  MODE_DIR1,
  MODE_DIR2
} addr_mode_t;

typedef void op_fn_t (uint8_t opcode);

typedef struct
{
  int cycles;
  op_fn_t *fn;
  char *mnem;
  addr_mode_t mode;
} op_info_t;

op_info_t op_tbl [256] =
{
  [0000] = {  4, op_NOP,     "NOP",             MODE_IMP },
  [0001] = { 10, op_LXI,     "LXI   BC,#%04xh", MODE_IMM2 },
  [0002] = {  7, op_STAX,    "STAX  BC",        MODE_IMP },
  [0003] = {  5, op_INX,     "INX   BC",        MODE_IMP },
  [0004] = {  5, op_INR,     "INR   B",         MODE_IMP },
  [0005] = {  5, op_DCR,     "DCR   B",         MODE_IMP },
  [0006] = {  7, op_MVI,     "MVI   B,#%02xh",  MODE_IMM1 },
  [0007] = {  4, op_RLC,     "RLC",             MODE_IMP },

  // 0x08 0010 - on 8080 equivalent to 000 NOP
  //           - on 8085, DSUB - double subtract, HL = HL - BC
  //           - on Z80, EX AF,AF
  [0010] = {  0, illegal_op, "???",             MODE_IMP },

  [0011] = { 10, op_DAD,     "DAD   BC",        MODE_IMP },
  [0012] = {  7, op_LDAX,    "LDAX  BC",        MODE_IMP },
  [0013] = {  5, op_DCX,     "DCX   BC",        MODE_IMP },
  [0014] = {  5, op_INR,     "INR   C",         MODE_IMP },
  [0015] = {  5, op_DCR,     "DCR   C",         MODE_IMP },
  [0016] = {  7, op_MVI,     "MVI   C,#%02xh",  MODE_IMM1 },
  [0017] = {  4, op_RRC,     "RRC",             MODE_IMP },

  // 0x10 0020 - on 8080 equivalent to 000 NOP
  //           - on 8085, ARHL - arithmetic shift HL right
  //           - on Z80, DJNZ d (relative)
  [0020] = {  0, illegal_op, "???",             MODE_IMP },  

  [0021] = { 10, op_LXI,     "LXI   DE,#%04xh", MODE_IMM2 },
  [0022] = {  7, op_STAX,    "STAX  DE",        MODE_IMP },
  [0023] = {  5, op_INX,     "INX   DE",        MODE_IMP },
  [0024] = {  5, op_INR,     "INR   D",         MODE_IMP },
  [0025] = {  5, op_DCR,     "DCR   D",         MODE_IMP },
  [0026] = {  7, op_MVI,     "MVI   D,#%02xh",  MODE_IMM1 },
  [0027] = {  4, op_RAL,     "RAL",             MODE_IMP },

  // 0x18 0030 - on 8080, equivalent to 000 NOP
  //           - on 8085, RDEL  rotate DE left through carry
  //           - on Z80, JR d (relative)
  [0030] = {  0, illegal_op, "???",             MODE_IMP },

  [0031] = { 10, op_DAD,     "DAD   DE",        MODE_IMP },
  [0032] = {  7, op_LDAX,    "LDAX  DE",        MODE_IMP },
  [0033] = {  5, op_DCX,     "DCX   DE",        MODE_IMP },
  [0034] = {  5, op_INR,     "INR   E",         MODE_IMP },
  [0035] = {  5, op_DCR,     "DCR   E",         MODE_IMP },
  [0036] = {  7, op_MVI,     "MVI   E,#%02xh",  MODE_IMM1 },
  [0037] = {  4, op_RAR,     "RAR",             MODE_IMP },

  // 0x20 0040 - on 8080, equivalent to 000 NOP
  //           - on 8085, RIM
  //           - on Z80, JR NZ, d  (relative)
  [0040] = {  0, illegal_op, "???",             MODE_IMP },

  [0041] = { 10, op_LXI,     "LXI   HL,#%04xh", MODE_IMM2 },
  [0042] = { 16, op_SHLD,    "SHLD  %04xh",     MODE_DIR2 },
  [0043] = {  5, op_INX,     "INX   HL",        MODE_IMP },
  [0044] = {  5, op_INR,     "INR   H",         MODE_IMP },
  [0045] = {  5, op_DCR,     "DCR   H",         MODE_IMP },
  [0046] = {  7, op_MVI,     "MVI   H,#%02xh",  MODE_IMM1 },
  [0047] = {  4, op_DAA,     "DAA",             MODE_IMP },

  // 0x28 0050 - on 8080, equivalent to 000 NOP
  //           - on 8085, LDHI  DE = HL + immediate byte
  //           - on Z80, JR Z, d  (relative)
  [0050] = {  0, illegal_op, "???",             MODE_IMP },

  [0051] = { 10, op_DAD,     "DAD   HL",        MODE_IMP },
  [0052] = { 16, op_LHLD,    "LHLD  %04xh",     MODE_DIR2 },
  [0053] = {  5, op_DCX,     "DCX   HL",        MODE_IMP },
  [0054] = {  5, op_INR,     "INR   L",         MODE_IMP },
  [0055] = {  5, op_DCR,     "DCR   L",         MODE_IMP },
  [0056] = {  7, op_MVI,     "MVI   L,#%02xh",  MODE_IMM1 },
  [0057] = {  4, op_CMA,     "CMA",             MODE_IMP },

  // 0x30 0060 - on 8080, equivalent to 000 NOP
  //           - on 8085, SIM
  //           - on Z80, JR NC, d  (relative)
  [0060] = {  0, illegal_op, "???",             MODE_IMP },

  [0061] = { 10, op_LXI,     "LXI   SP,#%04xh", MODE_IMM2 },
  [0062] = { 13, op_STA,     "STA   %04xh",     MODE_DIR2 },
  [0063] = {  5, op_INX,     "INX   SP",        MODE_IMP },
  [0064] = { 10, op_INR,     "INR   M",         MODE_IMP },
  [0065] = { 10, op_DCR,     "DCR   M",         MODE_IMP },
  [0066] = { 10, op_MVI,     "MVI   M,#%02xh",  MODE_IMM1 },
  [0067] = {  4, op_STC,     "STC",             MODE_IMP },

  // 0x38 0070 - on 8080, equivalent to 000 NOP
  //           - on 8085, LDSI  DE = SP + immediate byte
  //           - on Z80, JR C, d  (relative)
  [0070] = {  0, illegal_op, "???",             MODE_IMP },

  [0071] = { 10, op_DAD,     "DAD   SP",        MODE_IMP },
  [0072] = { 13, op_LDA,     "LDA   %04xh",     MODE_DIR2 },
  [0073] = {  5, op_DCX,     "DCX   SP",        MODE_IMP },
  [0074] = {  5, op_INR,     "INR   A",         MODE_IMP },
  [0075] = {  5, op_DCR,     "DCR   A",         MODE_IMP },
  [0076] = {  7, op_MVI,     "MVI   A,#%02xh",  MODE_IMM1 },
  [0077] = {  4, op_CMC,     "CMC",             MODE_IMP },

  [0100] = {  5, op_MOV,     "MOV   B,B",       MODE_IMP },
  [0101] = {  5, op_MOV,     "MOV   B,C",       MODE_IMP },
  [0102] = {  5, op_MOV,     "MOV   B,D",       MODE_IMP },
  [0103] = {  5, op_MOV,     "MOV   B,E",       MODE_IMP },
  [0104] = {  5, op_MOV,     "MOV   B,H",       MODE_IMP },
  [0105] = {  5, op_MOV,     "MOV   B,L",       MODE_IMP },
  [0106] = {  7, op_MOV,     "MOV   B,M",       MODE_IMP },
  [0107] = {  5, op_MOV,     "MOV   B,A",       MODE_IMP },
  [0110] = {  5, op_MOV,     "MOV   C,B",       MODE_IMP },
  [0111] = {  5, op_MOV,     "MOV   C,C",       MODE_IMP },
  [0112] = {  5, op_MOV,     "MOV   C,D",       MODE_IMP },
  [0113] = {  5, op_MOV,     "MOV   C,E",       MODE_IMP },
  [0114] = {  5, op_MOV,     "MOV   C,H",       MODE_IMP },
  [0115] = {  5, op_MOV,     "MOV   C,L",       MODE_IMP },
  [0116] = {  7, op_MOV,     "MOV   C,M",       MODE_IMP },
  [0117] = {  5, op_MOV,     "MOV   C,A",       MODE_IMP },
  [0120] = {  5, op_MOV,     "MOV   D,B",       MODE_IMP },
  [0121] = {  5, op_MOV,     "MOV   D,C",       MODE_IMP },
  [0122] = {  5, op_MOV,     "MOV   D,D",       MODE_IMP },
  [0123] = {  5, op_MOV,     "MOV   D,E",       MODE_IMP },
  [0124] = {  5, op_MOV,     "MOV   D,H",       MODE_IMP },
  [0125] = {  5, op_MOV,     "MOV   D,L",       MODE_IMP },
  [0126] = {  7, op_MOV,     "MOV   D,M",       MODE_IMP },
  [0127] = {  5, op_MOV,     "MOV   D,A",       MODE_IMP },
  [0130] = {  5, op_MOV,     "MOV   E,B",       MODE_IMP },
  [0131] = {  5, op_MOV,     "MOV   E,C",       MODE_IMP },
  [0132] = {  5, op_MOV,     "MOV   E,D",       MODE_IMP },
  [0133] = {  5, op_MOV,     "MOV   E,E",       MODE_IMP },
  [0134] = {  5, op_MOV,     "MOV   E,H",       MODE_IMP },
  [0135] = {  5, op_MOV,     "MOV   E,L",       MODE_IMP },
  [0136] = {  7, op_MOV,     "MOV   E,M",       MODE_IMP },
  [0137] = {  5, op_MOV,     "MOV   E,A",       MODE_IMP },
  [0140] = {  5, op_MOV,     "MOV   H,B",       MODE_IMP },
  [0141] = {  5, op_MOV,     "MOV   H,C",       MODE_IMP },
  [0142] = {  5, op_MOV,     "MOV   H,D",       MODE_IMP },
  [0143] = {  5, op_MOV,     "MOV   H,E",       MODE_IMP },
  [0144] = {  5, op_MOV,     "MOV   H,H",       MODE_IMP },
  [0145] = {  5, op_MOV,     "MOV   H,L",       MODE_IMP },
  [0146] = {  7, op_MOV,     "MOV   H,M",       MODE_IMP },
  [0147] = {  5, op_MOV,     "MOV   H,A",       MODE_IMP },
  [0150] = {  5, op_MOV,     "MOV   L,B",       MODE_IMP },
  [0151] = {  5, op_MOV,     "MOV   L,C",       MODE_IMP },
  [0152] = {  5, op_MOV,     "MOV   L,D",       MODE_IMP },
  [0153] = {  5, op_MOV,     "MOV   L,E",       MODE_IMP },
  [0154] = {  5, op_MOV,     "MOV   L,H",       MODE_IMP },
  [0155] = {  5, op_MOV,     "MOV   L,L",       MODE_IMP },
  [0156] = {  7, op_MOV,     "MOV   L,M",       MODE_IMP },
  [0157] = {  5, op_MOV,     "MOV   L,A",       MODE_IMP },
  [0160] = {  7, op_MOV,     "MOV   M,B",       MODE_IMP },
  [0161] = {  7, op_MOV,     "MOV   M,C",       MODE_IMP },
  [0162] = {  7, op_MOV,     "MOV   M,D",       MODE_IMP },
  [0163] = {  7, op_MOV,     "MOV   M,E",       MODE_IMP },
  [0164] = {  7, op_MOV,     "MOV   M,H",       MODE_IMP },
  [0165] = {  7, op_MOV,     "MOV   M,L",       MODE_IMP },
  [0166] = {  7, op_HLT,     "HLT",             MODE_IMP },
  [0167] = {  7, op_MOV,     "MOV   M,A",       MODE_IMP },
  [0170] = {  5, op_MOV,     "MOV   A,B",       MODE_IMP },
  [0171] = {  5, op_MOV,     "MOV   A,C",       MODE_IMP },
  [0172] = {  5, op_MOV,     "MOV   A,D",       MODE_IMP },
  [0173] = {  5, op_MOV,     "MOV   A,E",       MODE_IMP },
  [0174] = {  5, op_MOV,     "MOV   A,H",       MODE_IMP },
  [0175] = {  5, op_MOV,     "MOV   A,L",       MODE_IMP },
  [0176] = {  7, op_MOV,     "MOV   A,M",       MODE_IMP },
  [0177] = {  5, op_MOV,     "MOV   A,A",       MODE_IMP },

  [0200] = {  4, op_ADD,     "ADD   B",         MODE_IMP },
  [0201] = {  4, op_ADD,     "ADD   C",         MODE_IMP },
  [0202] = {  4, op_ADD,     "ADD   D",         MODE_IMP },
  [0203] = {  4, op_ADD,     "ADD   E",         MODE_IMP },
  [0204] = {  4, op_ADD,     "ADD   H",         MODE_IMP },
  [0205] = {  4, op_ADD,     "ADD   L",         MODE_IMP },
  [0206] = {  7, op_ADD,     "ADD   M",         MODE_IMP },
  [0207] = {  4, op_ADD,     "ADD   A",         MODE_IMP },
  [0210] = {  4, op_ADC,     "ADC   B",         MODE_IMP },
  [0211] = {  4, op_ADC,     "ADC   C",         MODE_IMP },
  [0212] = {  4, op_ADC,     "ADC   D",         MODE_IMP },
  [0213] = {  4, op_ADC,     "ADC   E",         MODE_IMP },
  [0214] = {  4, op_ADC,     "ADC   H",         MODE_IMP },
  [0215] = {  4, op_ADC,     "ADC   L",         MODE_IMP },
  [0216] = {  7, op_ADC,     "ADC   M",         MODE_IMP },
  [0217] = {  4, op_ADC,     "ADC   A",         MODE_IMP },
  [0220] = {  4, op_SUB,     "SUB   B",         MODE_IMP },
  [0221] = {  4, op_SUB,     "SUB   C",         MODE_IMP },
  [0222] = {  4, op_SUB,     "SUB   D",         MODE_IMP },
  [0223] = {  4, op_SUB,     "SUB   E",         MODE_IMP },
  [0224] = {  4, op_SUB,     "SUB   H",         MODE_IMP },
  [0225] = {  4, op_SUB,     "SUB   L",         MODE_IMP },
  [0226] = {  7, op_SUB,     "SUB   M",         MODE_IMP },
  [0227] = {  4, op_SUB,     "SUB   A",         MODE_IMP },
  [0230] = {  4, op_SBB,     "SBB   B",         MODE_IMP },
  [0231] = {  4, op_SBB,     "SBB   C",         MODE_IMP },
  [0232] = {  4, op_SBB,     "SBB   D",         MODE_IMP },
  [0233] = {  4, op_SBB,     "SBB   E",         MODE_IMP },
  [0234] = {  4, op_SBB,     "SBB   H",         MODE_IMP },
  [0235] = {  4, op_SBB,     "SBB   L",         MODE_IMP },
  [0236] = {  7, op_SBB,     "SBB   M",         MODE_IMP },
  [0237] = {  4, op_SBB,     "SBB   A",         MODE_IMP },
  [0240] = {  4, op_ANA,     "ANA   B",         MODE_IMP },
  [0241] = {  4, op_ANA,     "ANA   C",         MODE_IMP },
  [0242] = {  4, op_ANA,     "ANA   D",         MODE_IMP },
  [0243] = {  4, op_ANA,     "ANA   E",         MODE_IMP },
  [0244] = {  4, op_ANA,     "ANA   H",         MODE_IMP },
  [0245] = {  4, op_ANA,     "ANA   L",         MODE_IMP },
  [0246] = {  7, op_ANA,     "ANA   M",         MODE_IMP },
  [0247] = {  4, op_ANA,     "ANA   A",         MODE_IMP },
  [0250] = {  4, op_XRA,     "XRA   B",         MODE_IMP },
  [0251] = {  4, op_XRA,     "XRA   C",         MODE_IMP },
  [0252] = {  4, op_XRA,     "XRA   D",         MODE_IMP },
  [0253] = {  4, op_XRA,     "XRA   E",         MODE_IMP },
  [0254] = {  4, op_XRA,     "XRA   H",         MODE_IMP },
  [0255] = {  4, op_XRA,     "XRA   L",         MODE_IMP },
  [0256] = {  7, op_XRA,     "XRA   M",         MODE_IMP },
  [0257] = {  4, op_XRA,     "XRA   A",         MODE_IMP },
  [0260] = {  4, op_ORA,     "ORA   B",         MODE_IMP },
  [0261] = {  4, op_ORA,     "ORA   C",         MODE_IMP },
  [0262] = {  4, op_ORA,     "ORA   D",         MODE_IMP },
  [0263] = {  4, op_ORA,     "ORA   E",         MODE_IMP },
  [0264] = {  4, op_ORA,     "ORA   H",         MODE_IMP },
  [0265] = {  4, op_ORA,     "ORA   L",         MODE_IMP },
  [0266] = {  7, op_ORA,     "ORA   M",         MODE_IMP },
  [0267] = {  4, op_ORA,     "ORA   A",         MODE_IMP },
  [0270] = {  4, op_CMP,     "CMP   B",         MODE_IMP },
  [0271] = {  4, op_CMP,     "CMP   C",         MODE_IMP },
  [0272] = {  4, op_CMP,     "CMP   D",         MODE_IMP },
  [0273] = {  4, op_CMP,     "CMP   E",         MODE_IMP },
  [0274] = {  4, op_CMP,     "CMP   H",         MODE_IMP },
  [0275] = {  4, op_CMP,     "CMP   L",         MODE_IMP },
  [0276] = {  7, op_CMP,     "CMP   M",         MODE_IMP },
  [0277] = {  4, op_CMP,     "CMP   A",         MODE_IMP },

  [0300] = {  5, op_Rcond,   "RNZ",             MODE_IMP },  // +6 if taken
  [0301] = { 10, op_POP,     "POP   BC",        MODE_IMP },
  [0302] = { 10, op_Jcond,   "JNZ   %04xh",     MODE_DIR2 },
  [0303] = { 10, op_JMP,     "JMP   %04xh",     MODE_DIR2 },
  [0304] = { 10, op_Ccond,   "CNZ   %04xh",     MODE_DIR2 },  // +6 if taken
  [0305] = { 11, op_PUSH,    "PUSH  BC",        MODE_IMP },
  [0306] = {  7, op_ADI,     "ADI   #%02xh",    MODE_IMM1 },
  [0307] = { 11, op_RST,     "RST   0",         MODE_IMP },
  [0310] = {  5, op_Rcond,   "RZ",              MODE_IMP },  // +6 if taken
  [0311] = { 10, op_RET,     "RET",             MODE_IMP },
  [0312] = { 10, op_Jcond,   "JZ    %04xh",     MODE_DIR2 },

  // 0xCB 0313 - on 8080, equivalent to 0303 JMP
  //           - on 8085, RSTV   restart to 0x40 if overflow flag V set
  //           - on Z80, prefix byte
  [0313] = {  0, illegal_op, "???",             MODE_IMP },  

  [0314] = { 10, op_Ccond,   "CZ    %04xh",     MODE_DIR2 },  // +6 if taken
  [0315] = { 17, op_CALL,    "CALL  %04xh",     MODE_DIR2 },
  [0316] = {  7, op_ACI,     "ACI   #%02xh",    MODE_IMM1 },
  [0317] = { 11, op_RST,     "RST   1",         MODE_IMP },
  [0320] = {  5, op_Rcond,   "RNC",             MODE_IMP },   // +6 if taken
  [0321] = { 10, op_POP,     "POP   DE",        MODE_IMP },
  [0322] = { 10, op_Jcond,   "JNC   %04xh",     MODE_DIR2 },
  [0323] = { 10, op_OUT,     "OUT   %02xh",     MODE_DIR1 },
  [0324] = { 10, op_Ccond,   "CNC   %04xh",     MODE_DIR2 },  // +6 if taken
  [0325] = { 11, op_PUSH,    "PUSH  DE",        MODE_IMP },
  [0326] = {  7, op_SUI,     "SUI   #%02xh",    MODE_IMM1 },
  [0327] = { 11, op_RST,     "RST   2",         MODE_IMP },
  [0330] = {  5, op_Rcond,   "RC",              MODE_IMP },   // +6 if taken

  // 0xD9 0331 - on 8080, equivalent to 0311 RET
  //           - on 8085, SHLX  mem [DE] = HL
  //           - on Z80, EXX
  [0331] = {  0, illegal_op, "???",             MODE_IMP },

  [0332] = { 10, op_Jcond,   "JC    %04xh",     MODE_DIR2 },
  [0333] = { 10, op_IN,      "IN    %02xh",     MODE_DIR1 },
  [0334] = { 10, op_Ccond,   "CC    %04xh",     MODE_DIR2 },  // +6 if taken

  // 0xDD 0335 - on 8080, equivalent to 0315 CALL
  //           - on 8085, JNUI  jump on not UI flag, direct
  //           - on Z80, prefix byte
  [0335] = {  0, illegal_op, "???",             MODE_IMP },

  [0336] = {  7, op_SBI,     "SBI   #%02xh",    MODE_IMM1 },
  [0337] = { 11, op_RST,     "RST   3",         MODE_IMP },
  [0340] = {  5, op_Rcond,   "RPO",             MODE_IMP },   // +6 if taken
  [0341] = { 10, op_POP,     "POP   HL",        MODE_IMP },
  [0342] = { 10, op_Jcond,   "JPO   %04xh",     MODE_DIR2 },
  [0343] = { 18, op_XTHL,    "XTHL",            MODE_IMP },
  [0344] = { 10, op_Ccond,   "CPO   %04xh",     MODE_DIR2 },  // +6 if taken
  [0345] = { 11, op_PUSH,    "PUSH  HL",        MODE_IMP },
  [0346] = {  7, op_ANI,     "ANI   #%02xh",    MODE_IMM1 },
  [0347] = { 11, op_RST,     "RST   4",         MODE_IMP },
  [0350] = {  5, op_Rcond,   "RPE",             MODE_IMP },   // +6 if taken
  [0351] = {  5, op_PCHL,    "PCHL",            MODE_IMP },
  [0352] = { 10, op_Jcond,   "JPE   %04xh",     MODE_DIR2 },
  [0353] = {  4, op_XCHG,    "XCHG",            MODE_IMP },
  [0354] = { 10, op_Ccond,   "CPE   %04xh",     MODE_DIR2 },  // +6 if taken

  // 0xED 0355 - on 8080, equivalent to 0315 CALL
  //           - on 8085, LHLX    HL = mem [DE]
  //           - on Z80, prefix byte
  [0355] = {  0, op_disk_io, "disk  %04xh",     MODE_DIR2 },

  [0356] = {  7, op_XRI,     "XRI   #%04xh",    MODE_IMM1 },
  [0357] = { 11, op_RST,     "RST   5",         MODE_IMP },
  [0360] = {  5, op_Rcond,   "RP",              MODE_IMP },   // +6 taken
  [0361] = { 10, op_POP,     "POP   PSW",       MODE_IMP },
  [0362] = { 10, op_Jcond,   "JP    %04xh",     MODE_DIR2 },
  [0363] = {  4, op_DI,      "DI",              MODE_IMP },
  [0364] = { 10, op_Ccond,   "CP    %04xh",     MODE_DIR2 },  // +6 if taken
  [0365] = { 11, op_PUSH,    "PUSH  PSW",       MODE_IMP },
  [0366] = {  7, op_ORI,     "ORI   #%02xh",    MODE_IMM1 },
  [0367] = { 11, op_RST,     "RST   6",         MODE_IMP },
  [0370] = {  5, op_Rcond,   "RM",              MODE_IMP },   // +6 if taken
  [0371] = {  5, op_SPHL,    "SPHL",            MODE_IMP },
  [0372] = { 10, op_Jcond,   "JM    %04xh",     MODE_DIR2 },
  [0373] = {  4, op_EI,      "EI",              MODE_IMP },
  [0374] = { 10, op_Ccond,   "CM    %04xh",     MODE_DIR2 },  // +6 if taken

  // 0xFD 0375 - on 8080, equivalent to 0315 CALL
  //           - on 8085, JUI  jump on UI flag, direct
  //           - on Z80, prefix byte
  [0375] = {  0, illegal_op, "???",             MODE_IMP },

  [0376] = {  7, op_CPI,     "CPI   #%02xh",    MODE_IMM1 },
  [0377] = { 11, op_RST,     "RST   7",         MODE_IMP }
};


void disassemble_inst (FILE *f, uint16_t addr)
{
  uint8_t opcode = mem_read (addr++);
  addr_mode_t mode = op_tbl [opcode].mode;
  uint16_t data;

  switch (mode)
    {
    case MODE_IMM1:
    case MODE_DIR1:
      data = mem_read (addr++);
      fprintf (f, op_tbl [opcode].mnem, data);
      break;
    case MODE_IMM2:
    case MODE_DIR2:
      data = mem_read (addr++);
      data |= (mem_read (addr++) << 8);
      fprintf (f, op_tbl [opcode].mnem, data);
      break;
    default:
      fprintf (f, op_tbl [opcode].mnem);
    }
}

void execute_instruction (void)
{
  uint8_t opcode;
  uint16_t old_pc;
  old_pc = pc;
  opcode = mem_read (pc++);
  if (trace_f)
    {
      fprintf (trace_f, "%04x: %04o  ", old_pc, opcode);
      disassemble_inst (trace_f, old_pc);
      fprintf (trace_f, "\n");
    }
  (* op_tbl [opcode].fn) (opcode);
  cycle_count += op_tbl [opcode].cycles;
  if (trace_f)
    {
      fprintf (trace_f, "        BC=%04x DE=%04x HL=%04x AF=%02x%02x\n",
	       REG_PAIR_BC, REG_PAIR_DE, REG_PAIR_HL, REG_A, get_f ());
    }
}

void reset_processor (void)
{
  halted = false;
  interrupt_enabled = false;
  pc = 0x0000;
}


static void init_parity_table (void)
{
  int i;
  //printf ("  0123456789abcdef\n");
  //printf ("  ----------------\n");
  for (i = 0; i < 256; i++)
    {
      //if ((i & 0xf) == 0)
      //  printf ("%x ", i >> 4);
      int odd_parity = ((i >> 7) ^ (i >> 6) ^ (i >> 5) ^ (i >> 4) ^
			(i >> 3) ^ (i >> 2) ^ (i >> 1) ^ (i >> 0)) & 1;
      even_parity [i] = ! odd_parity;
      //printf ("%x", even_parity [i]);
      //if ((i & 0xf) == 0xf)
      //  printf ("\n");
    }
}


uint32_t hex_extract (char *p, int count)
{
  uint32_t data = 0;
  while (count--)
    {
      uint8_t c = *(p++);
      data <<= 4;
      if ((c >= '0') && (c <= '9'))
	data += (c - '0');
      else if ((c >= 'A') && (c <= 'F'))
	data += (c + 10 - 'A');
      else if ((c >= 'a') && (c <= 'f'))
	data += (c + 10 - 'a');
    }
  return data;
}

uint32_t start_addr = 0;

void load_hex (char *fn)
{
  FILE *f;
  char buf [257];

  f = fopen (fn, "rb");
  if (! f)
    {
      fprintf (stderr, "can't open file\n");
      exit (1);
    }

  while (fgets (buf, sizeof (buf), f))
    {
      uint8_t data_len;
      uint16_t addr;
      uint8_t rec_type;
      int i;
      if (buf [0] != ':')
	continue;
      data_len = hex_extract (& buf [1], 2);
      addr = hex_extract (& buf [3], 4);
      rec_type = hex_extract (& buf [7], 2);
      //printf ("addr %04x count %02x rec type %02x\n", addr, data_len, rec_type);
      switch (rec_type)
	{
	case 0x00:
	  for (i = 0; i < data_len; i++)
	    {
	      uint8_t byte = hex_extract (& buf [9 + 2 * i], 2);
	      mem_write (addr + i, byte);
	    }
	  break;
	case 0x03:
	  start_addr = addr;
	  break;
	}
      // $$$ need to verify checksum!
    }

  fclose (f);
}

void load_binary (char *fn)
{
  FILE *f;

  f = fopen (fn, "rb");
  if (! f)
    {
      fprintf (stderr, "can't open file\n");
      exit (1);
    }

  // $$$ BAD!  Should check result!
  fread (mem, sizeof (mem), 1, f);

  fclose (f);

  start_addr = 0x0000;
}

int main (int argc, char **argv)
{
  bool hex_file = false;
  bool bin_file = false;
  char *fn = NULL;
  char *trace_fn = NULL;

  progname = argv [0];

  init_parity_table ();

  while (--argc)
    {
      argv++;
      if (argv [0][0] == '-')
	{
	  if (strcmp (argv [0], "-b") == 0)
	    bin_file = true;
	  else if (strcmp (argv [0], "-h") == 0)
	    hex_file = true;
	  else if (strcmp (argv [0], "-t") == 0)
	    {
	      if (! --argc)
		fatal (EX_USAGE, "Missing argument for '-t' option.\n");
	      if (trace_fn)
		fatal (EX_USAGE, "Only one '-t' option may be specified.\n");
	      trace_fn = argv [1];
	      argv++;
	    }
	  else if (strcmp (argv [0], "--bdos") == 0)
	    bdos_sim = true;
	  else
	    fatal (EX_USAGE, "Unknown option '%s'.\n", argv [0]);
	}
      else
	{
	  if (! fn)
	    fn = argv [0];
	  else
	    fatal (EX_USAGE, "Only one bootstrap file may be specified.\n");
	}
    }

  if (! fn)
    fatal (EX_USAGE, "A bootstrap file must be specified.\n");

  if (hex_file && bin_file)
    fatal (EX_USAGE, "The -b and -h options are mutually exclusive.\n");

  if (trace_fn)
    {
      trace_f = fopen (trace_fn, "w");
      if (! trace_f)
	fatal (EX_CANTCREAT, "Can't create trace file '%f'.\n", trace_fn);
    }

  if (hex_file)
    load_hex (fn);
  else if (bin_file)
    load_binary (fn);

  console_init ();
  atexit (console_cleanup);

  reset_processor ();

  pc = start_addr;

  while (! halted)
    execute_instruction ();

  return 0;
}
