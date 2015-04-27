/* SUPDUP Server
 *
 *	Written Jan. 84 by David Bridgham.  The organization and some of
 *  the code was taken from the telnet server written by Berkeley for 4.2.
 */

/* Hacked by Mly to clarify ITS ITP July 1987
 * Hacked by Mly 29-Aug-87 to nuke stupid auto_right_margin lossage
 * Hacked by Mly 2-Sep-87 to send params as "4;" and "146;" rather than
 *  "\004" and "\222" to avoid stupid un*x 8-bit-and-control-d non-transparency
 * Hacked by wesommer@athena.mit.edu 25-Jan-88 to use winning 4.3BSD
 *  -p flag to /bin/login and to do TIOCSWINSZ stuff.
 *  #ifdef TTYLOC around ttyloc-hacking parts
 */

/* The magic number #o176 which appears in this file is the difference
 * between the real Supdup TDxxx codes and the kludge codes which appear
 * in the termcap/terminfo descriptions (which are needed because bloody
 * un*x deals incompetently with chars with the #o200 bit on
 * (as well as with #o004.  Cretins.)
 * Thus, for example, we have %TDMV0 = #o217, with "cm=\177\021%d;%d;:"
 * in the termcap entry; (- #o 217 #o021) => #o176
 */

/* #define TERMINFO 1 */	/* Define if want terminfo support. */
				/* there should be a TERMCAP too */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/file.h>

#include <netinet/in.h>

/* #include <arpa/telnet.h> */
#include "supdup.h"

#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <sgtty.h>
#include <netdb.h>

#ifdef TERMINFO
# include <term.h>
# undef CUR
# define CUR
#endif /* TERMINFO */

#ifndef BANNER
#define BANNER	"MIT Artificial Intelligence Laboratory 4.2 BSD UNIX (%s)\207%s"
#endif

#ifndef SBANNER
#define	SBANNER	"%s SUPDUP from %s"
#endif

/* * I/O data buffers, pointers, and counters. */
unsigned char ptyibuf[BUFSIZ], *ptyip = ptyibuf;
unsigned char ptyobuf[BUFSIZ], *pfrontp = ptyobuf, *pbackp = ptyobuf;
unsigned char netibuf[BUFSIZ], *netip = netibuf;
unsigned char netobuf[BUFSIZ], *nfrontp = netobuf, *nbackp = netobuf;
int pcc, ncc;

/* Filedesc for stream connected to local un*x pty. */
int pty;
/* Filedesc for stream connected to user (via internet) */
int net;
struct	sockaddr_in sin = { AF_INET };
int reapchild ();

char pty_name[] = "/dev/ptyq0";
#ifdef TTYLOC
char ttyloc[64];
#endif /* TTYLOC */

extern char **environ;
extern int errno;


/* Current cursor position */
int currcol, currline;

#ifndef TERMINFO /* terminfo defines these itself */
/* Number of columns, lines of virtual terminal */
int columns, lines;
#endif


#ifndef DEBUG
#define DPRINTF(a, b, c) {}
#else /* DEBUG */

#define DPRINTF(a, b, c) \
  if (debug_output) \
     { fprintf (debug_output, (a), (b), (c)); fflush (debug_output); }

FILE *debug_output = 0;
void
open_debug_output ()
{
  debug_output = fopen ("/supdupd.debug", "w");
  if (debug_output < 0)
    {
      fprintf (stderr, "Couldn't open \"/supdupd.debug\"\n");
      debug_output = 0;
    }

  DPRINTF ("Starting up supdupd...\n",0,0);
}
#endif /* DEBUG */

#if 0
/* this routine is used for debugging only */
echo_char (c)
     unsigned char c;
{
  static int count = 0;

  if (count++ > 16)
    {
      printf ("\n");
      count = 0;
    }
  if (c >= '\177') printf ("\\%o ", c);
  else if (c < '\041') printf ("\\%o ", c);
  else printf ("%c ", c);
  fflush (stdout);
}
#endif /* 0 */


#ifdef DAEMON
main (argc, argv)
     int argc;
     char *argv[];
{
  int s, pid, options;
  struct servent *sp;

#ifdef DEBUG
  open_debug_output ();
#endif /* DEBUG */

  sp = getservbyname ("supdup", "tcp");
  if (sp == 0)
    {
      fprintf (stderr, "supdupd: tcp/supdup: unknown service\n");
      exit (1);
    }
  sin.sin_port = sp->s_port;
  argc--, argv++;
  if (argc > 0 && !strcmp (*argv, "-d"))
    {
      options |= SO_DEBUG;
      argc--, argv++;
    }
  if (argc > 0)
    {
      sin.sin_port = atoi (*argv);
      if (sin.sin_port <= 0)
        {
          fprintf (stderr, "supdupd: %s: bad port #\n", *argv);
          exit (1);
        }
      sin.sin_port = htons ((u_short)sin.sin_port);
    }
#ifndef DEBUG
  if (fork ())
    exit (0);
  for (s = 0; s < 10; s++)
    (void) close (s);
  (void) open ("/", 0);
  (void) dup2 (0, 1);
  (void) dup2 (0, 2);
  {
    int tt = open ("/dev/tty", 2);
    if (tt > 0)
      {
        ioctl (tt, TIOCNOTTY, 0);
        close (tt);
      }
  }
#endif /* DEBUG */
again:
  s = socket (AF_INET, SOCK_STREAM, 0, 0);
  if (s < 0)
    {
      perror ("supdupd: socket");;
      sleep (5);
      goto again;
    }
  if (options & SO_DEBUG)
    if (setsockopt (s, SOL_SOCKET, SO_DEBUG, 0, 0) < 0)
      perror ("telnetd: setsockopt (SO_DEBUG)");
  if (setsockopt (s, SOL_SOCKET, SO_KEEPALIVE, 0, 0) < 0)
    perror ("supdupd: setsockopt (SO_KEEPALIVE)");
  while (bind (s, (caddr_t) &sin, sizeof (sin), 0) < 0)
    {
      perror ("supdupd: bind");
      sleep (5);
    }
  signal (SIGCHLD, reapchild);
  listen (s, 10);
  for (;;)
    {
      struct sockaddr_in from;
      int s2, fromlen = sizeof (from);

      s2 = accept (s, (caddr_t)&from, &fromlen);
      if (s2 < 0)
        {
          if (errno == EINTR)
            continue;
          perror ("supdupd: accept");
          sleep (1);
          continue;
        }
      if ((pid = fork ()) < 0)
        printf ("Out of processes\n");
      else if (pid == 0)
        {
          signal (SIGCHLD, SIG_DFL);
          doit (s2, &from);
        }
      close (s2);
    }
  /*NOTREACHED*/
}

