dnl  Copyright (c) 2006-2012 Red Hat, Inc. <http://www.redhat.com>
dnl  This file is part of GlusterFS.
dnl
dnl  This file is licensed to you under your choice of the GNU Lesser
dnl  General Public License, version 3 or any later version (LGPLv3 or
dnl  later), or the GNU General Public License, version 2 (GPLv2), in all
dnl  cases as published by the Free Software Foundation.

AC_INIT([glusterfs],
        [m4_esyscmd([build-aux/pkg-version --version])],
        [gluster-users@gluster.org],,[https://github.com/gluster/glusterfs.git])

AC_SUBST([PACKAGE_RELEASE],
         [m4_esyscmd([build-aux/pkg-version --release])])

AM_INIT_AUTOMAKE

# Removes warnings when using automake 1.14 around (...but option 'subdir-objects' is disabled )
#but libglusterfs fails to build with contrib (Then are not set up that way?)
#AM_INIT_AUTOMAKE([subdir-objects])

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

AC_CONFIG_HEADERS([config.h])

AC_CONFIG_FILES([Makefile
                libglusterfs/Makefile
                libglusterfs/src/Makefile
                geo-replication/src/peer_gsec_create
                geo-replication/src/peer_add_secret_pub
                geo-replication/syncdaemon/configinterface.py
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
                xlators/meta/Makefile
                xlators/meta/src/Makefile
                xlators/mount/Makefile
                xlators/mount/fuse/Makefile
                xlators/mount/fuse/src/Makefile
                xlators/mount/fuse/utils/mount.glusterfs
                xlators/mount/fuse/utils/mount_glusterfs
                xlators/mount/fuse/utils/Makefile
                xlators/storage/Makefile
                xlators/storage/posix/Makefile
                xlators/storage/posix/src/Makefile
                xlators/storage/bd/Makefile
                xlators/storage/bd/src/Makefile
                xlators/cluster/Makefile
                xlators/cluster/afr/Makefile
                xlators/cluster/afr/src/Makefile
                xlators/cluster/stripe/Makefile
                xlators/cluster/stripe/src/Makefile
                xlators/cluster/dht/Makefile
                xlators/cluster/dht/src/Makefile
                xlators/cluster/ec/Makefile
                xlators/cluster/ec/src/Makefile
                xlators/performance/Makefile
                xlators/performance/write-behind/Makefile
                xlators/performance/write-behind/src/Makefile
                xlators/performance/read-ahead/Makefile
                xlators/performance/read-ahead/src/Makefile
                xlators/performance/readdir-ahead/Makefile
                xlators/performance/readdir-ahead/src/Makefile
                xlators/performance/io-threads/Makefile
                xlators/performance/io-threads/src/Makefile
                xlators/performance/io-cache/Makefile
                xlators/performance/io-cache/src/Makefile
                xlators/performance/symlink-cache/Makefile
                xlators/performance/symlink-cache/src/Makefile
                xlators/performance/quick-read/Makefile
                xlators/performance/quick-read/src/Makefile
                xlators/performance/open-behind/Makefile
                xlators/performance/open-behind/src/Makefile
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
                xlators/features/changelog/Makefile
                xlators/features/changelog/src/Makefile
                xlators/features/changelog/lib/Makefile
                xlators/features/changelog/lib/src/Makefile
                xlators/features/glupy/Makefile
                xlators/features/glupy/examples/Makefile
                xlators/features/glupy/src/Makefile
                xlators/features/glupy/src/setup.py
                xlators/features/glupy/src/__init__.py
                xlators/features/locks/Makefile
                xlators/features/locks/src/Makefile
                xlators/features/quota/Makefile
                xlators/features/quota/src/Makefile
                xlators/features/marker/Makefile
                xlators/features/marker/src/Makefile
                xlators/features/read-only/Makefile
                xlators/features/read-only/src/Makefile
                xlators/features/compress/Makefile
                xlators/features/compress/src/Makefile
                xlators/features/mac-compat/Makefile
                xlators/features/mac-compat/src/Makefile
                xlators/features/quiesce/Makefile
                xlators/features/quiesce/src/Makefile
                xlators/features/barrier/Makefile
                xlators/features/barrier/src/Makefile
                xlators/features/index/Makefile
                xlators/features/index/src/Makefile
                xlators/features/protect/Makefile
                xlators/features/protect/src/Makefile
                xlators/features/gfid-access/Makefile
                xlators/features/gfid-access/src/Makefile
                xlators/features/snapview-server/Makefile
                xlators/features/snapview-server/src/Makefile
                xlators/features/snapview-client/Makefile
                xlators/features/snapview-client/src/Makefile
                xlators/features/bit-rot/Makefile
                xlators/features/bit-rot/src/Makefile
                xlators/features/bit-rot/src/stub/Makefile
                xlators/playground/Makefile
                xlators/playground/template/Makefile
                xlators/playground/template/src/Makefile
                xlators/encryption/Makefile
                xlators/encryption/rot-13/Makefile
                xlators/encryption/rot-13/src/Makefile
                xlators/encryption/crypt/Makefile
                xlators/encryption/crypt/src/Makefile
                xlators/features/qemu-block/Makefile
                xlators/features/qemu-block/src/Makefile
                xlators/system/Makefile
                xlators/system/posix-acl/Makefile
                xlators/system/posix-acl/src/Makefile
                xlators/nfs/Makefile
                xlators/nfs/server/Makefile
                xlators/nfs/server/src/Makefile
                xlators/mgmt/Makefile
                xlators/mgmt/glusterd/Makefile
                xlators/mgmt/glusterd/src/Makefile
                cli/Makefile
                cli/src/Makefile
                doc/Makefile
                extras/Makefile
                extras/glusterd.vol
                extras/init.d/Makefile
                extras/init.d/glusterd.plist
                extras/init.d/glusterd-Debian
                extras/init.d/glusterd-Redhat
                extras/init.d/glusterd-FreeBSD
                extras/init.d/glusterd-SuSE
                extras/systemd/Makefile
                extras/systemd/glusterd.service
                extras/run-gluster.tmpfiles
                extras/benchmarking/Makefile
                extras/hook-scripts/Makefile
                extras/ocf/Makefile
                extras/ocf/glusterd
                extras/ocf/volume
                extras/LinuxRPM/Makefile
                extras/geo-rep/Makefile
                extras/hook-scripts/add-brick/Makefile
                extras/hook-scripts/add-brick/pre/Makefile
                extras/hook-scripts/add-brick/post/Makefile
                extras/hook-scripts/start/Makefile
                extras/hook-scripts/start/post/Makefile
                extras/hook-scripts/set/Makefile
                extras/hook-scripts/set/post/Makefile
                extras/hook-scripts/stop/Makefile
                extras/hook-scripts/stop/pre/Makefile
                extras/hook-scripts/reset/Makefile
                extras/hook-scripts/reset/post/Makefile
                extras/hook-scripts/reset/pre/Makefile
                contrib/fuse-util/Makefile
                contrib/umountd/Makefile
                contrib/uuid/uuid_types.h
                glusterfs-api.pc
                libgfchangelog.pc
                api/Makefile
                api/src/Makefile
                api/examples/Makefile
                geo-replication/Makefile
                geo-replication/src/Makefile
                geo-replication/syncdaemon/Makefile
                heal/Makefile
                heal/src/Makefile
                glusterfs.spec])

AC_CANONICAL_HOST

AC_PROG_CC
AC_DISABLE_STATIC
AC_PROG_LIBTOOL
AC_SUBST([shrext_cmds])

AC_CHECK_PROG([RPCGEN], [rpcgen], [yes], [no])

if test "x$RPCGEN" = "xno"; then
   AC_MSG_ERROR([`rpcgen` not found, glusterfs needs `rpcgen` exiting..])
fi

# Initialize CFLAGS before usage
AC_ARG_ENABLE([debug],
              AC_HELP_STRING([--enable-debug],
                             [Enable debug build options.]))
if test "x$enable_debug" = "xyes"; then
        BUILD_DEBUG=yes
        CFLAGS="${CFLAGS} -g -O0 -DDEBUG"
else
        BUILD_DEBUG=no
        CFLAGS="${CFLAGS} -g -O2"
fi

case $host_os in
  darwin*)
    if ! test "`/usr/bin/sw_vers | grep ProductVersion: | cut -f 2 | cut -d. -f2`" -ge 7; then
       AC_MSG_ERROR([You need at least OS X 10.7 (Lion) to build Glusterfs])
    fi
    # OSX version lesser than 9 has llvm/clang optimization issues which leads to various segfaults
    if test "`/usr/bin/sw_vers | grep ProductVersion: | cut -f 2 | cut -d. -f2`" -lt 9; then
       CFLAGS="${CFLAGS} -g -O0 -DDEBUG"
    fi
    ;;
esac

AC_ARG_WITH([previous-options],
        [AS_HELP_STRING([--with-previous-options],
                        [read config.status for configure options])
        ],
        [ if test -r ./config.status && \
             args=$(grep 'ac_cs_config=' config.status | \
                    sed -e 's/.*"\(.*\)".*/\1/'| sed -e "s/'//g") ; then
            echo "###"
            echo "### Rerunning as '$0 $args'"
            echo "###"
            exec $0 $args
          fi
        ])

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

AC_ARG_WITH(systemddir,
            [  --with-systemddir=DIR systemd service files in DIR @<:@/usr/lib/systemd/system@:>@],
            [systemddir=$withval],
            [systemddir='/usr/lib/systemd/system'])
AC_SUBST(systemddir)

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

AC_ARG_WITH(tmpfilesdir,
            AC_HELP_STRING([--with-tmpfilesdir=DIR],
                           [tmpfiles config in DIR, disabled by default]),
            [tmpfilesdir=$withval],
            [tmpfilesdir=''])
AC_SUBST(tmpfilesdir)

AC_ARG_WITH([ocf],
            [AS_HELP_STRING([--without-ocf], [build OCF-compliant cluster resource agents])],
            ,
            [OCF_SUBDIR='ocf'],
            )
AC_SUBST(OCF_SUBDIR)

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


PKG_CHECK_MODULES([UNITTEST], [cmockery2], [
    UNITTEST_CPPFLAGS=`${PKG_CONFIG} --print-errors --cflags-only-I "cmockery2"`
    UNITTEST_CFLAGS=`${PKG_CONFIG} --print-errors --cflags-only-other "cmockery2"`
    UNITTEST_LDFLAGS=`${PKG_CONFIG} --print-errors --libs-only-L "cmockery2"`
    UNITTEST_LIBS=`${PKG_CONFIG} --print-errors --libs-only-l "cmockery2"`
],[
  AC_CHECK_LIB([cmockery], [mock_assert], [
    UNITTEST_LDFLAGS="-lcmockery -lgcov"
    UNITTEST_CFLAGS="-Wall -Werror -DUNIT_TESTING=1"
  ], [
    AC_MSG_ERROR([cmockery2 library is required to build glusterfs])
  ])
])

AC_CHECK_FUNC([dlopen], [has_dlopen=yes], AC_CHECK_LIB([dl], [dlopen], , AC_MSG_ERROR([Dynamic linking library required to build glusterfs])))

AC_CHECK_LIB([readline], [rl_do_undo], [RL_UNDO="yes"], [RL_UNDO="no"])

AC_CHECK_LIB([intl], [gettext])

AC_CHECK_HEADERS([sys/xattr.h])

AC_CHECK_HEADERS([sys/ioctl.h], AC_DEFINE(HAVE_IOCTL_IN_SYS_IOCTL_H, 1, [have sys/ioctl.h]))

AC_CHECK_HEADERS([sys/extattr.h])

# libglusterfs/checksum
AC_CHECK_HEADERS([openssl/md5.h])
AC_CHECK_LIB([z], [adler32], [ZLIB_LIBS="-lz"], AC_MSG_ERROR([zlib is required to build glusterfs]))
AC_SUBST(ZLIB_LIBS)

AC_CHECK_HEADERS([linux/falloc.h])

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

dnl test umount2 function
AC_CHECK_FUNC([umount2], [have_umount2=yes])

if test "x${have_umount2}" = "xyes"; then
   AC_DEFINE(HAVE_UMOUNT2, 1, [define if found umount2])
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

AC_ARG_ENABLE([bd-xlator],
              AC_HELP_STRING([--enable-bd-xlator], [Build BD xlator]))

if test "x$enable_bd_xlator" != "xno"; then
  AC_CHECK_LIB([lvm2app],
               [lvm_init,lvm_lv_from_name],
               [HAVE_BD_LIB="yes"],
               [HAVE_BD_LIB="no"])

if test "x$HAVE_BD_LIB" = "xyes"; then
    # lvm_lv_from_name() has been made public with lvm2-2.02.79
    AC_CHECK_DECLS(
                  [lvm_lv_from_name],
                  [NEED_LVM_LV_FROM_NAME_DECL="no"],
                  [NEED_LVM_LV_FROM_NAME_DECL="yes"],
                  [[#include <lvm2app.h>]])
  fi
fi

if test "x$enable_bd_xlator" = "xyes" -a "x$HAVE_BD_LIB" = "xno"; then
   echo "BD xlator requested but required lvm2 development library not found."
   exit 1
fi

BUILD_BD_XLATOR=no
if test "x${enable-bd-xlator}" != "xno" -a "x${HAVE_BD_LIB}" = "xyes"; then
  BUILD_BD_XLATOR=yes
  AC_DEFINE(HAVE_BD_XLATOR, 1, [define if lvm2app library found and bd xlator
                            enabled])
  if test "x$NEED_LVM_LV_FROM_NAME_DECL" = "xyes"; then
    AC_DEFINE(NEED_LVM_LV_FROM_NAME_DECL, 1, [defined if lvm_lv_from_name()
                  was not found in the lvm2app.h header, but can be linked])
  fi
fi

AM_CONDITIONAL([ENABLE_BD_XLATOR], [test x$BUILD_BD_XLATOR = xyes])

# start encryption/crypt section

AC_CHECK_HEADERS([openssl/cmac.h], [have_cmac_h=yes], [have_cmac_h=no])

AC_ARG_ENABLE([crypt-xlator],
	AC_HELP_STRING([--enable-crypt-xlator], [Build crypt encryption xlator]))

if test "x$enable_crypt_xlator" = "xyes" -a "x$have_cmac_h" = "xno"; then
   AC_MSG_ERROR([Encryption xlator requires OpenSSL with cmac.h])
fi

BUILD_CRYPT_XLATOR=no
if test "x$enable_crypt_xlator" != "xno" -a "x$have_cmac_h" = "xyes"; then
   BUILD_CRYPT_XLATOR=yes
   AC_DEFINE(HAVE_CRYPT_XLATOR, 1, [enable building crypt encryption xlator])
fi

AM_CONDITIONAL([ENABLE_CRYPT_XLATOR], [test x$BUILD_CRYPT_XLATOR = xyes])

AC_SUBST(FUSE_CLIENT_SUBDIR)
# end FUSE section


# FUSERMOUNT section
AC_ARG_ENABLE([fusermount],
              AC_HELP_STRING([--disable-fusermount],
                             [Use system's fusermount]))

BUILD_FUSERMOUNT="yes"
if test "x$enable_fusermount" = "xno"; then
   BUILD_FUSERMOUNT="no"
else
   AC_DEFINE(GF_FUSERMOUNT, 1, [Use our own fusermount])
   FUSERMOUNT_SUBDIR="contrib/fuse-util"
fi

AC_SUBST(FUSERMOUNT_SUBDIR)
#end FUSERMOUNT section

# QEMU_BLOCK section

AC_ARG_ENABLE([qemu-block],
                AC_HELP_STRING([--enable-qemu-block],
                        [Build QEMU Block formats translator]))

if test "x$enable_qemu_block" != "xno"; then
  PKG_CHECK_MODULES([GLIB], [glib-2.0],
                    [HAVE_GLIB_2="yes"],
  		    [HAVE_GLIB_2="no"])
fi

if test "x$enable_qemu_block" = "xyes" -a "x$HAVE_GLIB_2" = "xno"; then
   echo "QEMU Block formats translator requires libglib-2.0, but missing."
   exit 1
fi

BUILD_QEMU_BLOCK=no
if test "x${enable_qemu_block}" != "xno" -a "x${HAVE_GLIB_2}" = "xyes"; then
  BUILD_QEMU_BLOCK=yes
  AC_DEFINE(HAVE_QEMU_BLOCK, 1, [define if libglib-2.0 library found and QEMU
                  	     Block translator enabled])
fi


# end QEMU_BLOCK section

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
  AC_CHECK_LIB([rdmacm], [rdma_create_id], [HAVE_RDMACM="yes"], [HAVE_RDMACM="no"])
fi

if test "x$enable_ibverbs" = "xyes"; then
   if test "x$HAVE_LIBIBVERBS" = "xno"; then
      echo "ibverbs-transport requested, but libibverbs is not present."
      exit 1
   fi

   if test "x$HAVE_RDMACM" = "xno"; then
      echo "ibverbs-transport requested, but librdmacm is not present."
      exit 1
   fi
fi

BUILD_RDMA=no
BUILD_IBVERBS=no
if test "x$enable_ibverbs" != "xno" -a "x$HAVE_LIBIBVERBS" = "xyes" -a "x$HAVE_RDMACM" = "xyes"; then
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
  SYNCDAEMON_SUBDIR=geo-replication
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

# only install scripts from extras/geo-rep when enabled
if test "x$enable_georeplication" != "xno"; then
  GEOREP_EXTRAS_SUBDIR=geo-rep
fi
AC_SUBST(GEOREP_EXTRAS_SUBDIR)

# CDC xlator - check if libz is present if so enable HAVE_LIB_Z
BUILD_CDC=yes
PKG_CHECK_MODULES([ZLIB], [zlib >= 1.2.0],,
                  [AC_CHECK_LIB([z], [deflate], [ZLIB_LIBS="-lz"],
                                [BUILD_CDC=no])])
echo -n "features requiring zlib enabled: "
if test "x$BUILD_CDC" = "xyes" ; then
  echo "yes"
  AC_DEFINE(HAVE_LIB_Z, 1, [define if zlib is present])
else
  echo "no"
fi
AC_SUBST(ZLIB_CFLAGS)
AC_SUBST(ZLIB_LIBS)
# end CDC xlator secion

# check for systemtap/dtrace
BUILD_SYSTEMTAP=no
AC_MSG_CHECKING([whether to include systemtap tracing support])
AC_ARG_ENABLE([systemtap],
              [AS_HELP_STRING([--enable-systemtap],
              [Enable inclusion of systemtap trace support])],
              [ENABLE_SYSTEMTAP="${enableval}"], [ENABLE_SYSTEMTAP="def"])

AM_CONDITIONAL([ENABLE_SYSTEMTAP], [test "x${ENABLE_SYSTEMTAP}" = "xyes"])
AC_MSG_RESULT(${ENABLE_SYSTEMTAP})

if test "x${ENABLE_SYSTEMTAP}" != "xno"; then
    AC_CHECK_PROG(DTRACE, dtrace, "yes", "no")
    AC_CHECK_HEADER([sys/sdt.h], [SDT_H_FOUND="yes"],
                    [SDT_H_FOUND="no"])
fi

if test "x${ENABLE_SYSTEMTAP}" = "xyes"; then
    if test "x${DTRACE}" = "xno"; then
        AC_MSG_ERROR([dtrace not found])
    elif test "$x{SDT_H_FOUND}" = "xno"; then
        AC_MSG_ERROR([systemtap support needs sys/sdt.h header])
    fi
fi

if test "x${DTRACE}" = "xyes" -a "x${SDT_H_FOUND}" = "xyes"; then
    AC_MSG_CHECKING([x"${DTRACE}"xy"${SDT_H_FOUND}"y])
    AC_DEFINE([HAVE_SYSTEMTAP], [1], [Define to 1 if using  probes.])
    BUILD_SYSTEMTAP=yes
fi
# end of systemtap/dtrace

# xml-output
AC_ARG_ENABLE([xml-output],
              AC_HELP_STRING([--disable-xml-output],
                             [Disable the xml output]))
BUILD_XML_OUTPUT="yes"
if test "x$enable_xml_output" != "xno"; then
    #check if libxml is present if so enable HAVE_LIB_XML
    m4_ifdef([AM_PATH_XML2],[AM_PATH_XML2([2.6.19])], [no_xml=yes])
    if test "x${no_xml}" = "x"; then
        AC_DEFINE([HAVE_LIB_XML], [1], [Define to 1 if using libxml2.])
    else
	if test "x$enable_georeplication" != "xno"; then
           AC_MSG_ERROR([libxml2 devel libraries not found])
	else
	   AC_MSG_WARN([libxml2 devel libraries not found disabling XML support])
           BUILD_XML_OUTPUT="no"
	fi

    fi
else
    if test "x$enable_georeplication" != "xno"; then
       AC_MSG_ERROR([geo-replication requires xml output])
    fi
    BUILD_XML_OUTPUT="no"
fi
# end of xml-output

AC_CHECK_HEADERS([execinfo.h], [have_backtrace=yes])
if test "x${have_backtrace}" = "xyes"; then
   AC_DEFINE(HAVE_BACKTRACE, 1, [define if found backtrace])
fi
AC_SUBST(HAVE_BACKTRACE)

if test "x${have_backtrace}" != "xyes"; then
AC_TRY_COMPILE([#include <math.h>], [double x=0.0; x=ceil(0.0);],
   [have_math_h=yes],
   AC_MSG_ERROR([need math library for libexecinfo]))
if test "x${have_math_h}" = "xyes"; then
   LIBS="$LIBS -lm"
fi
fi

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
        CFLAGS="${CFLAGS} -D_INCOMPLETE_XOPEN_C063 -DCONFIG_MACHINE_BSWAP_H"
        ;;
esac
AC_CHECK_FUNC([linkat], [have_linkat=yes])
if test "x${have_linkat}" = "xyes"; then
   AC_DEFINE(HAVE_LINKAT, 1, [define if found linkat])
fi
AC_SUBST(HAVE_LINKAT)

dnl check for Monotonic clock
AC_CHECK_LIB([rt], [clock_gettime], ,
             AC_MSG_WARN([System doesn't have monotonic clock using contrib]))

dnl Add cmockery2 for unit tests
case $host_os in
  freebsd*)
    dnl remove --coverage on FreeBSD due to a known llvm packaging bug
    UNITTEST_CFLAGS="${UNITTEST_CPPFLAGS} ${UNITTEST_CFLAGS} -g -DDEBUG -O0"
    UNITTEST_LDFLAGS="${UNITTEST_LIBS} ${UNITTEST_LDFLAGS}"
    ;;
  *)
    UNITTEST_CFLAGS="${UNITTEST_CPPFLAGS} ${UNITTEST_CFLAGS} -g -DDEBUG -O0 --coverage"
    UNITTEST_LDFLAGS="${UNITTEST_LIBS} ${UNITTEST_LDFLAGS}"
    ;;
esac

AC_SUBST(UNITTEST_CFLAGS)
AC_SUBST(UNITTEST_LDFLAGS)

dnl Check for argp
AC_CHECK_HEADER([argp.h], AC_DEFINE(HAVE_ARGP, 1, [have argp]))

BUILD_ARGP_STANDALONE=no
if test "x${ac_cv_header_argp_h}" = "xno"; then
   AC_CONFIG_SUBDIRS(contrib/argp-standalone)
   BUILD_ARGP_STANDALONE=yes
   ARGP_STANDALONE_CPPFLAGS='-I${top_srcdir}/contrib/argp-standalone'
   ARGP_STANDALONE_LDADD='${top_builddir}/contrib/argp-standalone/libargp.a'
   ARGP_STANDALONE_DIR='${top_builddir}/contrib/argp-standalone'
fi

AC_SUBST(ARGP_STANDALONE_CPPFLAGS)
AC_SUBST(ARGP_STANDALONE_LDADD)
AC_SUBST(ARGP_STANDALONE_DIR)

AC_CHECK_HEADER([malloc.h], AC_DEFINE(HAVE_MALLOC_H, 1, [have malloc.h]))

AC_CHECK_FUNC([llistxattr], [have_llistxattr=yes])
if test "x${have_llistxattr}" = "xyes"; then
   AC_DEFINE(HAVE_LLISTXATTR, 1, [define if llistxattr exists])
fi

AC_CHECK_FUNC([fdatasync], [have_fdatasync=no])
if test "x${have_fdatasync}" = "xyes"; then
   AC_DEFINE(HAVE_FDATASYNC, 1, [define if fdatasync exists])
fi

AC_CHECK_FUNC([fallocate], [have_fallocate=yes])
if test "x${have_fallocate}" = "xyes"; then
   AC_DEFINE(HAVE_FALLOCATE, 1, [define if fallocate exists])
fi

AC_CHECK_FUNC([posix_fallocate], [have_posix_fallocate=yes])
if test "x${have_posix_fallocate}" = "xyes"; then
   AC_DEFINE(HAVE_POSIX_FALLOCATE, 1, [define if posix_fallocate exists])
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

dnl check for gcc -Werror=format-security
saved_GF_CFLAGS="-Wformat -Werror=format-security"
AC_MSG_CHECKING([whether $CC accepts -Werror=format-security])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM()], [cc_werror_format_security=yes], [cc_werror_format_security=no])
echo $cc_werror_format_security
if test "x$cc_werror_format_security" = "xno"; then
    GF_CFLAGS="$GF_CFLAGS"
else
    GF_CFLAGS="$saved_GF_CFLAGS $GF_CFLAGS"
fi

dnl check for gcc -Werror=implicit-function-declaration
saved_GF_CFLAGS=$GF_CFLAGS
GF_CFLAGS="-Werror=implicit-function-declaration"
AC_MSG_CHECKING([whether $CC accepts -Werror=implicit-function-declaration])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM()], [cc_werror_implicit=yes], [cc_werror_implicit=no])
echo $cc_werror_implicit
if test "x$cc_werror_implicit" = "xno"; then
    GF_CFLAGS="$saved_GF_CFLAGS"
else
    GF_CFLAGS="$saved_GF_CFLAGS $GF_CFLAGS"
fi

dnl clang is mostly GCC-compatible, but its version is much lower,
dnl so we have to check for it.
AC_MSG_CHECKING([if compiling with clang])

AC_COMPILE_IFELSE(
[AC_LANG_PROGRAM([], [[
#ifndef __clang__
       not clang
#endif
]])],
[CLANG=yes], [CLANG=no])

AC_MSG_RESULT([$CLANG])

if test "x$CLANG" = "xyes"; then
   GF_COMPILER_FLAGS="-Wno-gnu"
fi

if test "x$ac_cv_header_execinfo_h" = "xno"; then
   # The reason is that __builtin_frame_address(n) for n > 0 seems
   # to just crash on most platforms when -fomit-stack-pointer is
   # specified, which seems to be the default for many platforms on
   # -O2. The documentation says that __builtin_frame_address()
   # should return NULL in case it can't get the frame, but it
   # seems to crash instead.

   # execinfo.c in ./contrib/libexecinfo uses __builtin_frame_address(n)
   # for providing cross platform backtrace*() functions.
   if test "x$CLANG" = "xno"; then
      CFLAGS="$CFLAGS -fno-omit-frame-pointer"
   fi
fi

### Dirty hacky stuff to make LOCALSTATEDIR work
if test "x$prefix" = xNONE; then
   test $localstatedir = '${prefix}/var' && localstatedir=$ac_default_prefix/var
   localstatedir=/var
   LOCALSTATEDIR=$(eval echo ${localstatedir})
else
   LOCALSTATEDIR=$(eval echo ${localstatedir})
fi

case $host_os in
     linux*)
        GF_HOST_OS="GF_LINUX_HOST_OS"
        GF_CFLAGS="${GF_COMPILER_FLAGS} ${ARGP_STANDALONE_CPPFLAGS}"
        GF_LDADD="${ARGP_STANDALONE_LDADD}"
        GF_FUSE_CFLAGS="-DFUSERMOUNT_DIR=\\\"\$(bindir)\\\""
        GLUSTERD_WORKDIR="${LOCALSTATEDIR}/lib/glusterd"
        ;;
     solaris*)
        GF_HOST_OS="GF_SOLARIS_HOST_OS"
        GF_CFLAGS="${ARGP_STANDALONE_CPPFLAGS} -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS -m64"
        GF_LDFLAGS=""
        GF_LDADD="${ARGP_STANDALONE_LDADD}"
        BUILD_FUSE_CLIENT=no
        FUSE_CLIENT_SUBDIR=""
        GLUSTERD_WORKDIR="${LOCALSTATEDIR}/lib/glusterd"
        ;;
     *netbsd*)
        GF_HOST_OS="GF_BSD_HOST_OS"
        GF_CFLAGS="${ARGP_STANDALONE_CPPFLAGS} -D_INCOMPLETE_XOPEN_C063"
        GF_CFLAGS="${GF_CFLAGS} -DTHREAD_UNSAFE_BASENAME"
        GF_CFLAGS="${GF_CFLAGS} -DTHREAD_UNSAFE_DIRNAME"
        GF_FUSE_CFLAGS="-DFUSERMOUNT_DIR=\\\"\$(sbindir)\\\""
        GF_LDADD="${ARGP_STANDALONE_LDADD}"
        if test "x$ac_cv_header_execinfo_h" = "xyes"; then
           GF_LDFLAGS="-lexecinfo"
        fi
        GF_FUSE_LDADD="-lperfuse"
        BUILD_FUSE_CLIENT=yes
        LEXLIB=""
        BUILD_FUSERMOUNT=no
        FUSERMOUNT_SUBDIR=""
        GLUSTERD_WORKDIR="${LOCALSTATEDIR}/db/glusterd"
        ;;
     *freebsd*)
        GF_HOST_OS="GF_BSD_HOST_OS"
        GF_CFLAGS="${GF_COMPILER_FLAGS} ${ARGP_STANDALONE_CPPFLAGS} -O0"
        GF_CFLAGS="${GF_CFLAGS} -DTHREAD_UNSAFE_BASENAME"
        GF_CFLAGS="${GF_CFLAGS} -DTHREAD_UNSAFE_DIRNAME"
        GF_CFLAGS="${GF_CFLAGS} -D_LIBGEN_H_"
        GF_CFLAGS="${GF_CFLAGS} -DO_DSYNC=0"
        GF_CFLAGS="${GF_CFLAGS} -Dxdr_quad_t=xdr_longlong_t"
        GF_CFLAGS="${GF_CFLAGS} -Dxdr_u_quad_t=xdr_u_longlong_t"
        GF_FUSE_CFLAGS="-DFUSERMOUNT_DIR=\\\"\$(sbindir)\\\""
        GF_LDADD="${ARGP_STANDALONE_LDADD}"
        if test "x$ac_cv_header_execinfo_h" = "xyes"; then
           GF_LDFLAGS="-lexecinfo"
        fi
        BUILD_FUSE_CLIENT=yes
        BUILD_FUSERMOUNT=no
        FUSERMOUNT_SUBDIR=""
        GLUSTERD_WORKDIR="${LOCALSTATEDIR}/db/glusterd"
        ;;
     darwin*)
        GF_HOST_OS="GF_DARWIN_HOST_OS"
        LIBTOOL=glibtool
        GF_CFLAGS="${GF_COMPILER_FLAGS} ${ARGP_STANDALONE_CPPFLAGS} "
        GF_CFLAGS="${GF_CFLAGS} -D_REENTRANT -D_XOPEN_SOURCE "
        GF_CFLAGS="${GF_CFLAGS} -D_DARWIN_USE_64_BIT_INODE "
        GF_CFLAGS="${GF_CFLAGS} -DTHREAD_UNSAFE_BASENAME"
        GF_CFLAGS="${GF_CFLAGS} -DTHREAD_UNSAFE_DIRNAME"
        GF_LDADD="${ARGP_STANDALONE_LDADD}"
        GF_LDFLAGS=""
        GF_FUSE_CFLAGS="-I\$(CONTRIBDIR)/macfuse"
        BUILD_FUSERMOUNT="no"
        FUSERMOUNT_SUBDIR=""
        GLUSTERD_WORKDIR="${LOCALSTATEDIR}/db/glusterd"
        ;;
