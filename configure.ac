AC_INIT([glusterfs],[0.1-alpha0],[gluster-devel@nongnu.org])
AM_INIT_AUTOMAKE
AC_CONFIG_FILES([Makefile 
		libglusterfs/Makefile 
		libglusterfs/src/Makefile
		glusterfsd/Makefile
		glusterfsd/src/Makefile
		glusterfs-fuse/Makefile
		glusterfs-fuse/src/Makefile
		xlators/Makefile
		xlators/brick/Makefile
		xlators/brick/src/Makefile
		xlators/posix/Makefile
		xlators/posix/src/Makefile
		xlators/cement/Makefile
		xlators/cement/src/Makefile
		doc/Makefile])
AC_PROG_CC
AC_PROG_LD
AC_PROG_RANLIB
AC_PROG_LEX
AC_PROG_YACC

dnl AC_CHECK_LIB([fuse], [fuse_main], ,AC_MSG_ERROR([FUSE is needed. download from http://fuse.sf.net]))
AC_OUTPUT
