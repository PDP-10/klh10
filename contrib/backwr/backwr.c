#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <utime.h>

#include "sysdep.h"

#include "backup.h"

typedef unsigned int bool;
#  define false 0
#  define true  1

typedef unsigned char byte;
typedef unsigned long long int36;

#define const36(x) (int36) x##LL

#define tpf_RAW 1		/* Tape format - raw data. */
#define tpf_TAP 2		/* Tape format - .TAP container. */
#define tpf_TPC 3		/* Tape format - .TPC container. */

/*
** input/output buffers:
*/

byte diskbuffer[512*5];		/* Disk file data. */
int diskcount;			/* Amount of data in above. */

int36 taperecord[32+512];	/* Tape record we build/decode. */

FILE* tapefile;			/* Tape file we read/write. */
char* tapename;			/* Name of tape file. */
int tapeformat = tpf_TAP;	/* Format of tape file. */

FILE* diskfile;			/* Disk file we read/write. */
unsigned long filelength;	/*   Length in bytes. */
time_t filemodified;		/*   Time last modified. */

int dataoffset;			/* Data offset in file. */

bool asciiflag = false;		/* ASCII (newline <--> CRLF) conversion? */
bool ansiflag = true;		/* ANSI file format, or CORE-DUMP? */

int debug = 0;			/* Debug level. */
int verbose = 0;		/* Verbosity level. */

char** argfiles;		/* File spec's to write. */
int argcount;			/* Number of them. */

/*
** tops-10 file information:
*/

char topsname[100];		/* File name. */
char topsext[100];		/* Extension. */

/*
** Tape information:
*/

char systemname[100];
char savesetname[100];

/************************************************************************/

static void print36(int36 word)
{
  int l, r;

  l = (word >> 18) & 0777777;
  r = word & 0777777;

  printf("%06o,,%06o", l, r);
}

static int nextseq(void)
{
  static int record = 1;

  return (record++);
}

/*
** sixbit() makes a sixbit word from a (short) text.
*/

static int36 sixbit(char name[])
{
  int i;
  char c;
  int36 w;

  w = (int36) 0;
  c = '*';

  for (i = 0; i < 6; i += 1) {
    if (c != (char) 0) {
      c = name[i];
    }
    if (c != (char) 0) {
      if ((c >= 'a') && (c <= 'z')) {
	c = c - 'a' + 'A';
      }
      c -= 32;
    }
    w = (int36) w << 6;
    w += c;
  }
  return (w);
}

/*
** ascii() makes a word out of 0-5 seven-bit characters.
*/

static int36 ascii(char name[])
{
  int i;
  char c;
  int36 w;

  w = (int36) 0;
  c = '*';

  for (i = 0; i < 5; i += 1) {
    if (c != (char) 0) {
      c = name[i];
    }
    w = (int36) w << 7;
    w += c;
  }
  w <<= 1;
  return (w);
}

/*
** xwd() constructs a word out of two halfwords.
*/

static int36 xwd(int l, int r)
{
  int36 w;

  w = l;
  w <<= 18;
  w += r;
  return (w);
}

/*
** w_text() builds (and stores) a text sub-block at the given address.
*/

static int36* w_text(int36* ptr, int type, int words, char* text)
{
  int bytes;

  bytes = strlen(text) + 1;
  if (words == 0) {
    words = (bytes + 4) / 5;
  }
  *ptr++ = xwd(type, words + 1);
  while (words-- > 0) {
    *ptr++ = ascii(text);
    bytes -= 5;
    if (bytes > 0) {
      text += 5;
    } else {
      text = "";
    }
  }
  return (ptr);
}

/*
** checksumdata() computes a checksum over a number of 36-bit words.
** the algorithm used is (in macro-10):
**
** CHKSUM: MOVEI  T1,0
**         MOVSI  T2,-count
**         HRRI   T2,data
** LOOP:   ADD    T1,(T2)
**         ROT    T1,1
**         AOBJN  T2,LOOP
**         POPJ   P,
*/

static int36 checksumdata(int36* data, int count)
{
  int36 checksum;

  checksum = (int36) 0;
  while (count-- > 0) {
    checksum += (int36) *data++;
    if (checksum & (int36) 0x800000000LL) {
      checksum <<= 1;
      checksum += 1;
    } else {
      checksum <<= 1;
    }
    checksum &= (int36) 0xfffffffffLL;
  }
  return (checksum);
}

