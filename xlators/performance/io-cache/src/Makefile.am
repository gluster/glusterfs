xlator_LTLIBRARIES = io-cache.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

io_cache_la_LDFLAGS = -module -avoidversion 

io_cache_la_SOURCES = io-cache.c page.c ioc-inode.c
io_cache_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = io-cache.h ioc-mem-types.h

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -I$(CONTRIBDIR)/rbtree -shared $(GF_CFLAGS)

CLEANFILES = 
