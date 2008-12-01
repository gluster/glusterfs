dnl Copyright (c) 2006, 2007, 2008 Z RESEARCH, Inc. <http://www.zresearch.com>
dnl This file is part of GlusterFS.
dnl
dnl GlusterFS is free software; you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation; either version 3 of the License, or
dnl (at your option) any later version.
dnl
dnl GlusterFS is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with this program.  If not, see <http://www.gnu.org/licenses/>.

AC_INIT([glusterfs],[1.4.0pre13],[glusterfs-support@zresearch.com])

AM_INIT_AUTOMAKE

AM_CONFIG_HEADER([config.h])

AC_CONFIG_FILES([Makefile
		libglusterfs/Makefile 
		libglusterfs/src/Makefile
		libglusterfsclient/Makefile
		libglusterfsclient/src/Makefile
		mod_glusterfs/Makefile
		mod_glusterfs/apache/Makefile
		mod_glusterfs/apache/1.3/Makefile
		mod_glusterfs/apache/1.3/src/Makefile
		mod_glusterfs/apache/2.2/Makefile
		mod_glusterfs/apache/2.2/src/Makefile
		mod_glusterfs/lighttpd/Makefile
		mod_glusterfs/lighttpd/1.4/Makefile
		mod_glusterfs/lighttpd/1.5/Makefile
		glusterfs/Makefile
		glusterfs/src/Makefile
		booster/Makefile
		booster/src/Makefile
		xlators/Makefile
		xlators/mount/Makefile
		xlators/mount/fuse/Makefile
		xlators/mount/fuse/src/Makefile
		xlators/mount/fuse/utils/mount.glusterfs
		xlators/mount/fuse/utils/mount_glusterfs
		xlators/mount/fuse/utils/Makefile
		xlators/storage/Makefile
		xlators/storage/posix/Makefile
		xlators/storage/posix/src/Makefile
		xlators/cluster/Makefile
		xlators/cluster/unify/Makefile
		xlators/cluster/unify/src/Makefile
		xlators/cluster/afr/Makefile
		xlators/cluster/afr/src/Makefile
		xlators/cluster/stripe/Makefile
		xlators/cluster/stripe/src/Makefile
		xlators/cluster/dht/Makefile
		xlators/cluster/dht/src/Makefile
		xlators/cluster/ha/Makefile
		xlators/cluster/ha/src/Makefile
		xlators/cluster/nufa/Makefile
		xlators/cluster/nufa/src/Makefile
		xlators/performance/Makefile
		xlators/performance/write-behind/Makefile
		xlators/performance/write-behind/src/Makefile
		xlators/performance/read-ahead/Makefile
		xlators/performance/read-ahead/src/Makefile
		xlators/performance/io-threads/Makefile
		xlators/performance/io-threads/src/Makefile
		xlators/performance/io-cache/Makefile
		xlators/performance/io-cache/src/Makefile
		xlators/performance/symlink-cache/Makefile
		xlators/performance/symlink-cache/src/Makefile
		xlators/debug/Makefile
		xlators/debug/trace/Makefile
		xlators/debug/trace/src/Makefile
		xlators/debug/error-gen/Makefile
		xlators/debug/error-gen/src/Makefile
		xlators/protocol/Makefile
		xlators/protocol/client/Makefile
		xlators/protocol/client/src/Makefile
		xlators/protocol/server/Makefile
		xlators/protocol/server/src/Makefile
		xlators/features/Makefile
		xlators/features/posix-locks/Makefile
		xlators/features/posix-locks/src/Makefile
		xlators/features/path-convertor/Makefile
		xlators/features/path-convertor/src/Makefile
		xlators/features/trash/Makefile
		xlators/features/trash/src/Makefile
		xlators/features/filter/Makefile
		xlators/features/filter/src/Makefile
		xlators/features/quota/Makefile
		xlators/features/quota/src/Makefile
		xlators/encryption/Makefile
		xlators/encryption/rot-13/Makefile
		xlators/encryption/rot-13/src/Makefile
		scheduler/Makefile
		scheduler/alu/Makefile
		scheduler/alu/src/Makefile
		scheduler/random/Makefile
		scheduler/random/src/Makefile
		scheduler/nufa/Makefile
		scheduler/nufa/src/Makefile
		scheduler/rr/Makefile
		scheduler/rr/src/Makefile
		scheduler/switch/Makefile
		scheduler/switch/src/Makefile
		transport/Makefile
		transport/socket/Makefile
		transport/socket/src/Makefile
		transport/ib-verbs/Makefile
		transport/ib-verbs/src/Makefile
		auth/Makefile
		auth/addr/Makefile
		auth/addr/src/Makefile
		auth/login/Makefile
		auth/login/src/Makefile
		doc/Makefile
		doc/examples/Makefile
		doc/hacker-guide/Makefile
		extras/Makefile
		extras/init.d/Makefile
		extras/init.d/glusterfs-server.plist
		extras/benchmarking/Makefile
		glusterfs.spec])

