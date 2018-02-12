xlator_LTLIBRARIES = namespace.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

namespace_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

namespace_la_SOURCES = namespace.c
namespace_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = namespace.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-I$(top_srcdir)/xlators/lib/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
