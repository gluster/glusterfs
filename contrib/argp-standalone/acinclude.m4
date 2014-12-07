dnl Try to detect the type of the third arg to getsockname() et al
AC_DEFUN([LSH_TYPE_SOCKLEN_T],
[AH_TEMPLATE([socklen_t], [Length type used by getsockopt])
AC_CACHE_CHECK([for socklen_t in sys/socket.h], ac_cv_type_socklen_t,
[AC_EGREP_HEADER(socklen_t, sys/socket.h,
  [ac_cv_type_socklen_t=yes], [ac_cv_type_socklen_t=no])])
if test $ac_cv_type_socklen_t = no; then
        AC_MSG_CHECKING(for AIX)
        AC_EGREP_CPP(yes, [
#ifdef _AIX
 yes
#endif
],[
AC_MSG_RESULT(yes)
AC_DEFINE(socklen_t, size_t)
],[
AC_MSG_RESULT(no)
AC_DEFINE(socklen_t, int)
])
fi
])

dnl Choose cc flags for compiling position independent code
AC_DEFUN([LSH_CCPIC],
[AC_MSG_CHECKING(CCPIC)
AC_CACHE_VAL(lsh_cv_sys_ccpic,[
  if test -z "$CCPIC" ; then
    if test "$GCC" = yes ; then
      case `uname -sr` in
	BSD/OS*)
         case `uname -r` in
           4.*) CCPIC="-fPIC";;
           *) CCPIC="";;
         esac
	;;
	Darwin*)
	  CCPIC="-fPIC"
	;;
	SunOS\ 5.*)
	  # Could also use -fPIC, if there are a large number of symbol reference
	  CCPIC="-fPIC"
	;;
	CYGWIN*)
	  CCPIC=""
	;;
	*)
	  CCPIC="-fpic"
	;;
      esac
    else
      case `uname -sr` in
	Darwin*)
	  CCPIC="-fPIC"
	;;
        IRIX*)
          CCPIC="-share"
        ;;
	hp*|HP*) CCPIC="+z"; ;;
	FreeBSD*) CCPIC="-fpic";;
	SCO_SV*) CCPIC="-KPIC -dy -Bdynamic";;
        UnixWare*|OpenUNIX*) CCPIC="-KPIC -dy -Bdynamic";;
	Solaris*) CCPIC="-KPIC -Bdynamic";;
	Windows_NT*) CCPIC="-shared" ;;
      esac
    fi
  fi
  OLD_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS $CCPIC"
  AC_TRY_COMPILE([], [exit(0);],
    lsh_cv_sys_ccpic="$CCPIC", lsh_cv_sys_ccpic='')
  CFLAGS="$OLD_CFLAGS"
])
CCPIC="$lsh_cv_sys_ccpic"
AC_MSG_RESULT($CCPIC)
AC_SUBST([CCPIC])])

dnl LSH_PATH_ADD(path-id, directory)
AC_DEFUN([LSH_PATH_ADD],
[AC_MSG_CHECKING($2)
ac_exists=no
if test -d "$2/." ; then
  ac_real_dir=`cd $2 && pwd`
  if test -n "$ac_real_dir" ; then
    ac_exists=yes
    for old in $1_REAL_DIRS ; do
      ac_found=no
      if test x$ac_real_dir = x$old ; then
        ac_found=yes;
	break;
      fi
    done
    if test $ac_found = yes ; then
      AC_MSG_RESULT(already added)
    else
      AC_MSG_RESULT(added)
      # LDFLAGS="$LDFLAGS -L $2"
      $1_REAL_DIRS="$ac_real_dir [$]$1_REAL_DIRS"
      $1_DIRS="$2 [$]$1_DIRS"
    fi
  fi
fi
if test $ac_exists = no ; then
  AC_MSG_RESULT(not found)
fi
])

dnl LSH_RPATH_ADD(dir)
AC_DEFUN([LSH_RPATH_ADD], [LSH_PATH_ADD(RPATH_CANDIDATE, $1)])

dnl LSH_RPATH_INIT(candidates)
AC_DEFUN([LSH_RPATH_INIT],
[AC_MSG_CHECKING([for -R flag])
RPATHFLAG=''
case `uname -sr` in
  OSF1\ V4.*)
    RPATHFLAG="-rpath "
    ;;
  IRIX\ 6.*)
    RPATHFLAG="-rpath "
    ;;
  IRIX\ 5.*)
    RPATHFLAG="-rpath "
    ;;
  SunOS\ 5.*)
    if test "$TCC" = "yes"; then
      # tcc doesn't know about -R
      RPATHFLAG="-Wl,-R,"
    else
      RPATHFLAG=-R
    fi
    ;;
  Linux\ 2.*)
    RPATHFLAG="-Wl,-rpath,"
    ;;
  *)
    :
    ;;
esac

if test x$RPATHFLAG = x ; then
  AC_MSG_RESULT(none)
else
  AC_MSG_RESULT([using $RPATHFLAG])
fi

RPATH_CANDIDATE_REAL_DIRS=''
RPATH_CANDIDATE_DIRS=''

AC_MSG_RESULT([Searching for libraries])

for d in $1 ; do
  LSH_RPATH_ADD($d)
done
])    

