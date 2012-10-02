xlator_LTLIBRARIES = symlink-cache.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/testing/performance

symlink_cache_la_LDFLAGS = -module -avoidversion 

symlink_cache_la_SOURCES = symlink-cache.c
symlink_cache_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared $(GF_CFLAGS)

CLEANFILES = 
