# Makefile to build in the subdirectories.
# Designed to be both BSD Make and GNU Make compatible
# (as are all the other Makefiles).
#

# The RECURSE macro uses a shell variable $TARGET for delayed expansion.
RECURSE=for d in bld-* ; \
	do \
	    ${MAKE} -C $$d $${TARGET} ; \
	done

# This carefully crafted rule will run Make in all subdirectories with
# the same target, no matter what it is (as long as it is just one
# target).
#
# .TARGETS is a BSD specific variable, and MAKECMDGOALS is the GNU version.
# .TARGETS becomes "rEmOvEmE" when no targets are given on the
# command line (because this is the default target).
# MAKECMDGOALS remains empty in that case.
# Do not use "all" as the extra target, since that fails when that is
# given as target on the command line (you get duplicate targets).
# The shell variable substitution removes "rEmOvEmE" if present.
# So when no target is given, the recursion uses no target either.

${.TARGETS} ${MAKECMDGOALS} rEmOvEmE :
#	set -x; T="${.TARGETS}${MAKECMDGOALS}"; echo "T=$$T"; TARGET="$${T%%rEmOvEmE}"; echo "TARGET=$$TARGET"
	T="${.TARGETS}${MAKECMDGOALS}"; TARGET="$${T%%rEmOvEmE}"; ${RECURSE}

