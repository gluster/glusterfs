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
		xlators/brick/Makefile
		xlators/brick/src/Makefile
		xlators/posix/Makefile
		xlators/posix/src/Makefile
		xlators/cement/Makefile
		xlators/cement/src/Makefile
		xlators/filter/Makefile
		xlators/filter/src/Makefile
		xlators/getattr/Makefile
		xlators/getattr/src/Makefile			
		doc/Makefile])
AC_PROG_CC
AC_PROG_LD
AC_PROG_RANLIB
AC_PROG_LEX
AC_PROG_YACC

dnl AC_CHECK_LIB([fuse], [fuse_main], ,AC_MSG_ERROR([FUSE is needed. download from http://fuse.sf.net]))

AC_CHECK_TYPE([fuse_fill_dir_t], , AC_MSG_ERROR([FUSE >= 2.5.3 is needed. Download it from http://fuse.sf.net]), [#define _FILE_OFFSET_BITS 64
#include <fuse.h>])    
AC_OUTPUT