#else /* not DAEMON */
main (argc, argv)
     int argc;
     char *argv[];
{
  struct sockaddr_in from;
  int fromlen;

#ifdef DEBUG
  open_debug_output ();
#endif /* DEBUG */
  fromlen = sizeof (from);
  if (getpeername (0, &from, &fromlen) < 0)
    {
      fprintf (stderr, "%s: ", argv[0]);
      perror ("getpeername");
      _exit (1);
    }

/* MIT */
#ifdef KEEPALIVE
  if (setsockopt (0, SOL_SOCKET, SO_KEEPALIVE, 0, 0) < 0)
    {
      fprintf (stderr, "%s: ", argv[0]);
      perror ("setsockopt (SO_KEEPALIVE)");
    }
#endif /* KEEPALIVE */
/* MIT */
  doit (0, &from);
}
#endif /* DAEMON */


reapchild ()
{
  union wait status;

  while (wait3 (&status, WNOHANG, 0) > 0)
    ;
}

/* #ifdef TERMCAP */
char termcap[1024];
/* #endif /* TERMCAP */

#ifdef TERMINFO
char terminfo[64];
#endif /* TERMINFO */

char *envinit[] =
  {
    "TERM=supdup",
/* #ifdef TERMCAP */
    termcap,
/* #endif /* TERMCAP */
#ifdef TERMINFO
    terminfo,
#endif /* TERMINFO */
    0
  };

int cleanup ();

char *host;
#ifdef TIOCSWINSZ
struct winsize ws;
#endif

/*
 * Get a pty, scan input lines.
 */
doit (f, who)
     int f;
     struct sockaddr_in *who;
{
  char *cp = pty_name, *ntoa ();
  int i, p, cc, t;
  struct sgttyb b;
  struct hostent *hp;

  for (i = 0; i < 16; i++)
    {
      cp[strlen ("/dev/ptyq")] = "0123456789abcdef"[i];
      p = open (cp, O_RDWR, 0);
      if (p > 0)
        goto gotpty;
    }
  fatal (f, "All network ports in use");
  /*NOTREACHED*/
gotpty:
  dup2 (f, 0);
  cp[strlen ("/dev/")] = 't';
#ifdef TTYLOC
  sprintf (ttyloc, "/tmp/ttyq%c.ttyloc", cp[strlen ("/dev/ttyq")]);
  unlink (ttyloc);
#endif /* TTYLOC */
  t = open ("/dev/tty", O_RDWR, 0);
  if (t >= 0)
    {
      ioctl (t, TIOCNOTTY, 0);
      close (t);
    }
  t = open (cp, O_RDWR, 0);
  if (t < 0)
    fatalperror (f, cp, errno);
  ioctl (t, TIOCGETP, &b);
  /* MIT */
  b.sg_ispeed = B9600;
  b.sg_ospeed = B9600;
  /* MIT */
  b.sg_flags = XTABS | ANYP;      /* punted CRMOD */
  ioctl (t, TIOCSETP, &b);
  ioctl (p, TIOCGETP, &b);
  b.sg_flags &= ~ECHO;
  ioctl (p, TIOCSETP, &b);
  sup_options (f);
#ifdef TIOCSWINSZ
  ioctl (p, TIOCSWINSZ, &ws);
#endif /* TIOCSWINSZ */
  hp = gethostbyaddr (&who->sin_addr, sizeof (struct in_addr),
                      who->sin_family);
  if (hp)
    host = hp->h_name;
  else
    host = ntoa (who->sin_addr);
  if ((i = fork ()) < 0)
    fatalperror (f, "fork", errno);
  if (i)
    supdup (f, p);

