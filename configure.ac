dnl  Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
dnl  This file is part of GlusterFS.
dnl
dnl  This file is licensed to you under your choice of the GNU Lesser
dnl  General Public License, version 3 or any later version (LGPLv3 or
dnl  later), or the GNU General Public License, version 2 (GPLv2), in all
dnl  cases as published by the Free Software Foundation.

AC_INIT([glusterfs],[3git],[gluster-users@gluster.org],,[https://github.com/gluster/glusterfs.git])

AM_INIT_AUTOMAKE

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES(yes)])

if make --help 2>&1 | grep -q no-print-directory; then
   AM_MAKEFLAGS="$AM_MAKEFLAGS --no-print-directory";
fi

if make --help 2>&1 | grep -q quiet; then
   AM_MAKEFLAGS="$AM_MAKEFLAGS --quiet"
fi

if libtool --help 2>&1 | grep -q quiet; then
   AM_LIBTOOLFLAGS="--quiet";
fi

AM_CONFIG_HEADER([config.h])

AC_CONFIG_FILES([Makefile
		libglusterfs/Makefile
		libglusterfs/src/Makefile
		glusterfsd/Makefile
		glusterfsd/src/Makefile
                rpc/Makefile
                rpc/rpc-lib/Makefile
                rpc/rpc-lib/src/Makefile
                rpc/rpc-transport/Makefile
                rpc/rpc-transport/socket/Makefile
                rpc/rpc-transport/socket/src/Makefile
                rpc/rpc-transport/rdma/Makefile
                rpc/rpc-transport/rdma/src/Makefile
                rpc/xdr/Makefile
                rpc/xdr/src/Makefile
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
		xlators/cluster/afr/Makefile
		xlators/cluster/afr/src/Makefile
		xlators/cluster/stripe/Makefile
		xlators/cluster/stripe/src/Makefile
		xlators/cluster/dht/Makefile
		xlators/cluster/dht/src/Makefile
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
		xlators/performance/quick-read/Makefile
		xlators/performance/quick-read/src/Makefile
                xlators/performance/md-cache/Makefile
                xlators/performance/md-cache/src/Makefile
		xlators/debug/Makefile
		xlators/debug/trace/Makefile
		xlators/debug/trace/src/Makefile
		xlators/debug/error-gen/Makefile
		xlators/debug/error-gen/src/Makefile
		xlators/debug/io-stats/Makefile
		xlators/debug/io-stats/src/Makefile
		xlators/protocol/Makefile
		xlators/protocol/auth/Makefile
		xlators/protocol/auth/addr/Makefile
		xlators/protocol/auth/addr/src/Makefile
		xlators/protocol/auth/login/Makefile
		xlators/protocol/auth/login/src/Makefile
		xlators/protocol/client/Makefile
		xlators/protocol/client/src/Makefile
		xlators/protocol/server/Makefile
		xlators/protocol/server/src/Makefile
		xlators/features/Makefile
		xlators/features/locks/Makefile
		xlators/features/locks/src/Makefile
		xlators/features/quota/Makefile
		xlators/features/quota/src/Makefile
                xlators/features/marker/Makefile
                xlators/features/marker/src/Makefile
                xlators/features/marker/utils/Makefile
                xlators/features/marker/utils/src/Makefile
                xlators/features/marker/utils/syncdaemon/Makefile
		xlators/features/read-only/Makefile
		xlators/features/read-only/src/Makefile
		xlators/features/mac-compat/Makefile
		xlators/features/mac-compat/src/Makefile
		xlators/features/quiesce/Makefile
		xlators/features/quiesce/src/Makefile
                xlators/features/index/Makefile
                xlators/features/index/src/Makefile
		xlators/encryption/Makefile
		xlators/encryption/rot-13/Makefile
		xlators/encryption/rot-13/src/Makefile
                xlators/system/Makefile
                xlators/system/posix-acl/Makefile
                xlators/system/posix-acl/src/Makefile
                cli/Makefile
                cli/src/Makefile
		doc/Makefile
		extras/Makefile
		extras/init.d/Makefile
		extras/init.d/glusterd.plist
                extras/init.d/glusterd-Debian
                extras/init.d/glusterd-Redhat
                extras/init.d/glusterd-SuSE
		extras/benchmarking/Makefile
                extras/hook-scripts/Makefile
		contrib/fuse-util/Makefile
		contrib/uuid/uuid_types.h
                xlators/nfs/Makefile
                xlators/nfs/server/Makefile
                xlators/nfs/server/src/Makefile
                xlators/mgmt/Makefile
                xlators/mgmt/glusterd/Makefile
                xlators/mgmt/glusterd/src/Makefile
		glusterfs-api.pc
		api/Makefile
		api/src/Makefile
		glusterfs.spec])

