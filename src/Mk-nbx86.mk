# KLH10 Makefile for NetBSD on i386
# $Id: Mk-nbx86.mk,v 2.5 2002/04/26 05:56:48 klh Exp $
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

# Local config setup, for BSD "make"!
#	Recursively invokes make with right params for local platform.

# Build definitions
SRC = ../../src
CFLAGS = -c -g3 -O3
CFLAGS_LINT = -ansi -pedantic -Wall -Wshadow \
		-Wstrict-prototypes -Wmissing-prototypes \
		-Wmissing-declarations -Wredundant-decls

# Source definitions
CENVFLAGS = -DCENV_CPU_I386=1 -DCENV_SYS_NETBSD=1 -include netbsd-sucks.h

# Any target with no customized rule here is simply passed on to the
# standard Makefile.  If no target is specified, "usage" is passed on
# to generate a helpful printout.

usage:
	@make -f $(SRC)/Makefile.mk usage

install:
	@make -f $(SRC)/Makefile.mk install-unix

$(.TARGETS): netbsd-sucks.h
	@make -f $(SRC)/Makefile.mk $@ \
	    "SRC=$(SRC)" \
	    "CFLAGS=$(CFLAGS)" \
	    "CFLAGS_LINT=$(CFLAGS_LINT)" \
	    "CENVFLAGS=$(CENVFLAGS)"

# This auxiliary file is needed to get around a bug in the NetBSD
# /usr/include files.  <stdio.h> includes <sys/types.h> which includes
# <machine/types.h> which incorrectly exposes a typedef of vaddr_t (normally
# a kernel only type), thus conflicting with KLH10's vaddr_t.
# By including this file ahead of any other source files (see the -include
# in CENVFLAGS) we can nullify the typedef.
# And while we're at it, blast paddr_t for the same reason.

netbsd-sucks.h:
	@echo '/* DO NOT EDIT - dynamically generated, see Makefile */' > $@
	@echo "#define vaddr_t _kernel_vaddr_t" >> $@
	@echo "#define paddr_t _kernel_paddr_t" >> $@
	@echo "#include <sys/types.h>" >> $@
	@echo "#undef paddr_t" >> $@
	@echo "#undef vaddr_t" >> $@
