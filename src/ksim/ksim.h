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
#include <stdbool.h>

extern void ksim_init (char *, bool, FILE *);
extern void ksim_fatal (int ret, char *format, ...) __attribute__ ((noreturn));
extern uint32_t ksim_load_binary (char *);
extern uint32_t ksim_load_hex (char *);
extern void ksim_reset_processor (uint16_t);
extern bool ksim_execute_instruction (void);
