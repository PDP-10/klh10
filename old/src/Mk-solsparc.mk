# KLH10 Makefile for Solaris on SUN Sparc
# $Id: Mk-solsparc.mk,v 2.6 2002/04/24 18:25:59 klh Exp $
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

# Local config setup, for GNU "make"!
#	Recursively invokes make with right params for local platform.

# Note: this makefile forces the use of GCC; SUN C 4.2 proved to have
# bugs.  For a makefile that does use the SUN compiler, use
# Mk-solsparc-cc.mk instead.

# Build definitions
#	-lsocket and -lnsl are needed only for osdnet.c.
#	-lrt is needed for nanosleep().
CC=gcc
CFLAGS = -c -g -O3
LIBS = -lsocket -lnsl -lrt
CFLAGS_LINT = -ansi -pedantic -Wall -Wshadow \
		-Wstrict-prototypes -Wmissing-prototypes \
		-Wmissing-declarations -Wredundant-decls
CONFFLAGS_AUX= -DWORD10_USEGCCSPARC=1

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
