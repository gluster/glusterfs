xlator_LTLIBRARIES = server.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/nfs
nfsrpclibdir = $(top_srcdir)/rpc/rpc-lib/src
server_la_LDFLAGS = -module -avoid-version
server_la_SOURCES = nfs.c nfs-common.c nfs-fops.c nfs-inodes.c \
	nfs-generics.c mount3.c nfs3-fh.c nfs3.c nfs3-helpers.c nlm4.c \
	nlmcbk_svc.c mount3udp_svc.c acl3.c
server_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = nfs.h nfs-common.h nfs-fops.h nfs-inodes.h nfs-generics.h \
	mount3.h nfs3-fh.h nfs3.h nfs3-helpers.h nfs-mem-types.h nlm4.h \
	acl3.h

AM_CPPFLAGS = $(GF_CPPFLAGS) \
	-DLIBDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/auth\" \
	-I$(top_srcdir)/libglusterfs/src \
	-I$(nfsrpclibdir) -I$(CONTRIBDIR)/rbtree \
	-I$(top_srcdir)/rpc/xdr/src/

AM_CFLAGS = -Wall $(GF_CFLAGS)

AM_LDFLAGS = -L$(xlatordir)

CLEANFILES =
