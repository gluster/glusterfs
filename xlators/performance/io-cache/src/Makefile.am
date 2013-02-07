xlator_LTLIBRARIES = io-cache.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

io_cache_la_LDFLAGS = -module -avoid-version 

io_cache_la_SOURCES = io-cache.c page.c ioc-inode.c
io_cache_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = io-cache.h ioc-mem-types.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(CONTRIBDIR)/rbtree

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = 
