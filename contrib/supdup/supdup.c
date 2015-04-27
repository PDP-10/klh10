/*
 * User supdup program.
 *
 *	Written Jan. 1985 by David Bridgham.  Much of the code dealing
 * with the network was taken from the telnet user program released
 * with 4.2 BSD UNIX.
 */

/* Define exactly one of TERMCAP or TERMINFO.
 * Link with the appropriate TERMINFO or TERMCAP library.
 */

/* Hacked by Klotz 2/20/89 to remove +%TDORS from init string.
 * Hacked by Klotz 12/19/88 added response to TDORS.
 * Hacked by Mly 9-Jul-87 to improve reading of supdup escape commands
 * Hacked by Mly July 1987 to do bottom-of-screen cursor-positioning hacks
 *  when escape char is typed 
 * Hacked by Mly 29-Aug-87 to nuke stupid auto_right_margin lossage.

 * TODO: Meta, Super, Hyper prefix keys.
 *  Deal with lossage when running on very narrow screen
 *   (Things like "SUPDUP command ->" should truncate)
 *  Defer printing message at bottom of screen if input chars are pending
 *  Try multiple other host addresses if first fails.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <netinet/in.h>

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <netdb.h>

#include "supdup.h"

#ifdef TERMINFO
#include <term.h>
#endif /* TERMINFO */

#ifdef	TERMCAP
#include <sys/ioctl_compat.h>	/* Kludge assumption: BSDish system */

extern char *tgetstr();

#include "termcaps.h"		/* Get table of term caps we want */

static char tspace[2048], *aoftspace;

#define OUTSTRING_BUFSIZ 2048
unsigned char *outstring;

unsigned char *tparam(), *tgoto();
#endif /* TERMCAP */

#define TBUFSIZ 1024
unsigned char ttyobuf[TBUFSIZ], *ttyfrontp = ttyobuf;
unsigned char netobuf[TBUFSIZ], *netfrontp = netobuf;

unsigned char hisopts[256];
unsigned char myopts[256];


int connected = 0;

/* fd of network connection */
int net;

int showoptions = 0;
int options;

int debug = 0;
FILE *tdebug_file = 0;	/* For debugging terminal output */
FILE *indebug_file = 0;	/* For debugging network input */
#define TDEBUG_FILENAME "supdup-trmout"
#define INDEBUG_FILENAME "supdup-netin"

/* 0377 if terminal has a meta-key */
int mask = 0177;
int high_bits = 0;
/* user supdup command-escaper character */
unsigned char escape_char = ('^'&037);
/* As opposed to winningly-wrap */
int do_losingly_scroll = 0;

#if 0 /* brain death */
int crmod = 0;
#endif /* 0 */

/* jmp_buf toplevel; */
jmp_buf	peerdied;

extern int errno;

/* Impoverished un*x keyboards */
#define Ctl(c) ((c)&037)

int sd (), quit (), rlogout (), suspend (), help ();
int setescape (), status ();
int top ();
#if 0
int setcrmod (), setdebug (), setoptions ();
#endif /* 0 */

struct cmd
 {
   unsigned char name;          /* command name */
   char *help;                  /* help string */
   int (*handler)();            /* routine which executes command */
 };

struct cmd cmdtab[] =
  {
    /* also q */
    { 'l',	"logout connection and quit", rlogout },
    { 'c',	"close connection and exit", quit },
    /* also c-z */
    { 'p',	"suspend supdup", suspend },
    { 'e',	"set escape character",	setescape },
    { 't',	"set \"top\" bit on next character", top },
    { 's',	"print status information", status },
    /* also c-h */
    { '?',	"print help information", help },
#if 0
    { 'r',	"toggle mapping of received carriage returns", setcrmod },
    { 'd',	"toggle debugging", setdebug },
    { 'v',	"toggle viewing of options processing", setoptions },
#endif /* 0 */
    0
  };

int currcol, currline;	/* Current cursor position */

struct sockaddr_in tsin;

void	intr(int), deadpeer(int);
char	*key_name ();
struct	cmd *getcmd ();
struct	servent *sp;

struct	tchars otc;
struct	ltchars oltc;
struct	sgttyb ottyb;


putch (c)
     register int c;
{
  *ttyfrontp++ = c;
  /*>>>>> LOSES if overflows */
}


put_newline ()
{
#ifdef	TERMINFO
  if (newline)
    tputs (newline, 1, putch);
#else
#ifdef TERMCAP
  if (fresh_line)
    tputs (fresh_line, 1, putch);
#endif /* TERMCAP */
#endif /* TERMINFO */
  else
    {
      if (carriage_return)
        tputs (carriage_return, 0, putch);
      else
        putch ('\r');
      if (cursor_down)
        tputs (cursor_down, 0, putch);
      else
        putch ('\n');
    }
  ttyoflush ();
}

#ifdef TERMINFO
#define term_goto(c, l) \
  tputs (tparm (cursor_address, l, c), lines, putch)
#endif /* TERMINFO */
#ifdef TERMCAP
#define term_goto(c, l) \
  tputs (tgoto (cursor_address, c, l), lines, putch)
#endif /* TERMCAP */

char *hostname;

