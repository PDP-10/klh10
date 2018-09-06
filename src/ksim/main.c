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


#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include "ksim.h"

int main (int argc, char **argv)
{
  bool hex_file = false;
  bool bin_file = false;
  bool bdos_sim = false;
  char *fn = NULL;
  char *trace_fn = NULL;
  FILE *trace_f = NULL;
  int halted = 0;
  char *progname;
  uint16_t start_addr;

  progname = argv [0];

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
		ksim_fatal (EX_USAGE, "Missing argument for '-t' option.\n");
	      if (trace_fn)
		ksim_fatal (EX_USAGE, "Only one '-t' option may be specified.\n");
	      trace_fn = argv [1];
	      argv++;
	    }
	  else if (strcmp (argv [0], "--bdos") == 0)
	    bdos_sim = true;
	  else
	    ksim_fatal (EX_USAGE, "Unknown option '%s'.\n", argv [0]);
	}
      else
	{
	  if (! fn)
	    fn = argv [0];
	  else
	    ksim_fatal (EX_USAGE, "Only one bootstrap file may be specified.\n");
	}
    }

  if (! fn)
    ksim_fatal (EX_USAGE, "A bootstrap file must be specified.\n");

  if (hex_file && bin_file)
    ksim_fatal (EX_USAGE, "The -b and -h options are mutually exclusive.\n");

  if (trace_fn)
    {
      trace_f = fopen (trace_fn, "w");
      if (! trace_f)
	ksim_fatal (EX_CANTCREAT, "Can't create trace file '%f'.\n", trace_fn);
    }

  ksim_init (progname, bdos_sim, trace_f);

  if (hex_file)
    start_addr = ksim_load_hex (fn);
  else if (bin_file)
    start_addr = ksim_load_binary (fn);

  ksim_reset_processor (start_addr);

  while (! halted)
    halted = ksim_execute_instruction ();

  return 0;
}