  close (f);
  close (p);
  dup2 (t, 0);
  dup2 (t, 1);
  dup2 (t, 2);
  close (t);
#ifdef BSD4_3
  /* Winning -p switch passes through environment */
  execle ("/bin/login", "login", "-p", "-h", host, 0, envinit);
#else
#ifdef MIT
  /* Local /bin/login at ai.mit.edu machines hacked to pass TERM and TERMCAP */
  execle ("/bin/login", "login", "-h", host, 0, envinit);
  fatalperror (2, "/bin/login", errno);
#else
  /* Can't use /bin/login as that nukes TERM and TERMCAP.  Cretins! */
  execle ("/usr/etc/supdup-login", "login", "-h", host, 0, envinit);
  fatalperror (2, "/usr/etc/supdup-login", errno);
#endif /* MIT */
#endif /* not BSD4_3 */
  /*NOTREACHED*/
}

fatal (f, msg)
     int f;
     char *msg;
{
  char buf[BUFSIZ];

  (void) sprintf (buf, "supdupd: %s.\r\n", msg);
  (void) write (f, buf, strlen (buf));
  exit (1);
}

fatalperror (f, msg, errno)
     int f;
     char *msg;
     int errno;
{
  char buf[BUFSIZ];
  extern char *sys_errlist[];

  (void) sprintf (buf, "%s: %s", msg, sys_errlist[errno]);
  fatal (f, buf);
}

/*
 * Main loop.  Select from pty and network, and
 * hand data to supdup receiver finite state machine.
 */
supdup (f, p)
{
  int on = 1;
  char hostname[256];

  net = f, pty = p;
  ioctl (f, FIONBIO, &on);
  ioctl (p, FIONBIO, &on);
  signal (SIGTSTP, SIG_IGN);
  signal (SIGCHLD, cleanup);
  mode (ECHO|CRMOD, 0);

  /*
   * Print supdup banner.
   */
  gethostname (hostname, sizeof (hostname));
  sprintf (nfrontp, SBANNER, hostname, host);
  nfrontp += strlen (nfrontp);
  *nfrontp++ = TDNOP;
  netflush ();
  sleep (2);
	
  /*
   * Show banner that getty never gave.
   */

  *nfrontp++ = TDCLR;
  sprintf (nfrontp, BANNER, hostname, "");
  currline = 1;
  currcol = 0;
  nfrontp += strlen (nfrontp);
  for (;;)
    {
      int ibits = 0, obits = 0;
      register int c;

      /*
       * Never look for input if there's still
       * stuff in the corresponding output buffer
       */
      if ((nfrontp - nbackp) || pcc > 0)
        obits |= (1 << f);
      else
        ibits |= (1 << p);
      if ((pfrontp - pbackp) || ncc > 0)
        obits |= (1 << p);
      else
        ibits |= (1 << f);
      if (ncc < 0 && pcc < 0)
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
      if (ibits & (1 << f))
        {
          ncc = read (f, netibuf, BUFSIZ);
          if (ncc < 0 && errno == EWOULDBLOCK)
            ncc = 0;
          else
            {
              if (ncc <= 0)
                break;
              netip = netibuf;
            }
        }

      /*
       * Something to read from the pty...
       */
      if (ibits & (1 << p))
        {
          pcc = read (p, ptyibuf, BUFSIZ);
          if (pcc < 0 && errno == EWOULDBLOCK)
            pcc = 0;
          else
            {
              if (pcc <= 0)
                break;
              ptyip = ptyibuf;
            }
        }

      if (pcc > 0)
        supxmit ();
      if ((obits & (1 << f)) && (nfrontp - nbackp) > 0)
        netflush ();
      if (ncc > 0)
        suprcv ();
      if ((obits & (1 << p)) && (pfrontp - pbackp) > 0)
        ptyflush ();
    }
  cleanup ();
}
	
/* State for xmit fsm */
#define	XS_DATA		0	/* base state */
#define	XS_ESCAPE	1	/* supdup commands are escaped in TERMCAP */
#define	XS_MV0_V	2	/* getting vertical position for TDMV0 */
#define	XS_MV0_H	4	/* getting horizontal position for TDMV0 */
#define	XS_CRLF		6	/* got \r looking for \n */
#define XS_LFCR         7       /* got \n looking for \r */
#if (TDILP < 8) || (TDDLP < 8) || (TDICP < 8) || (TDDCP < 8)
 you lose!
#endif
#define	XS_ILINE	TDILP	/* waiting for number of lines to insert */
#define	XS_DLINE	TDDLP	/* waiting for number of lines to delete */
#define	XS_ICHAR	TDICP	/* waiting for number of chars to insert */
#define	XS_DCHAR	TDDCP	/* waiting for number of chars to delete */

/* Because of Un*x Brain Death(tm)
 * characters with the 200 bit don't go through the terminal driver reliably
 * and \004 won't go through a un*x at all pty (what sort of bozos wrote
 * this sh*t?)
 */

