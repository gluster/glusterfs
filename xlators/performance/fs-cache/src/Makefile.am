xlator_LTLIBRARIES = fs-cache.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

fs_cache_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

fs_cache_la_SOURCES = fs-cache.c fsc-inode.c fsc-block.c fsc-aux-thread.c
fs_cache_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = fs-cache.h fs-cache-messages.h fsc-mem-types.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
        -I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
        -I$(CONTRIBDIR)/rbtree

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =  