void
get_host (name)
     char *name;
{
  register struct hostent *host;
  host = gethostbyname (name);
  if (host)
    {
      tsin.sin_family = host->h_addrtype;
#ifdef notdef
      bcopy (host->h_addr_list[0], (caddr_t) &tsin.sin_addr, host->h_length);
#else
      bcopy (host->h_addr, (caddr_t) &tsin.sin_addr, host->h_length);
#endif /* h_addr */
      hostname = host->h_name;
    }
  else
    {
      tsin.sin_family = AF_INET;
      tsin.sin_addr.s_addr = inet_addr (name);
      if (tsin.sin_addr.s_addr == -1)
        hostname = 0;
      else
        hostname = name;
    }
}

main (argc, argv)
     int argc;
     char *argv[];
{
  sp = getservbyname ("supdup", "tcp");
  if (sp == 0)
    {
      fprintf (stderr, "supdup: tcp/supdup: unknown service.\n");
      exit (1);
    }
  ioctl (0, TIOCGETP, (char *) &ottyb);
  ioctl (0, TIOCGETC, (char *) &otc);
  ioctl (0, TIOCGLTC, (char *) &oltc);
  setbuf (stdin, 0);
  setbuf (stdout, 0);
  do_losingly_scroll = 0;
  if (argc > 1 && (!strcmp (argv[1], "-s") ||
                   !strcmp (argv[1], "-scroll")))
    {
      argc--; argv++;
      do_losingly_scroll = 1;
    }

  if (argc > 1 && (!strcmp (argv[1], "-d") ||
                   !strcmp (argv[1], "-debug")))
    {
      argv++; argc--;
      debug = 1;
    }
  if (argc > 1 && (!strcmp (argv[1], "-tdebug")))
    {
      argv++; argc--;
      tdebug_file = fopen(TDEBUG_FILENAME, "wb"); /* Open for binary write */
      if (tdebug_file == NULL)
        {
          fprintf (stderr, "Couldn't open debug file %s\n",
		   TDEBUG_FILENAME);
          exit (1);
        }
      setbuf(tdebug_file, NULL);	/* Unbuffered so see if we crash */
    }

  if (argc > 1 && !strcmp (argv[1], "-t"))
    {
      argv++, argc--;
      indebug_file = fopen(INDEBUG_FILENAME, "wb"); /* Open for binary write */
      if (indebug_file == NULL)
        {
          fprintf (stderr, "Couldn't open debug file %s\n",
		   INDEBUG_FILENAME);
          exit (1);
        }
    }

  if (argc == 1)
    {
      char *cp;
      char line[200];

    again:
      printf ("Host: ");
      if (fgets(line, sizeof(line), stdin) == 0)
        {
          if (feof (stdin))
            {
              clearerr (stdin);
              putchar ('\n');
            }
          goto again;
        }
      if (cp = strchr(line, '\n'))
	*cp = '\0';
      get_host (line);
      if (!hostname)
        {
          printf ("%s: unknown host.\n", line);
          goto again;
        }
    }
  else if (argc > 3)
    {
      printf ("usage: %s host-name [port] [-scroll]\n", argv[0]);
      return;
    }
  else
    {
      get_host (argv[1]);
      if (!hostname)
        {
          printf ("%s: unknown host.\n", argv[1]);
          exit (1);
        }
    }

  tsin.sin_port = sp->s_port;
  if (argc == 3)
    {
      tsin.sin_port = atoi (argv[2]);
      if (tsin.sin_port <= 0)
        {
          printf ("%s: bad port number.\n", argv[2]);
          return;
        }
      tsin.sin_port = htons (tsin.sin_port);
    }

  net = socket (AF_INET, SOCK_STREAM, 0);
  if (net < 0)
    {
      perror ("supdup: socket");
      return;
    }
  outstring = (unsigned char *) malloc (OUTSTRING_BUFSIZ);
  if (outstring == 0)
    {
      fprintf (stderr, "Memory exhausted.\n");
      exit (1);
    }
  sup_term ();
  if (debug && setsockopt (net, SOL_SOCKET, SO_DEBUG, 0, 0) < 0)
    perror ("setsockopt (SO_DEBUG)");
  signal (SIGINT, intr);
  signal (SIGPIPE, deadpeer);
  printf("Trying %s ...", inet_ntoa (tsin.sin_addr));
  fflush (stdout);
  if (connect (net, (struct sockaddr *) &tsin, sizeof (tsin)) < 0)
/* >> Should try other addresses here (like BSD telnet) #ifdef h_addr */
    {
      perror ("supdup: connect");
      signal (SIGINT, SIG_DFL);
      return;
    }
  connected = 1;
  printf ("Connected to %s.\n", hostname);
  printf ("Escape character is \"%s\".", key_name (escape_char));
  fflush (stdout);
  (void) mode (1);
  if (clr_eos)
    tputs (clr_eos, lines - currline, putch);
  put_newline ();
  if (setjmp (peerdied) == 0)
    supdup (net);
  ttyoflush ();
  (void) mode (0);
  fprintf (stderr, "Connection closed by %s.\n", hostname);
  exit (0);
}


#define	INIT_LEN	42	/* Number of bytes to send at initialization */
static char inits[] =
  {
    /* -wordcount,,0.  should always be -6 */
    077,	077,	-6,	0,	0,	0,
    /* TCTYP variable.  Always 7 (supdup) */
    0,	0,	0,	0,	0,	7,
    /* TTYOPT variable.  %TOMVB %TOMOR %TOLOW  %TPCBS  */
    1,	2,	020,	0,	0,	040,
    /* Height of screen -- updated later */
    0,	0,	0,	0,	0,	24,
    /* Width of screen minus one -- updated later */
    0,	0,	0,	0,	0,	79,
    /* auto scroll number of lines */
    0,	0,	0,	0,	0,	1,
    /* TTYSMT */
    0,	0,	0,	0,	0,	0
  };