esac

case $host_os in
     darwin*)
        GFAPI_EXTRA_LDFLAGS='-Wl,-alias_list,$(top_srcdir)/api/src/gfapi.aliases'
        ;;
     *)
        GFAPI_EXTRA_LDFLAGS='-Wl,--version-script=$(top_srcdir)/api/src/gfapi.map'
        ;;
esac

# lazy umount emulation
UMOUNTD_SUBDIR=""
if test "x${GF_HOST_OS}" != "xGF_LINUX_HOST_OS" ; then
        UMOUNTD_SUBDIR="contrib/umountd"
fi
AC_SUBST(UMOUNTD_SUBDIR)

# enable/disable QEMU
AM_CONDITIONAL([ENABLE_QEMU_BLOCK], [test x$BUILD_QEMU_BLOCK = xyes])


# enable debug section
AC_ARG_ENABLE([debug],
              AC_HELP_STRING([--enable-debug],
                             [Enable debug build options.]))


# syslog section
AC_ARG_ENABLE([syslog],
              AC_HELP_STRING([--disable-syslog],
                             [Disable syslog for logging]))

USE_SYSLOG="yes"
if test "x$enable_syslog" != "xno"; then
  AC_DEFINE(GF_USE_SYSLOG, 1, [Use syslog for logging])
