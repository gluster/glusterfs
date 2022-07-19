dnl  Copyright (c) 2006-2016 Red Hat, Inc. <http://www.redhat.com>
dnl  This file is part of GlusterFS.
dnl
dnl  This file is licensed to you under your choice of the GNU Lesser
dnl  General Public License, version 3 or any later version (LGPLv3 or
dnl  later), or the GNU General Public License, version 2 (GPLv2), in all
dnl  cases as published by the Free Software Foundation.

// clang-format off
AC_INIT([glusterfs],[m4_esyscmd([build-aux/pkg-version --version])],[gluster-users@gluster.org],[],[https://github.com/gluster/glusterfs.git])

AC_SUBST([PACKAGE_RELEASE],
         [m4_esyscmd([build-aux/pkg-version --release])])

AM_INIT_AUTOMAKE([tar-pax foreign])

# Removes warnings when using automake 1.14 around (...but option 'subdir-objects' is disabled )
#but libglusterfs fails to build with contrib (Then are not set up that way?)
#AM_INIT_AUTOMAKE([subdir-objects])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES(yes)])

AC_CONFIG_HEADERS([config.h site.h])

AC_CONFIG_FILES([Makefile
                libglusterfs/Makefile
                libglusterfs/src/Makefile
                geo-replication/src/peer_gsec_create
                geo-replication/src/peer_mountbroker
                geo-replication/src/peer_mountbroker.py
                geo-replication/src/peer_georep-sshkey.py
                extras/peer_add_secret_pub
                geo-replication/syncdaemon/conf.py
                geo-replication/gsyncd.conf
                extras/snap_scheduler/conf.py
                glusterfsd/Makefile
                glusterfsd/src/Makefile
                rpc/Makefile
                rpc/rpc-lib/Makefile
                rpc/rpc-lib/src/Makefile
                rpc/rpc-transport/Makefile
                rpc/rpc-transport/socket/Makefile
                rpc/rpc-transport/socket/src/Makefile
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
                xlators/cluster/Makefile
                xlators/cluster/afr/Makefile
                xlators/cluster/afr/src/Makefile
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
                xlators/performance/quick-read/Makefile
                xlators/performance/quick-read/src/Makefile
                xlators/performance/open-behind/Makefile
                xlators/performance/open-behind/src/Makefile
                xlators/performance/md-cache/Makefile
                xlators/performance/md-cache/src/Makefile
                xlators/performance/nl-cache/Makefile
                xlators/performance/nl-cache/src/Makefile
                xlators/debug/Makefile
                xlators/debug/sink/Makefile
                xlators/debug/sink/src/Makefile
                xlators/debug/trace/Makefile
                xlators/debug/trace/src/Makefile
                xlators/debug/error-gen/Makefile
                xlators/debug/error-gen/src/Makefile
                xlators/debug/delay-gen/Makefile
                xlators/debug/delay-gen/src/Makefile
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
                xlators/features/arbiter/Makefile
                xlators/features/arbiter/src/Makefile
                xlators/features/thin-arbiter/Makefile
                xlators/features/thin-arbiter/src/Makefile
                xlators/features/changelog/Makefile
                xlators/features/changelog/src/Makefile
                xlators/features/changelog/lib/Makefile
                xlators/features/changelog/lib/src/Makefile
                xlators/features/locks/Makefile
                xlators/features/locks/src/Makefile
                xlators/features/simple-quota/Makefile
                xlators/features/simple-quota/src/Makefile
                xlators/features/quota/Makefile
                xlators/features/quota/src/Makefile
                xlators/features/marker/Makefile
                xlators/features/marker/src/Makefile
                xlators/features/selinux/Makefile
                xlators/features/selinux/src/Makefile
                xlators/features/sdfs/Makefile
                xlators/features/sdfs/src/Makefile
                xlators/features/read-only/Makefile
                xlators/features/read-only/src/Makefile
                xlators/features/compress/Makefile
                xlators/features/compress/src/Makefile
                xlators/features/namespace/Makefile
                xlators/features/namespace/src/Makefile
                xlators/features/quiesce/Makefile
                xlators/features/quiesce/src/Makefile
                xlators/features/barrier/Makefile
                xlators/features/barrier/src/Makefile
                xlators/features/index/Makefile
                xlators/features/index/src/Makefile
                xlators/features/gfid-access/Makefile
                xlators/features/gfid-access/src/Makefile
                xlators/features/trash/Makefile
                xlators/features/trash/src/Makefile
                xlators/features/snapview-server/Makefile
                xlators/features/snapview-server/src/Makefile
                xlators/features/snapview-client/Makefile
                xlators/features/snapview-client/src/Makefile
                xlators/features/upcall/Makefile
                xlators/features/upcall/src/Makefile
                xlators/features/shard/Makefile
                xlators/features/shard/src/Makefile
                xlators/features/bit-rot/Makefile
                xlators/features/bit-rot/src/Makefile
                xlators/features/bit-rot/src/stub/Makefile
                xlators/features/bit-rot/src/bitd/Makefile
                xlators/features/leases/Makefile
                xlators/features/leases/src/Makefile
                xlators/features/cloudsync/Makefile
                xlators/features/cloudsync/src/Makefile
                xlators/features/utime/Makefile
                xlators/features/utime/src/Makefile
                xlators/features/cloudsync/src/cloudsync-plugins/Makefile
                xlators/features/cloudsync/src/cloudsync-plugins/src/Makefile
                xlators/features/cloudsync/src/cloudsync-plugins/src/cloudsyncs3/Makefile
                xlators/features/cloudsync/src/cloudsync-plugins/src/cloudsyncs3/src/Makefile
                xlators/features/cloudsync/src/cloudsync-plugins/src/cvlt/Makefile
                xlators/features/cloudsync/src/cloudsync-plugins/src/cvlt/src/Makefile
                xlators/features/metadisp/Makefile
                xlators/features/metadisp/src/Makefile
                xlators/playground/Makefile
                xlators/playground/template/Makefile
                xlators/playground/template/src/Makefile
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
                extras/cliutils/Makefile
                extras/init.d/Makefile
                extras/init.d/glusterd.plist
                extras/init.d/glusterd-Debian
                extras/init.d/glusterd-Redhat
                extras/init.d/glusterd-FreeBSD
                extras/init.d/glusterd-SuSE
                extras/init.d/glustereventsd-Debian
                extras/init.d/glustereventsd-Redhat
                extras/init.d/glustereventsd-FreeBSD
                extras/ganesha/Makefile
                extras/ganesha/config/Makefile
                extras/ganesha/scripts/Makefile
                extras/ganesha/ocf/Makefile
                extras/systemd/Makefile
                extras/systemd/glusterd.service
                extras/systemd/glustereventsd.service
                extras/systemd/glusterfssharedstorage.service
                extras/systemd/gluster-ta-volume.service
                extras/run-gluster.tmpfiles
                extras/benchmarking/Makefile
                extras/hook-scripts/Makefile
                extras/ocf/Makefile
                extras/ocf/glusterd
                extras/ocf/volume
                extras/LinuxRPM/Makefile
                extras/geo-rep/Makefile
                extras/geo-rep/schedule_georep.py
                extras/firewalld/Makefile
                extras/hook-scripts/add-brick/Makefile
                extras/hook-scripts/add-brick/pre/Makefile
                extras/hook-scripts/add-brick/post/Makefile
                extras/hook-scripts/create/Makefile
                extras/hook-scripts/create/post/Makefile
                extras/hook-scripts/delete/Makefile
                extras/hook-scripts/delete/pre/Makefile
                extras/hook-scripts/start/Makefile
                extras/hook-scripts/start/post/Makefile
                extras/hook-scripts/set/Makefile
                extras/hook-scripts/set/post/Makefile
                extras/hook-scripts/stop/Makefile
                extras/hook-scripts/stop/pre/Makefile
                extras/hook-scripts/reset/Makefile
                extras/hook-scripts/reset/post/Makefile
                extras/hook-scripts/reset/pre/Makefile
                extras/python/Makefile
                extras/snap_scheduler/Makefile
                events/Makefile
                events/src/Makefile
                events/src/eventsapiconf.py
                events/tools/Makefile
                contrib/fuse-util/Makefile
                contrib/umountd/Makefile
                glusterfs-api.pc
                libgfchangelog.pc
                api/Makefile
                api/src/Makefile
                api/examples/Makefile
                geo-replication/Makefile
                geo-replication/src/Makefile
                geo-replication/syncdaemon/Makefile
                tools/Makefile
                tools/gfind_missing_files/Makefile
                heal/Makefile
                heal/src/Makefile
                glusterfs.spec
                tools/glusterfind/src/tool.conf
                tools/glusterfind/glusterfind
                tools/glusterfind/Makefile
                tools/glusterfind/src/Makefile
                tools/setgfid2path/Makefile
                tools/setgfid2path/src/Makefile])

AC_CANONICAL_HOST

AC_PROG_CC
AC_DISABLE_STATIC
LT_INIT
AC_SUBST([shrext_cmds])

AC_CHECK_PROG([RPCGEN], [rpcgen], [yes], [no])

if test "x$RPCGEN" = "xno"; then
   AC_MSG_ERROR([`rpcgen` not found, glusterfs needs `rpcgen` exiting..])
fi

# Initialize CFLAGS before usage
AC_ARG_ENABLE([debug],
              AS_HELP_STRING([--enable-debug],[Enable debug build options.]))
if test "x$enable_debug" = "xyes"; then
        BUILD_DEBUG=yes
        GF_CFLAGS="${GF_CFLAGS} -g -O0 -DDEBUG"
else
        BUILD_DEBUG=no
fi

SANITIZER=none

AC_ARG_ENABLE([asan],
              AS_HELP_STRING([--enable-asan],[Enable Address Sanitizer support]))
if test "x$enable_asan" = "xyes"; then
        SANITIZER=asan
        AC_CHECK_LIB([asan], [_init], ,
                [AC_MSG_ERROR([--enable-asan requires libasan.so, exiting])])
        if test "x$ac_cv_lib_asan__init" = xyes; then
                AC_MSG_CHECKING([whether asan API can be used])
                saved_CFLAGS=${CFLAGS}
                CFLAGS="${CFLAGS} -fsanitize=address"
                AC_COMPILE_IFELSE(
                [AC_LANG_PROGRAM([
                   [#include <sanitizer/common_interface_defs.h>]],
                   [[__sanitizer_finish_switch_fiber(0, 0, 0)]])],
                   [ASAN_API=yes], [ASAN_API=no])
                AC_MSG_RESULT([$ASAN_API])
                if test x$ASAN_API = "xyes"; then
                   AC_DEFINE(HAVE_ASAN_API, 1, [Define if asan API can be used.])
                fi
                CFLAGS=${saved_CFLAGS}
        fi
        GF_CFLAGS="${GF_CFLAGS} -O2 -g -fsanitize=address -fno-omit-frame-pointer"

        dnl See: https://groups.google.com/d/msg/address-sanitizer/SD590XDinfQ/NMUPj_G0BgAJ
        GF_LDFLAGS="${GF_LDFLAGS} -fsanitize=address"
fi

AC_ARG_ENABLE([tsan],
              AS_HELP_STRING([--enable-tsan],[Enable Thread Sanitizer support]))
if test "x$enable_tsan" = "xyes"; then
        if test "x$SANITIZER" != "xnone"; then
                AC_MSG_ERROR([only one sanitizer can be enabled at once])
        fi
        SANITIZER=tsan
        AC_CHECK_LIB([tsan], [_init], ,
                [AC_MSG_ERROR([--enable-tsan requires libtsan.so, exiting])])
        if test "x$ac_cv_lib_tsan__init" = xyes; then
                AC_MSG_CHECKING([whether tsan API can be used])
                saved_CFLAGS=${CFLAGS}
                CFLAGS="${CFLAGS} -fsanitize=thread"
                AC_COMPILE_IFELSE(
                [AC_LANG_PROGRAM([
                   [#include <sanitizer/tsan_interface.h>]],
                   [[__tsan_create_fiber(0)]])],
                   [TSAN_API=yes], [TSAN_API=no])
                AC_MSG_RESULT([$TSAN_API])
                if test x$TSAN_API = "xyes"; then
                   AC_DEFINE(HAVE_TSAN_API, 1, [Define if tsan API can be used.])
                fi
                CFLAGS=${saved_CFLAGS}
        fi
        GF_CFLAGS="${GF_CFLAGS} -O2 -g -fsanitize=thread -fno-omit-frame-pointer"
        GF_LDFLAGS="${GF_LDFLAGS} -fsanitize=thread"
fi

AC_ARG_ENABLE([ubsan],
              AS_HELP_STRING([--enable-ubsan],[Enable Undefined Behavior Sanitizer support]))
if test "x$enable_ubsan" = "xyes"; then
        if test "x$SANITIZER" != "xnone"; then
                AC_MSG_ERROR([only one sanitizer can be enabled at once])
        fi
        SANITIZER=ubsan
        AC_CHECK_LIB([ubsan], [_init], ,
                [AC_MSG_ERROR([--enable-ubsan requires libubsan.so, exiting])])
        GF_CFLAGS="${GF_CFLAGS} -O2 -g -fsanitize=undefined -fno-omit-frame-pointer"
        GF_LDFLAGS="${GF_LDFLAGS} -fsanitize=undefined"
fi

# Initialize CFLAGS before usage
BUILD_TCMALLOC=no
USE_MEMPOOL=yes
AC_ARG_WITH([tcmalloc],
        [AS_HELP_STRING([--without-tcmalloc],[Use gluster based mempool])],
        [with_tcmalloc="no"], [with_tcmalloc="yes"])
if test "x$with_tcmalloc" = "xyes"; then
    AC_CHECK_LIB([tcmalloc_minimal], [malloc], [],
                 [AC_MSG_ERROR([tcmalloc library needs to be present])])
    BUILD_TCMALLOC=yes
    GF_CFLAGS="${GF_CFLAGS} -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free"
    GF_LDFLAGS="-ltcmalloc_minimal ${GF_LDFLAGS}"
    AC_DEFINE(GF_DISABLE_MEMPOOL, 1, [Disable the Gluster memory pooler.])
    USE_MEMPOOL=no
fi


dnl When possible, prefer libtirpc over glibc rpc.
dnl
dnl On newer linux with only libtirpc, use libtirpc. (Specifying
dnl --without-libtirpc is an error.)
dnl
dnl on older linux with glibc rpc and WITH libtirpc, use libtirpc
dnl by default except when configured with --without-libtirpc.
dnl
dnl on old linux with glibc rpc and WITHOUT libtirpc, default to
dnl use glibc rpc.
dnl
AC_ARG_WITH([libtirpc],
        [AS_HELP_STRING([--without-libtirpc],[Use legacy glibc RPC.])],
        [with_libtirpc="no"], [with_libtirpc="yes"])

dnl For --without-libtirpc, check whether legacy glibc rpc is available.
if test "x$with_libtirpc" = "xno"; then
    AC_CHECK_HEADER([rpc/types.h], ,
        AC_MSG_ERROR([Your C library lacks support for RPC. Please use libtirpc instead.]))
fi

dnl ipv6-default is off by default
dnl
dnl ipv6-default requires libtirpc. (glibc rpc does not support IPv6.)
dnl ipv6-default can only be enabled if libtipc is enabled.
dnl
AC_ARG_WITH([ipv6-default],
        AS_HELP_STRING([--with-ipv6-default],[Set IPv6 as default.]),
        [with_ipv6_default=${with_libtirpc}], [with_ipv6_default="no"])

if test x$cross_compiling != xyes; then
    AC_CHECK_FILE([/etc/centos-release])
    if test "x$ac_cv_file__etc_centos_release" = "xyes"; then
        if grep "release 6" /etc/centos-release; then
                with_ipv6_default="no"
        fi
    fi
fi

dnl On some distributions '-ldl' isn't automatically added to LIBS
AC_CHECK_LIB([dl], [dlopen], [LIB_DL=-ldl])
AC_SUBST(LIB_DL)

AC_ARG_ENABLE([privport_tracking],
              AS_HELP_STRING([--disable-privport_tracking],[Disable internal tracking of privileged ports.]))
TRACK_PRIVPORTS="yes"
if test x"$enable_privport_tracking" = x"no"; then
        TRACK_PRIVPORTS="no"
        AC_DEFINE(GF_DISABLE_PRIVPORT_TRACKING, 1,
                  [Disable internal tracking of privileged ports.])
fi

case $host_os in
  darwin*)
    # macOS version checking became a bit complicated since 11.x
    # It is preferred to match against kernel version now instead of product version
    # Darwin kernel is versioned as A.B.C where
    # A = Kernel Version Major, starts from 5 (OS X Puma), incremented by 1 for each new release
    # B = Kernel Version Minor
    # C = Kernel Version Patch

    # Should be greater than OS X Lion i.e. KernelMajor 11
    DARWIN_KERNEL_MAJOR="`/usr/bin/uname -r | cut -d . -f 1`"
    if ! test $DARWIN_KERNEL_MAJOR -ge 11; then
       AC_MSG_ERROR([You need at least OS X 10.7 (Lion) to build Glusterfs])
    fi
    # OSX version lesser than 9 has llvm/clang optimization issues which leads to various segfaults
    # If OS X Mavericks or lower i.e KernelMajor = 13
    if test $DARWIN_KERNEL_MAJOR -lt 13; then
       GF_CFLAGS="${GF_CFLAGS} -g -O0 -DDEBUG"
    fi
    ;;
esac

# --enable-valgrind prevents calling dlclose(), this leaks memory
AC_ARG_ENABLE([valgrind],
              AS_HELP_STRING([--enable-valgrind@<:@=memcheck,drd@:>@],[Enable valgrind for resource leak (memcheck, which is
                              the default) or thread synchronization (drd) debugging.]))
case x$enable_valgrind in
  xmemcheck|xyes)
    AC_DEFINE(RUN_WITH_MEMCHECK, 1,
              [Define if all processes should run under 'valgrind --tool=memcheck'.])
    VALGRIND_TOOL=memcheck
    ;;
  xdrd)
    AC_DEFINE(RUN_WITH_DRD, 1,
              [Define if all processes should run under 'valgrind --tool=drd'.])
    VALGRIND_TOOL=drd
    ;;
  x|xno)
    VALGRIND_TOOL=no
    ;;
  *)
    AC_MSG_ERROR([Please specify --enable-valgrind@<:@=memcheck,drd@:>@])
    ;;
esac

if test "x$VALGRIND_TOOL" != xno; then
   AC_MSG_CHECKING([whether valgrind API can be used])
   AC_COMPILE_IFELSE(
   [AC_LANG_PROGRAM([
      [#include <valgrind/valgrind.h>]],
      [[VALGRIND_STACK_DEREGISTER(VALGRIND_STACK_REGISTER(0, 0))]])],
      [HAVE_VALGRIND_API=yes], [HAVE_VALGRIND_API=no])
   AC_MSG_RESULT([$HAVE_VALGRIND_API])
   if test x$HAVE_VALGRIND_API = "xyes"; then
      AC_DEFINE(HAVE_VALGRIND_API, 1, [Define if valgrind API can be used.])
   fi
fi

AC_ARG_WITH([previous-options],
        [AS_HELP_STRING([--with-previous-options],
                        [read config.status for configure options])
        ],
        [ if test -r ./config.status && \
             args=$(grep 'ac_cs_config=' config.status | \
                    sed -e 's/.*"\(.*\)".*/\1/' -e "s/'//g" -e "s/--with-previous-options//g") ; then
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
            [  --with-systemddir=DIR systemd service files in DIR @<:@PREFIX/lib/systemd/system@:>@],
            [systemddir=$withval],
            [systemddir='${prefix}/lib/systemd/system'])
AC_SUBST(systemddir)
AM_CONDITIONAL([USE_SYSTEMD], test [ -d '/usr/lib/systemd/system' ])

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
            AS_HELP_STRING([--with-tmpfilesdir=DIR],[tmpfiles config in DIR, disabled by default]),
            [tmpfilesdir=$withval],
            [tmpfilesdir=''])
AC_SUBST(tmpfilesdir)

AC_ARG_WITH([ocf],
            [AS_HELP_STRING([--without-ocf], [build OCF-compliant cluster resource agents])],
            ,
            [OCF_SUBDIR='ocf'],
            )
AC_SUBST(OCF_SUBDIR)

AC_ARG_WITH([server],
            [AS_HELP_STRING([--without-server], [do not build server components])],
            [with_server=$withval],
            [with_server='yes'],
            )
AM_CONDITIONAL([WITH_SERVER], [test x$with_server = xyes])

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

AC_CHECK_LIB([intl], [gettext])

AC_CHECK_HEADERS([sys/xattr.h])

AC_CHECK_HEADERS([sys/ioctl.h], AC_DEFINE(HAVE_IOCTL_IN_SYS_IOCTL_H, 1, [have sys/ioctl.h]))

AC_CHECK_HEADERS([sys/extattr.h])

AC_CHECK_HEADERS([openssl/dh.h])

AC_CHECK_HEADERS([openssl/ecdh.h])

AC_CHECK_LIB([ssl], [SSL_CTX_get0_param], [AC_DEFINE([HAVE_SSL_CTX_GET0_PARAM], [1], [define if found OpenSSL SSL_CTX_get0_param])])

dnl Math library
AC_CHECK_LIB([m], [pow], [MATH_LIB='-lm'], [MATH_LIB=''])
AC_SUBST(MATH_LIB)

dnl depend on libuuid.so
PKG_CHECK_MODULES([UUID], [uuid],
        [have_uuid=yes
         AC_DEFINE(HAVE_LIBUUID, 1, [have libuuid.so])
         PKGCONFIG_UUID=uuid],
        [have_uuid=no])
AM_CONDITIONAL([HAVE_LIBUUID], [test x$have_uuid = xyes])

dnl older version of libuuid (from e2fsprogs) require including uuid/uuid.h
saved_CFLAGS=${CFLAGS}
CFLAGS="${CFLAGS} ${UUID_CFLAGS}"
AC_CHECK_HEADER([uuid.h], [], [AC_CHECK_HEADER([uuid/uuid.h])],
                [[#if HAVE_UUID_H
                  #include <uuid.h>
                  #endif
                ]])
CFLAGS=${saved_CFLAGS}
if test "x$ac_cv_header_uuid_uuid_h" = "xyes"; then
    UUID_CFLAGS="${UUID_CFLAGS} -I$(pkg-config --variable=includedir uuid)/uuid"
    have_uuid=yes
fi

if test "x$have_uuid" != "xyes"; then
    case $host_os in
        *freebsd*)
            AC_MSG_ERROR([e2fsprogs-libuuid is required to build glusterfs])
            ;;
        linux*)
            AC_MSG_ERROR([libuuid is required to build glusterfs])
            ;;
        *)
            AC_MSG_ERROR([a Linux compatible libuuid is required to build glusterfs])
            ;;
    esac