/*
 * Initialize the terminal description to be sent when the connection is
 * opened.
 */
sup_term ()
{
#ifdef TERMINFO
  int errret;

  setupterm (0, 1, &errret);
  if (errret == -1)
    {
      fprintf (stderr, "Bad terminfo database.\n");
      exit (1);
    }
  else if (errret == 0)
    {
      fprintf (stderr, "Unknown terminal type.\n");
      exit (1);
    }
#endif /* TERMINFO */
#ifdef TERMCAP
  static void zap();
  static char bp[2000];

  switch (systgetent (bp))
    {
    case 1:
	if (tdebug_file)
	    fprintf(tdebug_file, "TERMCAP data: %s\n", bp);
      zap ();
      break;

    case 0:
      fprintf (stderr, "Invalid terminal.\n");
      exit (1);

    case -1:
      fprintf (stderr, "Can't open termcap file.\n");
      exit (1);
    }
#endif /* TERMCAP */

  if (columns <= 1) {
      int badcols = columns;
      fprintf (stderr, "supdup: bogus # columns (%d), using %d\r\n",
	       badcols, columns = 80);
  }

/*
 *if (!cursor_address)
 *  {
 *    fprintf (stderr, "Can't position cursor on this terminal.\n");
 *    exit (1);
 *  }
 */
 
  if (do_losingly_scroll)
    if (no_scroll && !SF)
      {
        fprintf (stderr, "(Terminal won't scroll.  Hah!!)\n");
        do_losingly_scroll = 0;
      }
    else
      inits[13] |= 01;

  inits[23] = lines & 077;
  inits[22] = (lines >> 6) & 077;
  {
    register int w;

    if (auto_right_margin)
      /* Brain death!  Can't write in last column of last line
       * for fear that stupid terminal will scroll up.  Glag. */
      columns = columns - 1;

    /* Silly SUPDUP spec says that should specify (1- columns) */
    w = columns - 1;
    inits[29] = w & 077;
    inits[28] = (w >> 6) & 077;
  }
  if (clr_eol)		inits[12] |= 04;
  if (over_strike)	inits[13] |= 010;
  if (cursor_address)	inits[13] |= 04;
  if (has_meta_key || also_has_meta_key)
    {
      /* %TOFCI */
/* Don't do this -- it implies that we can generate full MIT 12-bit */
/*      inits[14] |= 010; */
      mask = 0377;
    }
  if ((delete_line || parm_delete_line) &&
      (insert_line || parm_insert_line))
    inits[14] |= 02;
  if ((delete_character || parm_dch) &&
      (insert_character || parm_ich))
    inits[14] |= 01;
}

#ifdef	TERMCAP
static void zap ()
{
  unsigned char *fp, **sp;
  char *namp;
  int *np;
    int i;
    struct tcent *tc = tcaptab;

    aoftspace = tspace;

  for (i = 0; i < (sizeof(tcaptab)/sizeof(tcaptab[0])); ++i, ++tc) {
      switch (tc->tctyp) {
      case TCTYP_STR:
	  tc->tcval.str = tgetstr(tc->tcname, &aoftspace);
	  if (tdebug_file)
	      fprintf(tdebug_file, "str %s: %s\n",
		      tc->tcname, (tc->tcval.str ? tc->tcval.str : "-null-"));
	  break;
      case TCTYP_NUM:
	  tc->tcval.num = tgetnum(tc->tcname);
	  if (tdebug_file)
	      fprintf(tdebug_file, "num %s: %d\n",
		      tc->tcname, tc->tcval.num);
	  break;
      case TCTYP_FLG:
	  tc->tcval.flg = tgetflag(tc->tcname);
	  if (tdebug_file)
	      fprintf(tdebug_file, "flg %s: %s\n",
		      tc->tcname, (tc->tcval.flg ? "true" : "false"));
	  break;
      default:
	  fprintf(stderr,"supdup: unknown termcap \"%s\"\n",
		  tcaptab[i].tcname);
	  continue;
      }
  }


/*
  if (!cursor_left)
    cursor_left = "\b";
  if (!carriage_return)
    carriage_return = "\r";
  if (!cursor_down)
    cursor_down = "\n";
*/

}

extern char *getenv ();

systgetent (bp)
     char *bp;
{
  register char *term;

  if (term = getenv ("TERM"))
    return tgetent (bp, term);
  else
    return 0;
}
#endif /* TERMCAP */

struct	tchars notc =	{ -1, -1, -1, -1, -1, -1 };
struct	ltchars noltc =	{ -1, -1, -1, -1, -1, -1 };

mode (f)
     register int f;
{
  static int prevmode = 0;
  struct tchars *tc;
  struct ltchars *ltc;
  struct sgttyb sb;
  int onoff, old;

  if (prevmode == f)
    return (f);
  old = prevmode;
  prevmode = f;
  sb = ottyb;
  switch (f)
    {
    case 0:
      onoff = 0;
      tc = &otc;
      ltc = &oltc;
      break;

    default:
      sb.sg_flags |= RAW;       /* was CBREAK */
      sb.sg_flags &= ~(ECHO|CRMOD);
      sb.sg_flags |= LITOUT;
#ifdef PASS8
      sb.sg_flags |= PASS8;
#endif
      sb.sg_erase = sb.sg_kill = -1;
      tc = &notc;
      ltc = &noltc;
      onoff = 1;
      break;
    }
  ioctl (fileno (stdin), TIOCSLTC, (char *) ltc);
  ioctl (fileno (stdin), TIOCSETC, (char *) tc);
  ioctl (fileno (stdin), TIOCSETP, (char *) &sb);
  ioctl (fileno (stdin), FIONBIO, &onoff);
  ioctl (fileno (stdout), FIONBIO, &onoff);
  return (old);
}

