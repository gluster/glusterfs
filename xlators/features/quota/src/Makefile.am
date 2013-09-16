xlator_LTLIBRARIES = quota.la quotad.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

quota_la_LDFLAGS = -module -avoid-version
quotad_la_LDFLAGS = -module -avoid-version

quota_la_SOURCES = quota.c quota-enforcer-client.c
quota_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

quotad_la_SOURCES = quotad.c quotad-helpers.c quotad-aggregator.c
quotad_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = quota-mem-types.h quota.h quotad-aggregator.h quotad-helpers.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/xlators/cluster/dht/src -I$(top_srcdir)/rpc/xdr/src/ \
	-I$(top_srcdir)/rpc/rpc-lib/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =

