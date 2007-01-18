AC_INIT([glusterfs],[1.2.2],[gluster-devel@nongnu.org])
AM_INIT_AUTOMAKE
AC_CONFIG_FILES([Makefile 
		libglusterfs/Makefile 
		libglusterfs/src/Makefile
		glusterfsd/Makefile
		glusterfsd/src/Makefile
		glusterfs-fuse/Makefile
		glusterfs-fuse/src/Makefile
		glusterfs-fuse/utils/mount.glusterfs
		glusterfs-fuse/utils/Makefile
		xlators/Makefile
		xlators/storage/Makefile
		xlators/storage/posix/Makefile
		xlators/storage/posix/src/Makefile
		xlators/cluster/Makefile
		xlators/cluster/unify/Makefile
		xlators/cluster/unify/src/Makefile
		xlators/performance/Makefile
		xlators/performance/write-back/Makefile
		xlators/performance/write-back/src/Makefile
		xlators/performance/read-ahead/Makefile
		xlators/performance/read-ahead/src/Makefile
		xlators/performance/stat-prefetch/Makefile
		xlators/performance/stat-prefetch/src/Makefile
		xlators/debug/Makefile
		xlators/debug/trace/Makefile
		xlators/debug/trace/src/Makefile
		xlators/protocol/Makefile
		xlators/protocol/client/Makefile
		xlators/protocol/client/src/Makefile
		xlators/protocol/server/Makefile
		xlators/protocol/server/src/Makefile
		xlators/features/Makefile
		xlators/features/filter/Makefile
		xlators/features/filter/src/Makefile
		scheduler/Makefile
		scheduler/alu/Makefile
		scheduler/alu/src/Makefile
		scheduler/random/Makefile
		scheduler/random/src/Makefile
		scheduler/nufa/Makefile
		scheduler/nufa/src/Makefile
		scheduler/rr/Makefile
		scheduler/rr/src/Makefile
		transport/Makefile
		transport/tcp/Makefile
		transport/tcp/server/Makefile
		transport/tcp/server/src/Makefile
		transport/tcp/client/Makefile
		transport/tcp/client/src/Makefile
		transport/ib-sdp/Makefile
		transport/ib-sdp/server/Makefile
		transport/ib-sdp/server/src/Makefile
		transport/ib-sdp/client/Makefile
		transport/ib-sdp/client/src/Makefile
		doc/Makefile
		doc/examples/Makefile])
AC_PROG_CC
AC_PROG_RANLIB
AC_PROG_LEX
AC_PROG_YACC

AC_CHECK_TOOL([LD],[ld])

dnl AC_CHECK_PROG(GUILE_CONFIG,[guile-config],[guile-config],[no])
dnl if test "$GUILE_CONFIG" = no ; then
dnl    AC_MSG_WARN([guile-config not found, disabling glusterfs-shell])
dnl else
dnl    GUILE_CLFAGS=`guile-config compile`
dnl    GUILE_LDFLAGS=`guile-config link`
dnl    AC_CHECK_LIB([guile $GUILE_CFLAGS $GUILE_LDFLAGS],scm_boot_guile,HAVE_GUILE=1,HAVE_GUILE=0)
dnl    if test $HAVE_GUILE -eq 1;
dnl    then
dnl       GUILE_DEPENDENT_PKGS=[glusterfs-shell]
dnl       AC_SUBST(GUILE_CFLAGS)
dnl       AC_SUBST(GUILE_LDFLAGS)
dnl       AC_SUBST(GUILE_DEPENDENT_PKGS)
dnl    else
dnl      AC_MSG_WARN([GNU Guile devel package not installed, disabling glusterfs-shell])
dnl    fi
dnl fi

AC_ARG_ENABLE([fuse-client],
	      AC_HELP_STRING([--disable-fuse-client],
			     [Do not build the fuse client. NOTE: you cannot mount glusterfs without the client]))

if test "x$enable_fuse_client" = "x"; then
  enable_fuse_client="yes";
fi

if test "$enable_fuse_client" = "yes"; then
  AC_CHECK_LIB([fuse],
               [fuse_req_interrupt_func], ,
	       AC_MSG_ERROR([fuse-2.6.x is needed to build the glusterfs client. Download it from http://fuse.sourceforge.net. You can disable client build by passing --disable-fuse-client to configure. Note that you will not be able to mount glusterfs without the fuse client]))
fi

HAVE_FUSE_CLIENT=1
if test "$enable_fuse_client" = "yes"; then
  HAVE_FUSE_CLIENT=1
  FUSE_CLIENT_SUBDIR=glusterfs-fuse
else
  HAVE_FUSE_CLIENT=0
  FUSE_CLIENT_SUBDIR=
fi

AC_SUBST(HAVE_FUSE_CLIENT)
AC_SUBST(FUSE_CLIENT_SUBDIR)
AC_OUTPUT