dnl Try to execute a main program, and if it fails, try adding some
dnl -R flag.
dnl LSH_RPATH_FIX
AC_DEFUN([LSH_RPATH_FIX],
[if test $cross_compiling = no -a "x$RPATHFLAG" != x ; then
  ac_success=no
  AC_TRY_RUN([int main(int argc, char **argv) { return 0; }],
    ac_success=yes, ac_success=no, :)
  
  if test $ac_success = no ; then
    AC_MSG_CHECKING([Running simple test program failed. Trying -R flags])
dnl echo RPATH_CANDIDATE_DIRS = $RPATH_CANDIDATE_DIRS
    ac_remaining_dirs=''
    ac_rpath_save_LDFLAGS="$LDFLAGS"
    for d in $RPATH_CANDIDATE_DIRS ; do
      if test $ac_success = yes ; then
  	ac_remaining_dirs="$ac_remaining_dirs $d"
      else
  	LDFLAGS="$RPATHFLAG$d $LDFLAGS"
dnl echo LDFLAGS = $LDFLAGS
  	AC_TRY_RUN([int main(int argc, char **argv) { return 0; }],
  	  [ac_success=yes
  	  ac_rpath_save_LDFLAGS="$LDFLAGS"
  	  AC_MSG_RESULT([adding $RPATHFLAG$d])
  	  ],
  	  [ac_remaining_dirs="$ac_remaining_dirs $d"], :)
  	LDFLAGS="$ac_rpath_save_LDFLAGS"
      fi
    done
    RPATH_CANDIDATE_DIRS=$ac_remaining_dirs
  fi
  if test $ac_success = no ; then
    AC_MSG_RESULT(failed)
  fi
fi
])

dnl Like AC_CHECK_LIB, but uses $KRB_LIBS rather than $LIBS.
dnl LSH_CHECK_KRB_LIB(LIBRARY, FUNCTION, [, ACTION-IF-FOUND [,
dnl                  ACTION-IF-NOT-FOUND [, OTHER-LIBRARIES]]])

AC_DEFUN([LSH_CHECK_KRB_LIB],
[AC_CHECK_LIB([$1], [$2],
  ifelse([$3], ,
      [[ac_tr_lib=HAVE_LIB`echo $1 | sed -e 's/[^a-zA-Z0-9_]/_/g' \
     	    -e 'y/abcdefghijklmnopqrstuvwxyz/ABCDEFGHIJKLMNOPQRSTUVWXYZ/'`
        AC_DEFINE_UNQUOTED($ac_tr_lib)
        KRB_LIBS="-l$1 $KRB_LIBS"
      ]], [$3]),
  ifelse([$4], , , [$4
])dnl
, [$5 $KRB_LIBS])
])

dnl LSH_LIB_ARGP(ACTION-IF-OK, ACTION-IF-BAD)
AC_DEFUN([LSH_LIB_ARGP],
[ ac_argp_save_LIBS="$LIBS"
  ac_argp_save_LDFLAGS="$LDFLAGS"
  ac_argp_ok=no
  # First check if we can link with argp.
  AC_SEARCH_LIBS(argp_parse, argp,
  [ LSH_RPATH_FIX
    AC_CACHE_CHECK([for working argp],
      lsh_cv_lib_argp_works,
      [ AC_TRY_RUN(
[#include <argp.h>
#include <stdlib.h>

static const struct argp_option
options[] =
{
  { NULL, 0, NULL, 0, NULL, 0 }
};

struct child_state
{
  int n;
};

static error_t
child_parser(int key, char *arg, struct argp_state *state)
{
  struct child_state *input = (struct child_state *) state->input;
  
  switch(key)
    {
    default:
      return ARGP_ERR_UNKNOWN;
    case ARGP_KEY_END:
      if (!input->n)
	input->n = 1;
      break;
    }
  return 0;
}

const struct argp child_argp =
{
  options,
  child_parser,
  NULL, NULL, NULL, NULL, NULL
};

struct main_state
{
  struct child_state child;
  int m;
};

static error_t
main_parser(int key, char *arg, struct argp_state *state)
{
  struct main_state *input = (struct main_state *) state->input;

  switch(key)
    {
    default:
      return ARGP_ERR_UNKNOWN;
    case ARGP_KEY_INIT:
      state->child_inputs[0] = &input->child;
      break;
    case ARGP_KEY_END:
      if (!input->m)
	input->m = input->child.n;
      
      break;
    }
  return 0;
}

static const struct argp_child
main_children[] =
{
  { &child_argp, 0, "", 0 },
  { NULL, 0, NULL, 0}
};

static const struct argp
main_argp =
{ options, main_parser, 
  NULL,
  NULL,
  main_children,
  NULL, NULL
};

int main(int argc, char **argv)
{
  struct main_state input = { { 0 }, 0 };
  char *v[2] = { "foo", NULL };

  argp_parse(&main_argp, 1, v, 0, NULL, &input);

  if ( (input.m == 1) && (input.child.n == 1) )
    return 0;
  else
    return 1;
}
], lsh_cv_lib_argp_works=yes,
   lsh_cv_lib_argp_works=no,
   lsh_cv_lib_argp_works=no)])

  if test x$lsh_cv_lib_argp_works = xyes ; then
    ac_argp_ok=yes
  else
    # Reset link flags
    LIBS="$ac_argp_save_LIBS"
    LDFLAGS="$ac_argp_save_LDFLAGS"
  fi])

  if test x$ac_argp_ok = xyes ; then
    ifelse([$1],, true, [$1])
  else
    ifelse([$2],, true, [$2])
  fi   
])

dnl LSH_GCC_ATTRIBUTES
dnl Check for gcc's __attribute__ construction

