#!/bin/sh
#
# autogen.sh
#
# Usage: ../path/to/autogen.sh [args for configure]
#
# (Re)constructs the configure script using the appropriate invocations
# of autoconf etc, and runs configure with the given arguments.
#
# It is strongly recommended to build in a separate directory
# for easy cleanup.

srcdir=$(dirname $0)
test -z "$srcdir" && srcdir=.

builddir=$(pwd)
cd "$srcdir"

autoconf -I aclocal
autoheader

cd "$builddir"
$srcdir/configure "$@"