unsigned char sibuf[TBUFSIZ], *sbp;
unsigned char tibuf[TBUFSIZ], *tbp;
int scc;
int tcc;

int escape_seen;
int saved_col, saved_row;

void
restore ()
{
  if (cursor_address)
    {
      if ((escape_seen & 1) != 0)
        {
          term_goto (0, currline);
          if (clr_eos)
            tputs (clr_eos, lines - currline, putch);
        }
      term_goto (currcol = saved_col, currline = saved_row);
    }
  escape_seen = 0;
  ttyoflush ();
}

void
clear_bottom_line ()
{
  if (LL || cursor_address)
    {
      currcol = 0; currline = lines - 1;
      if (LL)
        tputs (LL, 1, putch);
      else
        term_goto (currcol, currline);
      if (clr_eol)
        tputs (clr_eol, columns, putch);
    }
  ttyoflush ();
}

int
read_char ()
{
  int readfds;

  while (1)
    {
      tcc = read (fileno (stdin), tibuf, 1);
      if (tcc >= 0 || errno != EWOULDBLOCK)
        {
          register int c = (tcc <= 0) ? -1 : tibuf[0];
          tcc = 0; tbp = tibuf;
          return (c);
        }
      else
	{
	  readfds = 1 << fileno (stdin);
	  select(32, &readfds, 0, 0, 0, 0);
        }
    }
}


/*
 * Select from tty and network...
 */
supdup ()
{
  register int c;
  int tin = fileno (stdin), tout = fileno (stdout);
  int on = 1;

  ioctl (net, FIONBIO, &on);

  for (c = 0; c < INIT_LEN;)
    *netfrontp++ = inits[c++];

#ifdef TERMCAP
  if (VS) tputs (VS, 0, putch);
#endif /* TERMCAP */
  scc = 0;
  tcc = 0;
  escape_seen = 0;
  for (;;)
    {
      int ibits = 0, obits = 0;

      if (netfrontp != netobuf)
        obits |= (1 << net);
      else
        ibits |= (1 << tin);
      if (ttyfrontp != ttyobuf)
        obits |= (1 << tout);
      else
        ibits |= (1 << net);
      if (scc < 0 && tcc < 0)
        break;
      select (16, &ibits, &obits, 0, 0);
      if (ibits == 0 && obits == 0)
        {
          sleep (5);
          continue;
        }

      /*
       * Something to read from the network...
       */
      if ((escape_seen == 0) && (ibits & (1 << net)))
        {
          scc = read (net, sibuf, sizeof (sibuf));
          if (scc < 0 && errno == EWOULDBLOCK)
            scc = 0;
          else
            {
              if (scc <= 0)
                break;
              sbp = sibuf;
              if (indebug_file)
                fwrite(sibuf, scc, 1, indebug_file);
            }
        }

      /*
       * Something to read from the tty...
       */
      if (ibits & (1 << tin))
        {
          tcc = read (tin, tibuf, sizeof (tibuf));
          if (tcc < 0 && errno == EWOULDBLOCK)
            tcc = 0;
          else
            {
              if (tcc <= 0)
                break;
              tbp = tibuf;
            }
        }

      while (tcc > 0)
        {
          register int c;

          if ((&netobuf[sizeof(netobuf)] - netfrontp) < 2)
            break;
          c = *tbp++ & mask; tcc--;
          if (escape_seen > 2)
            {
              /* ``restore'' the screen (or at least the cursorpos) */
              restore ();
            }
          else if (escape_seen > 0)
            {
              escape_seen = escape_seen + 2;
              command (c);
              continue;
            }

          if (c == escape_char)
            {
              escape_seen = (tcc == 0) ? 1 : 2;
              saved_col = currcol;
              saved_row = currline;
              if (tcc == 0)
                {
                  clear_bottom_line ();
                  fprintf (stdout, "SUPDUP %s command -> ", hostname);
                  ttyoflush ();
                }
              continue;
            }

          if (c & 0200)
            {
              high_bits = 2;
              c &= 0177;
            }
          if ((c & 0140) == 0)
            {
              switch (c)
                {
                case 010:
                case 011:
                case 012:
                case 013:
                case 014:
                case 015:
                case 032:
                case 033:
                case 037:
                  break;
                default:
                  high_bits |= 1;
                  c = c + '@';
                  break;
                }
            }
          if (high_bits)
            {
              *netfrontp++ = ITP_ESCAPE;
              *netfrontp++ = high_bits + 0100;
              high_bits = 0;
            }
          *netfrontp++ = c;
        }
      if ((obits & (1 << net)) && (netfrontp != netobuf))
        netflush (0);
      if (scc > 0)
        suprcv ();
      if ((obits & (1 << tout)) && (ttyfrontp != ttyobuf))
        ttyoflush ();
    }
}