AC_DEFUN([LSH_GCC_ATTRIBUTES],
[AC_CACHE_CHECK(for __attribute__,
	       lsh_cv_c_attribute,
[ AC_TRY_COMPILE([
#include <stdlib.h>
],
[
static void foo(void) __attribute__ ((noreturn));

static void __attribute__ ((noreturn))
foo(void)
{
  exit(1);
}
],
lsh_cv_c_attribute=yes,
lsh_cv_c_attribute=no)])

AH_TEMPLATE([HAVE_GCC_ATTRIBUTE], [Define if the compiler understands __attribute__])
if test "x$lsh_cv_c_attribute" = "xyes"; then
  AC_DEFINE(HAVE_GCC_ATTRIBUTE)
fi

AH_BOTTOM(
[#if __GNUC__ || HAVE_GCC_ATTRIBUTE
# define NORETURN __attribute__ ((__noreturn__))
# define PRINTF_STYLE(f, a) __attribute__ ((__format__ (__printf__, f, a)))
# define UNUSED __attribute__ ((__unused__))
#else
# define NORETURN
# define PRINTF_STYLE(f, a)
# define UNUSED
#endif
])])

AC_DEFUN([LSH_GCC_FUNCTION_NAME],
[# Check for gcc's __FUNCTION__ variable
AH_TEMPLATE([HAVE_GCC_FUNCTION],
	    [Define if the compiler understands __FUNCTION__])
AH_BOTTOM(
[#if HAVE_GCC_FUNCTION
# define FUNCTION_NAME __FUNCTION__
#else
# define FUNCTION_NAME "Unknown"
#endif
])

AC_CACHE_CHECK(for __FUNCTION__,
	       lsh_cv_c_FUNCTION,
  [ AC_TRY_COMPILE(,
      [ #if __GNUC__ == 3
	#  error __FUNCTION__ is broken in gcc-3
	#endif
        void foo(void) { char c = __FUNCTION__[0]; } ],
      lsh_cv_c_FUNCTION=yes,
      lsh_cv_c_FUNCTION=no)])

if test "x$lsh_cv_c_FUNCTION" = "xyes"; then
  AC_DEFINE(HAVE_GCC_FUNCTION)
fi
])

# Check for alloca, and include the standard blurb in config.h
AC_DEFUN([LSH_FUNC_ALLOCA],
[AC_FUNC_ALLOCA
AC_CHECK_HEADERS([malloc.h])
AH_BOTTOM(
[/* AIX requires this to be the first thing in the file.  */
#ifndef __GNUC__
# if HAVE_ALLOCA_H
#  include <alloca.h>
# else
#  ifdef _AIX
 #pragma alloca
#  else
#   ifndef alloca /* predefined by HP cc +Olibcalls */
char *alloca ();
#   endif
#  endif
# endif
#else /* defined __GNUC__ */
# if HAVE_ALLOCA_H
#  include <alloca.h>
# endif
#endif
/* Needed for alloca on windows */
#if HAVE_MALLOC_H
# include <malloc.h>
#endif
])])

AC_DEFUN([LSH_FUNC_STRERROR],
[AC_CHECK_FUNCS(strerror)
AH_BOTTOM(
[#if HAVE_STRERROR
#define STRERROR strerror
#else
#define STRERROR(x) (sys_errlist[x])
#endif
])])

AC_DEFUN([LSH_FUNC_STRSIGNAL],
[AC_CHECK_FUNCS(strsignal)
AC_CHECK_DECLS([sys_siglist, _sys_siglist])
AH_BOTTOM(
[#if HAVE_STRSIGNAL
# define STRSIGNAL strsignal
#else /* !HAVE_STRSIGNAL */
# if HAVE_DECL_SYS_SIGLIST
#  define STRSIGNAL(x) (sys_siglist[x])
# else
#  if HAVE_DECL__SYS_SIGLIST
#   define STRSIGNAL(x) (_sys_siglist[x])
#  else
#   define STRSIGNAL(x) "Unknown signal"
#   if __GNUC__
#    warning Using dummy STRSIGNAL
#   endif
#  endif
# endif
#endif /* !HAVE_STRSIGNAL */
])])

dnl LSH_MAKE_CONDITIONAL(symbol, test)
AC_DEFUN([LSH_MAKE_CONDITIONAL],
[if $2 ; then
  IF_$1=''
  UNLESS_$1='# '
else
  IF_$1='# '
  UNLESS_$1=''
fi 
AC_SUBST(IF_$1)
AC_SUBST(UNLESS_$1)])

dnl LSH_DEPENDENCY_TRACKING

dnl Defines compiler flags DEP_FLAGS to generate dependency
dnl information, and DEP_PROCESS that is any shell commands needed for
dnl massaging the dependency information further. Dependencies are
dnl generated as a side effect of compilation. Dependency files
dnl themselves are not treated as targets.

