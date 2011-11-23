#!/bin/sh
# Run this to generate all the initial makefiles, etc.

# LIBTOOLIZE=${LIBTOOLIZE:-libtoolize}
LIBTOOLIZE_FLAGS="--automake --copy --force"
# ACLOCAL=${ACLOCAL:-aclocal}
# ACLOCAL_FLAGS=""
# AUTOHEADER=${AUTOHEADER:-autoheader}
# AUTOHEADER_FLAGS=""
# AUTOMAKE=${AUTOMAKE:-automake}
AUTOMAKE_FLAGS="--add-missing --copy --foreign"
# AUTOCONF=${AUTOCONF:-autoconf}
# AUTOCONF_FLAGS=""

ARGV0=$0
ARGS="$@"

run() {
    echo "$ARGV0: running \`$@' $ARGS"
    $@ $ARGS
}

if test x`which which 2>/dev/null` = x; then
  echo "which program not found, exiting"; exit 1
fi

if test x`which pkg-config 2>/dev/null` = x; then
  echo "pkg-config not found, exiting"; exit 1
fi

## jump out if one of the programs returns 'false'
set -e

## on macosx glibtoolize, others have libtool
if test x$LIBTOOLIZE = x; then
  if test \! x`which glibtoolize 2> /dev/null` = x; then
    LIBTOOLIZE=glibtoolize
  elif test \! x`which libtoolize-1.5 2> /dev/null` = x; then
    LIBTOOLIZE=libtoolize-1.5
  elif test \! x`which libtoolize 2> /dev/null` = x; then
    LIBTOOLIZE=libtoolize
  else 
    echo "libtoolize 1.5.x wasn't found, exiting"; exit 1
  fi
fi

## suse has aclocal and aclocal-1.9
if test x$ACLOCAL = x; then
  if test \! x`which aclocal-1.9 2> /dev/null` = x; then
    ACLOCAL=aclocal-1.9
  elif test \! x`which aclocal19 2> /dev/null` = x; then
    ACLOCAL=aclocal19
  elif test \! x`which aclocal 2> /dev/null` = x; then
    ACLOCAL=aclocal
  else 
    echo "automake 1.9.x (aclocal) wasn't found, exiting"; exit 1
  fi
fi

if test x$AUTOMAKE = x; then
  if test \! x`which automake-1.9 2> /dev/null` = x; then
    AUTOMAKE=automake-1.9
  elif test \! x`which automake19 2> /dev/null` = x; then
    AUTOMAKE=automake19
  elif test \! x`which automake 2> /dev/null` = x; then
    AUTOMAKE=automake
  else 
    echo "automake 1.9.x wasn't found, exiting"; exit 1
  fi
fi

## macosx has autoconf-2.59 and autoconf-2.60
if test x$AUTOCONF = x; then
  if test \! x`which autoconf-2.59 2> /dev/null` = x; then
    AUTOCONF=autoconf-2.59
  elif test \! x`which autoconf259 2> /dev/null` = x; then
    AUTOCONF=autoconf259
  elif test \! x`which autoconf 2> /dev/null` = x; then
    AUTOCONF=autoconf
  else 
    echo "autoconf 2.59+ wasn't found, exiting"; exit 1
  fi
fi

if test x$AUTOHEADER = x; then
  if test \! x`which autoheader-2.59 2> /dev/null` = x; then
    AUTOHEADER=autoheader-2.59
  elif test \! x`which autoheader259 2> /dev/null` = x; then
    AUTOHEADER=autoheader259
  elif test \! x`which autoheader 2> /dev/null` = x; then
    AUTOHEADER=autoheader
  else 
    echo "autoconf 2.59+ (autoheader) wasn't found, exiting"; exit 1
  fi
fi

run $LIBTOOLIZE $LIBTOOLIZE_FLAGS
run $ACLOCAL $ACLOCAL_FLAGS
run $AUTOHEADER $AUTOHEADER_FLAGS
run $AUTOMAKE $AUTOMAKE_FLAGS
run $AUTOCONF $AUTOCONF_FLAGS
cd argp-standalone;./autogen.sh
test "$ARGS" = "" && echo "Now type './configure ...' and 'make' to compile."
