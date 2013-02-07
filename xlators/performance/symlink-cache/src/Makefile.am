xlator_LTLIBRARIES = symlink-cache.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/testing/performance

symlink_cache_la_LDFLAGS = -module -avoid-version 

symlink_cache_la_SOURCES = symlink-cache.c
symlink_cache_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = 
