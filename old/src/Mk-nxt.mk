# KLH10 Makefile for NeXT on M68x
# $Id: Mk-nxt.mk,v 2.4 2001/11/10 21:28:59 klh Exp $
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

#####
# WARNING: This platform is no longer supported!  Its makefile is
# retained only as a guide in case anyone wants to re-port it.
#####

# Local config setup.
#	Recursively invokes make with right params for local platform.

# Build definitions
CC=gcc
CFLAGS = -c -finline-functions -fomit-frame-pointer -O -O2 -pipe
SRC = ../../src
CFLAGS_LINT = 

# Source definitions
CENVFLAGS = -DCENV_CPU_M68=1 -DCENV_SYS_NEXT=1

BASELIST = ks-t20

default:
	@echo "Must specify a target, one of \"$(BASELIST)\""

$(BASELIST):
	make -f $(SRC)/Makefile.mk kn10-ks wfconv tapedd vdkfmt
	    "SRC=$(SRC)" "CENVFLAGS=$(CENVFLAGS)"
	    "CONFFLAGS =
		-DWORD10_USEGCCSPARC=1	\
		-DKLH10_CPU_KS=1	\
		-DKLH10_SYS_T20=1	\
		$(TSYNCFLAGS) -DKLH10_CTYIO_INT=0" 
