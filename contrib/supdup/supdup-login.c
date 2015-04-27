/* Lucid changes and supdup changes merged into
   vanilla quotaless sun /bin/login by Mly 9-Jul-87 16:17:01

   supdup changes:
   pass on envvar TERMCAP if TERM is "supdup"

   lucid changes (kdo):
   disconnect after > 5 failed login attempts

   always prompt for password when insecure login

   if compiled with -DPARANOIA, disallows passwords
   of length < 6 when insecure !!!!!  This includes
   null passwords.
*/

/*
 * login [ name ]
 * login -r hostname (for rlogind)
 * login -h hostname (for telnetd, etc.)
 */
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>

#include <sgtty.h>
#include <utmp.h>
#include <signal.h>
#include <pwd.h>
#include <stdio.h>
#include <lastlog.h>
#include <errno.h>
#include <syslog.h>

#define	SCPYN(a, b)	strncpy(a, b, sizeof(a))

#define NMAX	sizeof(utmp.ut_name)

#define	FALSE	0
#define	TRUE	-1

char	QUOTAWARN[] =	"/usr/ucb/quota";	/* warn user about quotas */
char	CANTRUN[] =	"login: Can't run ";

char	nolog[] =	"/etc/nologin";
char	qlog[]  =	".hushlogin";
char	securetty[] =	"/etc/securetty";
char	maildir[30] =	"/usr/spool/mail/";
char	lastlog[] =	"/usr/adm/lastlog";
struct	passwd nouser = {"", "nope", -1, -1, -1, "", "", "", "" };
struct	sgttyb ttyb;
struct	utmp utmp;
char	minusnam[16] = "-";
/*
 * This bounds the time given to login.  We initialize it here
 * so it can be patched on machines where it's too small.
 */
int	timeout = 60;

char	homedir[64] = "HOME=";
char	shell[64] = "SHELL=";
char	term[64] = "TERM=";
char	user[20] = "USER=";
char	logname[23] = "LOGNAME=";

/* NOTE WELL extra zero at the end of envinit
   they are for possible TERMCAP */
char	*envinit[] =
    { homedir, shell,
      "PATH=:/usr/ucb:/bin:/usr/bin",
      user, logname,
      term,
      /* May be filled in by TERMCAP */
      0,
      0 };

#define LOG_SECURITY LOG_CRIT

struct	passwd *pwd;
struct	passwd *getpwnam();
char	*strcat(), *rindex(), *index();
int	setpwent();
int	timedout();
char	*ttyname();
char	*crypt();
char	*getpass();
char	*stypeof();
extern	char **environ;
extern	int errno;

struct	tchars tc = {
	CINTR, CQUIT, CSTART, CSTOP, CEOT, CBRK
};
struct	ltchars ltc = {
	CSUSP, CDSUSP, CRPRNT, CFLUSH, CWERASE, CLNEXT
};

int	rflag;
char	rusername[NMAX+1], lusername[NMAX+1];
char	rpassword[NMAX+1];
char	name[NMAX+1];
char	*rhost;