supxmit ()
{
  static int state = XS_DATA;
  register int c;
  static int piece_o_state, piece_o_state_v;

  while (pcc > 0)
    {
      if ((&netobuf[BUFSIZ] - nfrontp) < 4)
        /* Caller will flush pending output */
        return;
      c = *ptyip++ & 0377; pcc--;
      switch (state)
        {
        case XS_DATA:
          switch (c)
            {
            case ITP_ESCAPE:
              *nfrontp++ = c;
              *nfrontp++ = c;
              break;

            case KLUDGE_ESCAPE:
              state = XS_ESCAPE;
              break;

            case '\007':
              *nfrontp++ = TDBEL;
              break;

            case '\t':
              currcol = (currcol + 8) & ~7;
              if (currcol >= columns)
                currcol = columns;
              *nfrontp++ = TDMV0;
              *nfrontp++ = currline;
              *nfrontp++ = currcol;
              break;

            case '\b':
              currcol--;
              if (currcol < 0)
                currcol = 0;
              *nfrontp++ = TDMV0;
              *nfrontp++ = currline;
              *nfrontp++ = currcol;
              break;

            case '\r':
              DPRINTF ("\n<<Got a return>>",0,0);
              state = XS_CRLF;
              break;

            case '\n':
              DPRINTF ("\n<<Got a newline>>",0,0);
              state = XS_LFCR;
              break;

            case '\0':
              /* throw away nulls. Something is being
               * a twit and padding even though it
               * is not wanted! */
              /* (Of course, \0 is really a printable MIT Ascii character) */
              break;

            default:
              if (currcol < columns)
                {
                  currcol++;
                  *nfrontp++ = c;
                }
              else
                {
                  /* unread-char and do newline */
                  ptyip--; pcc++;
                  do_crlf ();
                }
              break;
            }
          break;

        case XS_ESCAPE:
          c += 0176;
          switch (c)
            {
            case TDCRL:
              state = XS_DATA;
              do_crlf ();
              break;

            case TDFS:
              currcol++;
              state = XS_DATA;
              *nfrontp++ = c;
              break;

            case TDMV0:
              state = XS_MV0_V;
              piece_o_state = 0;
              piece_o_state_v = 0;
              break;

            case TDCLR:
              DPRINTF ("\n<<In TDCLR handler>>",0,0);
              currcol = 0; currline = 0;
              state = XS_DATA;
              *nfrontp++ = c;
              break;

            case TDLF:
              /* this is not in the SUPDUP spec */
              /* Move down one row vertically */
              state = XS_DATA;
              if (currline < (lines - 1))
                currline++;
              *nfrontp++ = TDMV0;
              *nfrontp++ = currline;
              *nfrontp++ = currcol;
              break;

            case TDUP:
              state = XS_DATA;
              if (currline > 0) currline--;
              *nfrontp++ = TDMV0;
              *nfrontp++ = currline;
              *nfrontp++ = currcol;
              break;

            case TDILP:
            case TDDLP:
            case TDICP:
            case TDDCP:
              state = c;
              piece_o_state = 0;
              break;
				
            case KLUDGE_ESCAPE:
              /* Really shouldn't get two */
              /* of these but it's happening */
              break;

            default:
              state = XS_DATA;
              *nfrontp++ = c;
              break;

            }
          break;


          /* The newline algorithm is as follows:
           *	A \r causes the cursor to be sent to the
           * beginning of the line unless there is a \n
           * immediately following on the input.
           *	If the next character is a \n then a
           * %TDCRL is sent.
           *	A lone \n causes the cursor to be moved
           * straight down one line wrapping if necessary
           * or doing a %TDCRL if at the bottom of the screen
           * and it is a scrolling terminal.
           *
           * The CR and NL handling is way too complicated...
           */
        case XS_LFCR:
          {
            DPRINTF ("\n<<In XS_LFCR handler>>",0,0);
            if (c == '\r')
              do_crlf ();
            else
              {
                /* Got a \012 followed by something other than a \015 */
                /* Treat the \012 as `move down down one row */
                if (currline < (lines - 1))
                  {
                    *nfrontp++ = TDMV0;
                    *nfrontp++ = ++currline;
                    *nfrontp++ = currcol;
                  }
                else if (TOROL)
                  {
                    /* this is not the right thing, but
                     * what else can I do? */
                    currcol = 0;
                    *nfrontp++ = TDCRL;
                  }
                else
                  {
                    currline = 0;
                    *nfrontp++ = TDMV0;
                    *nfrontp++ = 0;
                    *nfrontp++ = currcol;
                    *nfrontp++ = TDEOL;
                  }
                ptyip--; pcc++; /* unread-char */
                DPRINTF ("\n<<LFCR: unreading %c>>", *ptyip,0);
              }
            state = XS_DATA;
            break;
          }

        case XS_CRLF:
          {
            DPRINTF ("\n<<In XS_CRLF handler>>",0,0);
            if (c == '\n')
              do_crlf ();
            else
              {
                /* \013 followed by something other than \010 */
                /* Treat the \010 as a request to move to start of row */
                currcol = 0;
                *nfrontp++ = TDMV0;
                *nfrontp++ = currline;
                *nfrontp++ = 0;
                ptyip--; pcc++; /* unread-char */
                DPRINTF ("\n<<CRLF: unreading %c>>", *ptyip,0);
              }
            state = XS_DATA;
            break;
          }

        case XS_MV0_V:
        case XS_MV0_H:
        case TDILP:
        case TDDLP:
        case TDICP:
        case TDDCP:
          {
            DPRINTF ("\n<<arg:%d+%c>>", piece_o_state, c);
            if (c >= '0' && c <= '9')
              {
                piece_o_state = piece_o_state * 10 + c - '0';
                if (piece_o_state >= lines && piece_o_state >= columns)
                  {
                    state = XS_DATA;
                    break;
                  }
              }
            else if (c != ';')
              {
                state = XS_DATA;
                break;
              }
            else switch (state)
              {
              case XS_MV0_V:
                piece_o_state_v = piece_o_state;
                piece_o_state = 0;
                state = XS_MV0_H;
                break;
              case XS_MV0_H:
                currline = piece_o_state_v;
                currcol = piece_o_state;
                state = XS_DATA;
                *nfrontp++ = TDMV0;
                *nfrontp++ = currline;
                *nfrontp++ = currcol;
                break;
              default: /* TDILP, TDDLP, RDICP, TDDCP */
                while (pcc > 4 && (ptyip[0] & 0377) == KLUDGE_ESCAPE &&
                       (ptyip[1] & 0377) == state - 0176 &&
                       (ptyip[2] & 0377) == '1' &&
                       (ptyip[3] & 0377) == ';')
                  /* Merge successive single insert/delete-line/char requests
                     (pander to clients who don't understand TERMCAP
                     AL/DL (and less-importantly IC/DC) capabilities) */
                  {
                    piece_o_state++;
                    pcc -= 4;
                    ptyip += 4;
                  }
                *nfrontp++ = state;
                *nfrontp++ = piece_o_state;
                if (state == TDILP || state == TDDLP)
                  {
                    /* Just to be on the safe side */
                    currcol = 0;
                    *nfrontp++ = TDMV0;
                    *nfrontp++ = currline;
                    *nfrontp++ = currcol;
                  }
                state = XS_DATA;
                break;
              }
            DPRINTF ("\n<<now:%d,%d>>", piece_o_state_v, piece_o_state);
            break;
          }

        default:
          state = XS_DATA;
          break;
        }
    }
}

