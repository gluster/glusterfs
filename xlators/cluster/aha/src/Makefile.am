
xlator_LTLIBRARIES = aha.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/cluster

aha_la_LDFLAGS = -module -avoid-version

aha_la_SOURCES = aha.c aha-fops.c aha-helpers.c aha-retry.c
aha_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = aha-mem-types.h aha.h aha-helpers.h aha.h aha-retry.h aha-fops.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src \
	-I$(top_srcdir)/rpc/rpc-lib/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