/*
** checksumbuffer() computes the main checksum of the tape record.
*/

static void checksumbuffer(void)
{
  taperecord[G_CHECK] = (int36) 0;
  taperecord[G_CHECK] = checksumdata(taperecord, 544);
}

/*
** writetape() writes the tape record to the output tape.
*/

static void writetape(void)
{
  int i;
  int36 w;

  switch (tapeformat) {
  case tpf_TAP:
    fputc(0xa0, tapefile);
    fputc(0x0a, tapefile);
    fputc(0x00, tapefile);
    fputc(0x00, tapefile);
    break;
  }

  for (i = 0; i < 544; i += 1) {
    w = taperecord[i];
    fputc((w >> 28) & 0xff, tapefile);
    fputc((w >> 20) & 0xff, tapefile);
    fputc((w >> 12) & 0xff, tapefile);
    fputc((w >> 4) & 0xff, tapefile);
    fputc(w & 0x0f, tapefile);
  }

  switch (tapeformat) {
  case tpf_TAP:
    fputc(0xa0, tapefile);
    fputc(0x0a, tapefile);
    fputc(0x00, tapefile);
    fputc(0x00, tapefile);
    break;
  }
}

/*
** wr_eof() writes an EOF record to the output tape, in whatever
** format we are using.
*/