do_crlf ()
{
  currcol = 0;
  if (currline < (lines - 1))
    {
      *nfrontp++ = TDCRL;
      currline++;
    }
  else if (TOROL)
    *nfrontp++ = TDCRL;
  else
    {
      currline = 0;
      *nfrontp++ = TDMV0;
      *nfrontp++ = 0;
      *nfrontp++ = 0;
      *nfrontp++ = TDEOL;
    }
}


/*
 * State for recv fsm
 */

/* base state */
#define RS_DATA		0

/* recieved ITP_ESCAPE (034) Waiting for `m' */
#define	RS_ITP_ESCAPE	1
/* received ITP_ESCAPE, `m'>#o100
   Waiting for `n'.
   Char will be (+ (* (- `m' #o100) #o200) `n') */
#define RS_BUCKY	2

/* Recived ITP_ESCAPE, ITP_FLOW_CONTROL_INCREASE
   Ignore next char, since un*x can't hack real, winning, flow control */
#define RS_FLOW_CONTROL_INCREASE 3

/* Recived ITP_ESCAPE, ITP_CURSORPOS
   Waiting for `row' */
#define RS_CURSORPOS_1	4
/* Recived ITP_ESCAPE, ITP_CURSORPOS, `row'
   Waiting for `column' */
#define RS_CURSORPOS_2	5

/* received SUPDUP_ESCAPE (#o300)
   Waiting for `m' */
#define	RS_SUPDUP_ESCAPE 6

/* received SUPDUP_ESCAPE, SUPDUP_LOCATION.
   Now receiving 000-terminated
   location string */
#define	RS_LOCATION	7

suprcv ()
{
  register int	c;

  static int state = RS_DATA;
  static int piece_o_state = 69;

  while ((ncc > 0) &&
         /* Caller must flush pending output */
         ((&ptyobuf[BUFSIZ] - pfrontp) > 3))
    {
      c = *netip++ & 0377; ncc--;
      switch (state)
        {
        case RS_DATA:
          {
            if (c == SUPDUP_ESCAPE)
              state = RS_SUPDUP_ESCAPE;
            else if (c == ITP_ESCAPE)
              state = RS_ITP_ESCAPE;
            else
              *pfrontp++ = c;
            break;
          }

        case RS_ITP_ESCAPE:
          {
            if (c & 0100)
              {
                piece_o_state = c;
                state = RS_BUCKY;
              }
            else switch (c)
              {
              case ITP_ESCAPE:
                *pfrontp++ = c;
                state = RS_DATA;
                break;
              case ITP_PIATY:
                /* %piaty --- user's screen is munged.
                   Cretinous unix provides no way to use this.
                   Conceivably we could send through a c-L character (barf!)
                   */
              case ITP_STOP_OUTPUT:
              case ITP_RESTART_OUTPUT:
              case ITP_FLOW_CONTROL_START:
              case ITP_FLOW_CONTROL_END:
                /* ignore it */
                state = RS_DATA;
                break;
              case ITP_FLOW_CONTROL_INCREASE:
                state = RS_FLOW_CONTROL_INCREASE;
                break;
              case ITP_CURSORPOS:
                /* read cursorpos -- v then h */
                state = RS_CURSORPOS_1;
                break;
              default:          /* huh?! */
                state = RS_DATA;
                break;
              }
          break;
          }

        case RS_BUCKY:
          {
            register int bucky;

            bucky = ITP_CHAR (piece_o_state, c);
            /* unix supdup server uses 0237 (CTL_PREFIX) as a control escape.
             * c-a	001
             * m-a	341
             * c-m-a	201
             * c-1	237 061
             * m-1	261
             * c-m-1	237 261
             * c-m-_	237 237
             */

            switch (bucky)
              {
              case SUPDUP_HELP_KEY:
                c = 'h' & 037; /* hack for Help key */
                break;
              case SUPDUP_ESCAPE_KEY:
                c = '\033';
                break;
              case SUPDUP_SUSPEND_KEY:
                c = 'z' & 037;
                break;
              case SUPDUP_CLEAR_KEY:
                c = -1;
                break;
              default:
                c = bucky & 0177;
                if (bucky & ITP_TOP)
                  c &= 037;	/* This is pretty random -- Mly */
                if (bucky & ITP_CTL)
                  {
                    if ((c >= 'a' && c <= 'z') ||
                        (c >= '@' && c <= 'Z') ||
                        (c >= '\\' && c <= '_'))
                      c &= 037;
                    else
                      *pfrontp++ = CTL_PREFIX;
                  }
                if (bucky & ITP_MTA)
                  c |= 0200;
                if (c == CTL_PREFIX)
                  /* Double control prefix to send 0237 (200 + ^_)
                     through pseudo-transparently */
                  *pfrontp++ = c;
              }
            if (c >= 0)
              *pfrontp++ = c;
            state = RS_DATA;
            break;
          }

        case RS_FLOW_CONTROL_INCREASE:
          /* Ignore it -- we can't hack this */
          state = RS_DATA;
          break;

        case RS_CURSORPOS_1:
          piece_o_state = c;
          state = RS_CURSORPOS_2;
          break;

        case RS_CURSORPOS_2:
          {
            /* read cursorpos -- v then h */
            if ((piece_o_state < lines) && (c < columns))
              {
                currcol = piece_o_state;
                currline = c;
              }
            state = RS_DATA;
          }


        case RS_SUPDUP_ESCAPE:
          switch (c)
            {
            case SUPDUP_LOGOUT:
              cleanup ();

            case SUPDUP_LOCATION:
              state = RS_LOCATION;
              break;

            default:            /* WTF? */
              state = RS_DATA;
              break;
            }
          break;

        case RS_LOCATION:
          {
            static char buf[BUFSIZ];
            static char *p = &buf[0];
            int f;

            if (c != '\000')
              {
                if (p - buf < BUFSIZ)
                  *p++ = c;
              }
            else
              {
#ifdef TTYLOC
                f = creat (ttyloc, 0644);
                if (f > 0)
                  {
                    (void) write (f, buf, p - buf);
                    (void) close (f);
                  }
#endif /* TTYLOC */
                state = RS_DATA;
              }
          }
          break;
        }
    }
}

