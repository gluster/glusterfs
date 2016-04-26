xlator_LTLIBRARIES = quick-read.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

quick_read_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

quick_read_la_SOURCES = quick-read.c
quick_read_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = quick-read.h quick-read-mem-types.h quick-read-messages.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = 
