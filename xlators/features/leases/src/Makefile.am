xlator_LTLIBRARIES = leases.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

leases_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

leases_la_SOURCES = leases.c leases-internal.c

leases_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = leases.h leases-mem-types.h leases-messages.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-I$(CONTRIBDIR)/timer-wheel

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