fi

dnl libglusterfs needs uuid.h, practically everything depends on it
GF_CFLAGS="${GF_CFLAGS} ${UUID_CFLAGS}"
dnl PKGCONFIG_UUID is used for the dependency in *.pc.in files
AC_SUBST(PKGCONFIG_UUID)

dnl Check for xxhash library.
PKG_CHECK_MODULES([XXHASH], [libxxhash],
  [have_libxxhash=yes
   AC_DEFINE(HAVE_LIBXXHASH, 1, [Define to 1 if you have libxxhash.so.])],
  [have_libxxhash=no])
AM_CONDITIONAL([HAVE_LIBXXHASH], [test x$have_libxxhash = xyes])

dnl Note xxhsum binary is required to run gfid2path tests.
if test "x$have_libxxhash" = "xyes"; then
    AC_CHECK_PROG([XXHSUM], [xxhsum], [yes], [no])
    if test "x$XXHSUM" = "xno"; then
        AC_MSG_WARN([install 'xxhsum' binary if you plan to run the tests.])
    fi
    GF_CFLAGS="${GF_CFLAGS} ${XXHASH_CFLAGS}"
fi

dnl NetBSD does not support POSIX ACLs :-(
case $host_os in
     *netbsd* | darwin*)
        AC_MSG_WARN([platform does not support POSIX ACLs... disabling them])
        ACL_LIBS=''
        USE_POSIX_ACLS='0'
        BUILD_POSIX_ACLS='no'
        ;;
     *)
        AC_CHECK_HEADERS([sys/acl.h], ,
                         AC_MSG_ERROR([Support for POSIX ACLs is required]))
        USE_POSIX_ACLS='1'
        BUILD_POSIX_ACLS='yes'
        case $host_os in
             linux*)
                ACL_LIBS='-lacl'
                ;;
             solaris*)
                ACL_LIBS='-lsec'
                ;;
             *freebsd*)
                ACL_LIBS='-lc'
                ;;
             darwin*)
                ACL_LIBS='-lc'
                ;;
        esac
        if test "x${ACL_LIBS}" = "x-lacl"; then
          AC_CHECK_HEADERS([acl/libacl.h], , AC_MSG_ERROR([libacl is required for building on ${host_os}]))
        fi
        ;;