AC_CANONICAL_HOST

AC_PROG_CC
AC_PROG_LIBTOOL

# LEX needs a check
AC_PROG_LEX
if test  "x${LEX}" != "xflex" -a "x${FLEX}" != "xlex"; then
   AC_MSG_ERROR([Flex or lex required to build glusterfs.])
fi

# YACC needs a check
AC_PROG_YACC
if test "x${YACC}" = "xbyacc" -o "x${YACC}" = "xyacc" -o "x${YACC}" = "x"; then
   AC_MSG_ERROR([GNU Bison required to build glusterfs.])
fi

AC_CHECK_TOOL([LD],[ld])

AC_CHECK_LIB([pthread], [pthread_mutex_init], , AC_MSG_ERROR([Posix threads library is required to build glusterfs]))
		 
AC_CHECK_FUNC([dlopen], [has_dlopen=yes], AC_CHECK_LIB([dl], [dlopen], , AC_MSG_ERROR([Dynamic linking library required to build glusterfs])))


AC_CHECK_HEADERS([sys/xattr.h])

AC_CHECK_HEADERS([sys/extattr.h])

dnl Mac OS X does not have spinlocks
AC_CHECK_FUNC([pthread_spin_init], [have_spinlock=yes])
if test "x${have_spinlock}" = "xyes"; then
   AC_DEFINE(HAVE_SPINLOCK, 1, [define if found spinlock])
fi
AC_SUBST(HAVE_SPINLOCK)

dnl some os may not have GNU defined strnlen function
AC_CHECK_FUNC([strnlen], [have_strnlen=yes])
if test "x${have_strnlen}" = "xyes"; then
   AC_DEFINE(HAVE_STRNLEN, 1, [define if found strnlen])
fi
AC_SUBST(HAVE_STRNLEN)


AC_CHECK_FUNC([setfsuid], [have_setfsuid=yes])
AC_CHECK_FUNC([setfsgid], [have_setfsgid=yes])

if test "x${have_setfsuid}" = "xyes" -a "x${have_setfsgid}" = "xyes"; then
  AC_DEFINE(HAVE_SET_FSID, 1, [define if found setfsuid setfsgid])
fi


# LIBGLUSTERFSCLIENT section
AC_ARG_ENABLE([libglusterfsclient],
	      AC_HELP_STRING([--disable-libglusterfsclient],
			     [Do not build libglusterfsclient]))

BUILD_LIBGLUSTERFSCLIENT="no"

if test "x$enable_libglusterfsclient" != "xno"; then
   LIBGLUSTERFSCLIENT_SUBDIR="libglusterfsclient"
   BUILD_LIBGLUSTERFSCLIENT="yes"
fi

AC_SUBST(LIBGLUSTERFSCLIENT_SUBDIR)
# end LIBGLUSTERFSCLIENT section


# MOD_GLUSTERFS section
AC_ARG_ENABLE([mod_glusterfs],
	      AC_HELP_STRING([--disable-mod_glusterfs],
			     [Do not build glusterfs module for webserver. Currently supported module is for apache/1.3.x]))

