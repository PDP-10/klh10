# KLH10 Makefile for OSF/1 (DU, Tru64) on Alpha
# $Id: Mk-osfaxp.mk,v 2.3 2001/11/10 21:28:59 klh Exp $
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

# Local config setup, for OSF1/DU/Tru64 "make"!
#	Recursively invokes make with right params for local platform.

# Build definitions
#	librt.a is necessary in order to get memlk (mlockall).
#	May also want -non_shared in LDFLAGS to avoid OSF version problems.
SRC = ../../src
CFLAGS = -c -g3 -O -std1 -I. -I$(SRC)
CFLAGS_LINT =
LDFLAGS = 
LIBS = -lrt

# Source definitions
CENVFLAGS = -DCENV_CPU_ALPHA=1 -DCENV_SYS_DECOSF=1

# Targets

# Any target with no customized rule here is simply passed on to the
# standard Makefile.  If no target is specified, "usage" is passed on
# to generate a helpful printout.

usage .DEFAULT:
	@make -f $(SRC)/Makefile.mk $@ \
	    "SRC=$(SRC)" \
	    "CFLAGS=$(CFLAGS)" \
	    "CFLAGS_LINT=$(CFLAGS_LINT)" \
	    "CENVFLAGS=$(CENVFLAGS)" \
	    "LDFLAGS=$(LDFLAGS)" \
	    "LIBS=$(LIBS)"

install:
	make -f $(SRC)/Makefile.mk install-unix

