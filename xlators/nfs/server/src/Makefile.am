xlator_LTLIBRARIES = server.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/nfs
rpclibdir = $(top_srcdir)/xlators/nfs/lib/src/
server_la_LDFLAGS = -module -avoidversion
server_la_SOURCES = nfs.c nfs-common.c nfs-fops.c nfs-inodes.c nfs-generics.c mount3.c nfs3-fh.c
server_la_LIBADD = $(top_builddir)/xlators/nfs/lib/src/librpcsvc.la $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = nfs.h nfs-common.h nfs-fops.h nfs-inodes.h nfs-generics.h mount3.h nfs3-fh.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)\
	-I$(rpclibdir) -L$(xlatordir)/ -I$(CONTRIBDIR)/rbtree

CLEANFILES =