if test "x$enable_mod_glusterfs" != "xno"; then
   AC_ARG_WITH([apxs],
		AC_HELP_STRING([--with-apxs],
				[directory containing apxs binary]))
    if test "x$with_apxs" != "x"; then
        APXS_BIN=$with_apxs
    else
	APXS_BIN="$PATH"
    fi
    AC_CHECK_TOOL([APXS],[apxs], ["no"], [$APXS_BIN])
    if test "X$APXS" = "Xno"; then
        HAVE_APXS="no";
    else
	if test "x$with_apxs" != "x"; then
	    APXS="$with_apxs/apxs";
        fi
	HAVE_APXS="yes";
    fi

    HAVE_LIBGLUSTERFSCLIENT="no";
    if test "x$BUILD_LIBGLUSTERFSCLIENT" = "xyes"; then
        HAVE_LIBGLUSTERFSCLIENT="yes";
    fi

    AC_ARG_WITH([apxspath],
                AC_HELP_STRING([--with-apxspath],
                                [Path to apxs binary]))

    AC_ARG_WITH([apachepath],
                AC_HELP_STRING([--with-apachepath],
                                [Path to apache binary]))       
fi

if test "x$enable_mod_glusterfs" = "xyes" -a "x$HAVE_APXS" = "xno"; then
    echo "apxs is required to build mod_glusterfs. Use --with-apxs to specify path to apxs. If mod_glusterfs is not required, do not pass --enable-mod_glusterfs option to configure "
    exit 1
fi

if test "x$enable_mod_glusterfs" = "xyes" -a "x$HAVE_LIBGLUSTERFSCLIENT" = "xno"; then
    echo "libglusterfsclient is required to build mod_glusterfs. Do not specify --disable-libglusterfsclient to configure script. If mod_glusterfs is not required, do not pass --enable-mod_glusterfs option to configure "
    exit 1
fi

BUILD_MOD_GLUSTERFS=no
MOD_GLUSTERFS_HTTPD_VERSION=""

if test "x$enable_mod_glusterfs" != "xno" -a "x$HAVE_APXS" = "xyes" -a "x$HAVE_LIBGLUSTERFSCLIENT" = "xyes"; then
   BUILD_MOD_GLUSTERFS="yes";
   MOD_GLUSTERFS_SUBDIR="mod_glusterfs";
fi

if test "x$BUILD_MOD_GLUSTERFS" = "xyes"; then
   HTTPD_BIN_DIR=`$APXS -q SBINDIR`
   MOD_GLUSTERFS_HTTPD_VERSION=`$HTTPD_BIN_DIR/httpd -V | head -1 | awk "{print $3}" | sed 's/[[^0-9.]]//g' |        sed 's/\(.*\..*\)\..*/\1/'`
fi

if test "x$with_apxspath" != "x"; then
   APXS_MANUAL=$with_apxspath
fi

if test "x$with_apachepath" != "x"; then
   HTTPD_MANUAL=$with_apachepath
fi

if test "x$with_apxspath" != "x" -a "x$with_apachepath" != "x"; then
   BUILD_MOD_GLUSTERFS="yes";
   MOD_GLUSTERFS_SUBDIR="mod_glusterfs";
   APACHE_MANUAL=yes
fi

if test "x$APACHE_MANUAL" = "xyes"; then
   HTTPD_BIN_DIR=`$APXS_MANUAL -q SBINDIR`
   MOD_GLUSTERFS_HTTPD_VERSION=`$HTTPD_MANUAL -V | head -1 | awk "{print $3}" | sed 's/[[^0-9.]]//g' | sed 's/\(.*\..*\)\..*/\1/'`
   APXS=$APXS_MANUAL
fi

AC_SUBST(MOD_GLUSTERFS_SUBDIR)
AC_SUBST(APXS)
AC_SUBST(MOD_GLUSTERFS_HTTPD_VERSION)
# end MOD_GLUSTERFS section


# FUSE section
# TODO: make a clean version check of libfuse
AC_ARG_ENABLE([fuse-client],
	      AC_HELP_STRING([--disable-fuse-client],
			     [Do not build the fuse client. NOTE: you cannot mount glusterfs without the client]))

