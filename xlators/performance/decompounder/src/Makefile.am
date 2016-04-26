xlator_LTLIBRARIES = decompounder.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

decompounder_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

decompounder_la_SOURCES = decompounder.c
decompounder_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = decompounder-mem-types.h decompounder-messages.h \
	decompounder.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