command (chr)
     unsigned char chr;
{
  register struct cmd *c;

  /* flush typeahead */
  tcc = 0;
  if (chr == escape_char)
    {
      *netfrontp++ = chr;
      restore ();
      return;
    }

  for (c = cmdtab; c->name; c++)
    if (c->name == chr)
      break;

  if (!c->name && (chr >= 'A' && chr <= 'Z'))
    for (c = cmdtab; c->name; c++)
      if (c->name == (chr - 'A' + 'a'))
        break;

  if (c->name)
    (*c->handler) ();
  else if (chr == '\177' || chr == Ctl ('g'))
    restore ();
  else if (chr == Ctl ('z'))
    suspend ();
  else if (chr == Ctl ('h'))
    help ();
  else if (chr == 'q')
    rlogout ();
  else
    {
      clear_bottom_line ();
      printf ("?Invalid SUPDUP command \"%s\"",
              key_name (chr));
      ttyoflush ();
      return;
    }
  ttyoflush ();
  if (!connected)
    exit (1);
  return;
}

status ()
{
  if (cursor_address)
    {
      currcol = 0; currline = lines - 3;
      term_goto (currcol, currline);
      if (clr_eos)
        tputs (clr_eos, lines - currline, putch);
    }
  ttyoflush ();
  if (connected)
    printf ("Connected to %s.", hostname);
  else
    printf ("No connection.");
  ttyoflush ();
  put_newline ();
  printf ("Escape character is \"%s\".", key_name (escape_char));
  ttyoflush ();
}

suspend ()
{
  register int save;

  if (cursor_home)
    tputs (cursor_home, 1, putch);
  else if (cursor_address)
    term_goto (0, 0);
  if (clr_eol)
    tputs (clr_eol, columns, putch);
#ifdef TERMCAP
  if (VE) tputs (VE, 0, putch);
#endif /* TERMCAP */
  ttyoflush ();
  save = mode (0);
  if (!cursor_address)
    putchar ('\n');
  kill (0, SIGTSTP);
  /* reget parameters in case they were changed */
  ioctl (0, TIOCGETP, (char *) &ottyb);
  ioctl (0, TIOCGETC, (char *) &otc);
  ioctl (0, TIOCGLTC, (char *) &oltc);
  (void) mode (save);
#ifdef TERMCAP
  if (VS) tputs (VS, 0, putch);
#endif /* TERMCAP */
  *netfrontp++ = ITP_ESCAPE;      /* Tell other end that it sould refresh */
  *netfrontp++ = ITP_PIATY;       /* the screen */
  restore ();
}

/*
 * Help command.
 */
help ()
{
  register struct cmd *c;
  
  if (cursor_address)
    {
      for (c = cmdtab, currline = lines - 1 ;
           c->name;
           c++, currline--)
        ;
      currcol = 0;
      currline--;                   /* For pass-through `command' doc */
      term_goto (currcol, currline);
      if (clr_eos)
        tputs (clr_eos, lines - currline, putch);
    }
      
  ttyoflush ();
  printf ("Type \"%s\" followed by the command character.  Commands are:",
          key_name (escape_char));
  ttyoflush ();
  put_newline ();
  printf (" %-8s%s",
          key_name (escape_char),
          "sends escape character through");
  ttyoflush ();
  for (c = cmdtab; c->name; c++)
    {
      put_newline ();
      printf (" %-8s%s",
              key_name (c->name),
              c->help);
    }
  ttyoflush ();

  {
    register int c;
    c = read_char ();
    restore ();
    if (c < 0)
      return;
    if (c == ' ')
      return;
    /* unread-char */
    tibuf[0] = c; tcc = 1; tbp = tibuf;
  }
  return;
}

punt (logout_p)
     int logout_p;
{
  register int c;

  clear_bottom_line ();
  /* flush typeahead */
  tcc = 0;
  fprintf (stdout, "Quit (and %s from %s)? ",
           logout_p ? "logout" : "disconnect",
           hostname);
  ttyoflush ();
  while (1)
    {
      c = read_char ();
      if (c == 'y' || c == 'Y')
        break;
      else if (c == 'n' || c == 'N' || c == '\177' || c == Ctl ('g'))
        {
          restore ();
          return;
        }
    }
  if (logout_p)
    {
      netflush (1);
      *netfrontp++ = SUPDUP_ESCAPE;
      *netfrontp++ = SUPDUP_LOGOUT;
      netflush (1);
    }
  if (cursor_home)
    tputs (cursor_home, 1, putch);
  else if (cursor_address)
    term_goto (0, 0);
#ifdef TERMCAP
  if (VE) tputs (VE, 0, putch);
#endif /* TERMCAP */
  ttyoflush ();
  (void) mode (0);
  if (!cursor_address)
    putchar ('\n');
  if (connected)
    {
      shutdown (net, 2);
      printf ("Connection closed.\n");
      ttyoflush ();
      close (net);
    }
  exit (0);
}

quit ()
{
  punt (0);
}

rlogout ()
{
  punt (1);
}


/*
 * Supdup receiver states for fsm
 */
#define	SR_DATA		0
#define	SR_M0		1
#define	SR_M1		2
#define	SR_M2		3
#define	SR_M3		4
#define	SR_QUOTE	5
#define	SR_IL		6
#define	SR_DL		7
#define	SR_IC		8
#define	SR_DC		9