if test "x$enable_fuse_client" != "xno"; then
   AC_CHECK_LIB([fuse],
                 [fuse_req_interrupt_func],
	         [HAVE_LIBFUSE="yes"],
	         [HAVE_LIBFUSE="no"])
fi

if test "x$enable_fuse_client" = "xyes" -a "x$HAVE_LIBFUSE" = "xno"; then
   echo "FUSE requested but not found."
   exit 1
fi

BUILD_FUSE_CLIENT=no
if test "x$enable_fuse_client" != "xno" -a "x$HAVE_LIBFUSE" = "xyes"; then
   FUSE_CLIENT_SUBDIR=fuse
   BUILD_FUSE_CLIENT="yes"
fi

AC_SUBST(FUSE_CLIENT_SUBDIR)
# end FUSE section


# EPOLL section
AC_ARG_ENABLE([epoll],
	      AC_HELP_STRING([--disable-epoll],
			     [Use poll instead of epoll.]))

BUILD_EPOLL=no
if test "x$enable_epoll" != "xno"; then
   AC_CHECK_HEADERS([sys/epoll.h],
                    [BUILD_EPOLL=yes],
		    [BUILD_EPOLL=no])
fi
# end EPOLL section


# IBVERBS section
AC_ARG_ENABLE([ibverbs],
	      AC_HELP_STRING([--disable-ibverbs],
			     [Do not build the ibverbs transport]))

if test "x$enable_ibverbs" != "xno"; then
  AC_CHECK_LIB([ibverbs],
               [ibv_get_device_list],
	       [HAVE_LIBIBVERBS="yes"],
	       [HAVE_LIBIBVERBS="no"])
fi

if test "x$enable_ibverbs" = "xyes" -a "x$HAVE_LIBIBVERBS" = "xno"; then
   echo "ibverbs requested but not found."
   exit 1
fi


BUILD_IBVERBS=no
if test "x$enable_ibverbs" != "xno" -a "x$HAVE_LIBIBVERBS" = "xyes"; then
  IBVERBS_SUBDIR=ib-verbs
  BUILD_IBVERBS=yes
fi

AC_SUBST(IBVERBS_SUBDIR)
# end IBVERBS section

# Berkely-DB section
# storage/bdb requires Berkeley-DB version 4.6.21 or higher
_GLFS_DB_VERSION_MAJOR=4
_GLFS_DB_VERSION_MINOR=6
_GLFS_DB_VERSION_PATCH=21
AC_ARG_ENABLE([db],
	      AC_HELP_STRING([--disable-bdb],
			     [Do not build the Berkeley-DB translator]))

if test "x$enable_bdb" != "xno"; then
   AC_CHECK_HEADERS([db.h],
                    [HAVE_BDB="yes"],
                    [HAVE_BDB="no"])
   if test "x$HAVE_BDB" = "xyes"; then
      AC_CHECK_LIB([db],
                   [db_create],
	           [HAVE_BDB="yes"],
	           [HAVE_BDB="no"])
   fi

   if test "x$HAVE_BDB" = "xyes"; then
      AC_TRY_COMPILE([#include <db.h>],
	 	      #if (DB_VERSION_MAJOR < $_GLFS_DB_VERSION_MAJOR) ||\
		          (DB_VERSION_MAJOR == $_GLFS_DB_VERSION_MAJOR && \
		           DB_VERSION_MINOR < $_GLFS_DB_VERSION_MINOR) || \
		          (DB_VERSION_MAJOR == $_GLFS_DB_VERSION_MAJOR && \
		           DB_VERSION_MINOR == $_GLFS_DB_VERSION_MINOR && \
			   DB_VERSION_PATCH < $_GLFS_DB_VERSION_PATCH)
		      #error "bdb older than required"
		      #endif
		      ,
		     [HAVE_BDB_VERSION="yes"],
		     [HAVE_BDB_VERSION="no"])

      dnl check for DB->stat having 4 arguments.
      AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <db.h>]], 
			[[DB *bdb; bdb->stat (NULL, NULL, NULL, 0);]])],
		  	[HAVE_BDB_VERSION=yes], [HAVE_BDB_VERSION=no])

      dnl check for DBC->c_get presence. 
      AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <db.h>]],
                        [[DBC *cursor; cursor->get (NULL, NULL, NULL, 0);]])],
		        [HAVE_BDB_CURSOR_GET=yes], [HAVE_BDB_CURSOR_GET=no])

   fi		     