esac
AC_SUBST(ACL_LIBS)
AC_SUBST(USE_POSIX_ACLS)

# libglusterfs/checksum
AC_CHECK_HEADERS([openssl/md5.h])

# Zlib is used by libglusterfs checksum and cdc xlator.
PKG_CHECK_MODULES([ZLIB], [zlib >= 1.2.0], [have_zlib=yes], [have_zlib=no])
if test x$have_zlib = xyes; then
   AC_DEFINE(HAVE_ZLIB, 1, [Define if zlib is found.])
   AC_SUBST([ZLIB_CFLAGS])
   AC_SUBST([ZLIB_LIBS])
else
   AC_MSG_ERROR([zlib is required to build glusterfs])
fi

AC_CHECK_HEADERS([linux/falloc.h])

AC_CHECK_HEADERS([linux/oom.h], AC_DEFINE(HAVE_LINUX_OOM_H, 1, [have linux/oom.h]))

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

dnl Check Python Availability
have_python=no
dnl if the user has not specified a python, pick one
if test -z "${PYTHON}"; then
  case $host_os in
    freebsd*)
      if test -x /usr/local/bin/python3; then
        PYTHON=/usr/local/bin/python3
      else
        PYTHON=/usr/local/bin/python2
      fi
      ;;
    *)
      if test -x /usr/bin/python3; then
        PYTHON=/usr/bin/python3
      else
        PYTHON=/usr/bin/python2
      fi
      ;;
  esac
fi
AM_PATH_PYTHON([2.6],,[:])
if test -n "${PYTHON}"; then
   have_python=yes
fi
AM_CONDITIONAL(HAVE_PYTHON, test "x$have_python" = "xyes")

dnl Use pkg-config to get runtime search path missing from ${PYTHON}-config
dnl Just do "true" on failure so that configure does not bail out
dnl Note: python 2.6's devel pkg (e.g. in CentOS/RHEL 6) does not have
dnl pkg-config files, so this work-around instead
if test "x${PYTHON_VERSION}" = "x2.6"; then
  PYTHON_CFLAGS=$(python-config --includes)
  PYTHON_LIBS=$(python-config --libs)
else
  PKG_CHECK_MODULES([PYTHON], "python-${PYTHON_VERSION}",,true)
fi

PYTHON_CFLAGS=$(echo ${PYTHON_CFLAGS} | sed -e 's|-I|-isystem |')

BUILD_PYTHON_SITE_PACKAGES=${pythondir}
AC_SUBST(BUILD_PYTHON_SITE_PACKAGES)

# Eval two times to expand fully. First eval replaces $exec_prefix into $prefix
# Second eval will expand $prefix
build_python_site_packages_temp="${pythondir}"
eval build_python_site_packages_temp=\"${build_python_site_packages_temp}\"
eval build_python_site_packages_temp=\"${build_python_site_packages_temp}\"
BUILD_PYTHON_SITE_PACKAGES_EXPANDED=${build_python_site_packages_temp}
AC_SUBST(BUILD_PYTHON_SITE_PACKAGES_EXPANDED)

# FUSE section
AC_ARG_ENABLE([fuse-client],
              AS_HELP_STRING([--disable-fuse-client],[Do not build the fuse client. NOTE: you cannot mount glusterfs without the client]))

BUILD_FUSE_CLIENT=no
if test "x$enable_fuse_client" != "xno"; then
   FUSE_CLIENT_SUBDIR=fuse
   BUILD_FUSE_CLIENT="yes"
fi

AC_SUBST(FUSE_CLIENT_SUBDIR)

AC_ARG_ENABLE([fuse-notifications],
  AS_HELP_STRING([--disable-fuse-notifications], [Disable FUSE notifications]))

AS_IF([test "x$enable_fuse_notifications" != "xno"], [
  AC_DEFINE([HAVE_FUSE_NOTIFICATIONS], [1], [Use FUSE notifications])
])

# end FUSE section