mode (on, off)
     int on, off;
{
  struct sgttyb b;

  ptyflush ();
  ioctl (pty, TIOCGETP, &b);
  b.sg_flags |= on;
  b.sg_flags &= ~off;
  ioctl (pty, TIOCSETP, &b);
}

ptyflush ()
{
  int n;

  if ((n = pfrontp - pbackp) > 0)
    n = write (pty, pbackp, n);
  if (n < 0)
    return;
  pbackp += n;
  if (pbackp == pfrontp)
    pbackp = pfrontp = ptyobuf;
}

netflush ()
{
  int n;

  if ((n = nfrontp - nbackp) > 0)
    {
      n = write (net, nbackp, n);
#ifdef DEBUG
      if (debug_output)
        {
          fflush (debug_output);
          (void) write (fileno (debug_output), nbackp, n);
        }
#endif /* DEBUG */
    }
  if (n < 0)
    {
      if (errno == EWOULDBLOCK)
        return;
      /* should blow this guy away... */
      return;
    }
  nbackp += n;
  if (nbackp == nfrontp)
    nbackp = nfrontp = netobuf;
}

cleanup ()
{
#ifdef TERMINFO
  clean_terminfo ();
#endif /* TERMINFO */
	
  rmut ();
#ifdef TTYLOC
  unlink (ttyloc);
#endif /* TTYLOC */
  vhangup ();                   /* XXX */
  shutdown (net, 2);
  kill (0, SIGKILL);
  exit (1);
}

#ifdef	TERMINFO
/* Cleans up the files created for the TERMINFO stuff.
 */
clean_terminfo ()
{
  char	dir[128];
  int	pid;

  pid = getpid ();
  sprintf (dir, "/tmp/%d/s/supdup", pid);
  unlink (dir);
  sprintf (dir, "/tmp/%d/s", pid);
  rmdir (dir);
  sprintf (dir, "/tmp/%d", pid);
  rmdir (dir);
}

#endif /* TERMINFO */


#include <utmp.h>

struct	utmp wtmp;
char	wtmpf[]	= "/usr/adm/wtmp";
char	utmp[] = "/etc/utmp";
#define SCPYN(a, b)	strncpy (a, b, sizeof (a))
#define SCMPN(a, b)	strncmp (a, b, sizeof (a))

rmut ()
{
  register f;
  int found = 0;

  f = open (utmp, O_RDWR, 0);
  if (f >= 0)
    {
      while (sizeof (wtmp) == read (f, (char *) &wtmp, sizeof (wtmp)))
        {
          if (SCMPN (wtmp.ut_line, pty_name + strlen ("/dev/")) ||
              wtmp.ut_name[0] == 0)
            continue;
          lseek (f, - (long)sizeof (wtmp), 1);
          SCPYN (wtmp.ut_name, "");
          SCPYN (wtmp.ut_host, "");
          time (&wtmp.ut_time);
          write (f, (char *) &wtmp, sizeof (wtmp));
          found++;
        }
      close (f);
    }
  if (found)
    {
      f = open (wtmpf, O_WRONLY, 0);
      if (f >= 0)
        {
          SCPYN (wtmp.ut_line, pty_name + strlen ("/dev/"));
          SCPYN (wtmp.ut_name, "");
          SCPYN (wtmp.ut_host, "");
          time (&wtmp.ut_time);
          lseek (f, (long) 0, 2);
          write (f, (char *) &wtmp, sizeof (wtmp));
          close (f);
        }
    }
  chmod (pty_name, 0666);
  chown (pty_name, 0, 0);
  pty_name[strlen ("/dev/")] = 'p';
  chmod (pty_name, 0666);
  chown (pty_name, 0, 0);
}