AC_DEFUN([LSH_DEPENDENCY_TRACKING],
[AC_ARG_ENABLE(dependency_tracking,
  AC_HELP_STRING([--disable-dependency-tracking],
    [Disable dependency tracking. Dependency tracking doesn't work with BSD make]),,
  [enable_dependency_tracking=yes])

DEP_FLAGS=''
DEP_PROCESS='true'
if test x$enable_dependency_tracking = xyes ; then
  if test x$GCC = xyes ; then
    gcc_version=`gcc --version | head -1`
    case "$gcc_version" in
      2.*|*[[!0-9.]]2.*)
        enable_dependency_tracking=no
        AC_MSG_WARN([Dependency tracking disabled, gcc-3.x is needed])
      ;;
      *)
        DEP_FLAGS='-MT $[]@ -MD -MP -MF $[]@.d'
        DEP_PROCESS='true'
      ;;
    esac
  else
    enable_dependency_tracking=no
    AC_MSG_WARN([Dependency tracking disabled])
  fi
fi

if test x$enable_dependency_tracking = xyes ; then
  DEP_INCLUDE='include '
else
  DEP_INCLUDE='# '
fi

AC_SUBST([DEP_INCLUDE])
AC_SUBST([DEP_FLAGS])
AC_SUBST([DEP_PROCESS])])