else
  USE_SYSLOG="no"
fi
AM_CONDITIONAL([ENABLE_SYSLOG], [test x$USE_SYSLOG = xyes])
#end syslog section

BUILD_READLINE=no
AC_CHECK_LIB([readline -lcurses],[readline],[RLLIBS="-lreadline -lcurses"])
AC_CHECK_LIB([readline -ltermcap],[readline],[RLLIBS="-lreadline -ltermcap"])
AC_CHECK_LIB([readline -lncurses],[readline],[RLLIBS="-lreadline -lncurses"])

if test "x$RLLIBS" != "x"; then
   if test "x$RL_UNDO" = "xyes"; then
      AC_DEFINE(HAVE_READLINE, 1, [readline enabled CLI])
      BUILD_READLINE=yes
   else
      BUILD_READLINE="no (present but missing undo)"
   fi

fi

BUILD_LIBAIO=no
AC_CHECK_LIB([aio],[io_setup],[LIBAIO="-laio"])

if test "x$LIBAIO" != "x"; then
   AC_DEFINE(HAVE_LIBAIO, 1, [libaio based POSIX enabled])
   BUILD_LIBAIO=yes
fi

# glupy section
BUILD_GLUPY=no
have_python2=no
have_Python_h=no

AM_PATH_PYTHON()
if echo $PYTHON_VERSION | grep ^2; then
  have_python2=yes
