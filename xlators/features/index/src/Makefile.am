xlator_LTLIBRARIES = index.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

index_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

index_la_SOURCES = index.c
index_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = index.h index-mem-types.h index-messages.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-I$(top_srcdir)/rpc/rpc-lib/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
