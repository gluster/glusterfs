xlator_LTLIBRARIES = upcall.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

upcall_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

upcall_la_SOURCES = upcall.c upcall-internal.c

upcall_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la \
	$(top_builddir)/rpc/xdr/src/libgfxdr.la

noinst_HEADERS = upcall.h upcall-mem-types.h upcall-messages.h \
	upcall-cache-invalidation.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/rpc-lib/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall -fno-strict-aliasing $(GF_CFLAGS)

CLEANFILES =
