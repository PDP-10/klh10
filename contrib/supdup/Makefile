# Makefile for the supdup server and user.

# Definitions for supdupd.
# TERMINFO	Does setup for terminfo.
# TTYLOC	Sets your ttyloc from supdup.  The code included is specific
#		to borax.  You would probably want to replace the code in
#		those sections with whatever you do locally for ttyloc.
# BANNER
# SBANNER
# DEBUG
#
# Definitions for supdup.
# TERMCAP	Uses the termcap database.
# TERMINFO	Uses the terminfo database.  Exactly one of TERMCAP or
#		TERMINFO must be defined and the corresponding library
#		(-ltermcap or -lterminfo) must be linked in.
# DEBUG

# This printf string is send as the supdup server's greeting.
# Its `arguments' are the local and remote host names.
# It should output ASCII
sbanner = "%s SUPDUP from %s.\r\nBugs to bug-unix-supdup@ai.mit.edu"

# This printf string is sent before /bin/login is invoked.
# Its `argument' is the local host name.
# It should output SUPDUP codes (eg #o207 rather than #o15 #o10 for newline)
banner ="SunOS Unix (%s)\207%s"

#Uncomment next line if your host isn't running in the ancient past
#lresolv = -lresolv
lresolv =

all: supdup supdupd supdup-login

supdup: supdup.c termcaps.h
	cc -g -o supdup -DTERMCAP supdup.c -ltermcap ${lresolv}

supdupd: supdupd.c supdup.h
	cc -g -o supdupd supdupd.c ${lresolv} '-DBANNER=${banner}' '-DSBANNER=${sbanner}' -DMIT -DTTYLOC -DKEEPALIVE -DBSD4_3

#in.supdupd: supdupd

supdup-login: supdup-login.c
	cc -g -o supdup-login supdup-login.c