fi

# Save flags before testing python
saved_CFLAGS=$CFLAGS
saved_CPPFLAGS=$CPPFLAGS
saved_LDFLAGS=$LDFLAGS

CFLAGS="`${PYTHON}-config --cflags`"
CPPFLAGS=$CFLAGS
LDFLAGS="-L`${PYTHON}-config --prefix`/lib -L`${PYTHON}-config --prefix`/$libdir `${PYTHON}-config --ldflags`"

AC_CHECK_HEADERS([python$PYTHON_VERSION/Python.h],[have_Python_h=yes],[])
AC_ARG_ENABLE([glupy],
              AS_HELP_STRING([--enable-glupy],
                             [build glupy]))
case x$enable_glupy in
   xyes)
      if test "x$have_python2" = "xyes" -a "x$have_Python_h" = "xyes"; then
        BUILD_GLUPY=yes
        PYTHONDEV_CFLAGS="$CFLAGS"
        PYTHONDEV_CPPFLAGS="$CPPFLAGS"
        PYTHONDEV_LDFLAGS="$LDFLAGS"
        AC_SUBST(PYTHONDEV_CFLAGS)
        AC_SUBST(PYTHONDEV_CPPFLAGS)
        AC_SUBST(PYTHONDEV_LDFLAGS)
      else
        AC_MSG_ERROR([glupy requires python-devel/python-dev package and python2.x])
      fi
      ;;
   xno)
      ;;
   *)
      if test "x$have_python2" = "xyes" -a "x$have_Python_h" = "xyes"; then
        BUILD_GLUPY=yes
        PYTHONDEV_CFLAGS="$CFLAGS"
        PYTHONDEV_CPPFLAGS="$CPPFLAGS"
        PYTHONDEV_LDFLAGS="$LDFLAGS"
        AC_SUBST(PYTHONDEV_CFLAGS)
        AC_SUBST(PYTHONDEV_CPPFLAGS)
        AC_SUBST(PYTHONDEV_LDFLAGS)
      else
        AC_MSG_WARN([
        ---------------------------------------------------------------------------------
         cannot build glupy. python 2.x and python-devel/python-dev package are required.
        ---------------------------------------------------------------------------------])
      fi
      ;;