main(argc, argv)
	char *argv[];
{
	register char *namep;
	int t, f, c, i;
	int invalid, quietlog;
/* loseit */
        int ttysecure = 0;
/* loseit */
	FILE *nlfd;
	char *ttyn, *tty;
	int ldisc = NTTYDISC, zero = 0;
	int locl = LCRTBS|LCTLECH|LDECCTQ;

	signal(SIGALRM, timedout);
	alarm(timeout);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	setpriority(PRIO_PROCESS, 0, 0);
	for (t = getdtablesize(); t > 3; t--)
		close(t);
	/*
	 * -r is used by rlogind to cause the autologin protocol;
	 * -h is used by other servers to pass the name of the
	 * remote host to login so that it may be placed in utmp and wtmp
	 */
	if (argc > 1) {
		if (strcmp(argv[1], "-r") == 0) {
			rflag = doremotelogin(argv[2]);
			SCPYN(utmp.ut_host, argv[2]);
			argc = 0;
		}
		if (strcmp(argv[1], "-h") == 0 && getuid() == 0) {
			SCPYN(utmp.ut_host, argv[2]);
			argc = 0;
		}
	}
	ioctl(0, TIOCLSET, &zero);
	ioctl(0, TIOCNXCL, 0);
	ioctl(0, FIONBIO, &zero);
	ioctl(0, FIOASYNC, &zero);
	ioctl(0, TIOCGETP, &ttyb);
	/*
	 * If talking to an rlogin process,
	 * propagate the terminal type and
	 * baud rate across the network.
	 */
	if (rflag)
		doremoteterm(term, &ttyb);
	if (ttyb.sg_ospeed >= B1200)
		locl |= LCRTERA|LCRTKIL;
	ioctl(0, TIOCLSET, &locl);
	ioctl(0, TIOCSLTC, &ltc);
	ioctl(0, TIOCSETC, &tc);
	ioctl(0, TIOCSETP, &ttyb);
	ttyn = ttyname(0);
	if (ttyn == (char *)0)
		ttyn = "/dev/tty??";
	tty = rindex(ttyn, '/');
	if (tty == NULL)
          tty = ttyn;
	else
          tty++;
/* loseit */
        t = 0;
/* loseit */
	do {
		ldisc = 0;
		ioctl(0, TIOCSETD, &ldisc);
		invalid = FALSE;
		SCPYN(utmp.ut_name, "");
		/*
		 * Name specified, take it.
		 */
		if (argc > 1) {
			SCPYN(utmp.ut_name, argv[1]);
			argc = 0;
		}
		/*
		 * If remote login take given name,
		 * otherwise prompt user for something.
		 */
		if (rflag) {
			SCPYN(utmp.ut_name, lusername);
			/* autologin failed, prompt for passwd */
			if (rflag == -1)
				rflag = 0;
		} else
			getloginname(&utmp);
		if (!strcmp(pwd->pw_shell, "/bin/csh")) {
			ldisc = NTTYDISC;
			ioctl(0, TIOCSETD, &ldisc);
		}
		/*
		 * If no remote login authentication and
		 * a password exists for this user, prompt
		 * for one and verify it.
		 */
/* loseit */
		ttysecure = rootterm(tty); /* See if tty believed secure */
/* loseit */

		if (!rflag && (*pwd->pw_passwd != '\0'
/* loseit */
                               /* Always prompt for password if insecure */
                               || !ttysecure
/* loseit */
                               )) {
			char *pp;

			setpriority(PRIO_PROCESS, 0, -4);
			pp = getpass("Password:");
			namep = crypt(pp, pwd->pw_passwd);
			setpriority(PRIO_PROCESS, 0, 0);
/* loseit */
#ifdef PARANOIA
			/* If not secure check that password is long enough
			   and don't allow no-password logins */
			if (!ttysecure) {
			  if (strlen(pp)<6) invalid = TRUE;
			  if (*pwd->pw_passwd == '\0') invalid = TRUE;
			}
#endif /* loseit PARANOIA */
			if (strcmp(namep, pwd->pw_passwd))
			  invalid = TRUE;
		}
		/*
		 * If user not super-user, check for logins disabled.
		 */
		if (pwd->pw_uid != 0 && (nlfd = fopen(nolog, "r")) > 0) {
			while ((c = getc(nlfd)) != EOF)
				putchar(c);
			fflush(stdout);
			sleep(5);
			exit(0);
		}
		/*
		 * If valid so far and root is logging in,
		 * see if root logins on this terminal are permitted.
		 */
		if (!invalid && pwd->pw_uid == 0 &&
		    !ttysecure) {
			syslog(LOG_SECURITY, "ROOT LOGIN REFUSED %s", tty);
			invalid = TRUE;
		}
		if (invalid) {
			printf("Login incorrect\n");
/* loseit */
			if (++t >= 5)
                          {
                            syslog(LOG_SECURITY,
                                   "REPEATED LOGIN FAILURES %s, %s",
                                   tty, utmp.ut_name);
                            ioctl(0, TIOCHPCL, (struct sgttyb *) 0);
                            close(0), close(1), close(2);
                            sleep(10);
                            exit(1);
                          }
/* loseit */
		}
		if (*pwd->pw_shell == '\0')
			pwd->pw_shell = "/bin/sh";
		if (chdir(pwd->pw_dir) < 0 && !invalid ) {
			if (chdir("/") < 0) {
				printf("No directory!\n");
				invalid = TRUE;
			} else {
				printf("No directory! %s\n",
				   "Logging in with home=/");
				pwd->pw_dir = "/";
			}
		}
		/*
		 * Remote login invalid must have been because
		 * of a restriction of some sort, no extra chances.
		 */
		if (rflag && invalid)
			exit(1);
	} while (invalid);
/* committed to login turn off timeout */
	alarm(0);
	time(&utmp.ut_time);
	t = ttyslot();
	if (t > 0 && (f = open("/etc/utmp", O_WRONLY)) >= 0) {
		lseek(f, (long)(t*sizeof(utmp)), 0);
		SCPYN(utmp.ut_line, tty);
		write(f, (char *)&utmp, sizeof(utmp));
		close(f);
	}
	if (t > 0 && (f = open("/usr/adm/wtmp", O_WRONLY|O_APPEND)) >= 0) {
		write(f, (char *)&utmp, sizeof(utmp));
		close(f);
	}
	quietlog = access(qlog, F_OK) == 0;
	if ((f = open(lastlog, O_RDWR)) >= 0) {
		struct lastlog ll;

		lseek(f, (long)pwd->pw_uid * sizeof (struct lastlog), 0);
		if (read(f, (char *) &ll, sizeof ll) == sizeof ll &&
		    ll.ll_time != 0 && !quietlog) {
			printf("Last login: %.*s ",
			    24-5, (char *)ctime(&ll.ll_time));
			if (*ll.ll_host != '\0')
				printf("from %.*s\n",
				    sizeof (ll.ll_host), ll.ll_host);
			else
				printf("on %.*s\n",
				    sizeof (ll.ll_line), ll.ll_line);
		}
		lseek(f, (long)pwd->pw_uid * sizeof (struct lastlog), 0);
		time(&ll.ll_time);
		SCPYN(ll.ll_line, tty);
		SCPYN(ll.ll_host, utmp.ut_host);
		write(f, (char *) &ll, sizeof ll);
		close(f);
	}
	chown(ttyn, pwd->pw_uid, pwd->pw_gid);
	chmod(ttyn, 0622);
	setgid(pwd->pw_gid);
	strncpy(name, utmp.ut_name, NMAX);
	name[NMAX] = '\0';
	initgroups(name, pwd->pw_gid);
	setuid(pwd->pw_uid);
	strncat(homedir, pwd->pw_dir, sizeof(homedir)-6);
	strncat(shell, pwd->pw_shell, sizeof(shell)-7);
	if (term[strlen("TERM=")] == 0)
		strncat(term, stypeof(ttyn), sizeof(term)-6);
	strncat(user, pwd->pw_name, sizeof(user)-6);
	strncat(logname, pwd->pw_name, sizeof(user)-9);
	environ = envinit;
	if ((namep = rindex(pwd->pw_shell, '/')) == NULL)
		namep = pwd->pw_shell;
	else
		namep++;
	strcat(minusnam, namep);
	umask(022);
	if (tty[sizeof("tty")-1] == 'd')
		syslog(LOG_INFO, "DIALUP %s, %s", tty, pwd->pw_name);
	if (pwd->pw_uid == 0)
		syslog(LOG_SECURITY, "ROOT LOGIN %s", tty);
	if (!quietlog) {
		int pid, w;
                struct stat st;

		showmotd();
		strcat(maildir, pwd->pw_name);
		if (stat(maildir, &st) == 0 && st.st_size != 0)
			printf("You have %smail.\n",
				(st.st_mtime > st.st_atime) ? "new " : "");
		if ((pid = vfork()) == 0) {
			execl(QUOTAWARN, QUOTAWARN, (char *)0);
			write(2, CANTRUN, sizeof(CANTRUN));
			_perror(QUOTAWARN);
			_exit(127);
		} else if (pid == -1) {
			fprintf(stderr, CANTRUN);
			perror(QUOTAWARN);
		} else {
			while ((w = wait((int *)NULL)) != pid && w != -1)
				;
		}
	}
	signal(SIGALRM, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGTSTP, SIG_IGN);
	execlp(pwd->pw_shell, minusnam, (char *)0);
	perror(pwd->pw_shell);
	printf("No shell\n");
	exit(0);
}

