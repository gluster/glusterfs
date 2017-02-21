xlator_LTLIBRARIES = sink.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/debug

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
        -I$(top_builddir)/rpc/xdr/src
AM_CFLAGS = -Wall $(GF_CFLAGS)

sink_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

sink_la_SOURCES = sink.c
sink_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

CLEANFILES =

