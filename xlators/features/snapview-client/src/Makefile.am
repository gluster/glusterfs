xlator_LTLIBRARIES = snapview-client.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

snapview_client_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

snapview_client_la_SOURCES = snapview-client.c
snapview_client_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = snapview-client.h snapview-client-mem-types.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