getloginname(up)
	register struct utmp *up;
{
	register char *namep;
	char c;

	while (up->ut_name[0] == '\0') {
		namep = up->ut_name;
		printf("login: ");
		while ((c = getchar()) != '\n') {
			if (c == ' ')
				c = '_';
			if (c == EOF)
				exit(0);
			if (namep < up->ut_name+NMAX)
				*namep++ = c;
		}
	}
	strncpy(lusername, up->ut_name, NMAX);
	lusername[NMAX] = 0;
	setpwent();
	if ((pwd = getpwnam(lusername)) == NULL)
		pwd = &nouser;
	endpwent();
}

timedout()
{

	printf("Login timed out after %d seconds\n", timeout);
	exit(0);
}

int	stopmotd;
catch()
{

	signal(SIGINT, SIG_IGN);
	stopmotd++;
}

rootterm(tty)
	char *tty;
{
	register FILE *fd;
	char buf[100];

	if ((fd = fopen(securetty, "r")) == NULL)
		return(1);
	while (fgets(buf, sizeof buf, fd) != NULL) {
		buf[strlen(buf)-1] = '\0';
		if (strcmp(tty, buf) == 0) {
			fclose(fd);
			return(1);
		}
	}
	fclose(fd);
	return(0);
}

showmotd()
{
	FILE *mf;
	register c;

	signal(SIGINT, catch);
	if ((mf = fopen("/etc/motd","r")) != NULL) {
		while ((c = getc(mf)) != EOF && stopmotd == 0)
			putchar(c);
		fclose(mf);
	}
	signal(SIGINT, SIG_IGN);
}