dnl @synopsis AX_CREATE_STDINT_H [( HEADER-TO-GENERATE [, HEADERS-TO-CHECK])]
dnl
dnl the "ISO C9X: 7.18 Integer types <stdint.h>" section requires the
dnl existence of an include file <stdint.h> that defines a set of 
dnl typedefs, especially uint8_t,int32_t,uintptr_t.
dnl Many older installations will not provide this file, but some will
dnl have the very same definitions in <inttypes.h>. In other environments
dnl we can use the inet-types in <sys/types.h> which would define the
dnl typedefs int8_t and u_int8_t respectively.
dnl
dnl This macros will create a local "_stdint.h" or the headerfile given as 
dnl an argument. In many cases that file will just "#include <stdint.h>" 
dnl or "#include <inttypes.h>", while in other environments it will provide 
dnl the set of basic 'stdint's definitions/typedefs: 
dnl   int8_t,uint8_t,int16_t,uint16_t,int32_t,uint32_t,intptr_t,uintptr_t
dnl   int_least32_t.. int_fast32_t.. intmax_t
dnl which may or may not rely on the definitions of other files,
dnl or using the AC_CHECK_SIZEOF macro to determine the actual
dnl sizeof each type.
dnl
dnl if your header files require the stdint-types you will want to create an
dnl installable file mylib-int.h that all your other installable header
dnl may include. So if you have a library package named "mylib", just use
dnl      AX_CREATE_STDINT_H(mylib-int.h) 
dnl in configure.ac and go to install that very header file in Makefile.am
dnl along with the other headers (mylib.h) - and the mylib-specific headers
dnl can simply use "#include <mylib-int.h>" to obtain the stdint-types.
dnl
dnl Remember, if the system already had a valid <stdint.h>, the generated
dnl file will include it directly. No need for fuzzy HAVE_STDINT_H things...
dnl
dnl @, (status: used on new platforms) (see http://ac-archive.sf.net/gstdint/)
dnl @version $Id: acinclude.m4,v 1.27 2004/11/23 21:27:35 nisse Exp $
dnl @author  Guido Draheim <guidod@gmx.de> 

AC_DEFUN([AX_CREATE_STDINT_H],
[# ------ AX CREATE STDINT H -------------------------------------
AC_MSG_CHECKING([for stdint types])
ac_stdint_h=`echo ifelse($1, , _stdint.h, $1)`
# try to shortcircuit - if the default include path of the compiler
# can find a "stdint.h" header then we assume that all compilers can.
AC_CACHE_VAL([ac_cv_header_stdint_t],[
old_CXXFLAGS="$CXXFLAGS" ; CXXFLAGS=""
old_CPPFLAGS="$CPPFLAGS" ; CPPFLAGS=""
old_CFLAGS="$CFLAGS"     ; CFLAGS=""
AC_TRY_COMPILE([#include <stdint.h>],[int_least32_t v = 0;],
[ac_cv_stdint_result="(assuming C99 compatible system)"
 ac_cv_header_stdint_t="stdint.h"; ],
[ac_cv_header_stdint_t=""])
CXXFLAGS="$old_CXXFLAGS"
CPPFLAGS="$old_CPPFLAGS"
CFLAGS="$old_CFLAGS" ])

v="... $ac_cv_header_stdint_h"
if test "$ac_stdint_h" = "stdint.h" ; then
 AC_MSG_RESULT([(are you sure you want them in ./stdint.h?)])
elif test "$ac_stdint_h" = "inttypes.h" ; then
 AC_MSG_RESULT([(are you sure you want them in ./inttypes.h?)])
elif test "_$ac_cv_header_stdint_t" = "_" ; then
 AC_MSG_RESULT([(putting them into $ac_stdint_h)$v])
else
 ac_cv_header_stdint="$ac_cv_header_stdint_t"
 AC_MSG_RESULT([$ac_cv_header_stdint (shortcircuit)])
fi

if test "_$ac_cv_header_stdint_t" = "_" ; then # can not shortcircuit..

dnl .....intro message done, now do a few system checks.....
dnl btw, all CHECK_TYPE macros do automatically "DEFINE" a type, therefore
dnl we use the autoconf implementation detail _AC CHECK_TYPE_NEW instead

inttype_headers=`echo $2 | sed -e 's/,/ /g'`

ac_cv_stdint_result="(no helpful system typedefs seen)"
AC_CACHE_CHECK([for stdint uintptr_t], [ac_cv_header_stdint_x],[
 ac_cv_header_stdint_x="" # the 1997 typedefs (inttypes.h)
  AC_MSG_RESULT([(..)])
  for i in stdint.h inttypes.h sys/inttypes.h $inttype_headers ; do
   unset ac_cv_type_uintptr_t 
   unset ac_cv_type_uint64_t
   _AC_CHECK_TYPE_NEW(uintptr_t,[ac_cv_header_stdint_x=$i],dnl
     continue,[#include <$i>])
   AC_CHECK_TYPE(uint64_t,[and64="/uint64_t"],[and64=""],[#include<$i>])
   ac_cv_stdint_result="(seen uintptr_t$and64 in $i)"
   break;
  done
  AC_MSG_CHECKING([for stdint uintptr_t])
 ])

if test "_$ac_cv_header_stdint_x" = "_" ; then
AC_CACHE_CHECK([for stdint uint32_t], [ac_cv_header_stdint_o],[
 ac_cv_header_stdint_o="" # the 1995 typedefs (sys/inttypes.h)
  AC_MSG_RESULT([(..)])
  for i in inttypes.h sys/inttypes.h stdint.h $inttype_headers ; do
   unset ac_cv_type_uint32_t
   unset ac_cv_type_uint64_t
   AC_CHECK_TYPE(uint32_t,[ac_cv_header_stdint_o=$i],dnl
     continue,[#include <$i>])
   AC_CHECK_TYPE(uint64_t,[and64="/uint64_t"],[and64=""],[#include<$i>])
   ac_cv_stdint_result="(seen uint32_t$and64 in $i)"
   break;
  done
  AC_MSG_CHECKING([for stdint uint32_t])
 ])
fi

if test "_$ac_cv_header_stdint_x" = "_" ; then
if test "_$ac_cv_header_stdint_o" = "_" ; then
AC_CACHE_CHECK([for stdint u_int32_t], [ac_cv_header_stdint_u],[
 ac_cv_header_stdint_u="" # the BSD typedefs (sys/types.h)
  AC_MSG_RESULT([(..)])
  for i in sys/types.h inttypes.h sys/inttypes.h $inttype_headers ; do
   unset ac_cv_type_u_int32_t
   unset ac_cv_type_u_int64_t
   AC_CHECK_TYPE(u_int32_t,[ac_cv_header_stdint_u=$i],dnl
     continue,[#include <$i>])
   AC_CHECK_TYPE(u_int64_t,[and64="/u_int64_t"],[and64=""],[#include<$i>])
   ac_cv_stdint_result="(seen u_int32_t$and64 in $i)"
   break;
  done
  AC_MSG_CHECKING([for stdint u_int32_t])
 ])
fi fi

dnl if there was no good C99 header file, do some typedef checks...
if test "_$ac_cv_header_stdint_x" = "_" ; then
   AC_MSG_CHECKING([for stdint datatype model])
   AC_MSG_RESULT([(..)])
   AC_CHECK_SIZEOF(char)
   AC_CHECK_SIZEOF(short)
   AC_CHECK_SIZEOF(int)
   AC_CHECK_SIZEOF(long)
   AC_CHECK_SIZEOF(void*)
   ac_cv_stdint_char_model=""
   ac_cv_stdint_char_model="$ac_cv_stdint_char_model$ac_cv_sizeof_char"
   ac_cv_stdint_char_model="$ac_cv_stdint_char_model$ac_cv_sizeof_short"
   ac_cv_stdint_char_model="$ac_cv_stdint_char_model$ac_cv_sizeof_int"
   ac_cv_stdint_long_model=""
   ac_cv_stdint_long_model="$ac_cv_stdint_long_model$ac_cv_sizeof_int"
   ac_cv_stdint_long_model="$ac_cv_stdint_long_model$ac_cv_sizeof_long"
   ac_cv_stdint_long_model="$ac_cv_stdint_long_model$ac_cv_sizeof_voidp"
   name="$ac_cv_stdint_long_model"
   case "$ac_cv_stdint_char_model/$ac_cv_stdint_long_model" in
    122/242)     name="$name,  IP16 (standard 16bit machine)" ;;
    122/244)     name="$name,  LP32 (standard 32bit mac/win)" ;;
    122/*)       name="$name        (unusual int16 model)" ;; 
    124/444)     name="$name, ILP32 (standard 32bit unixish)" ;;
    124/488)     name="$name,  LP64 (standard 64bit unixish)" ;;
    124/448)     name="$name, LLP64 (unusual  64bit unixish)" ;;
    124/*)       name="$name        (unusual int32 model)" ;; 
    128/888)     name="$name, ILP64 (unusual  64bit numeric)" ;;
    128/*)       name="$name        (unusual int64 model)" ;; 
    222/*|444/*) name="$name        (unusual dsptype)" ;;
     *)          name="$name        (very unusal model)" ;;
   esac
   AC_MSG_RESULT([combined for stdint datatype model...  $name])
fi

if test "_$ac_cv_header_stdint_x" != "_" ; then
   ac_cv_header_stdint="$ac_cv_header_stdint_x"
elif  test "_$ac_cv_header_stdint_o" != "_" ; then
   ac_cv_header_stdint="$ac_cv_header_stdint_o"
elif  test "_$ac_cv_header_stdint_u" != "_" ; then
   ac_cv_header_stdint="$ac_cv_header_stdint_u"
else
   ac_cv_header_stdint="stddef.h"
fi

AC_MSG_CHECKING([for extra inttypes in chosen header])
AC_MSG_RESULT([($ac_cv_header_stdint)])
dnl see if int_least and int_fast types are present in _this_ header.
unset ac_cv_type_int_least32_t
unset ac_cv_type_int_fast32_t
AC_CHECK_TYPE(int_least32_t,,,[#include <$ac_cv_header_stdint>])
AC_CHECK_TYPE(int_fast32_t,,,[#include<$ac_cv_header_stdint>])
AC_CHECK_TYPE(intmax_t,,,[#include <$ac_cv_header_stdint>])

fi # shortcircut to system "stdint.h"
# ------------------ PREPARE VARIABLES ------------------------------
if test "$GCC" = "yes" ; then
ac_cv_stdint_message="using gnu compiler "`$CC --version | head -1` 
else
ac_cv_stdint_message="using $CC"
fi

AC_MSG_RESULT([make use of $ac_cv_header_stdint in $ac_stdint_h dnl
$ac_cv_stdint_result])

# ----------------- DONE inttypes.h checks START header -------------
AC_CONFIG_COMMANDS([$ac_stdint_h],[
AC_MSG_NOTICE(creating $ac_stdint_h : $_ac_stdint_h)
ac_stdint=$tmp/_stdint.h

echo "#ifndef" $_ac_stdint_h >$ac_stdint
echo "#define" $_ac_stdint_h "1" >>$ac_stdint
echo "#ifndef" _GENERATED_STDINT_H >>$ac_stdint
echo "#define" _GENERATED_STDINT_H '"'$PACKAGE $VERSION'"' >>$ac_stdint
echo "/* generated $ac_cv_stdint_message */" >>$ac_stdint
if test "_$ac_cv_header_stdint_t" != "_" ; then 
echo "#define _STDINT_HAVE_STDINT_H" "1" >>$ac_stdint
fi

cat >>$ac_stdint <<STDINT_EOF

/* ................... shortcircuit part ........................... */

#if defined HAVE_STDINT_H || defined _STDINT_HAVE_STDINT_H
#include <stdint.h>
#else
#include <stddef.h>

/* .................... configured part ............................ */

STDINT_EOF

echo "/* whether we have a C99 compatible stdint header file */" >>$ac_stdint
if test "_$ac_cv_header_stdint_x" != "_" ; then
  ac_header="$ac_cv_header_stdint_x"
  echo "#define _STDINT_HEADER_INTPTR" '"'"$ac_header"'"' >>$ac_stdint
else
  echo "/* #undef _STDINT_HEADER_INTPTR */" >>$ac_stdint
fi

echo "/* whether we have a C96 compatible inttypes header file */" >>$ac_stdint
if  test "_$ac_cv_header_stdint_o" != "_" ; then
  ac_header="$ac_cv_header_stdint_o"
  echo "#define _STDINT_HEADER_UINT32" '"'"$ac_header"'"' >>$ac_stdint
else
  echo "/* #undef _STDINT_HEADER_UINT32 */" >>$ac_stdint
fi

echo "/* whether we have a BSD compatible inet types header */" >>$ac_stdint
if  test "_$ac_cv_header_stdint_u" != "_" ; then
  ac_header="$ac_cv_header_stdint_u"
  echo "#define _STDINT_HEADER_U_INT32" '"'"$ac_header"'"' >>$ac_stdint
else
  echo "/* #undef _STDINT_HEADER_U_INT32 */" >>$ac_stdint
fi

echo "" >>$ac_stdint

if test "_$ac_header" != "_" ; then if test "$ac_header" != "stddef.h" ; then
  echo "#include <$ac_header>" >>$ac_stdint
  echo "" >>$ac_stdint
fi fi

echo "/* which 64bit typedef has been found */" >>$ac_stdint
if test "$ac_cv_type_uint64_t" = "yes" ; then
echo "#define   _STDINT_HAVE_UINT64_T" "1"  >>$ac_stdint
else
echo "/* #undef _STDINT_HAVE_UINT64_T */" >>$ac_stdint
fi
if test "$ac_cv_type_u_int64_t" = "yes" ; then
echo "#define   _STDINT_HAVE_U_INT64_T" "1"  >>$ac_stdint
else
echo "/* #undef _STDINT_HAVE_U_INT64_T */" >>$ac_stdint
fi
echo "" >>$ac_stdint

echo "/* which type model has been detected */" >>$ac_stdint
if test "_$ac_cv_stdint_char_model" != "_" ; then
echo "#define   _STDINT_CHAR_MODEL" "$ac_cv_stdint_char_model" >>$ac_stdint
echo "#define   _STDINT_LONG_MODEL" "$ac_cv_stdint_long_model" >>$ac_stdint
else
echo "/* #undef _STDINT_CHAR_MODEL // skipped */" >>$ac_stdint
echo "/* #undef _STDINT_LONG_MODEL // skipped */" >>$ac_stdint
fi
echo "" >>$ac_stdint

echo "/* whether int_least types were detected */" >>$ac_stdint
if test "$ac_cv_type_int_least32_t" = "yes"; then
echo "#define   _STDINT_HAVE_INT_LEAST32_T" "1"  >>$ac_stdint
else
echo "/* #undef _STDINT_HAVE_INT_LEAST32_T */" >>$ac_stdint
fi
echo "/* whether int_fast types were detected */" >>$ac_stdint
if test "$ac_cv_type_int_fast32_t" = "yes"; then
echo "#define   _STDINT_HAVE_INT_FAST32_T" "1" >>$ac_stdint
else
echo "/* #undef _STDINT_HAVE_INT_FAST32_T */" >>$ac_stdint
fi
echo "/* whether intmax_t type was detected */" >>$ac_stdint
if test "$ac_cv_type_intmax_t" = "yes"; then
echo "#define   _STDINT_HAVE_INTMAX_T" "1" >>$ac_stdint
else
echo "/* #undef _STDINT_HAVE_INTMAX_T */" >>$ac_stdint
fi
echo "" >>$ac_stdint

  cat >>$ac_stdint <<STDINT_EOF
/* .................... detections part ............................ */

/* whether we need to define bitspecific types from compiler base types */
#ifndef _STDINT_HEADER_INTPTR
#ifndef _STDINT_HEADER_UINT32
#ifndef _STDINT_HEADER_U_INT32
#define _STDINT_NEED_INT_MODEL_T
#else
#define _STDINT_HAVE_U_INT_TYPES
#endif
#endif
#endif

#ifdef _STDINT_HAVE_U_INT_TYPES
#undef _STDINT_NEED_INT_MODEL_T
#endif

#ifdef  _STDINT_CHAR_MODEL
#if     _STDINT_CHAR_MODEL+0 == 122 || _STDINT_CHAR_MODEL+0 == 124
#ifndef _STDINT_BYTE_MODEL
#define _STDINT_BYTE_MODEL 12
#endif
#endif
#endif

#ifndef _STDINT_HAVE_INT_LEAST32_T
#define _STDINT_NEED_INT_LEAST_T
#endif

#ifndef _STDINT_HAVE_INT_FAST32_T
#define _STDINT_NEED_INT_FAST_T
#endif

#ifndef _STDINT_HEADER_INTPTR
#define _STDINT_NEED_INTPTR_T
#ifndef _STDINT_HAVE_INTMAX_T
#define _STDINT_NEED_INTMAX_T
#endif
#endif


/* .................... definition part ............................ */

/* some system headers have good uint64_t */
#ifndef _HAVE_UINT64_T
#if     defined _STDINT_HAVE_UINT64_T  || defined HAVE_UINT64_T
#define _HAVE_UINT64_T
#elif   defined _STDINT_HAVE_U_INT64_T || defined HAVE_U_INT64_T
#define _HAVE_UINT64_T
typedef u_int64_t uint64_t;
#endif
#endif

#ifndef _HAVE_UINT64_T
/* .. here are some common heuristics using compiler runtime specifics */
#if defined __STDC_VERSION__ && defined __STDC_VERSION__ >= 199901L
#define _HAVE_UINT64_T
typedef long long int64_t;
typedef unsigned long long uint64_t;

#elif !defined __STRICT_ANSI__
#if defined _MSC_VER || defined __WATCOMC__ || defined __BORLANDC__
#define _HAVE_UINT64_T
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;

#elif defined __GNUC__ || defined __MWERKS__ || defined __ELF__
/* note: all ELF-systems seem to have loff-support which needs 64-bit */
#if !defined _NO_LONGLONG
#define _HAVE_UINT64_T
typedef long long int64_t;
typedef unsigned long long uint64_t;
#endif

#elif defined __alpha || (defined __mips && defined _ABIN32)
#if !defined _NO_LONGLONG
typedef long int64_t;
typedef unsigned long uint64_t;
#endif
  /* compiler/cpu type to define int64_t */
#endif
#endif
#endif

#if defined _STDINT_HAVE_U_INT_TYPES
/* int8_t int16_t int32_t defined by inet code, redeclare the u_intXX types */
typedef u_int8_t uint8_t;
typedef u_int16_t uint16_t;
typedef u_int32_t uint32_t;

/* glibc compatibility */
#ifndef __int8_t_defined
#define __int8_t_defined
#endif
#endif

#ifdef _STDINT_NEED_INT_MODEL_T
/* we must guess all the basic types. Apart from byte-addressable system, */
/* there a few 32-bit-only dsp-systems that we guard with BYTE_MODEL 8-} */
/* (btw, those nibble-addressable systems are way off, or so we assume) */

dnl   /* have a look at "64bit and data size neutrality" at */
dnl   /* http://unix.org/version2/whatsnew/login_64bit.html */
dnl   /* (the shorthand "ILP" types always have a "P" part) */

#if defined _STDINT_BYTE_MODEL
#if _STDINT_LONG_MODEL+0 == 242
/* 2:4:2 =  IP16 = a normal 16-bit system                */
typedef unsigned char   uint8_t;
typedef unsigned short  uint16_t;
typedef unsigned long   uint32_t;
#ifndef __int8_t_defined
#define __int8_t_defined
typedef          char    int8_t;
typedef          short   int16_t;
typedef          long    int32_t;
#endif
#elif _STDINT_LONG_MODEL+0 == 244 || _STDINT_LONG_MODEL == 444
/* 2:4:4 =  LP32 = a 32-bit system derived from a 16-bit */
/* 4:4:4 = ILP32 = a normal 32-bit system                */
typedef unsigned char   uint8_t;
typedef unsigned short  uint16_t;
typedef unsigned int    uint32_t;
#ifndef __int8_t_defined
#define __int8_t_defined
typedef          char    int8_t;
typedef          short   int16_t;
typedef          int     int32_t;
#endif
#elif _STDINT_LONG_MODEL+0 == 484 || _STDINT_LONG_MODEL+0 == 488
/* 4:8:4 =  IP32 = a 32-bit system prepared for 64-bit    */
/* 4:8:8 =  LP64 = a normal 64-bit system                 */
typedef unsigned char   uint8_t;
typedef unsigned short  uint16_t;
typedef unsigned int    uint32_t;
#ifndef __int8_t_defined
#define __int8_t_defined
typedef          char    int8_t;
typedef          short   int16_t;
typedef          int     int32_t;
#endif
/* this system has a "long" of 64bit */
#ifndef _HAVE_UINT64_T
#define _HAVE_UINT64_T
typedef unsigned long   uint64_t;
typedef          long    int64_t;
#endif
#elif _STDINT_LONG_MODEL+0 == 448
/*      LLP64   a 64-bit system derived from a 32-bit system */
typedef unsigned char   uint8_t;
typedef unsigned short  uint16_t;
typedef unsigned int    uint32_t;
#ifndef __int8_t_defined
#define __int8_t_defined
typedef          char    int8_t;
typedef          short   int16_t;
typedef          int     int32_t;
#endif
/* assuming the system has a "long long" */
#ifndef _HAVE_UINT64_T
#define _HAVE_UINT64_T
typedef unsigned long long uint64_t;
typedef          long long  int64_t;
#endif
#else
#define _STDINT_NO_INT32_T
#endif
#else
#define _STDINT_NO_INT8_T
#define _STDINT_NO_INT32_T
#endif
#endif

/*
 * quote from SunOS-5.8 sys/inttypes.h:
 * Use at your own risk.  As of February 1996, the committee is squarely
 * behind the fixed sized types; the "least" and "fast" types are still being
 * discussed.  The probability that the "fast" types may be removed before
 * the standard is finalized is high enough that they are not currently
 * implemented.
 */

#if defined _STDINT_NEED_INT_LEAST_T
typedef  int8_t    int_least8_t;
typedef  int16_t   int_least16_t;
typedef  int32_t   int_least32_t;
#ifdef _HAVE_UINT64_T
typedef  int64_t   int_least64_t;
#endif

typedef uint8_t   uint_least8_t;
typedef uint16_t  uint_least16_t;
typedef uint32_t  uint_least32_t;
#ifdef _HAVE_UINT64_T
typedef uint64_t  uint_least64_t;
#endif
  /* least types */
#endif

#if defined _STDINT_NEED_INT_FAST_T
typedef  int8_t    int_fast8_t; 
typedef  int       int_fast16_t;
typedef  int32_t   int_fast32_t;
#ifdef _HAVE_UINT64_T
typedef  int64_t   int_fast64_t;
#endif

typedef uint8_t   uint_fast8_t; 
typedef unsigned  uint_fast16_t;
typedef uint32_t  uint_fast32_t;
#ifdef _HAVE_UINT64_T
typedef uint64_t  uint_fast64_t;
#endif
  /* fast types */
#endif

#ifdef _STDINT_NEED_INTMAX_T
#ifdef _HAVE_UINT64_T
typedef  int64_t       intmax_t;
typedef uint64_t      uintmax_t;
#else
typedef          long  intmax_t;
typedef unsigned long uintmax_t;
#endif
#endif

#ifdef _STDINT_NEED_INTPTR_T
#ifndef __intptr_t_defined
#define __intptr_t_defined
/* we encourage using "long" to store pointer values, never use "int" ! */
#if   _STDINT_LONG_MODEL+0 == 242 || _STDINT_LONG_MODEL+0 == 484
typedef  unsinged int   uintptr_t;
typedef           int    intptr_t;
#elif _STDINT_LONG_MODEL+0 == 244 || _STDINT_LONG_MODEL+0 == 444
typedef  unsigned long  uintptr_t;
typedef           long   intptr_t;
#elif _STDINT_LONG_MODEL+0 == 448 && defined _HAVE_UINT64_T
typedef        uint64_t uintptr_t;
typedef         int64_t  intptr_t;
#else /* matches typical system types ILP32 and LP64 - but not IP16 or LLP64 */
typedef  unsigned long  uintptr_t;
typedef           long   intptr_t;
#endif
#endif
#endif

  /* shortcircuit*/
#endif
  /* once */
#endif
#endif
STDINT_EOF
    if cmp -s $ac_stdint_h $ac_stdint 2>/dev/null; then
      AC_MSG_NOTICE([$ac_stdint_h is unchanged])
    else
      ac_dir=`AS_DIRNAME(["$ac_stdint_h"])`
      AS_MKDIR_P(["$ac_dir"])
      rm -f $ac_stdint_h
      mv $ac_stdint $ac_stdint_h
    fi
],[# variables for create stdint.h replacement
PACKAGE="$PACKAGE"
VERSION="$VERSION"
ac_stdint_h="$ac_stdint_h"
_ac_stdint_h=AS_TR_CPP(_$PACKAGE-$ac_stdint_h)
ac_cv_stdint_message="$ac_cv_stdint_message"
ac_cv_header_stdint_t="$ac_cv_header_stdint_t"
ac_cv_header_stdint_x="$ac_cv_header_stdint_x"
ac_cv_header_stdint_o="$ac_cv_header_stdint_o"
ac_cv_header_stdint_u="$ac_cv_header_stdint_u"
ac_cv_type_uint64_t="$ac_cv_type_uint64_t"
ac_cv_type_u_int64_t="$ac_cv_type_u_int64_t"
ac_cv_stdint_char_model="$ac_cv_stdint_char_model"
ac_cv_stdint_long_model="$ac_cv_stdint_long_model"
ac_cv_type_int_least32_t="$ac_cv_type_int_least32_t"
ac_cv_type_int_fast32_t="$ac_cv_type_int_fast32_t"
ac_cv_type_intmax_t="$ac_cv_type_intmax_t"
])
])