dnl Find out OpenSSL trusted certificates path
AC_MSG_CHECKING([for OpenSSL trusted certificates path])
SSL_CERT_PATH=$(openssl version -d | sed -e 's|OPENSSLDIR: "\(.*\)".*|\1|')
if test -d "${SSL_CERT_PATH}" 1>/dev/null 2>&1; then
   AC_MSG_RESULT([$SSL_CERT_PATH])
   AC_DEFINE_UNQUOTED(SSL_CERT_PATH, ["$SSL_CERT_PATH"], [Path to OpenSSL trusted certificates.])
   AC_SUBST(SSL_CERT_PATH)
else
   AC_MSG_ERROR([Unable to detect path to OpenSSL trusted certificates])
fi

AC_CHECK_LIB([ssl], TLS_method, [HAVE_OPENSSL_1_1="yes"], [HAVE_OPENSSL_1_1="no"])
if test "x$HAVE_OPENSSL_1_1" = "xyes"; then
    AC_DEFINE([HAVE_TLS_METHOD], [1], [Using OpenSSL-1.1 TLS_method])
else
    AC_CHECK_LIB([ssl], TLSv1_2_method, [AC_DEFINE([HAVE_TLSV1_2_METHOD], [1], [Using OpenSSL-1.0 TLSv1_2_method])])
fi


# FUSERMOUNT section
AC_ARG_ENABLE([fusermount],
              AS_HELP_STRING([--disable-fusermount],[Use system's fusermount]))

BUILD_FUSERMOUNT="yes"
if test "x$enable_fusermount" = "xno"; then
   BUILD_FUSERMOUNT="no"
else
   AC_DEFINE(GF_FUSERMOUNT, 1, [Use our own fusermount])
   FUSERMOUNT_SUBDIR="contrib/fuse-util"
fi

AC_SUBST(FUSERMOUNT_SUBDIR)
#end FUSERMOUNT section

# EPOLL section
AC_ARG_ENABLE([epoll],
              AS_HELP_STRING([--disable-epoll],[Use poll instead of epoll.]))

BUILD_EPOLL=no
if test "x$enable_epoll" != "xno"; then
   AC_CHECK_HEADERS([sys/epoll.h],
                    [BUILD_EPOLL=yes],
                    [BUILD_EPOLL=no])
fi
# end EPOLL section

# SYNCDAEMON section
AC_ARG_ENABLE([georeplication],
              AS_HELP_STRING([--disable-georeplication],[Do not install georeplication components]))

BUILD_SYNCDAEMON=no
case $host_os in
     freebsd*)
#do nothing
       ;;
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
if test "x${with_server}" = "xyes" -a "x${enable_georeplication}" != "xno"; then
  if test "x${have_python}" = "xno" ; then
    AC_MSG_ERROR([only python 2 and 3 are supported])
  else
    SYNCDAEMON_SUBDIR=geo-replication
    SYNCDAEMON_COMPILE=1

    BUILD_SYNCDAEMON="yes"
    AC_MSG_CHECKING([if python has ctypes support...])
    if "${PYTHON}" -c 'import ctypes' 2>/dev/null; then
      AC_MSG_RESULT("yes")
    else
      AC_MSG_ERROR([python does not have ctypes support])
    fi
  fi
fi
AC_SUBST(SYNCDAEMON_COMPILE)
AC_SUBST(SYNCDAEMON_SUBDIR)
# end SYNCDAEMON section

# only install scripts from extras/geo-rep when enabled
if test "x${with_server}" = "xyes" -a "x$enable_georeplication" != "xno"; then
  GEOREP_EXTRAS_SUBDIR=geo-rep
fi
AC_SUBST(GEOREP_EXTRAS_SUBDIR)
AM_CONDITIONAL(USE_GEOREP, test "x$enable_georeplication" != "xno")

# METADISP section
AC_ARG_ENABLE([metadisp],
              AS_HELP_STRING([--enable-metadisp],[Enable the metadata dispersal xlator]))
BUILD_METADISP=no
if test "x${enable_metadisp}" = "xyes"; then
  BUILD_METADISP=yes
fi
AM_CONDITIONAL([BUILD_METADISP], [test "x$BUILD_METADISP" = "xyes"])
# end METADISP section

# Events section
AC_ARG_ENABLE([events],
              AS_HELP_STRING([--disable-events],[Do not install Events components]))

BUILD_EVENTS=no
EVENTS_ENABLED=0
EVENTS_SUBDIR=
if test "x$enable_events" != "xno"; then
  EVENTS_SUBDIR=events
  EVENTS_ENABLED=1

  BUILD_EVENTS="yes"

  if test "x${have_python}" = "xno"; then
     if test "x${enable_events}" = "xyes"; then
        AC_MSG_ERROR([python 2 or 3 required. exiting.])
     fi
     AC_MSG_WARN([python not found, disabling events])
     EVENTS_SUBDIR=
     EVENTS_ENABLED=0
     BUILD_EVENTS="no"
  else
    AC_DEFINE(USE_EVENTS, 1, [define if events enabled])
  fi
fi
AC_SUBST(EVENTS_ENABLED)
AC_SUBST(EVENTS_SUBDIR)
AM_CONDITIONAL([BUILD_EVENTS], [test "x${BUILD_EVENTS}" = "xyes"])
# end Events section

#start firewalld section
BUILD_FIREWALLD="no"
AC_ARG_ENABLE([firewalld],
              AS_HELP_STRING([--enable-firewalld],[enable installation configuration for firewalld]),
              [BUILD_FIREWALLD="${enableval}"], [BUILD_FIREWALLD="no"])

if test "x${with_server}" = "xyes" -a "x${BUILD_FIREWALLD}" = "xyes"; then
        if !(test -d /usr/lib/firewalld/services 1>/dev/null 2>&1) ; then
                BUILD_FIREWALLD="no (firewalld not installed)"
        fi
fi
AM_CONDITIONAL([USE_FIREWALLD],test ["x${BUILD_FIREWALLD}" =  "xyes"])

#endof firewald section

# xml-output
AC_ARG_ENABLE([xml-output],
              AS_HELP_STRING([--disable-xml-output],[Disable the xml output]))
BUILD_XML_OUTPUT="yes"
if test "x$enable_xml_output" != "xno"; then
    PKG_CHECK_MODULES([XML], [libxml-2.0], [], [no_xml="yes"])
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

dnl cloudsync section
BUILD_CLOUDSYNC="no"
AC_CHECK_LIB([curl], [curl_easy_setopt], [LIBCURL="-lcurl"])
if test -n "$LIBCURL";then
        HAVE_LIBCURL="yes"
fi
AC_CHECK_HEADERS([openssl/hmac.h openssl/evp.h openssl/bio.h openssl/buffer.h], [HAVE_OPENSSL="yes"])
if test "x$HAVE_LIBCURL" = "xyes" -a "x$HAVE_OPENSSL" = "xyes";then
   HAVE_AMAZONS3="yes"
fi
AM_CONDITIONAL([BUILD_AMAZONS3_PLUGIN], [test "x$HAVE_AMAZONS3" = "xyes"])
if test "x$HAVE_AMAZONS3" = "xyes";then
   BUILD_CLOUDSYNC="yes"
fi
BUILD_CVLT_PLUGIN="no"
case $host_os in
#enable cvlt plugin only for linux platforms
     linux*)
       BUILD_CVLT_PLUGIN="yes"
       BUILD_CLOUDSYNC="yes"
       ;;
     *)
       ;;
esac
AM_CONDITIONAL([BUILD_CVLT_PLUGIN], [test "x$BUILD_CVLT_PLUGIN" = "xyes"])
AM_CONDITIONAL([BUILD_CLOUDSYNC], [test "x$BUILD_CLOUDSYNC" = "xyes"])
dnl end cloudsync section

dnl SELinux feature enablement
case $host_os in
  linux*)
    AC_ARG_ENABLE([selinux],
                  AS_HELP_STRING([--disable-selinux],[Disable SELinux features]),
                  [USE_SELINUX="${enableval}"], [USE_SELINUX="yes"])
    ;;
  *)
    USE_SELINUX=no
    ;;
esac
AM_CONDITIONAL(USE_SELINUX, test "x${USE_SELINUX}" = "xyes")
dnl end of SELinux feature enablement

AC_CHECK_HEADERS([execinfo.h], [have_backtrace=yes])
if test "x${have_backtrace}" = "xyes"; then
   AC_DEFINE(HAVE_BACKTRACE, 1, [define if found backtrace])
fi
AC_SUBST(HAVE_BACKTRACE)
AM_CONDITIONAL([HAVE_BACKTRACE], [test x$have_backtrace = xyes])

dnl Old (before C11) compiler can compile (but not link) this:
dnl
dnl int main () {
dnl     _Static_assert(1, "True");
dnl     return 0;
dnl }
dnl
dnl assuming that _Static_assert is an implicitly declared function. So
dnl we're trying to link just to make sure that this is not the case.

AC_MSG_CHECKING([whether $CC supports C11 _Static_assert])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[_Static_assert(1, "True");]])],[STATIC_ASSERT=yes],[STATIC_ASSERT=no])

AC_MSG_RESULT([$STATIC_ASSERT])
if test x$STATIC_ASSERT = "xyes"; then
   AC_DEFINE(HAVE_STATIC_ASSERT, 1, [Define if C11 _Static_assert is supported.])
fi

if test "x${have_backtrace}" != "xyes"; then
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <math.h>]], [[double x=0.0; x=ceil(0.0);]])],[],[AC_MSG_ERROR(need math library for libexecinfo)])
fi

dnl glusterfs prints memory usage to stderr by sending it SIGUSR1
dnl try mallinfo2 first, fallback to mallinfo if not available
AC_CHECK_FUNC([mallinfo2], [have_mallinfo2=yes])
if test "x${have_mallinfo2}" = "xyes"; then
   AC_DEFINE(HAVE_MALLINFO2, 1, [define if found mallinfo2])
