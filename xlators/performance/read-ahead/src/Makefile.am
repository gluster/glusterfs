xlator_LTLIBRARIES = read-ahead.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

read_ahead_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

read_ahead_la_SOURCES = read-ahead.c page.c
read_ahead_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = read-ahead.h read-ahead-mem-types.h read-ahead-messages.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = 
