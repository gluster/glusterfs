xlator_LTLIBRARIES = server.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/nfs
nfsrpclibdir = $(top_srcdir)/rpc/rpc-lib/src
server_la_LDFLAGS = -module -avoid-version -export-symbols \
	$(top_srcdir)/xlators/nfs/server/src/nfsserver.sym

server_la_SOURCES = nfs.c nfs-common.c nfs-fops.c nfs-inodes.c \
	nfs-generics.c mount3.c nfs3-fh.c nfs3.c nfs3-helpers.c nlm4.c \
	nlmcbk_svc.c mount3udp_svc.c acl3.c netgroups.c exports.c \
	mount3-auth.c auth-cache.c

server_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/api/src/libgfapi.la

noinst_HEADERS = nfs.h nfs-common.h nfs-fops.h nfs-inodes.h nfs-generics.h \
	mount3.h nfs3-fh.h nfs3.h nfs3-helpers.h nfs-mem-types.h nlm4.h \
	acl3.h netgroups.h exports.h mount3-auth.h auth-cache.h nfs-messages.h

AM_CPPFLAGS = $(GF_CPPFLAGS) \
	-DLIBDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/auth\" \
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/api/src \
	-I$(top_srcdir)/rpc/xdr/src/ -I$(top_builddir)/rpc/xdr/src/ \
	-I$(nfsrpclibdir) -I$(CONTRIBDIR)/rbtree \
	-DDATADIR=\"$(localstatedir)\"

AM_CFLAGS = -Wall $(GF_CFLAGS)

AM_LDFLAGS = -L$(xlatordir)

CLEANFILES =

EXTRA_DIST = nfsserver.sym