suprcv ()
{
  register int c;
  static int state = SR_DATA;
  static int y;

  while (scc > 0)
    {
      c = *sbp++ & 0377; scc--;
      switch (state)
        {
        case SR_DATA:
          if ((c & 0200) == 0)
            {
              if (currcol < columns)
                {
                  currcol++;
                  *ttyfrontp++ = c;
                }
              else
                {
                  /* Supdup (ITP) terminals should `stick' at the end
                     of `long' lines (ie not do TERMCAP `am') */
                }
              continue;
            }
          else switch (c)
            {
            case TDMOV:
              state = SR_M0;
              continue;
            case TDMV1:
            case TDMV0:
              state = SR_M2;
              continue;
            case TDEOF:
              if (clr_eos)
                tputs (clr_eos, lines - currline, putch);
              continue;
            case TDEOL:
              if (clr_eol)
                tputs (clr_eol, columns - currcol, putch);
              continue;
            case TDDLF:
              putch (' ');
              goto foo;
            case TDBS:
              currcol--;
            foo:
              if (currcol < 0)
                currcol = 0;
              else if (cursor_left)
                tputs (cursor_left, 0, putch);
              else if (BS)
                putch ('\b');
              else if (cursor_address)
                term_goto (currcol, currline);
              continue;
            case TDCR:
              currcol = 0;
              if (carriage_return)
                tputs (carriage_return, 0, putch);
              else if (cursor_address)
                term_goto (currcol, currline);
              else
                putch ('\r');
              continue;
            case TDLF:
              currline++;
              if (currline >= lines)
                currline--;
              else if (cursor_down)
                tputs (cursor_down, 0, putch);
              else if (cursor_address)
                term_goto (currcol, currline);
              else
                putch ('\n');
              continue;
            case TDCRL:
              put_newline ();              currcol = 0;
              currline++;
              if (clr_eol)
                tputs (clr_eol, columns - currcol, putch);
              continue;
            case TDNOP:
              continue;
            case TDORS:         /* ignore interrupts and */
	      netflush (0);     /* send cursorpos back every time */
	      *netfrontp++ = ITP_ESCAPE;
	      *netfrontp++ = ITP_CURSORPOS;
	      *netfrontp++ = ((unsigned char) currline);
	      *netfrontp++ = ((unsigned char) currcol);
	      netflush (0);
              continue;
            case TDQOT:
              state = SR_QUOTE;
              continue;
            case TDFS:
              if (currcol < columns)
                {
                  currcol++;
                  if (cursor_right)
                    tputs (cursor_right, 1, putch);
                  else if (cursor_address)
                    term_goto (currcol, currline);
                  else
                    currcol--;
                }
              continue;
            case TDCLR:
              currcol = 0;
              currline = 0;
              if (clear_screen)
                tputs (clear_screen, lines, putch);
              else
                {
                  if (cursor_home)
                    tputs (cursor_home, 1, putch);
                  else if (cursor_address)
                    term_goto (0, 0);
                  if (clr_eos)
                    tputs (clr_eos, lines, putch);
                }
              continue;
            case TDBEL:
              if (flash_screen)
                tputs (flash_screen, 0, putch);
              else if (bell)
                tputs (bell, 0, putch);
              else
                /* >>>> ?? */
                putch ('\007');
              continue;
            case TDILP:
              state = SR_IL;
              continue;
            case TDDLP:
              state = SR_DL;
              continue;
            case TDICP:
              state = SR_IC;
              continue;
            case TDDCP:
              state = SR_DC;
              continue;
            case TDBOW:
              if (enter_standout_mode)
                tputs (enter_standout_mode, 0, putch);
              continue;
            case TDRST:
              if (exit_standout_mode)
                tputs (exit_standout_mode, 0, putch);
              continue;
            default:
              ttyoflush ();
              fprintf (stderr, ">>>bad supdup opcode %o ignored<<<", c);
              ttyoflush ();
              if (cursor_address)
                term_goto (currcol, currline);
            }
        case SR_M0:
          state = SR_M1;
          continue;
        case SR_M1:
          state = SR_M2;
          continue;
        case SR_M2:
          y = c;
          state = SR_M3;
          continue;
        case SR_M3:
          if (c < columns && y < lines)
            {
              currcol = c; currline = y;
              term_goto (currcol, currline);
            }
          state = SR_DATA;
          continue;
        case SR_QUOTE:
          putch (c);
          state = SR_DATA;
          continue;
        case SR_IL:
          if (parm_insert_line)
            {
#ifdef TERMINFO
              tputs (tparm (parm_insert_line, c), c, putch);
#endif /* TERMINFO */
#ifdef TERMCAP
              outstring = tparam (parm_insert_line,
                                  outstring, OUTSTRING_BUFSIZ,
                                  c);
              tputs (outstring, c, putch);
#endif /* TERMCAP */
            }
          else
            if (insert_line)
              while (c--)
                tputs (insert_line, 1, putch);
          state = SR_DATA;
          continue;
        case SR_DL:
          if (parm_delete_line)
            {
#ifdef TERMINFO
              tputs (tparm (parm_delete_line, c), c, putch);
#endif /* TERMINFO */
#ifdef TERMCAP
              outstring = tparam (parm_delete_line,
                                  outstring, OUTSTRING_BUFSIZ,
                                  c);
              tputs (outstring, c, putch);
#endif /* TERMCAP */
            }
          else
            if (delete_line)
              while (c--)
                tputs (delete_line, 1, putch);
          state = SR_DATA;
          continue;
        case SR_IC:
          if (parm_ich)
            {
#ifdef TERMINFO
              tputs (tparm (parm_ich, c), c, putch);
#endif /* TERMINFO */
#ifdef TERMCAP
              outstring = tparam (parm_ich,
                                  outstring, OUTSTRING_BUFSIZ,
                                  c);
              tputs (outstring, c, putch);
#endif /* TERMCAP */
            }
          else
            if (insert_character)
              while (c--)
                tputs (insert_character, 1, putch);
          state = SR_DATA;
          continue;
        case SR_DC:
          if (parm_dch)
            {
#ifdef TERMINFO
              tputs (tparm (parm_dch, c), c, putch);
#endif /* TERMINFO */
#ifdef TERMCAP
              outstring = tparam (parm_dch,
                                  outstring, OUTSTRING_BUFSIZ,
                                  c);
              tputs (outstring, c, putch);
#endif /* TERMCAP */
            }
          else
            if (delete_character)
              while (c--)
                tputs (delete_character, 1, putch);
          state = SR_DATA;
          continue;
        }
    }
}

