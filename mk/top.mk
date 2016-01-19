# KLH10 Makefile for @KLH10S_CENV_SYS_@ on @KLH10S_CENV_CPU_@
#
#  Copyright © 2016 Olaf 'Rhialto' Seibert
#  All Rights Reserved
#
#  This file is part of the KLH10 Distribution.  Use, modification, and
#  re-distribution is permitted subject to the terms in the file
#  named "LICENSE", which contains the full text of the legal notices
#  and should always accompany this Distribution.
#
#  This software is provided "AS IS" with NO WARRANTY OF ANY KIND.
#
#  This notice (including the copyright and warranty disclaimer)
#  must be included in all copies or derivations of this software.
#
#####################################################################

SRC = @top_srcdir@/src
BLDSRC = @top_builddir@/src
CC = @CC@
CFLAGS = -c @CFLAGS@ -I$(BLDSRC) -I$(SRC)
CFLAGS_LINT = -ansi -pedantic -Wall -Wshadow \
                -Wstrict-prototypes -Wmissing-prototypes \
                -Wmissing-declarations -Wredundant-decls
LIBS = @LIBS@
NETLIBS = @NETLIBS@

prefix = @prefix@
exec_prefix = @exec_prefix@
KLH10_HOME = ${DESTDIR}@bindir@

# Source definitions
CENVFLAGS = @CENVFLAGS@
MAKEFILE = @MAKEFILE@

#---