esac
# Restore flags
CFLAGS=$saved_CFLAGS
CPPFLAGS=$saved_CPPFLAGS
LDFLAGS=$saved_LDFLAGS

case $host_os in
     darwin*)
        BUILD_GLUPY=no
        ;;
esac

if test "x$BUILD_GLUPY" = "xyes"; then
   BUILD_PYTHON_SITE_PACKAGES=`$PYTHON -c 'from distutils.sysconfig import get_python_lib; print(get_python_lib())'`
   BUILD_PYTHON_INC=`$PYTHON -c "from distutils import sysconfig; print sysconfig.get_python_inc()"`
   BUILD_PYTHON_LIB=python$PYTHON_VERSION
   GLUPY_SUBDIR=glupy
   GLUPY_SUBDIR_MAKEFILE=xlators/features/glupy/Makefile
   GLUPY_SUBDIR_SRC_MAKEFILE=xlators/features/glupy/src/Makefile
   echo "building glupy with -isystem $BUILD_PYTHON_INC -l $BUILD_PYTHON_LIB"
   AC_SUBST(BUILD_PYTHON_SITE_PACKAGES)
   AC_SUBST(BUILD_PYTHON_INC)
   AC_SUBST(BUILD_PYTHON_LIB)
   AC_SUBST(GLUPY_SUBDIR)
   AC_SUBST(GLUPY_SUBDIR_MAKEFILE)
   AC_SUBST(GLUPY_SUBDIR_SRC_MAKEFILE)