/*
 * Convert network-format internet address
 * to base 256 d.d.d.d representation.
 */
char *
ntoa (in)
     struct in_addr in;
{
  static char b[18];
  register char *p;

  p = (char *) &in;
#define	UC(b)	(((int)b)&0xff)
  sprintf (b, "%d.%d.%d.%d", UC (p[0]), UC (p[1]), UC (p[2]), UC (p[3]));
  return (b);
}

/* Read the 36 bit options from the net setting variables and create
 * the TERMCAP environment variable.
 */
sup_options (net)
     int net;
{
  char temp[6];
  int count;
  int tcmxh, tcmxv;

  read (net, temp, 6);          /* Read count */
  count = -((-1 << 6) | temp[2]);
  if (count--)
    {
      read (net, temp, 6);      /* Discard TCTYP */
      if (count--)
        {
          read (net, ttyopt, 6);
          if (count--)
            {
              read (net, temp, 6);
              tcmxv = (temp[5] & 0377) | ((temp[4] & 0377) << 6);
#if 0 /* No longer needed, since we now encode positions using %d */
              /* Un*x braindeath (tm) */
              if (tcmxv >= 128 - 5) tcmxv = 128 - 5 - 1;
#endif 0
              if (count--)
                {
                  read (net, temp, 6);
                  /* The 1 + is because supdup spec transmits 1 - columns */
                  tcmxh = 1 + (temp[5] & 0377) | ((temp[4] & 0377) << 6);
#if 0
                  /* Un*x braindeath (tm) */
                  if (tcmxh >= 128 - 5) tcmxh = 128 - 5 - 1;
#endif 0
                  if (count--)
                    {
                      read (net, temp, 6);
                      ttyrol = temp[5] & 077;
                      while (count--)
                        read (net, temp, 6);
                    }
                }
            }
        }
    }

  lines = tcmxv;
  columns = tcmxh;

#ifdef TIOCSWINSZ
  ws.ws_row = lines;
  ws.ws_col = columns;
  ws.ws_xpixel = columns * 6;
  ws.ws_ypixel = lines * 10;
#endif /* TIOCSWINSZ */

  sprintf (termcap, "TERMCAP=SD|supdup|SUPDUP virtual terminal:co#%d:li#%d:",
           columns, lines);
  strcat (termcap, "vb=\\177\\023:nd=\\177\\020:MT:");
  strcat (termcap, "cl=\\177\\022:so=\\177\\031:se=\\177\\032:pt:");
  if (TOERS)
    strcat (termcap, "ce=\\177\\005:ec=\\177\\006:cd=\\177\\004:");
  if (TOMVB)
    strcat (termcap, "bs:");
  if (TOOVR)
    strcat (termcap, "os:");
  if (TOMVU)
    {
      strcat (termcap, "up=\\177\\041:cm=\\177\\021%d;%d;:");
      strcat (termcap, "do=\\177\\014:nl=\\177\\014:");
    }
  if (TOLID)
    {
      strcat (termcap, "al=\\177\\0251;:dl=\\177\\0261;:");
      strcat (termcap, "AL=\\177\\025%d;:DL=\\177\\026%d;:");
    }
  if (TOCID)
    {
      strcat (termcap, "mi:im=:ei=:dm=:ed=:");
      strcat (termcap, "ic=\\177\\0271;:dc=\\177\\0301;:");
      strcat (termcap, "IC=\\177\\027%d;:DC=\\177\\030%d;:");
    }
  if (!TOROL)
    strcat (termcap, "ns:");
/*if (TOFCI) -- I don't think that always doing this can hurt much */
    strcat (termcap, "km:");

  /* We only wrap if a character is written when we are
     -past- the last column.  This way we avoid brain-dead unwanted
     scrolling, whilst still allowing un*x' completely brain lack of
     terminal-handling (which assumes that directly sending an
     arbitrary-length string containing no newlines to the terminal
     will result in the user seeing all of it.
     As PGS says, ``filthy damned unix pinheads.'' */
  /* strcat (termcap, "am:"); We don't lose in this way any more. */
  /* Not-widely-known flag, which says that writing in the last column of
     the last line doesn't cause idiotic scrolling */
  strcat (termcap, "LP:");
  /* Not-widely-known flag, which says not to use idiotic
     c-s/c-q so-called ``flow control'' under any circumstances. */
  strcat (termcap, "NF:");

#ifdef TERMINFO
  init_terminfo ();
#endif /* TERMINFO */
}


#ifdef TERMINFO

char names[] = "supdup|sd|supdup virtual terminal";
#define	MAGIC_NUM	0432

struct head
  {
    short	magic;
    short	name_len;
    short	bools_len;
    short	nums_len;
    short	str_len;
    short	strtab_len;
  } header;

