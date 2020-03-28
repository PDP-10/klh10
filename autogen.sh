#!/bin/sh
#
# autogen.sh
#
# Usage: ../path/to/autogen.sh [args for configure]
#
# (Re)constructs the configure script using the appropriate invocations
# of autoconf etc.
#
# It is strongly recommended to build in a separate directory
# for easy cleanup.
#
# When this is done, it runs configure with the given arguments.

srcdir=$(dirname $0)
test -z "$srcdir" && srcdir=.

builddir=$(pwd)
cd "$srcdir"
srcdir=$(pwd)

aclocal         # creates aclocal.m4 from configure.ac
autoconf	# creates configure from configure.ac
autoheader	# creates src/config.h.in from configure.ac

if [ "$srcdir" != "$builddir" ]
then
    echo "Now running configure $@ in $builddir"
    cd "$builddir"
    $srcdir/configure "$@"
else
    echo "Not running configure in the source directory."
fi