else
   AC_CHECK_FUNC([mallinfo], [have_mallinfo=yes])
   if test "x${have_mallinfo}" = "xyes"; then
      AC_DEFINE(HAVE_MALLINFO, 1, [define if found mallinfo])
   fi
fi
AC_SUBST(HAVE_MALLINFO2)
AC_SUBST(HAVE_MALLINFO)

dnl Linux, Solaris, Cygwin
AC_CHECK_MEMBERS([struct stat.st_atim.tv_nsec])
dnl FreeBSD, NetBSD
AC_CHECK_MEMBERS([struct stat.st_atimespec.tv_nsec])
case $host_os in
        *netbsd*)
        GF_CFLAGS="${GF_CFLAGS} -D_INCOMPLETE_XOPEN_C063 -DCONFIG_MACHINE_BSWAP_H"
        ;;
esac
AC_CHECK_FUNC([linkat], [have_linkat=yes])
if test "x${have_linkat}" = "xyes"; then
   AC_DEFINE(HAVE_LINKAT, 1, [define if found linkat])
fi
AC_SUBST(HAVE_LINKAT)

dnl prefer futimens for nanosecond resolution
AC_CHECK_FUNC([futimens], [have_futimens=yes])
if test "x${have_futimens}" = "xyes"; then
   AC_DEFINE(HAVE_FUTIMENS, 1, [define if found futimens])
else
   AC_CHECK_FUNC([futimes], [have_futimes=yes])
   if test "x${have_futimes}" = "xyes"; then
      AC_DEFINE(HAVE_FUTIMES, 1, [define if found futimes])
   else
      AC_MSG_ERROR([This system doesn't support neither futimens() nor futimes()])
   fi
fi
AC_SUBST(HAVE_FUTIMENS)
AC_SUBST(HAVE_FUTIMES)

dnl check for Monotonic clock
AC_CHECK_LIB([rt], [clock_gettime], ,
             AC_MSG_WARN([System doesn't have monotonic clock using contrib]))

dnl check for argp, FreeBSD has the header in /usr/local/include
case $host_os in
    *freebsd*)
    CFLAGS="${CFLAGS} -isystem /usr/local/include"
    ARGP_LDADD=-largp
    ;;
    *netbsd*)
    ARGP_LDADD=-largp
    ;;
esac
dnl argp-standalone does not provide a pkg-config file
AC_CHECK_HEADER([argp.h], AC_DEFINE(HAVE_ARGP, 1, [have argp]))
if test "x$ac_cv_header_argp_h" != "xyes"; then
    AC_MSG_ERROR([argp.h not found, install libargp or argp-standalone])
fi
AC_SUBST(ARGP_LDADD)

dnl Check for atomic operation support
AC_MSG_CHECKING([for gcc __atomic builtins])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[int v; __atomic_load_n(&v, __ATOMIC_ACQUIRE);]])],[have_atomic_builtins=yes],[have_atomic_builtins=no])
if test "x${have_atomic_builtins}" = "xyes"; then
   AC_DEFINE(HAVE_ATOMIC_BUILTINS, 1, [define if __atomic_*() builtins are available])
fi
AC_SUBST(HAVE_ATOMIC_BUILTINS)
AC_MSG_RESULT([$have_atomic_builtins])

dnl __sync_*() will not be needed if __atomic_*() is available
AC_MSG_CHECKING([for gcc __sync builtins])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[__sync_synchronize();]])],[have_sync_builtins=yes],[have_sync_builtins=no])
if test "x${have_sync_builtins}" = "xyes"; then
   AC_DEFINE(HAVE_SYNC_BUILTINS, 1, [define if __sync_*() builtins are available])
fi
AC_SUBST(HAVE_SYNC_BUILTINS)
AC_MSG_RESULT([$have_sync_builtins])

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

# On fedora-29, copy_file_range syscall and the libc API both are present.
# Whereas, on some machines such as centos-7, RHEL-7, the API is not there.
# Only the system call is present. So, this change is to determine whether
# the API is present or not. If not, then check whether the system call is
# present or not. Accordingly sys_copy_file_range function will first call
# the API if it is there. Otherwise it will call syscall(SYS_copy_file_range).
AC_CHECK_FUNC([copy_file_range], [have_copy_file_range=yes])
if test "x${have_copy_file_range}" = "xyes"; then
   AC_DEFINE(HAVE_COPY_FILE_RANGE, 1, [define if copy_file_range exists])
