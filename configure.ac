AC_INIT([glusterfs],[1.0.rc2],[gluster-devel@nongnu.org])
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
		xlators/filter/Makefile
		xlators/filter/src/Makefile
		xlators/storage/Makefile
		xlators/storage/posix/Makefile
		xlators/storage/posix/src/Makefile
		xlators/cluster/Makefile
		xlators/cluster/unify/Makefile
		xlators/cluster/unify/src/Makefile
		xlators/optimizer/Makefile
		xlators/optimizer/write-back/Makefile
		xlators/optimizer/write-back/src/Makefile
		xlators/optimizer/io-cache/Makefile
		xlators/optimizer/io-cache/src/Makefile
		xlators/debug/Makefile
		xlators/debug/trace/Makefile
		xlators/debug/trace/src/Makefile
		xlators/protocol/Makefile
		xlators/protocol/client/Makefile
		xlators/protocol/client/src/Makefile
		xlators/protocol/server/Makefile
		xlators/protocol/server/src/Makefile
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
		doc/Makefile])
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


AC_CHECK_LIB([fuse], [fuse_req_interrupt_func], ,AC_MSG_ERROR([FUSE >= 2.6.0 is needed. Download it from http://fuse.sf.net]))
AC_OUTPUT