ttyoflush ()
{
  int n;
  unsigned char *back = ttyobuf;

  fflush (stdout);
  while ((n = ttyfrontp - back) > 0)
    {
	if (tdebug_file) {
	    fprintf(tdebug_file, "Outraw %d:[", n);
	    fwrite(back, n, 1, tdebug_file);
	    fprintf(tdebug_file, "]\n");
	}

      n = write (fileno (stdout), back, n);
/*      fflush (stdout); */
      if (n >= 0)
        back += n;
      else
        if (errno  == EWOULDBLOCK)
          continue;
      else
        /* Here I am being a typical un*x programmer and just
           ignoring other error codes.
           I really hate this environment!
         */
        return;
    }
  ttyfrontp = ttyobuf;
}

netflush (dont_die)
     int dont_die;
{
  int n;
  unsigned char *back = netobuf;

  while ((n = netfrontp - back) > 0)
    {
      n = write (net, back, n);
      if (n < 0)
        {
          if (errno == ENOBUFS || errno == EWOULDBLOCK)
            return;
          if (dont_die)
            return;
          (void) mode (0);
          perror (hostname);
          close (net);
          longjmp (peerdied, -1);
          /*NOTREACHED*/
        }
      back += n;
    }
  netfrontp = netobuf;
}


char key_name_buffer[20];
char *
key_name (c)
     register int c;
{
  register char *p = key_name_buffer;
  if (c >= 0200)
    {
      *p++ = 'M';
      *p++ = '-';
      c -= 0200;
    }
  if (c < 040)
    {
      if (c == 033)
	{
	  *p++ = 'E';
	  *p++ = 'S';
	  *p++ = 'C';
	}
      else if (c == Ctl ('I'))
	{
	  *p++ = 'T';
	  *p++ = 'A';
	  *p++ = 'B';
	}
      else if (c == Ctl ('J'))
	{
	  *p++ = 'L';
	  *p++ = 'F';
	  *p++ = 'D';
	}
      else if (c == Ctl ('M'))
	{
	  *p++ = 'R';
	  *p++ = 'E';
	  *p++ = 'T';
	}
      else
	{
	  *p++ = 'C';
	  *p++ = '-';
	  if (c > 0 && c <= Ctl ('Z'))
	    *p++ = c + 0140;
	  else
	    *p++ = c + 0100;
	}
    }
  else if (c == 0177)
    {
      *p++ = 'D';
      *p++ = 'E';
      *p++ = 'L';
    }
  else if (c == ' ')
    {
      *p++ = 'S';
      *p++ = 'P';
      *p++ = 'C';
    }
  else
    *p++ = c;
  *p++ = 0;
  return (key_name_buffer);
}

void deadpeer(int sig)
{
  (void) mode (0);
  longjmp (peerdied, -1);
}

void intr (int sig)
{
  (void) mode (0);
  exit (1);
}

top ()
{
  high_bits |= 020;
  restore ();
}

/*
 * Set the escape character.
 */
setescape ()
{
  clear_bottom_line ();
  printf ("Type new escape character: ");
  ttyoflush ();
  escape_char = read_char ();
  clear_bottom_line ();
  printf ("Escape character is \"%s\".", key_name (escape_char));
  ttyoflush ();
}

#if 0
setoptions ()
{
  showoptions = !showoptions;
  clear_bottom_line ();
  printf ("%s show option processing.", showoptions ? "Will" : "Wont");
  ttyoflush ();
}
#endif /* 0 */

#if 0
/* >>>>>> ???!! >>>>>> */
setcrmod ()
{
  crmod = !crmod;
  clear_bottom_line ();
  printf ("%s map carriage return on output.", crmod ? "Will" : "Wont");
  ttyoflush ();
}
#endif /* 0 */

#if 0
setdebug ()
{
  debug = !debug;
  clear_bottom_line ();
  printf ("%s turn on socket level debugging.", debug ? "Will" : "Wont");
  if (debug && net > 0 && setsockopt (net, SOL_SOCKET, SO_DEBUG, 0, 0) < 0)
    perror ("setsockopt (SO_DEBUG)");
  ttyoflush ();
}
#endif /* 0 */


#ifdef	TERMCAP

/* Assuming STRING is the value of a termcap string entry
   containing `%' constructs to expand parameters,
   merge in parameter values and store result in block OUTSTRING points to.
   LEN is the length of OUTSTRING.  If more space is needed,
   a block is allocated with `malloc'.

   The value returned is the address of the resulting string.
   This may be OUTSTRING or may be the address of a block got with `malloc'.
   In the latter case, the caller must free the block.

   The fourth and following args to tparam serve as the parameter values.  */

static unsigned char *tparam1 ();

/* VARARGS 2 */
unsigned char *
tparam (string, outstring, len, arg0, arg1, arg2, arg3)
     unsigned char *string;
     unsigned char *outstring;
     int len;
     int arg0, arg1, arg2, arg3;
{
  int arg[4];
  arg[0] = arg0;
  arg[1] = arg1;
  arg[2] = arg2;
  arg[3] = arg3;
  return tparam1 (string, outstring, len, 0, 0, arg);
}