AC_CANONICAL_HOST

AC_PROG_CC
AC_DISABLE_STATIC
AC_PROG_LIBTOOL

AC_ARG_WITH(pkgconfigdir,
            [  --with-pkgconfigdir=DIR      pkgconfig file in DIR @<:@LIBDIR/pkgconfig@:>@],
            [pkgconfigdir=$withval],
            [pkgconfigdir='${libdir}/pkgconfig'])
AC_SUBST(pkgconfigdir)

AC_ARG_WITH(mountutildir,
            [  --with-mountutildir=DIR mount helper utility in DIR @<:@/sbin@:>@],
            [mountutildir=$withval],
            [mountutildir='/sbin'])
AC_SUBST(mountutildir)

AC_ARG_WITH(initdir,
            [  --with-initdir=DIR init.d scripts in DIR @<:@/etc/init.d@:>@],
            [initdir=$withval],
            [initdir='/etc/init.d'])
AC_SUBST(initdir)

AC_ARG_WITH(launchddir,
            [  --with-launchddir=DIR launchd services in DIR @<:@/Library/LaunchDaemons@:>@],
            [launchddir=$withval],
            [launchddir='/Library/LaunchDaemons'])
AC_SUBST(launchddir)

# LEX needs a check
AC_PROG_LEX
if test  "x${LEX}" != "xflex" -a "x${FLEX}" != "xlex"; then
   AC_MSG_ERROR([Flex or lex required to build glusterfs.])
fi

dnl
dnl Word sizes...
dnl
AC_CHECK_SIZEOF(short)
AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(long)
AC_CHECK_SIZEOF(long long)
SIZEOF_SHORT=$ac_cv_sizeof_short
SIZEOF_INT=$ac_cv_sizeof_int
SIZEOF_LONG=$ac_cv_sizeof_long
SIZEOF_LONG_LONG=$ac_cv_sizeof_long_long
AC_SUBST(SIZEOF_SHORT)
AC_SUBST(SIZEOF_INT)
AC_SUBST(SIZEOF_LONG)
AC_SUBST(SIZEOF_LONG_LONG)

# YACC needs a check
AC_PROG_YACC
if test "x${YACC}" = "xbyacc" -o "x${YACC}" = "xyacc" -o "x${YACC}" = "x"; then
   AC_MSG_ERROR([GNU Bison required to build glusterfs.])
fi

AC_CHECK_TOOL([LD],[ld])

AC_CHECK_LIB([crypto], [MD5], , AC_MSG_ERROR([OpenSSL crypto library is required to build glusterfs]))

AC_CHECK_LIB([pthread], [pthread_mutex_init], , AC_MSG_ERROR([Posix threads library is required to build glusterfs]))

AC_CHECK_FUNC([dlopen], [has_dlopen=yes], AC_CHECK_LIB([dl], [dlopen], , AC_MSG_ERROR([Dynamic linking library required to build glusterfs])))


AC_CHECK_HEADERS([sys/xattr.h])

AC_CHECK_HEADERS([sys/extattr.h])

AC_CHECK_HEADERS([openssl/md5.h])

case $host_os in
  darwin*)
    if ! test "`/usr/bin/sw_vers | grep ProductVersion: | cut -f 2 | cut -d. -f2`" -ge 5; then
      AC_MSG_ERROR([You need at least OS X 10.5 (Leopard) to build Glusterfs])
    fi
    ;;
esac

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


# FUSE section
AC_ARG_ENABLE([fuse-client],
	      AC_HELP_STRING([--disable-fuse-client],
			     [Do not build the fuse client. NOTE: you cannot mount glusterfs without the client]))

BUILD_FUSE_CLIENT=no
if test "x$enable_fuse_client" != "xno"; then
   FUSE_CLIENT_SUBDIR=fuse
   BUILD_FUSE_CLIENT="yes"
fi

AC_SUBST(FUSE_CLIENT_SUBDIR)
# end FUSE section


# FUSERMOUNT section
AC_ARG_ENABLE([fusermount],
              AC_HELP_STRING([--enable-fusermount],
                             [Build fusermount]))

