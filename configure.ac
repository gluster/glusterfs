AC_INIT([glusterfs],[0.1-alpha0],[gluster-devel@nongnu.org])
AM_INIT_AUTOMAKE
AC_CONFIG_FILES([Makefile 
		libglusterfs/Makefile 
		libglusterfs/src/Makefile
		glusterfsd/Makefile
		glusterfsd/src/Makefile
		glusterfs-fuse/Makefile
		glusterfs-fuse/src/Makefile
		glusterfs-shell/Makefile
		glusterfs-shell/src/Makefile
		glusterfs-shell/extensions/Makefile
		xlators/Makefile
		xlators/xport/Makefile
		xlators/xport/socket/Makefile
		xlators/xport/socket/src/Makefile
		xlators/storage/Makefile
		xlators/storage/posix/Makefile
		xlators/storage/posix/src/Makefile
		xlators/sched/Makefile
		xlators/sched/unify/Makefile
		xlators/sched/unify/src/Makefile
		xlators/filter/Makefile
		xlators/filter/src/Makefile
		xlators/steroid/Makefile
		xlators/steroid/stat-prefetch/Makefile
		xlators/steroid/stat-prefetch/src/Makefile
		scheduler/Makefile
		scheduler/alu/Makefile
		scheduler/alu/src/Makefile
		scheduler/random/Makefile
		scheduler/random/src/Makefile
		doc/Makefile])
AC_PROG_CC
AC_PROG_LD
AC_PROG_RANLIB
AC_PROG_LEX
AC_PROG_YACC

AC_CHECK_LIB([guile],[scm_boot_guile],HAVE_GUILE=1,HAVE_GUILE=0)

if test $HAVE_GUILE -eq 1;
then
   GUILE_CLFAGS=`guile-config compile`
   GUILE_LDFLAGS=`guile-config link`
fi

AC_SUBST(GUILE_CFLAGS)
AC_SUBST(GUILE_LDFLAGS)
		
dnl AC_CHECK_LIB([fuse], [fuse_main], ,AC_MSG_ERROR([FUSE is needed. download from http://fuse.sf.net]))

AC_CHECK_TYPE([fuse_fill_dir_t], , AC_MSG_ERROR([FUSE >= 2.5.3 is needed. Download it from http://fuse.sf.net]), [#define _FILE_OFFSET_BITS 64
#include <fuse.h>])    
AC_OUTPUT