fi

if test "x$HAVE_BDB_CURSOR_GET" = "xyes" -a "x$HAVE_BDB_VERSION" = "xyes"; then
   AC_DEFINE(HAVE_BDB_CURSOR_GET, 1, [Berkeley-DB version has cursor->get()])
fi

if test "x$enable_bdb" = "xyes" -a "x$HAVE_BDB" = "xno" -a "x$HAVE_BDB_VERSION" = "xno" -a "x$HAVE_BDB_CURSOR_GET" = "xno"; then
   echo "Berkeley-DB requested but not found. glusterfs bdb feature requires db version 4.6.21 or higher"
   exit 1
fi


BUILD_BDB=no
if test "x$enable_bdb" != "xno" -a "x$HAVE_BDB" = "xyes"; then
      BDB_SUBDIR=bdb	       
      BUILD_BDB=yes
fi



AC_SUBST(BDB_SUBDIR)
# end BDB section

dnl FreeBSD > 5 has execinfo as a Ported library for giving a workaround
dnl solution to GCC backtrace functionality

AC_CHECK_HEADERS([execinfo.h], [have_backtrace=yes],
               AC_CHECK_LIB([execinfo], [backtrace], [have_backtrace=yes]))
dnl               AC_MSG_ERROR([libexecinfo not found libexecinfo required.])))

if test "x${have_backtrace}" = "xyes"; then
   AC_DEFINE(HAVE_BACKTRACE, 1, [define if found backtrace])
fi
AC_SUBST(HAVE_BACKTRACE)

dnl glusterfs prints memory usage to stderr by sending it SIGUSR1
AC_CHECK_FUNC([malloc_stats], [have_malloc_stats=yes])
if test "x${have_malloc_stats}" = "xyes"; then
   AC_DEFINE(HAVE_MALLOC_STATS, 1, [define if found malloc_stats])
fi
AC_SUBST(HAVE_MALLOC_STATS)

dnl check for st_atim in struct stat
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
				     #include <sys/stat.h>]], 
		  [[struct stat buf; buf.st_atim.tv_nsec = 0;]])],
		  [have_tv_nsec=yes], [have_tv_nsec=no])

if test "x${have_tv_nsec}" = "xyes"; then
  AC_DEFINE(HAVE_TV_NSEC, 1, [define if found tv_nsec])
fi
AC_SUBST(HAVE_TV_NSEC)

AC_CHECK_HEADER([argp.h], AC_DEFINE(HAVE_ARGP, 1, [have argp]))
AC_CONFIG_SUBDIRS(argp-standalone)
BUILD_ARGP_STANDALONE=no
if test "x${ac_cv_header_argp_h}" = "xno"; then 
   BUILD_ARGP_STANDALONE=yes
   ARGP_STANDALONE_CPPFLAGS='-I${top_srcdir}/argp-standalone'
   ARGP_STANDALONE_LDADD='${top_builddir}/argp-standalone/libargp.a'
fi

AC_SUBST(ARGP_STANDALONE_CPPFLAGS)
AC_SUBST(ARGP_STANDALONE_LDADD)

AC_CHECK_HEADER([malloc.h], AC_DEFINE(HAVE_MALLOC_H, 1, [have malloc.h]))

AC_CHECK_FUNC([llistxattr], [have_llistxattr=yes])
if test "x${have_llistxattr}" = "xyes"; then
  AC_DEFINE(HAVE_LLISTXATTR, 1, [define if llistxattr exists])
fi

AC_CHECK_FUNC([fdatasync], [have_fdatasync=yes])
if test "x${have_fdatasync}" = "xyes"; then
  AC_DEFINE(HAVE_FDATASYNC, 1, [define if fdatasync exists])