BUILD_FUSERMOUNT="no"
if test "x$enable_fusermount" = "xyes"; then
  FUSERMOUNT_SUBDIR="contrib/fuse-util"
  BUILD_FUSERMOUNT="yes"
  AC_DEFINE(GF_FUSERMOUNT, 1, [Use our own fusermount])
fi

AC_SUBST(FUSERMOUNT_SUBDIR)
#end FUSERMOUNT section


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

BUILD_RDMA=no
BUILD_IBVERBS=no
if test "x$enable_ibverbs" != "xno" -a "x$HAVE_LIBIBVERBS" = "xyes"; then
  IBVERBS_SUBDIR=ib-verbs
  BUILD_IBVERBS=yes
  RDMA_SUBDIR=rdma
  BUILD_RDMA=yes
fi

AC_SUBST(IBVERBS_SUBDIR)
AC_SUBST(RDMA_SUBDIR)
# end IBVERBS section


# SYNCDAEMON section
AC_ARG_ENABLE([georeplication],
	      AC_HELP_STRING([--disable-georeplication],
			     [Do not install georeplication components]))

BUILD_SYNCDAEMON=no
case $host_os in
     linux*)
#do nothing
       ;;
     netbsd*)
#do nothing
       ;;
     *)
#disabling geo replication for non-linux platforms
	enable_georeplication=no
        ;;
esac
SYNCDAEMON_COMPILE=0
if test "x$enable_georeplication" != "xno"; then
  SYNCDAEMON_SUBDIR=utils
  SYNCDAEMON_COMPILE=1

  BUILD_SYNCDAEMON="yes"
  AM_PATH_PYTHON([2.4])
  echo -n "checking if python is python 2.x... "
  if echo $PYTHON_VERSION | grep ^2; then
    :
  else
    echo no
    AC_MSG_ERROR([only python 2.x is supported])
  fi
  echo -n "checking if python has ctypes support... "
  if "$PYTHON" -c 'import ctypes' 2>/dev/null; then
    echo yes
  else
    echo no
    AC_MSG_ERROR([python does not have ctypes support])
  fi
fi
AC_SUBST(SYNCDAEMON_COMPILE)
AC_SUBST(SYNCDAEMON_SUBDIR)
# end SYNCDAEMON section

# check for systemtap/dtrace
BUILD_SYSTEMTAP=no
AC_MSG_CHECKING([whether to include systemtap tracing support])
AC_ARG_ENABLE([systemtap],
              [AS_HELP_STRING([--enable-systemtap],
              [Enable inclusion of systemtap trace support])],
              [ENABLE_SYSTEMTAP="${enableval}"], [ENABLE_SYSTEMTAP="def"])

AM_CONDITIONAL([ENABLE_SYSTEMTAP], [test "x${ENABLE_SYSTEMTAP}" == "xyes"])
AC_MSG_RESULT(${ENABLE_SYSTEMTAP})

if test "x${ENABLE_SYSTEMTAP}" != "xno"; then
    AC_CHECK_PROG(DTRACE, dtrace, "yes", "no")
    AC_CHECK_HEADER([sys/sdt.h], [SDT_H_FOUND="yes"],
                    [SDT_H_FOUND="no"])
fi

if test "x${ENABLE_SYSTEMTAP}" == "xyes"; then
    if test "x${DTRACE}" == "xno"; then
        AC_MSG_ERROR([dtrace not found])
    elif test "$x{SDT_H_FOUND}" == "xno"; then
        AC_MSG_ERROR([systemtap support needs sys/sdt.h header])
    fi
fi

if test "x${DTRACE}" == "xyes" -a "x${SDT_H_FOUND}" == "xyes"; then
    AC_MSG_CHECKING([x"${DTRACE}"xy"${SDT_H_FOUND}"y])
    AC_DEFINE([HAVE_SYSTEMTAP], [1], [Define to 1 if using  probes.])
    BUILD_SYSTEMTAP=yes
fi
# end of systemtap/dtrace

#check if libxml is present if so enable HAVE_LIB_XML
echo -n "checking if libxml2 is present... "

PKG_CHECK_MODULES([LIBXML2], [libxml-2.0 >= 2.6.19],
                  [echo "yes (features requiring libxml2 enabled)"  AC_DEFINE(HAVE_LIB_XML, 1, [define if libxml2 is present])],
                  [echo "no"] )