unsigned char Booleans[BOOLCOUNT + 1];
short	Numbers[NUMCOUNT];
short	Strings[STRCOUNT];
char	String_Table[] = {
#define	FLASH_SCREEN	0
	'\177', '\023', '\0',
#define	CURSOR_RIGHT FLASH_SCREEN + 3
	'\177', '\020', '\0',
#define	CLEAR_SCREEN CURSOR_RIGHT + 3
	'\177', '\022', '\0',
#define	ENTER_STANDOUT_MODE CLEAR_SCREEN + 3
	'\177', '\031', '\0',
#define	EXIT_STANDOUT_MODE ENTER_STANDOUT_MODE + 3
	'\177', '\032', '\0',
#define	CLR_EOL	EXIT_STANDOUT_MODE + 3
	'\177', '\005', '\0',
#define	CLR_EOS	CLR_EOL + 3
	'\177', '\004', '\0',
#define	CURSOR_UP CLR_EOS + 3
	'\177', '\041', '\0',
#define	CURSOR_DOWN CURSOR_UP + 3
	'\177', '\014', '\0',
#define	NEWLINE	CURSOR_DOWN + 3
	'\177', '\011', '\0',	/* this does cursor_down (\014) in termcap */
#define	CURSOR_ADDRESS NEWLINE + 3
        /* %p1%d;%p2%d; */
	'\177', '\021',
        '%', 'p', '1', '%', 'd', ';', '%', 'p', '2', '%', 'd', ';', '\0',
#define	INSERT_LINE CURSOR_ADDRESS + 15
	'\177', '\025', '1', ';', '\0',
#define	DELETE_LINE INSERT_LINE + 5
	'\177', '\026', '1', ';', '\0',
#define	PARM_INSERT_LINE DELETE_LINE + 5
	'\177', '\025', '%', 'p', '1', '%', 'd', ';', '\0',
#define	PARM_DELETE_LINE PARM_INSERT_LINE + 9
	'\177', '\026', '%', 'p', '1', '%', 'd', ';', '\0',
#define	CURSOR_LEFT PARM_DELETE_LINE + 9
        /* >> BARF!! */
	'', '\0',
#define	DELETE_CHARACTER CURSOR_LEFT + 2
	'\177', '\030', '1', ';', '\0',
#define	INSERT_CHARACTER DELETE_CHARACTER + 5
	'\177', '\027', '1', ';', '\0',
#define	PARM_DCH INSERT_CHARACTER + 5
	'\177', '\030', '%', 'p', '1', '%', 'd', ';', '\0',
#define	PARM_ICH PARM_DCH + 9
	'\177', '\027', '%', 'p', '1', '%', 'd', ';', '\0',
#define	STRTABLEN PARM_ICH + 9 + 10
};


/* This routine sets up the files and environment variables for using the
 * TERMINFO data base.
 */
init_terminfo ()
{
  register int i;
  int	file;
  int	pid;
  char	directory[128];

  pid = getpid ();
  sprintf (directory, "/tmp/%d", pid);
  mkdir (directory, 0777);
  sprintf (terminfo, "TERMINFO=%s", directory);
  strcat (directory, "/s");
  mkdir (directory, 0777);
  strcat (directory, "/supdup");

  file = open (directory, O_WRONLY | O_CREAT, 0777);
  if (file <= 0) cleanup ();

  for (i = 0; i < BOOLCOUNT + 1; i++)
    Booleans[i] = 0;
  for (i = 0; i < NUMCOUNT; i++)
    Numbers[i] = -1;
  for (i = 0; i < STRCOUNT; i++)
    Strings[i] = -1;

  header.magic = MAGIC_NUM;
  header.name_len = strlen (names) + 1;
  header.bools_len = BOOLCOUNT;
  header.nums_len = NUMCOUNT;
  header.str_len = STRCOUNT;
  header.strtab_len = STRTABLEN;

  lines = tcmxv;
  columns = tcmxh;
  auto_right_margin = 0;
  flash_screen = FLASH_SCREEN;
  cursor_right = CURSOR_RIGHT;
  clear_screen = CLEAR_SCREEN;
  enter_standout_mode = ENTER_STANDOUT_MODE;
  exit_standout_mode = EXIT_STANDOUT_MODE;
  init_tabs = 8;
  if (TOERS)
    {
      clr_eol = CLR_EOL;
      clr_eos = CLR_EOS;
    }
  if (TOMVB)
    cursor_left = CURSOR_LEFT;
  if (TOOVR)
    over_strike = 1;
  if (TOMVU)
    {
      cursor_up = CURSOR_UP;
      cursor_down = CURSOR_DOWN;
      newline = NEWLINE;
      cursor_address = CURSOR_ADDRESS;
    }
  if (TOLID)
    {
      insert_line = INSERT_LINE;
      delete_line = DELETE_LINE;
      parm_insert_line = PARM_INSERT_LINE;
      parm_delete_line = PARM_DELETE_LINE;
    }
  if (TOCID)
    {
      insert_character = INSERT_CHARACTER;
      delete_character = DELETE_CHARACTER;
      parm_dch = PARM_DCH;
      parm_ich = PARM_ICH;
    }
/*if (TOFCI) -- I don't think that always doing this can hurt much */
    has_meta_key = 1;

  write (file, &header, sizeof (struct head));
  write (file, names, header.name_len);
  write (file, Booleans, (header.name_len + header.bools_len) & 1
			  ? header.bools_len + 1
			  : header.bools_len);
  write (file, Numbers, 2 * header.nums_len);
  write (file, Strings, 2 * header.str_len);
  write (file, String_Table, header.strtab_len + 5);
	
}

#endif /* TERMINFO */