static void wr_eof(void)
{
  byte emptyrecord[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

  switch (tapeformat) {
  case tpf_TAP:
    fwrite(emptyrecord, sizeof(byte), 8, tapefile);
    break;
  }
}

/*
** time2udt() converts a unix time_t to a 36-bit tops UDT.
*/

static int36 time2udt(time_t t)
{
  int days, seconds;

  t += sy_getgmtoffset();

  days = t / 86400;
  seconds = t - days * 86400;

  days += 0117213;
  seconds *= 2048;
  seconds /= 675;

  return (((int36) days << 18) + seconds);
}

/*
** udt2time() converts a 36-bit tops UDT to a unix time_t.
*/

static time_t udt2time(int36 udt)
{
  int days, seconds;

  days = (udt >> 18) & 0777777;
  seconds = udt & 0777777;

  days -= 0117213;
  seconds *= 675;
  seconds /= 2048;

  return (days * 86400 + seconds - sy_getgmtoffset());
}

/*
** getnow() returns a word with the current date/time in a DEC10
** universial date/time format.
*/

static int36 getnow(void)
{
  return (time2udt(time(NULL)));
}

/*
** zerotaperecord() clears out the tape record buffer.
*/

static void zerotaperecord(void)
{
  int i;

  for (i = 0; i < 544; i += 1) {
    taperecord[i] = (int36) 0;
  }
}

/*
** setup_general() inits a general record.
*/

static void setup_general(int blocktype)
{
  zerotaperecord();
  taperecord[G_TYPE] = blocktype;
  taperecord[G_SEQ] = nextseq();
  taperecord[G_RTNM] = 1;	/* Tape number, only one. */
  taperecord[G_FLAGS] = 0;	/* No flags. */
}

/*
** setup_BE() inits a T$BEGIN or T$END record.
*/

static void setup_BE(int blocktype)
{
  setup_general(blocktype);	/* Do the common fields. */

  taperecord[S_DATE] = getnow();/* (014) Date/time. */
  taperecord[S_FORMAT] = 1;	/* (015) Format */
  taperecord[S_BVER] = 0;	/* (016) Backup version. */
  taperecord[S_BVER] = xwd(0300, 0411);
  taperecord[S_MONTYP] = 0;	/* (017) Strange system... */
  taperecord[S_SVER] = 0;	/* (020) OS version. */
  taperecord[S_APR] = 4097;	/* (021) BAH will hate this. */
  taperecord[S_DEVICE] = sixbit("mta0"); /* (022) Device. */
  taperecord[S_MTCHAR] = 4;	/* (023) 1600 bpi. */

  (void) w_text(&taperecord[32], O_SYSNAME, 6, sy_getsystem());
  taperecord[G_LND] = 7;
}

static int36* w_foo(int36* ptr, int type, char* text)
{
  char buffer[100];
  int bytes, words;

  bytes = strlen(text);
  words = (bytes + 6) / 5;
  buffer[0] = type;
  buffer[1] = words;
  strcpy(&buffer[2], text);
  text = buffer;
  while (words-- > 0) {
    *ptr++ = ascii(text);
    text += 5;
  }
  return (ptr);
}

static void mkheadpath(int36 sum)
{
  int36* pos;

  pos = &taperecord[F_PTH];
  pos = w_foo(pos, 2, topsname);
  pos = w_foo(pos, 3, topsext);

  taperecord[F_PCHK] = sum;
}

/*
** parsetops() sets up topsname and topsext to a suitable pair of strings.
*/

static void parsetops(char* name)
{
  char* namepart;
  char* extpart;
  int pos;
  char c;

  namepart = strrchr(name, '/');
  if (namepart == NULL) {
    namepart = name;
  } else {
    namepart++;
  }
  extpart = strrchr(namepart, '.');
  if (extpart) {
    extpart++;
  }

  pos = 0;
  while ((c = *namepart++) != 0) {
    if ((c >= 'a') && (c <= 'z')) {
      c = c - 'a' + 'A';
    }
    if (c == '.') break;
    if (pos < 6) {
      topsname[pos] = c;
      pos += 1;
    }
  }
  topsname[pos] = 0;

  pos = 0;
  if (extpart != NULL) {
    while ((c = *extpart++) !=0) {
      if ((c >= 'a') && (c <= 'z')) {
	c = c - 'a' + 'A';
      }
      if (pos < 3) {
	topsext[pos] = c;
	pos += 1;
      }
    }
  }
  topsext[pos] = 0;
}

/*
** openread() opens a disk file for reading.
*/

static bool openread(char* name)
{
  struct stat statbuf;

  diskfile = fopen(name, "rb");
  if (diskfile == NULL) {
    fprintf(stderr, "Can't open %s for reading.\n", name);
    return (false);
  }

  if (fstat(fileno(diskfile), &statbuf) != 0) {
    fprintf(stderr, "... fstat failed...\n");
  }

  filelength = statbuf.st_size;
  filemodified = statbuf.st_mtime;

  if (asciiflag) {		/* If we are expanding newlines to */
    byte buffer[512];		/* <CR><LF>, we have to update the */
    int count;			/* file length. */
    int i;

    while ((count = fread(buffer, sizeof(byte), 512, diskfile)) > 0) {
      for (i = 0; i < count; i += 1) {
	if (buffer[i] == '\012') {
	  filelength += 1;
	}
      }
    }
    rewind(diskfile);
  }

  dataoffset = 0;
}

/*
** aread() does sort of fread(), expanding newlines to <CR><LF>.
*/

static int aread(int maxcount)
{
  static byte buffer[512];
  static int bufpos = 0;
  static int bufcount = 0;
  static bool lfflag = false;

  int count = 0;
  byte b;
  
  for (;;) {
    while ((bufpos < bufcount) || lfflag) {
      if (lfflag) {
	b = '\012';
	lfflag = false;
      } else {
	b = buffer[bufpos++];
	if (b == '\012') {
	  lfflag = true;
	  b = '\015';
	}
      }
      diskbuffer[count++] = b;
      if (count >= maxcount) {
	return (maxcount);
      }
    }
    bufcount = fread(buffer, sizeof(byte), 512, diskfile);
    bufpos = 0;
    if (bufcount == 0) {
      return (count);
    }    
  }
}

/*
** readdisk() fills the global buffer "diskbuffer" with data.
*/

static void readdisk(void)
{
  int i;

  if (asciiflag) {
    diskcount = aread(5*512);
  } else {
    diskcount = fread(diskbuffer, sizeof(byte), 5*512, diskfile);
  }

  for (i = diskcount; i < (5*512); i += 1) {
    diskbuffer[i] = 0;
  }    
}

/*
** closeread() closes the current disk file we are reading.
*/

static void closeread(void)
{
  (void) fclose(diskfile);
}

/*
** binarydata() guesses if the input file is ASCII or binary on the
** TOPS side of the world.
*/

static bool binarydata(void)
{
  int i;

  for (i = 4; i < diskcount; i += 5) {
    if (diskbuffer[i] & 0x80) {
      return (true);
    }
  }
  return (false);
}

/*
** blockcount() returns a faked-up allocated size of a file.
** we bluntly assume a tops-10 cluster size of 5.
*/

static int blockcount(void)
{
  int count;

  count = (filelength + 639) / 640;	/* Blocks for data. */
  count = (count + 6) / 5;		/* Clusters, including RIBs. */
  count = count * 5;			/* Blocks. */
  return (count);
}

/*
** copy2tape() copies a given amount of data from the disk buffer
** to the tape block.
*/

static void copy2tape(int36* ptr, int bytes)
{
  int i;
  int36 w;

  for (i = 0; i < bytes; i += 5) {
    if (ansiflag) {		/* ANSI format? */
      w = (int36) (diskbuffer[i] & 0x7f) << 29;
      w |= (int36) (diskbuffer[i+1] & 0x7f) << 22;
      w |= (int36) (diskbuffer[i+2] & 0x7f) << 15;
      w |= (int36) (diskbuffer[i+3] & 0x7f) << 8;
      w |= (int36) (diskbuffer[i+4] & 0x7f) << 1;
      if (diskbuffer[i+4] & 0x80) {
	w |= 1;
      }
    } else {			/* CORE-DUMP format. */
      w = (int36) (diskbuffer[i] & 0xff) << 28;
      w |= (int36) (diskbuffer[i+1] & 0xff) << 20;
      w |= (int36) (diskbuffer[i+2] & 0xff) << 12;
      w |= (int36) (diskbuffer[i+3] & 0xff) << 4;
      w |= (int36) (diskbuffer[i+4] & 0x0f);
    }
    *ptr++ = w;
    dataoffset += 1;
  }

  taperecord[G_SIZE] = (bytes + 4) /5;
}

/*
** wr_file() is the main routine that writes a single file to the current
** tape file.
*/

static void wr_file(char* name)
{
  int36* pos;
  int36 pthchecksum;

  if (name[0] == '-') {
    if (debug > 0) {
      fprintf(stderr, "handling file option %s\n", name);
    }
    switch (name[1]) {
    case 'a':
    case 'A':
      asciiflag = true;
      ansiflag = true;
      break;
    case 'b':
    case 'B':
      asciiflag = false;
      ansiflag = true;
      break;
    case 'c':
    case 'C':
      asciiflag = false;
      ansiflag = false;
      break;
    }
    return;
  }

  if (debug > 0) {
    fprintf(stderr, "writing file %s in ", name);
    if (ansiflag) {
      fprintf(stderr, "ansi");
    } else {
      fprintf(stderr, "core-dump");
    }
    fprintf(stderr, " mode");
    if (asciiflag) {
      fprintf(stderr, "with newline expansion");
    }
    fprintf(stderr, ".\n");
  }

  if (openread(name)) {
    parsetops(name);		/* Get a suitable tops-10 filename. */
    readdisk();			/* Slurp in first chunk of data. */

    setup_general(T_FILE);	/* Set up for a file block. */
    if (diskcount <= 1280) {	/* Fits in one record? */
      taperecord[G_FLAGS] = xwd(GF_SOF+GF_EOF, 0);
    } else {
      taperecord[G_FLAGS] = xwd(GF_SOF, 0);
    }

    /* build name block: */

    taperecord[32] = xwd(O_NAME, 0200);
    pos = &taperecord[33];
    pos = w_text(pos, 2, 0, topsname);
    pos = w_text(pos, 3, 0, topsext);

    /* compute checksum of file name block: */

    pthchecksum = checksumdata(&taperecord[32], 0200);

    /* build attribute block: */

    taperecord[32+0200] = xwd(O_FILE, 0200);

    pos = &taperecord[32+0201];

    pos[A_FHLN] = 032;
    pos[A_WRIT] = time2udt(filemodified);
    pos[A_ALLS] = blockcount() * 128;
    if (binarydata()) {
      pos[A_MODE] = 016;
      pos[A_LENG] = (filelength + 4) / 5;
      pos[A_BSIZ] = 36;
    } else{
      pos[A_MODE] = 0;
      pos[A_LENG] = filelength;
      pos[A_BSIZ] = 7;
    }

    taperecord[G_LND] = 0400;

    /* fill in header: */

    mkheadpath(pthchecksum);

    if (diskcount < 1280) {
      copy2tape(&taperecord[32+0400], diskcount);
      if (debug > 0) {
	fprintf(stderr,
		"    writing all data (%d bytes) in first tape block.\n",
		diskcount);
      }
      checksumbuffer();
      writetape();
    } else {
      bool more = true;

      checksumbuffer();
      writetape();
      if (debug > 0) {
	fprintf(stderr, "    writing first tape block with no data.\n");
      }

      while (more) {
	setup_general(T_FILE);	/* Set up for next file block. */
	mkheadpath(pthchecksum);
	taperecord[F_RDW] = dataoffset;
	copy2tape(&taperecord[32], diskcount);
	if (debug > 0) {
	  fprintf(stderr, "    writing %d bytes of data.\n", diskcount);
	}
	readdisk();
	if (diskcount == 0) {
	  taperecord[G_FLAGS] = xwd(GF_EOF, 0);
	  more = false;
	} else {
	  taperecord[G_FLAGS] = xwd(0, 0);
	}
	checksumbuffer();
	writetape();
      }
    }
    closeread();
  }
}

/*
** docreate()  performs the job of "backup -c ..."
*/

static void docreate(void)
{
  if (strcmp(tapename, "-") != 0) {
    tapefile = fopen(tapename, "wb");
    if (tapefile == NULL) {
      fprintf(stderr, "backup: can't open output tape file\n");
      exit(1);
    }
  } else {
    tapefile = stdout;
  }

  setup_BE(T_BEGIN);
  checksumbuffer();
  writetape();

  while (argcount-- > 0) {
    wr_file(*(argfiles++));
  }

  setup_BE(T_END);
  checksumbuffer();
  writetape();

  wr_eof();
}

/*
** main program and command decoder.
*/

int main(int argc, char* argv[])
{
  char* s;
  bool namenext = false;
  bool actgiven = false;
  char action;

  if (--argc > 0) {
    for (s = *(++argv); *s != (char) 0; s++) {
      switch (*s) {
      case '-':
        break;
      case 'A':			/* -A, turn on ASCII conversion flag. */
	asciiflag = true;
	ansiflag = true;
	break;
      case 'B':			/* -B, turn off ASCII conversion flag. */
	asciiflag = false;
	ansiflag = true;
	break;
      case 'C':			/* -C, turn on CORE-DUMP format. */
	asciiflag = false;
	ansiflag = false;
	break;
      case 'd':			/* -d, increase debug level. */
	debug++;
	break;
      case 'f':			/* -f, specify tape file. */
	namenext = true;
	break;
      case 'v':			/* -v, increment verbosity level. */
	verbose++;
	break;
      case 'R':			/* -R, select raw tape format. */
	tapeformat = tpf_RAW;
	break;
      case 'T':			/* -T, select .TAP format. */
	tapeformat = tpf_TAP;
	break;
      case 'c':			/* -c, create tape. */
	if (actgiven && action != *s) {
	  fprintf(stderr, "backup: incompatible actions given; %c/%c\n",
		  action, *s);
	  exit(1);
	}
	action = *s;
	actgiven = true;
	break;
      default:
	fprintf(stderr, "backup: bad option %c\n", *s);
	exit(1);
      }
    }
  }

  if (namenext) {
    if (--argc > 0) {
      tapename = *(++argv);
    } else {
      fprintf(stderr, "backup: input file name missing\n");
      exit(1);
    }
  } else {
    tapename = "-";		/* stdin/stdout. */
  }

  /* keep track of the rest of the arguments. */

  argfiles = ++argv;		/* Keep rest of arguments. */
  argcount = --argc;		/* ... and count 'em. */

  /* take whatever action is requested. */

  if (!actgiven) {
    fprintf(stderr, "backup: no action given\n");
    exit(1);
  }

  switch (action) {
  case 'c':
    docreate();
    break;
  default:
    fprintf(stderr, "backup: internal error in program\n");
    exit(1);
  }
  exit(0);
}