AC_SUBST(LIBXML2_CFLAGS)
AC_SUBST(LIBXML2_LIBS)

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

dnl Linux, Solaris, Cygwin
AC_CHECK_MEMBERS([struct stat.st_atim.tv_nsec])
dnl FreeBSD, NetBSD
AC_CHECK_MEMBERS([struct stat.st_atimespec.tv_nsec])
case $host_os in
	*netbsd*)
	CFLAGS=-D_INCOMPLETE_XOPEN_C063
	;;
esac
AC_CHECK_FUNC([linkat], [have_linkat=yes])
if test "x${have_linkat}" = "xyes"; then
   AC_DEFINE(HAVE_LINKAT, 1, [define if found linkat])
fi
AC_SUBST(HAVE_LINKAT)

dnl Check for argp
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

# Check the distribution where you are compiling glusterfs on 

GF_DISTRIBUTION=
AC_CHECK_FILE([/etc/debian_version])
AC_CHECK_FILE([/etc/SuSE-release])
AC_CHECK_FILE([/etc/redhat-release])

if test "x$ac_cv_file__etc_debian_version" = "xyes"; then
   GF_DISTRIBUTION=Debian
fi
if test "x$ac_cv_file__etc_SuSE_release" = "xyes"; then
   GF_DISTRIBUTION=SuSE
fi
if test "x$ac_cv_file__etc_redhat_release" = "xyes"; then
   GF_DISTRIBUTION=Redhat
fi

AC_SUBST(GF_DISTRIBUTION)

GF_HOST_OS=""
GF_LDFLAGS="-rdynamic"
CFLAGS="-g"

case $host_os in
     linux*)
     dnl	GF_LINUX_HOST_OS=1
        GF_HOST_OS="GF_LINUX_HOST_OS"
	GF_CFLAGS="${ARGP_STANDALONE_CPPFLAGS} -O0"
	GF_GLUSTERFS_CFLAGS="${GF_CFLAGS}"
	GF_LDADD="${ARGP_STANDALONE_LDADD}"
	GF_FUSE_CFLAGS="-DFUSERMOUNT_DIR=\\\"\$(bindir)\\\""
	;;
     solaris*)
        GF_HOST_OS="GF_SOLARIS_HOST_OS"
	GF_CFLAGS="${ARGP_STANDALONE_CPPFLAGS} -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS -O0 -m64"
	GF_LDFLAGS=""
	GF_GLUSTERFS_CFLAGS="${GF_CFLAGS}"
	GF_LDADD="${ARGP_STANDALONE_LDADD}"
	GF_GLUSTERFS_LIBS="-lnsl -lresolv -lsocket"
        BUILD_FUSE_CLIENT=no
        FUSE_CLIENT_SUBDIR=""
	;;
     *netbsd*)
	GF_HOST_OS="GF_BSD_HOST_OS"
	GF_CFLAGS="${ARGP_STANDALONE_CPPFLAGS} -D_INCOMPLETE_XOPEN_C063"
	GF_CFLAGS="${GF_CFLAGS} -DTHREAD_UNSAFE_BASENAME"
	GF_CFLAGS="${GF_CFLAGS} -DTHREAD_UNSAFE_DIRNAME"
	GF_GLUSTERFS_CFLAGS="${GF_CFLAGS}"
	GF_LDADD="${ARGP_STANDALONE_LDADD}"
	if test "x$ac_cv_header_execinfo_h" = "xyes"; then
	   GF_GLUSTERFS_LIBS="-lexecinfo"
	fi
	GF_FUSE_LDADD="-lperfuse"
	BUILD_FUSE_CLIENT=yes
	LEXLIB=""
	;;
     *bsd*)
        GF_HOST_OS="GF_BSD_HOST_OS"
	GF_CFLAGS="${ARGP_STANDALONE_CPPFLAGS} -O0"
	GF_CFLAGS="${GF_CFLAGS} -DTHREAD_UNSAFE_BASENAME"
	GF_CFLAGS="${GF_CFLAGS} -DTHREAD_UNSAFE_DIRNAME"
	GF_GLUSTERFS_CFLAGS="${GF_CFLAGS}"
	GF_LDADD="${ARGP_STANDALONE_LDADD}"
	if test "x$ac_cv_header_execinfo_h" = "xyes"; then
	   GF_GLUSTERFS_LIBS="-lexecinfo"
	fi
	BUILD_FUSE_CLIENT=no
	;;
     darwin*)
        GF_HOST_OS="GF_DARWIN_HOST_OS"
	LIBTOOL=glibtool
	GF_CFLAGS="${ARGP_STANDALONE_CPPFLAGS} -D__DARWIN_64_BIT_INO_T -bundle -undefined suppress -flat_namespace -D_XOPEN_SOURCE -O0"
	GF_CFLAGS="${GF_CFLAGS} -DTHREAD_UNSAFE_BASENAME"
	GF_CFLAGS="${GF_CFLAGS} -DTHREAD_UNSAFE_DIRNAME"
	GF_GLUSTERFS_CFLAGS="${ARGP_STANDALONE_CPPFLAGS} -D__DARWIN_64_BIT_INO_T -undefined suppress -flat_namespace -O0"
	GF_LDADD="${ARGP_STANDALONE_LDADD}"
	GF_FUSE_CFLAGS="-I\$(CONTRIBDIR)/macfuse"
	;;