else
   OLD_CFLAGS=${CFLAGS}
   CFLAGS="-D_GNU_SOURCE"
   AC_CHECK_DECL([SYS_copy_file_range], , , [#include <sys/syscall.h>])
   if test "x${ac_cv_have_decl_SYS_copy_file_range}" = "xyes"; then
      AC_DEFINE(HAVE_COPY_FILE_RANGE_SYS, 1, [define if SYS_copy_file_range is available])
   fi
   CFLAGS=${OLD_CFLAGS}
fi

AC_CHECK_FUNC([syncfs], [have_syncfs=yes])
if test "x${have_syncfs}" = "xyes"; then
   AC_DEFINE(HAVE_SYNCFS, 1, [define if syncfs exists])
else
   OLD_CFLAGS=${CFLAGS}
   CFLAGS="-D_GNU_SOURCE"
   AC_CHECK_DECL([SYS_syncfs], , , [#include <sys/syscall.h>])
   if test "x${ac_cv_have_decl_SYS_syncfs}" = "xyes"; then
      AC_DEFINE(HAVE_SYNCFS_SYS, 1, [define if SYS_syncfs is available])
   fi
   CFLAGS=${OLD_CFLAGS}
fi

BUILD_NANOSECOND_TIMESTAMPS=no
AC_CHECK_FUNC([utimensat], [have_utimensat=yes])
if test "x${have_utimensat}" = "xyes"; then
   BUILD_NANOSECOND_TIMESTAMPS=yes
   AC_DEFINE(HAVE_UTIMENSAT, 1, [define if utimensat exists])
fi

OLD_CFLAGS=${CFLAGS}
CFLAGS="-D_GNU_SOURCE"
AC_CHECK_DECL([SEEK_HOLE], , , [#include <unistd.h>])
if test "x${ac_cv_have_decl_SEEK_HOLE}" = "xyes"; then
   AC_DEFINE(HAVE_SEEK_HOLE, 1, [define if SEEK_HOLE is available])
fi
CFLAGS=${OLD_CFLAGS}

AC_CHECK_FUNC([accept4], [have_accept4=yes])
if test "x${have_accept4}" = "xyes"; then
   AC_DEFINE(HAVE_ACCEPT4, 1, [define if accept4 exists])
fi

AC_CHECK_FUNC([paccept], [have_paccept=yes])
if test "x${have_paccept}" = "xyes"; then
AC_DEFINE(HAVE_PACCEPT, 1, [define if paccept exists])
fi

AC_CHECK_FUNC([pipe2], [have_pipe2=yes])
if test "x${have_pipe2}" = "xyes"; then
AC_DEFINE(HAVE_PIPE2, 1, [Define if (Linux-specific) pipe2() exists.])
fi

dnl Looking for OS-dependent non-POSIX function to set thread name.

PTHREAD_SETNAME_FOUND=no

dnl Linux-like version assuming glibc (so _GNU_SOURCE).
if test x$PTHREAD_SETNAME_FOUND = xno; then
   AC_MSG_CHECKING([for pthread_setname_np(thread, name)])
   saved_CFLAGS=${CFLAGS}
   CFLAGS="-D_GNU_SOURCE -pthread ${saved_CFLAGS}"
   AC_LINK_IFELSE(
      [AC_LANG_PROGRAM([
         [#include <pthread.h>]],
         [[pthread_t t; pthread_setname_np(t, "name");]])],
         [pthread_setname_np_two_args=yes], [pthread_setname_np_two_args=no])
   AC_MSG_RESULT([$pthread_setname_np_two_args])
   CFLAGS="$saved_CFLAGS"

   if test x$pthread_setname_np_two_args = xyes; then
      AC_DEFINE(HAVE_PTHREAD_SETNAME_NP_TWO_ARGS, 1,
                [Define if 'pthread_setname_np(thread, name)' is present.])
      PTHREAD_SETNAME_FOUND=yes
   fi
fi

dnl NetBSD-like version with three arguments.
if test x$PTHREAD_SETNAME_FOUND = xno; then
   AC_MSG_CHECKING([for pthread_setname_np(thread, name, arg)])
   saved_CFLAGS=${CFLAGS}
   CFLAGS="-pthread ${saved_CFLAGS}"
   AC_LINK_IFELSE(
      [AC_LANG_PROGRAM([
         [#include <pthread.h>]],
         [[pthread_t t; pthread_setname_np(t, "name", NULL);]])],
         [pthread_setname_np_three_args=yes], [pthread_setname_np_three_args=no])
   AC_MSG_RESULT([$pthread_setname_np_three_args])
   CFLAGS="$saved_CFLAGS"

   if test x$pthread_setname_np_three_args = xyes; then
      AC_DEFINE(HAVE_PTHREAD_SETNAME_NP_THREE_ARGS, 1,
                [Define if 'pthread_setname_np(thread, name, arg)' is present.])
      PTHREAD_SETNAME_FOUND=yes
   fi
fi

dnl FreeBSD provides pthread_np.h header file and both (not sure
dnl for versions older than 13.0) 'pthread_setname_np(thread, name)'
dnl and 'pthread_set_name_np(thread, name)'.

if test x$PTHREAD_SETNAME_FOUND = xno; then
   AC_MSG_CHECKING([for pthread_set_name_np(thread, name)])
   saved_CFLAGS=${CFLAGS}
   CFLAGS="-pthread ${saved_CFLAGS}"
   AC_LINK_IFELSE(
      [AC_LANG_PROGRAM([
         [#include <pthread_np.h>]],
         [[pthread_t t; pthread_set_name_np(t, "name");]])],
         [pthread_set_name_np=yes], [pthread_set_name_np=no])
   AC_MSG_RESULT([$pthread_set_name_np])
   CFLAGS="$saved_CFLAGS"

   if test x$pthread_set_name_np = xyes; then
      AC_DEFINE(HAVE_PTHREAD_SET_NAME_NP, 1,
                [Define if 'pthread_set_name_np(thread, name)' is present.])
      PTHREAD_SETNAME_FOUND=yes
   fi
fi

if test x$PTHREAD_SETNAME_FOUND = xno; then
   AC_MSG_WARN([Unable to detect function to set thread name])
fi

# Check the distribution where you are compiling glusterfs on.
# For cross-compiling, you're advised to specify it manually.

GF_DISTRIBUTION=

if test x$cross_compiling != xyes; then
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
else
    GF_DISTRIBUTION=Unspecified
fi

AC_SUBST(GF_DISTRIBUTION)

AC_CHECK_FILE([/etc/os-release])

if test "x$ac_cv_file__etc_os_release" = "xyes"; then
   AC_CANONICAL_HOST

   if test "x${host_cpu}" = "xs390x" || test "x${host_cpu}" = "xs390" ; then
      if grep "Red Hat Enterprise Linux 8\|SUSE Linux Enterprise Server 15" /etc/os-release ; then
         AC_DEFINE(REDHAT8_OR_SLES15_ON_S390X, 1, [Red Hat 8 or SLES 15 on s390x])
      fi
   fi
fi

GF_HOST_OS=""
GF_LDFLAGS="${GF_LDFLAGS} -rdynamic"

dnl see --with-libtirpc option check above, libtirpc(-devel) is required for
dnl ipv6-default
if test "x${with_libtirpc}" = "xyes" || test "x${with_ipv6_default}" = "xyes" ; then
    PKG_CHECK_MODULES([TIRPC], [libtirpc],
       [with_libtirpc="yes"; GF_CFLAGS="$GF_CFLAGS $TIRPC_CFLAGS"; GF_LDFLAGS="$GF_LDFLAGS $TIRPC_LIBS";],
       [with_libtirpc="missing"; with_ipv6_default="no"])
fi

if test "x${with_libtirpc}" = "xyes" ; then
    AC_DEFINE(HAVE_LIBTIRPC, 1, [Using libtirpc])
fi

if test "x${with_libtirpc}" = "xmissing" ; then
    AC_CHECK_HEADERS([rpc/rpc.h],[
        AC_MSG_WARN([
            ---------------------------------------------------------------------------------
            libtirpc (and/or ipv6-default) were enabled but libtirpc-devel is not installed.
            Disabling libtirpc and ipv6-default and falling back to legacy glibc rpc headers.
            This is a transitional warning message. Eventually it will be an error message.
            ---------------------------------------------------------------------------------])],[
        AC_MSG_ERROR([
            ---------------------------------------------------------------------------------
            libtirpc (and/or ipv6-default) were enabled but libtirpc-devel is not installed
            and there were no legacy glibc rpc headers and library to fall back to.
            ---------------------------------------------------------------------------------])])
fi

if test "x$with_ipv6_default" = "xyes" ; then
   GF_CFLAGS="$GF_CFLAGS -DIPV6_DEFAULT"
fi

USE_BRICKMUX="no"
if test "x$enable_brickmux" = "xyes" ; then
   USE_BRICKMUX="yes"
   AC_DEFINE(GF_ENABLE_BRICKMUX, 1, [Enable Brick Mux.])
fi

dnl check for gcc -Werror=format-security
saved_CFLAGS=$CFLAGS
CFLAGS="-Wformat -Werror=format-security"
AC_MSG_CHECKING([whether $CC accepts -Werror=format-security])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM()], [cc_werror_format_security=yes], [cc_werror_format_security=no])
echo $cc_werror_format_security
if test "x$cc_werror_format_security" = "xyes"; then
    GF_CFLAGS="$GF_CFLAGS ${CFLAGS}"
fi
CFLAGS="$saved_CFLAGS"

dnl check for gcc -Werror=implicit-function-declaration
saved_CFLAGS=$CFLAGS
CFLAGS="-Werror=implicit-function-declaration"
AC_MSG_CHECKING([whether $CC accepts -Werror=implicit-function-declaration])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM()], [cc_werror_implicit=yes], [cc_werror_implicit=no])
echo $cc_werror_implicit
if test "x$cc_werror_implicit" = "xyes"; then
    GF_CFLAGS="${GF_CFLAGS} ${CFLAGS}"
fi
CFLAGS="$saved_CFLAGS"

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
   GF_CFLAGS="${GF_CFLAGS} -Wno-gnu"
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
      GF_CFLAGS="${GF_CFLAGS} -fno-omit-frame-pointer"
   fi
fi

old_prefix=$prefix
if test "x$prefix" = xNONE; then
        prefix=$ac_default_prefix
fi
old_exec_prefix=$exec_prefix
if test "x$exec_prefix" = xNONE; then
        exec_prefix="$(eval echo $prefix)"
fi
GLUSTERFS_LIBEXECDIR="$(eval echo $libexecdir)/glusterfs"
prefix=$old_prefix
exec_prefix=$old_exec_prefix

### Dirty hacky stuff to make LOCALSTATEDIR work
if test "x$prefix" = xNONE; then
   test "${localstatedir}" = '${prefix}/var' && localstatedir=$ac_default_prefix/var
   localstatedir=/var
fi
localstatedir="$(eval echo ${localstatedir})"
LOCALSTATEDIR=$localstatedir

GLUSTERFSD_MISCDIR="$(eval echo ${localstatedir})/lib/misc/glusterfsd"

old_prefix=$prefix
if test "x$prefix" = xNONE; then
    prefix=$ac_default_prefix
fi
GLUSTERD_VOLFILE="$(eval echo ${sysconfdir})/glusterfs/glusterd.vol"
prefix=$old_prefix


GFAPI_EXTRA_LDFLAGS='-Wl,--version-script=$(top_srcdir)/api/src/gfapi.map'
case $host_os in
     linux*)
        GF_HOST_OS="GF_LINUX_HOST_OS"
        GF_FUSE_CFLAGS="-DFUSERMOUNT_DIR=\\\"\$(bindir)\\\""
        GLUSTERD_WORKDIR="${LOCALSTATEDIR}/lib/glusterd"
        ;;
     solaris*)
        GF_HOST_OS="GF_SOLARIS_HOST_OS"
        GF_CFLAGS="${GF_CFLAGS} -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS -m64"
        BUILD_FUSE_CLIENT=no
        FUSE_CLIENT_SUBDIR=""
        GLUSTERD_WORKDIR="${LOCALSTATEDIR}/lib/glusterd"
        ;;
     *netbsd*)
        GF_HOST_OS="GF_BSD_HOST_OS"
        GF_CFLAGS="${GF_CFLAGS} -D_INCOMPLETE_XOPEN_C063"
        GF_CFLAGS="${GF_CFLAGS} -DTHREAD_UNSAFE_BASENAME"
        GF_CFLAGS="${GF_CFLAGS} -DTHREAD_UNSAFE_DIRNAME"
        GF_FUSE_CFLAGS="-DFUSERMOUNT_DIR=\\\"\$(sbindir)\\\""
        GF_LDADD="${ARGP_LDADD}"
        if test "x$ac_cv_header_execinfo_h" = "xyes"; then
           GF_LDFLAGS="${GF_LDFLAGS} -lexecinfo"
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
        GF_CFLAGS="${GF_CFLAGS} -DO_DSYNC=0"
        GF_CFLAGS="${GF_CFLAGS} -Dxdr_quad_t=xdr_longlong_t"
        GF_CFLAGS="${GF_CFLAGS} -Dxdr_u_quad_t=xdr_u_longlong_t"
        GF_FUSE_CFLAGS="-DFUSERMOUNT_DIR=\\\"\$(sbindir)\\\""
        GF_LDADD="${ARGP_LDADD}"
        if test "x$ac_cv_header_execinfo_h" = "xyes"; then
           GF_LDFLAGS="${GF_LDFLAGS} -lexecinfo"
        fi
        BUILD_FUSE_CLIENT=yes
        BUILD_FUSERMOUNT=no
        FUSERMOUNT_SUBDIR=""
        GLUSTERD_WORKDIR="${LOCALSTATEDIR}/db/glusterd"
        ;;
     darwin*)
        GF_HOST_OS="GF_DARWIN_HOST_OS"
        LIBTOOL=glibtool
        GF_CFLAGS="${GF_CFLAGS} -D_REENTRANT -D_XOPEN_SOURCE "
        GF_CFLAGS="${GF_CFLAGS} -D_DARWIN_USE_64_BIT_INODE "
        GF_CFLAGS="${GF_CFLAGS} -DTHREAD_UNSAFE_BASENAME"
        GF_CFLAGS="${GF_CFLAGS} -DTHREAD_UNSAFE_DIRNAME"
        GF_LDADD="${ARGP_LDADD}"
        GF_LDFLAGS="${GF_LDFLAGS}"
        GF_FUSE_CFLAGS="-I\$(CONTRIBDIR)/macfuse"
        BUILD_FUSERMOUNT="no"
        FUSERMOUNT_SUBDIR=""
        GLUSTERD_WORKDIR="${LOCALSTATEDIR}/db/glusterd"
        GFAPI_EXTRA_LDFLAGS='-Wl,-alias_list,$(top_srcdir)/api/src/gfapi.aliases'
        ;;
esac

# LTO section
AC_ARG_ENABLE([lto],
              AS_HELP_STRING([--disable-lto],[Disable buidling with LTO]))

LTO_BUILD=no
AS_IF([ test "x$BUILD_DEBUG" = "xno" && test "x$enable_lto" != "xno" ], [
    CC_VER=`"$CC" -dumpversion 2>/dev/null | cut -f 1 -d '.'`
    AS_IF([ test ! -z "$CC_VER" && test "$CC_VER" -ge "10" ], [
        GF_CFLAGS="${GF_CFLAGS} -flto"
        GF_LDFLAGS="${GF_LDFLAGS} -flto"
        LTO_BUILD=yes
        AC_MSG_NOTICE([building with LTO])],
        [AC_MSG_NOTICE([not using LTO for gcc_ver=$CC_VER]) ])
    ], [
    AC_MSG_NOTICE([LTO is disabled])
])

dnl ------------------------------
dnl Checking posix_spawn(3) sanity
dnl ------------------------------

AC_ARG_ENABLE(posix_spawn,
              AS_HELP_STRING([--disable-posix_spawn],
                             [Disable usage of posix_spawn when calling out]),
              AC_MSG_CHECKING([if posix_spawn returns properly on failure])
              [ case "$enableval" in
                no)
                     AC_MSG_RESULT(no)
                     posix_spawn=no
                     ;;
                *)   AC_MSG_RESULT(yes)
                    posix_spawn=yes
                     ;;
                esac ],
              AC_MSG_CHECKING([if posix_spawn returns properly on failure])
              AC_RUN_IFELSE([AC_LANG_SOURCE([[
/*
 * Test posix_spawn(3) return value on failure,
 * adapted from and because of
 * https://sourceware.org/bugzilla/show_bug.cgi?id=18433
 */
#include <stdio.h>
#include <spawn.h>
#include <errno.h>

int main()
{
  extern char **environ;
  char *argv[[]] = { "3a7e588d3bdc4a069dbef540886ecf78", 0 };
  pid_t pid = -1;
  int ret = ret = posix_spawn(&pid, argv[[0]], 0, 0, argv, environ);
  return ret == ENOENT ? 0 : 1;
}
]])
],
    AC_MSG_RESULT(yes)
    posix_spawn=yes,
    AC_MSG_RESULT(no)
    posix_spawn=no,
    AC_MSG_RESULT(yes)
    posix_spawn=yes
))
AM_CONDITIONAL([USE_POSIX_SPAWN], [test x$posix_spawn = xyes])

# Default value for sbindir
prefix_temp=$prefix
exec_prefix_temp=$exec_prefix

test "${prefix}" = "NONE" && prefix="${ac_default_prefix}"
test "${exec_prefix}" = "NONE" && exec_prefix='${prefix}'
sbintemp="${sbindir}"
eval sbintemp=\"${sbintemp}\"
eval sbintemp=\"${sbintemp}\"
SBIN_DIR=${sbintemp}

sysconfdirtemp="${sysconfdir}"
eval sysconfdirtemp=\"${sysconfdirtemp}\"
SYSCONF_DIR=${sysconfdirtemp}

prefix=$prefix_temp
exec_prefix=$exec_prefix_temp

AC_SUBST(SBIN_DIR)
AC_SUBST(SYSCONF_DIR)

# lazy umount emulation
UMOUNTD_SUBDIR=""
if test "x${GF_HOST_OS}" != "xGF_LINUX_HOST_OS" ; then
        UMOUNTD_SUBDIR="contrib/umountd"
fi
AC_SUBST(UMOUNTD_SUBDIR)


# enable debug section
AC_ARG_ENABLE([debug],
              AS_HELP_STRING([--enable-debug],[Enable debug build options.]))

dnl Add readline support in CLI if available.
PKG_CHECK_MODULES([READLINE], [readline], [BUILD_READLINE="yes"],
                  [BUILD_READLINE="no"])
if test x$BUILD_READLINE = xyes; then
    AC_DEFINE(HAVE_READLINE, 1, [Enable CLI readline support.])
fi

BUILD_LIBAIO=no
AC_CHECK_LIB([aio],[io_setup],[LIBAIO="-laio"])

if test -n "$LIBAIO"; then
   AC_DEFINE(HAVE_LIBAIO, 1, [libaio based POSIX enabled])
   BUILD_LIBAIO=yes
fi

BUILD_LINUX_IO_URING="no"
BUILD_LIBURING="no"

case $host_os in
    linux*)
        AC_ARG_ENABLE([linux_io_uring],
                      AS_HELP_STRING([--disable-linux-io_uring],[Disable io-uring support.]))

        if test "x$enable_linux_io_uring" != "xno" ; then
            AC_CHECK_HEADERS([liburing.h],
                             [AC_DEFINE(HAVE_LIBURING, 1, [io-uring based POSIX enabled]) LIBURING="-luring"],
                             AC_MSG_ERROR([Install liburing library and headers or use --disable-linux-io_uring]))
            BUILD_LIBURING=yes

            AC_CHECK_HEADER([linux/io_uring.h],
                            [
                                AC_DEFINE([HAVE_IO_URING], [1], "io_uring support")
                                BUILD_LINUX_IO_URING="yes"
                            ])
        fi
        ;;
esac

AM_CONDITIONAL([BUILD_LINUX_IO_URING], [test x$BUILD_LINUX_IO_URING = xyes])

dnl gnfs section
BUILD_GNFS="no"
RPCBIND_SERVICE=""
AC_ARG_ENABLE([gnfs],
              AS_HELP_STRING([--enable-gnfs],[Enable legacy gnfs server xlator.]))
if test "x${with_server}" = "xyes" -a "x$enable_gnfs" = "xyes"; then
    BUILD_GNFS="yes"
    GF_CFLAGS="$GF_CFLAGS -DBUILD_GNFS"
    RPCBIND_SERVICE="rpcbind.service"
fi
AM_CONDITIONAL([BUILD_GNFS], [test x$BUILD_GNFS = xyes])
AC_SUBST(BUILD_GNFS)
AC_SUBST(RPCBIND_SERVICE)
dnl end gnfs section

dnl Check for userspace-rcu
PKG_CHECK_MODULES([URCU], [liburcu-bp], [],
  [AC_CHECK_HEADERS([urcu-bp.h],
     [URCU_LIBS='-lurcu-bp'],
     AC_MSG_ERROR([liburcu-bp not found]))])
PKG_CHECK_MODULES([URCU_CDS], [liburcu-cds >= 0.8], [],
  [PKG_CHECK_MODULES([URCU_CDS], [liburcu-cds >= 0.7],
    [AC_DEFINE(URCU_OLD, 1, [Define if liburcu 0.6 or 0.7 is found])
     USE_CONTRIB_URCU='yes'],
    [AC_CHECK_HEADERS([urcu/cds.h],
      [AC_DEFINE(URCU_OLD, 1, [Define if liburcu 0.6 or 0.7 is found])
       URCU_CDS_LIBS='-lurcu-cds'
       USE_CONTRIB_URCU='yes'],
      [AC_MSG_ERROR([liburcu-cds not found])])])])

BUILD_UNITTEST="no"
AC_ARG_ENABLE([cmocka],
              AS_HELP_STRING([--enable-cmocka],[Enable cmocka build options.]))
if test "x$enable_cmocka" = "xyes"; then
    BUILD_UNITTEST="yes"
    PKG_CHECK_MODULES([UNITTEST], [cmocka >= 1.0.1], [BUILD_UNITTEST="yes"],
        [AC_MSG_ERROR([cmocka library is required to build glusterfs])]
    )
fi
AM_CONDITIONAL([UNITTEST], [test x$BUILD_UNITTEST = xyes])

dnl Define UNIT_TESTING only for building cmocka binaries.
UNITTEST_CFLAGS="${UNITTEST_CFLAGS} -DUNIT_TESTING=1"

dnl Add cmocka for unit tests
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

AC_SUBST(CFLAGS)
# end enable debug section

# EC dynamic code generation section

EC_DYNAMIC_SUPPORT="none"
EC_DYNAMIC_ARCH="none"

AC_ARG_ENABLE([ec-dynamic],
              AS_HELP_STRING([--disable-ec-dynamic],[Disable all dynamic code generation extensions for EC module]))

AC_ARG_ENABLE([ec-dynamic-intel],
              AS_HELP_STRING([--disable-ec-dynamic-intel],[Disable all INTEL dynamic code generation extensions for EC module]))

AC_ARG_ENABLE([ec-dynamic-arm],
              AS_HELP_STRING([--disable-ec-dynamic-arm],[Disable all ARM dynamic code generation extensions for EC module]))

AC_ARG_ENABLE([ec-dynamic-x64],
              AS_HELP_STRING([--disable-ec-dynamic-x64],[Disable dynamic INTEL x64 code generation for EC module]))

AC_ARG_ENABLE([ec-dynamic-sse],
              AS_HELP_STRING([--disable-ec-dynamic-sse],[Disable dynamic INTEL SSE code generation for EC module]))

AC_ARG_ENABLE([ec-dynamic-avx],
              AS_HELP_STRING([--disable-ec-dynamic-avx],[Disable dynamic INTEL AVX code generation for EC module]))

AC_ARG_ENABLE([ec-dynamic-neon],
              AS_HELP_STRING([--disable-ec-dynamic-neon],[Disable dynamic ARM NEON code generation for EC module]))

if test "x$enable_ec_dynamic" != "xno"; then
  case $host in
    x86_64*)
      if test "x$enable_ec_dynamic_intel" != "xno"; then
        if test "x$enable_ec_dynamic_x64" != "xno"; then
          EC_DYNAMIC_SUPPORT="$EC_DYNAMIC_SUPPORT x64"
          AC_DEFINE(USE_EC_DYNAMIC_X64, 1, [Defined if using dynamic INTEL x64 code])
        fi
        if test "x$enable_ec_dynamic_sse" != "xno"; then
          EC_DYNAMIC_SUPPORT="$EC_DYNAMIC_SUPPORT sse"
          AC_DEFINE(USE_EC_DYNAMIC_SSE, 1, [Defined if using dynamic INTEL SSE code])
        fi
        if test "x$enable_ec_dynamic_avx" != "xno"; then
          EC_DYNAMIC_SUPPORT="$EC_DYNAMIC_SUPPORT avx"
          AC_DEFINE(USE_EC_DYNAMIC_AVX, 1, [Defined if using dynamic INTEL AVX code])
        fi

        if test "x$EC_DYNAMIC_SUPPORT" != "xnone"; then
          EC_DYNAMIC_ARCH="intel"
        fi
      fi
      ;;
    arm*)
      if test "x$enable_ec_dynamic_arm" != "xno"; then
        if test "x$enable_ec_dynamic_neon" != "xno"; then
          EC_DYNAMIC_SUPPORT="$EC_DYNAMIC_SUPPORT neon"
          AC_DEFINE(USE_EC_DYNAMIC_NEON, 1, [Defined if using dynamic ARM NEON code])
        fi

        if test "x$EC_DYNAMIC_SUPPORT" != "xnone"; then
          EC_DYNAMIC_ARCH="arm"
        fi
      fi
      ;;
  esac

  EC_DYNAMIC_SUPPORT="${EC_DYNAMIC_SUPPORT#none }"
