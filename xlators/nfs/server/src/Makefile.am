xlator_LTLIBRARIES = server.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/nfs
nfsrpclibdir = $(top_srcdir)/rpc/rpc-lib/src
server_la_LDFLAGS = -module -avoidversion
server_la_SOURCES = nfs.c nfs-common.c nfs-fops.c nfs-inodes.c \
	nfs-generics.c mount3.c nfs3-fh.c nfs3.c nfs3-helpers.c nlm4.c \
	nlmcbk_svc.c mount3udp_svc.c
server_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = nfs.h nfs-common.h nfs-fops.h nfs-inodes.h nfs-generics.h \
	mount3.h nfs3-fh.h nfs3.h nfs3-helpers.h nfs-mem-types.h nlm4.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-DLIBDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/auth\" \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)\
	-I$(nfsrpclibdir) -L$(xlatordir)/ -I$(CONTRIBDIR)/rbtree\
	-I$(top_srcdir)/rpc/xdr/src/

CLEANFILES =