unsigned char *BC;
unsigned char *UP;

static unsigned char tgoto_buf[50];

unsigned char *
tgoto (cm, hpos, vpos)
     char *cm;
     int hpos, vpos;
{
  int args[2];
  if (!cm)
    return 0;
  args[0] = vpos;
  args[1] = hpos;
  return tparam1 (cm, tgoto_buf, 50, UP, BC, args);
}

static unsigned char *
tparam1 (string, outstring, len, up, left, argp)
     unsigned char *string;
     unsigned char *outstring;
     int len;
     unsigned char *up, *left;
     register int *argp;
{
  register int c;
  register unsigned char *p = string;
  register unsigned char *op = outstring;
  unsigned char *outend;
  int outlen = 0;

  register int tem;
  int *oargp = argp;
  unsigned char *doleft = 0;
  unsigned char *doup = 0;

  outend = outstring + len;

  while (1)
    {
      /* If the buffer might be too short, make it bigger.  */
      if (op + 5 >= outend)
	{
	  register unsigned char *new;
	  if (outlen == 0)
	    {
	      new = (unsigned char *) malloc (outlen = 40 + len);
	      outend += 40;
	    }
	  else
	    {
	      outend += outlen;
	      new = (unsigned char *) realloc (outstring, outlen *= 2);
	    }
	  op += new - outstring;
	  outend += new - outstring;
	  outstring = new;
	}
      if (!(c = *p++))
	break;
      if (c == '%')
	{
	  c = *p++;
	  tem = *argp;
	  switch (c)
	    {
	    case 'd':		/* %d means output in decimal */
	      if (tem < 10)
		goto onedigit;
	      if (tem < 100)
		goto twodigit;
	    case '3':		/* %3 means output in decimal, 3 digits. */
	      if (tem > 999)
		{
		  *op++ = tem / 1000 + '0';
		  tem %= 1000;
		}
	      *op++ = tem / 100 + '0';
	    case '2':		/* %2 means output in decimal, 2 digits. */
	    twodigit:
	      tem %= 100;
	      *op++ = tem / 10 + '0';
	    onedigit:
	      *op++ = tem % 10 + '0';
	      argp++;
	      break;

	    case 'C':
	      /* For c-100: print quotient of value by 96, if nonzero,
		 then do like %+ */
	      if (tem >= 96)
		{
		  *op++ = tem / 96;
		  tem %= 96;
		}
	    case '+':		/* %+x means add character code of char x */
	      tem += *p++;
	    case '.':		/* %. means output as character */
	      if (left)
		{
		  /* If want to forbid output of 0 and \n,
		     and this is one, increment it.  */
		  if (tem == 0 || tem == '\n')
		    {
		      tem++;
		      if (argp == oargp)
			outend -= strlen (doleft = left);
		      else
			outend -= strlen (doup = up);
		    }
		}
	      *op++ = tem;
	    case 'f':		/* %f means discard next arg */
	      argp++;
	      break;

	    case 'b':		/* %b means back up one arg (and re-use it) */
	      argp--;
	      break;

	    case 'r':		/* %r means interchange following two args */
	      argp[0] = argp[1];
	      argp[1] = tem;
	      oargp++;
	      break;

	    case '>':		/* %>xy means if arg is > char code of x, */
	      if (argp[0] > *p++) /* then add char code of y to the arg, */
		argp[0] += *p;	/* and in any case don't output. */
	      p++;		/* Leave the arg to be output later. */
	      break;

	    case 'a':		/* %a means arithmetic */
	      /* Next character says what operation.
		 Add or subtract either a constant or some other arg */
	      /* First following character is + to add or - to subtract
		 or = to assign.  */
	      /* Next following char is 'p' and an arg spec
		 (0100 plus position of that arg relative to this one)
		 or 'c' and a constant stored in a character */
	      tem = p[2] & 0177;
	      if (p[1] == 'p')
		tem = argp[tem - 0100];
	      if (p[0] == '-')
		argp[0] -= tem;
	      else if (p[0] == '+')
		argp[0] += tem;
	      else if (p[0] == '*')
		argp[0] *= tem;
	      else if (p[0] == '/')
		argp[0] /= tem;
	      else
		argp[0] = tem;

	      p += 3;
	      break;

	    case 'i':		/* %i means add one to arg, */
	      argp[0] ++;	/* and leave it to be output later. */
	      argp[1] ++;	/* Increment the following arg, too!  */
	      break;

	    case '%':		/* %% means output %; no arg. */
	      goto ordinary;

	    case 'n':		/* %n means xor each of next two args with 140 */
	      argp[0] ^= 0140;
	      argp[1] ^= 0140;
	      break;

	    case 'm':		/* %m means xor each of next two args with 177 */
	      argp[0] ^= 0177;
	      argp[1] ^= 0177;
	      break;

	    case 'B':		/* %B means express arg as BCD char code. */
	      argp[0] += 6 * (tem / 10);
	      break;

	    case 'D':		/* %D means weird Delta Data transformation */
	      argp[0] -= 2 * (tem % 16);
	      break;
	    }
	}
      else
	/* Ordinary character in the argument string.  */
      ordinary:
	*op++ = c;
    }
  *op = 0;
  if (doleft)
    strcpy (op, doleft);
  if (doup)
    strcpy (op, doup);
  return outstring;
}
#endif /* TERMCAP */

