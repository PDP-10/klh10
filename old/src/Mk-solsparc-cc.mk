# KLH10 Makefile for Solaris on SUN Sparc (using SUN's cc)
# $Id: Mk-solsparc-cc.mk,v 2.3 2002/04/24 08:03:02 klh Exp $
#
#  Copyright © 2001 Kenneth L. Harrenstien
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

# Local config setup, for SUN's make & compiler.
#	Recursively invokes make with right params for local platform.

# WARNING!  DO NOT USE THIS for SUN C 4.2 and possibly others.

# Note: these simple compile flags are known to work for Solaris 5 and 8
# (SunOS 5.5.1 and 5.8); using "-fast" fails on 5.8.
# Libraries are harder; -lrt doesn't exist on 5.5.1 and will have to
# be removed or modified if not building for 5.8.
#	-lsocket and -lnsl are needed only for osdnet.c.
#	-lrt is needed for nanosleep().

# Build definitions
#	These LIBS are needed only for things using osdnet.c.
CC = /opt/SUNWspro/bin/cc
CFLAGS = -c -g -O
LIBS = -lsocket -lnsl -lrt
CONFFLAGS_AUX=-DWORD10_USEHUN=1

# Source definitions
SRC = ../../src
CENVFLAGS = -DCENV_CPU_SPARC=1 -DCENV_SYS_SOLARIS=1 \
		-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE

# Targets

# Any target with no customized rule here is simply passed on to the
# standard Makefile.  If no target is specified, "usage" is passed on
# to generate a helpful printout.

usage .DEFAULT:
	@make -f $(SRC)/Makefile.mk $@ \
	    "CC=$(CC)" \
	    "SRC=$(SRC)" \
	    "CFLAGS=$(CFLAGS)" \
	    "CFLAGS_LINT=$(CFLAGS_LINT)" \
	    "CENVFLAGS=$(CENVFLAGS)" \
	    "CONFFLAGS_AUX=$(CONFFLAGS_AUX)" \
	    "LDFLAGS=$(LDFLAGS)" \
	    "LIBS=$(LIBS)"

install:
	make -f $(SRC)/Makefile.mk install-unix