esac

# enable debug section
AC_ARG_ENABLE([debug],
              AC_HELP_STRING([--enable-debug],
                             [Enable debug build options.]))

DEBUG=no
if test "x$enable_debug" = "xyes"; then
   DEBUG=yes
   CFLAGS="-O0 $CFLAGS"
else
   CFLAGS="-O2 $CFLAGS"
   DEBUG=no
fi
AC_SUBST(CFLAGS)
# end enable debug section

BUILD_READLINE=no
AC_CHECK_LIB([readline -lcurses],[readline],[RLLIBS="-lreadline -lcurses"])
AC_CHECK_LIB([readline -ltermcap],[readline],[RLLIBS="-lreadline -ltermcap"])
AC_CHECK_LIB([readline -lncurses],[readline],[RLLIBS="-lreadline -lncurses"])

if test "x$RLLIBS" != "x"; then
   AC_DEFINE(HAVE_READLINE, 1, [readline enabled CLI])
   BUILD_READLINE=yes
fi

BUILD_LIBAIO=no
AC_CHECK_LIB([aio],[io_setup],[LIBAIO="-laio"])

if test "x$LIBAIO" != "x"; then
   AC_DEFINE(HAVE_LIBAIO, 1, [libaio based POSIX enabled])
   BUILD_LIBAIO=yes
fi


AC_SUBST(GF_HOST_OS)
AC_SUBST([GF_GLUSTERFS_LIBS])
AC_SUBST(GF_GLUSTERFS_CFLAGS)
AC_SUBST(GF_CFLAGS)
AC_SUBST(GF_LDFLAGS)
AC_SUBST(GF_LDADD)
AC_SUBST(GF_FUSE_LDADD)
AC_SUBST(GF_FUSE_CFLAGS)
AC_SUBST(RLLIBS)
AC_SUBST(LIBAIO)
AC_SUBST(AM_MAKEFLAGS)
AC_SUBST(AM_LIBTOOLFLAGS)

CONTRIBDIR='$(top_srcdir)/contrib'
AC_SUBST(CONTRIBDIR)

GF_CPPDEFINES='-D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D$(GF_HOST_OS)'
GF_CPPINCLUDES='-I$(top_srcdir)/libglusterfs/src -I$(CONTRIBDIR)/uuid'
GF_CPPFLAGS="$GF_CPPDEFINES $GF_CPPINCLUDES"
AC_SUBST([GF_CPPFLAGS])

AM_CONDITIONAL([GF_DARWIN_HOST_OS], test "${GF_HOST_OS}" = "GF_DARWIN_HOST_OS")

AM_CONDITIONAL([GF_INSTALL_VAR_LIB_GLUSTERD], test ! -d ${localstatedir}/lib/glusterd && test -d ${sysconfdir}/glusterd )

AC_OUTPUT

echo
echo "GlusterFS configure summary"
echo "==========================="
echo "FUSE client        : $BUILD_FUSE_CLIENT"
echo "Infiniband verbs   : $BUILD_IBVERBS"
echo "epoll IO multiplex : $BUILD_EPOLL"
echo "argp-standalone    : $BUILD_ARGP_STANDALONE"
echo "fusermount         : $BUILD_FUSERMOUNT"
echo "readline           : $BUILD_READLINE"
echo "georeplication     : $BUILD_SYNCDAEMON"
echo "Linux-AIO          : $BUILD_LIBAIO"
echo "Enable Debug       : $DEBUG"
echo "systemtap          : $BUILD_SYSTEMTAP"
echo