fi
# end glupy section

dnl Check for userspace-rcu
PKG_CHECK_MODULES([URCU], [liburcu-bp])
PKG_CHECK_MODULES([URCU_CDS], [liburcu-cds >= 0.8], [],
  [PKG_CHECK_MODULES([URCU_CDS], [liburcu-cds >= 0.7],
    [AC_DEFINE(URCU_0_7, 1, [Define if liburcu 0.7 is found])],
    [AC_MSG_ERROR([liburcu >= 0.7 required])])])

AC_SUBST(CFLAGS)
# end enable debug section

dnl Required if cmockery2 headers are not in standar paths
GF_CFLAGS="${GF_CFLAGS} ${UNITTEST_CPPFLAGS}"

AC_SUBST(GF_HOST_OS)
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

AM_CONDITIONAL([GF_LINUX_HOST_OS], test "${GF_HOST_OS}" = "GF_LINUX_HOST_OS")
AM_CONDITIONAL([GF_DARWIN_HOST_OS], test "${GF_HOST_OS}" = "GF_DARWIN_HOST_OS")
AM_CONDITIONAL([GF_BSD_HOST_OS], test "${GF_HOST_OS}" = "GF_BSD_HOST_OS")

AC_SUBST(GLUSTERD_WORKDIR)
AM_CONDITIONAL([GF_INSTALL_GLUSTERD_WORKDIR], test ! -d ${GLUSTERD_WORKDIR} && test -d ${sysconfdir}/glusterd )

