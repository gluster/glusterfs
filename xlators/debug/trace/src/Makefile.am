
xlator_LTLIBRARIES = trace.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/debug

trace_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

trace_la_SOURCES = trace.c
trace_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = trace.h trace-mem-types.h
AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = 