#undef	UNKNOWN
#define UNKNOWN "su"

char *
stypeof(ttyid)
	char *ttyid;
{
	static char typebuf[16];
	char buf[50];
	register FILE *f;
	register char *p, *t, *q;

	if (ttyid == NULL)
		return (UNKNOWN);
	/*
	 * CJL's kludge to make supdup win: If TERM is supdup,
	 * preserve TERM and TERMCAP. SUPDUP sets them up the right way.
	 */
        t = (char *) getenv("TERM");

	if (t && !strcmp ("supdup", t))
          {
            p = (char *) getenv ("TERMCAP");
            if (p)
              {
                char **ep;
                for (ep = envinit; *ep; ep++);
                *ep = p-strlen("TERMCAP=");
              }
            return (t);
          }
	f = fopen("/etc/ttytype", "r");
	if (f == NULL)
		return (UNKNOWN);
	/* split off end of name */
	for (p = q = ttyid; *p != 0; p++)
		if (*p == '/')
			q = p + 1;

	/* scan the file */
	while (fgets(buf, sizeof buf, f) != NULL) {
		for (t = buf; *t != ' ' && *t != '\t'; t++)
			if (*t == '\0')
				goto next;
		*t++ = 0;
		while (*t == ' ' || *t == '\t')
			t++;
		for (p = t; *p > ' '; p++)
			;
		*p = 0;
		if (strcmp(q,t) == 0) {
			strcpy(typebuf, buf);
			fclose(f);
			return (typebuf);
		}
	next: ;
	}
	fclose (f);
	return (UNKNOWN);
}