dnl pkg-config versioning
dnl
dnl Once we released gluster-api.pc with version=4. Since then we undid the
dnl library versioning and replaced it with symbol-versioning. The current
dnl libgfapi.so has version 0, but the symbols have the version from the main
dnl package at the time they were added.
dnl
dnl Because other packages (like samba) use the pkg-config version, we can not
dnl drop it, or decrease the version easily. The simplest solution is to keep
dnl the version=4 and add sub-digits for the actual package/symbol versions.
GFAPI_VERSION="4."${PACKAGE_VERSION}
LIBGFCHANGELOG_VERSION="0.0.1"
AC_SUBST(GFAPI_VERSION)
AC_SUBST(LIBGFCHANGELOG_VERSION)

dnl libtool versioning
LIBGFXDR_LT_VERSION="0:1:0"
LIBGFRPC_LT_VERSION="0:1:0"
LIBGLUSTERFS_LT_VERSION="0:1:0"
LIBGFCHANGELOG_LT_VERSION="0:1:0"
GFAPI_LT_VERSION="0:0:0"
AC_SUBST(LIBGFXDR_LT_VERSION)
AC_SUBST(LIBGFRPC_LT_VERSION)
AC_SUBST(LIBGLUSTERFS_LT_VERSION)
AC_SUBST(LIBGFCHANGELOG_LT_VERSION)
AC_SUBST(GFAPI_LT_VERSION)
AC_SUBST(GFAPI_EXTRA_LDFLAGS)

