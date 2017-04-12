xlator_LTLIBRARIES = nl-cache.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance
nl_cache_la_LDFLAGS = -module -avoid-version
nl_cache_la_SOURCES = nl-cache.c nl-cache-helper.c
nl_cache_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la
noinst_HEADERS = nl-cache.h nl-cache-mem-types.h nl-cache-messages.h
AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
        -I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
        -I$(CONTRIBDIR)/timer-wheel

AM_CFLAGS = -Wall -fno-strict-aliasing $(GF_CFLAGS)
CLEANFILES =