fi

AM_CONDITIONAL([ENABLE_EC_DYNAMIC_INTEL], [test "x$EC_DYNAMIC_ARCH" = "xintel"])
AM_CONDITIONAL([ENABLE_EC_DYNAMIC_ARM], [test "x$EC_DYNAMIC_ARCH" = "xarm"])

AM_CONDITIONAL([ENABLE_EC_DYNAMIC_X64], [test "x${EC_DYNAMIC_SUPPORT##*x64*}" = "x"])
AM_CONDITIONAL([ENABLE_EC_DYNAMIC_SSE], [test "x${EC_DYNAMIC_SUPPORT##*sse*}" = "x"])
AM_CONDITIONAL([ENABLE_EC_DYNAMIC_AVX], [test "x${EC_DYNAMIC_SUPPORT##*avx*}" = "x"])
AM_CONDITIONAL([ENABLE_EC_DYNAMIC_NEON], [test "x${EC_DYNAMIC_SUPPORT##*neon*}" = "x"])

AC_SUBST(USE_EC_DYNAMIC_X64)
AC_SUBST(USE_EC_DYNAMIC_SSE)
AC_SUBST(USE_EC_DYNAMIC_AVX)
AC_SUBST(USE_EC_DYNAMIC_NEON)

# end EC dynamic code generation section

