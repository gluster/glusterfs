xlator_LTLIBRARIES = symlink-cache.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

symlink_cache_la_LDFLAGS = -module -avoidversion 

symlink_cache_la_SOURCES = symlink-cache.c
symlink_cache_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 
