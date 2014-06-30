dnl Process this file with autoconf to produce a configure script.

dnl This configure.ac is only for building a standalone argp library.
AC_INIT([argp], [standalone-1.3])
AC_PREREQ(2.54)
AC_CONFIG_SRCDIR([argp-ba.c])
# Needed to stop autoconf from looking for files in parent directories.
AC_CONFIG_AUX_DIR([.])

AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS(config.h)

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES(yes)])

# GNU libc defaults to supplying the ISO C library functions only. The
# _GNU_SOURCE define enables these extensions, in particular we want
# errno.h to declare program_invocation_name. Enable it on all
# systems; no problems have been reported with it so far.
AC_GNU_SOURCE

# Checks for programs.
AC_PROG_CC
AC_PROG_MAKE_SET
AC_PROG_RANLIB
AC_PROG_CC

if test "x$am_cv_prog_cc_stdc" = xno ; then
  AC_ERROR([the C compiler doesn't handle ANSI-C])
fi

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(libintl.h limits.h malloc.h unistd.h sysexits.h stdarg.h)

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T

LSH_GCC_ATTRIBUTES

# Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_VPRINTF
AC_CHECK_FUNCS(strerror sleep getpid snprintf)

AC_REPLACE_FUNCS(mempcpy strndup strchrnul strcasecmp vsnprintf)

dnl ARGP_CHECK_FUNC(includes, function-call [, if-found [, if-not-found]])
AC_DEFUN([ARGP_CHECK_FUNC],
  [AS_VAR_PUSHDEF([ac_func], m4_substr([$2], 0, m4_index([$2], [(])))
   AS_VAR_PUSHDEF([ac_var], [ac_cv_func_call_]ac_func)
   AH_TEMPLATE(AS_TR_CPP(HAVE_[]ac_func),
               [Define to 1 if you have the `]ac_func[' function.])
   AC_CACHE_CHECK([for $2], ac_var,
     [AC_TRY_LINK([$1], [$2],
                  [AS_VAR_SET(ac_var, yes)],
                  [AS_VAR_SET(ac_var, no)])])
   if test AS_VAR_GET(ac_var) = yes ; then
     ifelse([$3],,
            [AC_DEFINE_UNQUOTED(AS_TR_CPP(HAVE_[]ac_func))],
            [$3
])
   else
     ifelse([$4],, true, [$4])
   fi
   AS_VAR_POPDEF([ac_var])
   AS_VAR_POPDEF([ac_func])
   ])

# At least on freebsd, putc_unlocked is a macro, so the standard
# AC_CHECK_FUNCS doesn't work well.
ARGP_CHECK_FUNC([#include <stdio.h>], [putc_unlocked('x', stdout)])

AC_CHECK_FUNCS(flockfile)
AC_CHECK_FUNCS(fputs_unlocked fwrite_unlocked)

# Used only by argp-test.c, so don't use AC_REPLACE_FUNCS.
AC_CHECK_FUNCS(strdup asprintf)

AC_CHECK_DECLS([program_invocation_name, program_invocation_short_name],
               [], [], [[#include <errno.h>]])

# Set these flags *last*, or else the test programs won't compile
if test x$GCC = xyes ; then
  # Using -ggdb3 makes (some versions of) Redhat's gcc-2.96 dump core
  if "$CC" --version | grep '^2\.96$' 1>/dev/null 2>&1; then
    true
  else
    CFLAGS="$CFLAGS -ggdb3"
  fi
  CFLAGS="$CFLAGS -Wall -W \
 -Wmissing-prototypes -Wmissing-declarations -Wstrict-prototypes \
 -Waggregate-return \
 -Wpointer-arith -Wbad-function-cast -Wnested-externs"
fi

CPPFLAGS="$CPPFLAGS -I$srcdir"

dnl Added for C99 standards
CFLAGS="$CFLAGS -std=gnu89 -static"

AC_OUTPUT(Makefile)