dnl libglusterfs.so uses math functions
GF_LDADD="${GF_LDADD} ${MATH_LIB}"

case $host_os in
  dnl Can't use libtool's portable "-no-undefined" as it seems to be ignored on Linux
  linux*)
    GF_NO_UNDEFINED='-Wl,--no-undefined'
    ;;
  darwin*)
    GF_NO_UNDEFINED='-Wl,-undefined'
    ;;
  *)
  dnl There's an issue on FreeBSD with reference to __progname used in some parts of code
    GF_NO_UNDEFINED=''
    ;;
esac
dnl GF_XLATOR_DEFAULT_LDFLAGS is for most xlators that expose a common set of symbols
GF_XLATOR_DEFAULT_LDFLAGS='-avoid-version -export-symbols $(top_srcdir)/xlators/xlator.sym $(UUID_LIBS) $(GF_NO_UNDEFINED) $(TIRPC_LIBS)'
dnl GF_XLATOR_LDFLAGS is for xlators that expose extra symbols, e.g. dht
GF_XLATOR_LDFLAGS='-avoid-version $(UUID_LIBS) $(GF_NO_UNDEFINED) $(TIRPC_LIBS)'

AC_SUBST(GF_HOST_OS)
AC_SUBST(GF_CFLAGS)
AC_SUBST(GF_LDFLAGS)
AC_SUBST(GF_LDADD)
AC_SUBST(GF_FUSE_LDADD)
AC_SUBST(GF_FUSE_CFLAGS)
AC_SUBST(RLLIBS)
AC_SUBST(LIBAIO)
AC_SUBST(LIBURING)
AC_SUBST(AM_MAKEFLAGS)
AC_SUBST(AM_LIBTOOLFLAGS)
AC_SUBST(GF_NO_UNDEFINED)
AC_SUBST(GF_XLATOR_DEFAULT_LDFLAGS)
AC_SUBST(GF_XLATOR_LDFLAGS)
AC_SUBST(GF_XLATOR_MGNT_LIBADD)

case $host_os in
     *freebsd*)
        GF_XLATOR_MGNT_LIBADD="-lutil -lprocstat"
        ;;
esac

CONTRIBDIR='$(top_srcdir)/contrib'
AC_SUBST(CONTRIBDIR)

GF_CPPDEFINES='-D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D$(GF_HOST_OS)'
GF_CPPINCLUDES='-include $(top_builddir)/config.h -include $(top_builddir)/site.h -I$(top_srcdir)/libglusterfs/src -I$(top_builddir)/libglusterfs/src'
if test "x${USE_CONTRIB_URCU}" = "xyes"; then
    GF_CPPINCLUDES="${GF_CPPINCLUDES} -I\$(CONTRIBDIR)/userspace-rcu"
fi
GF_CPPFLAGS="$GF_CPPFLAGS $GF_CPPDEFINES $GF_CPPINCLUDES"
AC_SUBST([GF_CPPFLAGS])

dnl Note GF_BSD_HOST_OS and GF_SOLARIS_HOST_OS are not used
dnl through Makefile.am and so not defined as conditionals.
AM_CONDITIONAL([GF_LINUX_HOST_OS], test "${GF_HOST_OS}" = "GF_LINUX_HOST_OS")
AM_CONDITIONAL([GF_DARWIN_HOST_OS], test "${GF_HOST_OS}" = "GF_DARWIN_HOST_OS")

AC_SUBST(GLUSTERD_WORKDIR)
AM_CONDITIONAL([GF_INSTALL_GLUSTERD_WORKDIR], test ! -d "${GLUSTERD_WORKDIR}" && test -d "${sysconfdir}/glusterd" )
AC_SUBST(GLUSTERD_VOLFILE)
AC_SUBST(GLUSTERFS_LIBEXECDIR)
AC_SUBST(GLUSTERFSD_MISCDIR)

dnl pkg-config versioning
dnl
dnl Once we released gluster-api.pc with version=7. Since then we undid the
dnl library versioning and replaced it with symbol-versioning. The current
dnl libgfapi.so has version 0, but the symbols have the version from the main
dnl package at the time they were added.
dnl
dnl Because other packages (like samba) use the pkg-config version, we can not
dnl drop it, or decrease the version easily. The simplest solution is to keep
dnl the version=7 and add sub-digits for the actual package/symbol versions.
GFAPI_VERSION="7."${PACKAGE_VERSION}
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

GFAPI_LIBS="${ACL_LIBS}"
AC_SUBST(GFAPI_LIBS)

dnl this change necessary for run-tests.sh
AC_CONFIG_FILES([tests/env.rc],[ln -s ${ac_abs_builddir}/env.rc ${ac_abs_srcdir}/env.rc 2>/dev/null])

AC_OUTPUT

echo
echo "GlusterFS configure summary"
echo "==========================="
echo "FUSE client          : $BUILD_FUSE_CLIENT"
echo "epoll IO multiplex   : $BUILD_EPOLL"
echo "fusermount           : $BUILD_FUSERMOUNT"
echo "readline             : $BUILD_READLINE"
echo "georeplication       : $BUILD_SYNCDAEMON"
echo "Linux-AIO            : $BUILD_LIBAIO"
echo "Linux io_uring       : $BUILD_LINUX_IO_URING"
echo "Use liburing         : $BUILD_LIBURING"
echo "Enable Debug         : $BUILD_DEBUG"
echo "Run with Valgrind    : $VALGRIND_TOOL"
echo "Sanitizer enabled    : $SANITIZER"
echo "XML output           : $BUILD_XML_OUTPUT"
echo "Unit Tests           : $BUILD_UNITTEST"
echo "Track priv ports     : $TRACK_PRIVPORTS"
echo "POSIX ACLs           : $BUILD_POSIX_ACLS"
echo "SELinux features     : $USE_SELINUX"
echo "firewalld-config     : $BUILD_FIREWALLD"
echo "Events               : $BUILD_EVENTS"
echo "EC dynamic support   : $EC_DYNAMIC_SUPPORT"
echo "Use memory pools     : $USE_MEMPOOL"
echo "Nanosecond m/atimes  : $BUILD_NANOSECOND_TIMESTAMPS"
echo "Server components    : $with_server"
echo "Legacy gNFS server   : $BUILD_GNFS"
echo "IPV6 default         : $with_ipv6_default"
echo "Use TIRPC            : $with_libtirpc"
echo "With Python          : ${PYTHON_VERSION}"
echo "Cloudsync            : $BUILD_CLOUDSYNC"
echo "Metadata dispersal   : $BUILD_METADISP"
echo "Link with TCMALLOC   : $BUILD_TCMALLOC"
echo "Enable Brick Mux     : $USE_BRICKMUX"
echo "Building with LTO    : $LTO_BUILD"
echo

# dnl Note: ${X^^} capitalization assumes bash >= 4.x
if test "x$SANITIZER" != "xnone"; then
        echo "Note: since glusterfs processes are daemon processes, use"
        echo "'export ${SANITIZER^^}_OPTIONS=log_path=/path/to/xxx.log' to collect"
        echo "sanitizer output. Further details and more options can be"
        echo "found at https://github.com/google/sanitizers."
fi
