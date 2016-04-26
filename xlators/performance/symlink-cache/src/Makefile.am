xlator_LTLIBRARIES = symlink-cache.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/testing/performance

symlink_cache_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

symlink_cache_la_SOURCES = symlink-cache.c
symlink_cache_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = symlink-cache-messages.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = 