dnl this change necessary for run-tests.sh
AC_CONFIG_FILES([tests/env.rc],[ln -s ${ac_abs_builddir}/env.rc ${ac_abs_srcdir}/env.rc 2>/dev/null])

AC_OUTPUT

echo
echo "GlusterFS configure summary"
echo "==========================="
echo "FUSE client          : $BUILD_FUSE_CLIENT"
echo "Infiniband verbs     : $BUILD_IBVERBS"
echo "epoll IO multiplex   : $BUILD_EPOLL"
echo "argp-standalone      : $BUILD_ARGP_STANDALONE"
echo "fusermount           : $BUILD_FUSERMOUNT"
echo "readline             : $BUILD_READLINE"
echo "georeplication       : $BUILD_SYNCDAEMON"
echo "Linux-AIO            : $BUILD_LIBAIO"
echo "Enable Debug         : $BUILD_DEBUG"
echo "systemtap            : $BUILD_SYSTEMTAP"
echo "Block Device xlator  : $BUILD_BD_XLATOR"
echo "glupy                : $BUILD_GLUPY"
echo "Use syslog           : $USE_SYSLOG"
echo "XML output           : $BUILD_XML_OUTPUT"
echo "QEMU Block formats   : $BUILD_QEMU_BLOCK"
echo "Encryption xlator    : $BUILD_CRYPT_XLATOR"
echo
