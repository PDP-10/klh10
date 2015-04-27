#include <time.h>
#include <sys/utsname.h>

typedef unsigned int bool;
#  define false 0
#  define true  1

int sy_getgmtoffset(void)
{
  struct tm* tm;
  time_t now;
  static bool firsttime = true;
  static int offset;

  if (firsttime) {
    now = time(NULL);
    tm = localtime(&now);
    offset = timegm(tm) - now;
    firsttime = false;
  }

  return (offset);
}

char* sy_getsystem(void)
{
  static struct utsname unameinfo;
  static bool firsttime = true;

  if (firsttime) {
    (void) uname(&unameinfo);	/* Get sysname etc. */
    firsttime = false;
  }

  return(unameinfo.sysname);
}