doremotelogin(host)
	char *host;
{
	FILE *hostf;
	int first = 1;
	char domain[256];

	if (getdomainname(domain, sizeof(domain)) < 0) {
		fprintf(stderr, "login: getdomainname system call missing\n");
		goto bad;
	}
	getstr(rusername, sizeof (rusername), "remuser");
	getstr(lusername, sizeof (lusername), "locuser");
	getstr(term+5, sizeof(term)-5, "Terminal type");
	if (getuid()) {
		pwd = &nouser;
		goto bad;
	}
	setpwent();
	pwd = getpwnam(lusername);
	endpwent();
	if (pwd == NULL) {
		pwd = &nouser;
		goto bad;
	}
	hostf = pwd->pw_uid ? fopen("/etc/hosts.equiv", "r") : 0;
again:
	if (hostf) {
		char ahost[32];
		int hostmatch, usermatch;

		while (fgets(ahost, sizeof (ahost), hostf)) {
			char *user;

			if ((user = index(ahost, '\n')) != 0)
				*user++ = '\0';
			if ((user = index(ahost, ' ')) != 0)
				*user++ = '\0';
			if (ahost[0] == '+' && ahost[1] == 0)
				hostmatch = 1;
			else if (ahost[0] == '+' && ahost[1] == '@')
				hostmatch = innetgr(ahost + 2, host,
				    (char *)NULL, domain);
			else if (ahost[0] == '-' && ahost[1] == '@') {
				if (innetgr(ahost+2, host, (char *)NULL,
				    domain))
					break;
			}
			else if (ahost[0] == '-') {
				if (!strcmp(host, ahost+1))
					break;
			}
			else
				hostmatch = !strcmp(host, ahost);
			if (user) {
				if (user[0] == '+' && user[1] == 0)
					usermatch = 1;
				else if (user[0] == '+' && user[1] == '@')
					usermatch = innetgr(user+2, (char *)NULL,
					    rusername, domain);
				else if (user[0] == '-' && user[1] == '@') {
					if (innetgr(user+2, (char *)NULL,
					    rusername, domain))
						break;
				}
				else if (user[0] == '-') {
					if (!strcmp(user+1, rusername))
						break;
				}
				else
					usermatch = !strcmp(user, rusername);
			}
			else
				usermatch = !strcmp(rusername, lusername);
			if (hostmatch && usermatch) {
				fclose(hostf);
				return (1);
			}
		}
		fclose(hostf);
	}
	if (first == 1) {
		char *rhosts = ".rhosts";
		struct stat sbuf;

		first = 0;
		if (chdir(pwd->pw_dir) < 0)
			goto again;
		if (lstat(rhosts, &sbuf) < 0)
			goto again;
		if ((sbuf.st_mode & S_IFMT) == S_IFLNK) {
			printf("login: .rhosts is a soft link.\r\n");
			goto bad;
		}
		hostf = fopen(rhosts, "r");
		fstat(fileno(hostf), &sbuf);
		if (sbuf.st_uid && sbuf.st_uid != pwd->pw_uid) {
			printf("login: Bad .rhosts ownership.\r\n");
			fclose(hostf);
			goto bad;
		}
		goto again;
	}
bad:
	return (-1);
}

getstr(buf, cnt, err)
	char *buf;
	int cnt;
	char *err;
{
	char c;

	do {
		if (read(0, &c, 1) != 1)
			exit(1);
		if (--cnt < 0) {
			printf("%s too long\r\n", err);
			exit(1);
		}
		*buf++ = c;
	} while (c != 0);
}

char	*speeds[] =
    { "0", "50", "75", "110", "134", "150", "200", "300",
      "600", "1200", "1800", "2400", "4800", "9600", "19200", "38400" };
#define	NSPEEDS	(sizeof (speeds) / sizeof (speeds[0]))

doremoteterm(term, tp)
	char *term;
	struct sgttyb *tp;
{
	char *cp = index(term, '/');
	register int i;

	if (cp) {
		*cp++ = 0;
		for (i = 0; i < NSPEEDS; i++)
			if (!strcmp(speeds[i], cp)) {
				tp->sg_ispeed = tp->sg_ospeed = i;
				break;
			}
	}
	tp->sg_flags = ECHO|CRMOD|ANYP|XTABS;
}

#ifdef 0
/* Use syslog instead */
logerr(fmt, a1, a2, a3)
	char *fmt, *a1, *a2, *a3;
{
#ifdef LOGERR
	FILE *cons = fopen("/dev/console", "w");

	if (cons != NULL) {
		fprintf(cons, fmt, a1, a2, a3);
		fprintf(cons, "\n\r");
		fclose(cons);
	}
#endif
}
#endif /* 0 */