fi

GF_HOST_OS=""
GF_LDFLAGS="-rdynamic"
GF_BOOSTER_SUBDIR="booster"
GF_FUSE_LDADD="-lfuse"
case $host_os in
     linux*)
     dnl	GF_LINUX_HOST_OS=1
        GF_HOST_OS="GF_LINUX_HOST_OS"
	GF_CFLAGS="${ARGP_STANDALONE_CPPFLAGS}"
	GF_GLUSTERFS_CFLAGS="${GF_CFLAGS}"
	GF_LDADD="${ARGP_STANDALONE_LDADD}"
	;;
     solaris*)
        GF_HOST_OS="GF_SOLARIS_HOST_OS"
	GF_CFLAGS="${ARGP_STANDALONE_CPPFLAGS} -D_REENTRANT"
	GF_LDFLAGS=""
	GF_GLUSTERFS_CFLAGS="${GF_CFLAGS}"
	GF_LDADD="${ARGP_STANDALONE_LDADD}"
	GF_GLUSTERFS_LDFLAGS="-lnsl -lresolv -lsocket"
	GF_BOOSTER_SUBDIR=""
	;;
     *bsd*)
        GF_HOST_OS="GF_BSD_HOST_OS"
	GF_CFLAGS="${ARGP_STANDALONE_CPPFLAGS}"
	GF_GLUSTERFS_CFLAGS="${GF_CFLAGS}"
	GF_LDADD="${ARGP_STANDALONE_LDADD}"
	BUILD_MOD_GLUSTERFS=no
	MOD_GLUSTERFS_SUBDIR=""
	BUILD_LIBGLUSTERFSCLIENT=no
	LIBGLUSTERFSCLIENT_SUBDIR=""
	GF_BOOSTER_SUBDIR=""
	;;
     darwin*)
        GF_HOST_OS="GF_DARWIN_HOST_OS"
	LIBTOOL=glibtool
	GF_CFLAGS="${ARGP_STANDALONE_CPPFLAGS} -D__DARWIN_64_BIT_INO_T -bundle -undefined suppress -flat_namespace"
	GF_GLUSTERFS_CFLAGS="${ARGP_STANDALONE_CPPFLAGS} -D__DARWIN_64_BIT_INO_T -undefined suppress -flat_namespace"
	GF_LDADD="${ARGP_STANDALONE_LDADD}"
	GF_FUSE_LDADD="-liconv -lfuse_ino64"
	BUILD_MOD_GLUSTERFS=no
	MOD_GLUSTERFS_SUBDIR=""
	BUILD_LIBGLUSTERFSCLIENT=no
	LIBGLUSTERFSCLIENT_SUBDIR=""
	GF_BOOSTER_SUBDIR=""
	;;
esac

AC_SUBST(GF_HOST_OS)
AC_SUBST(GF_GLUSTERFS_LDFLAGS)
AC_SUBST(GF_GLUSTERFS_CFLAGS)
AC_SUBST(GF_CFLAGS)
AC_SUBST(GF_LDFLAGS)
AC_SUBST(GF_LDADD)
AC_SUBST(GF_FUSE_LDADD)
AC_SUBST(GF_BOOSTER_SUBDIR)

AM_CONDITIONAL([GF_DARWIN_HOST_OS], test "${GF_HOST_OS}" = "GF_DARWIN_HOST_OS")	

AC_OUTPUT

exec >&2

echo
echo "GlusterFS configure summary"
echo "==========================="
echo "FUSE client        : $BUILD_FUSE_CLIENT"
echo "Infiniband verbs   : $BUILD_IBVERBS"
echo "epoll IO multiplex : $BUILD_EPOLL"
echo "Berkeley-DB        : $BUILD_BDB"
echo "libglusterfsclient : $BUILD_LIBGLUSTERFSCLIENT"
echo "mod_glusterfs      : $BUILD_MOD_GLUSTERFS ($MOD_GLUSTERFS_HTTPD_VERSION)"
echo "argp-standalone    : $BUILD_ARGP_STANDALONE"
echo
