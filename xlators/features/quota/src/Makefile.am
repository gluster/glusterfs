xlator_LTLIBRARIES = quota.la quotad.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

quota_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)
quotad_la_LDFLAGS = -module -avoid-version -export-symbols $(top_srcdir)/xlators/features/quota/src/quotad.sym

quota_la_SOURCES = quota.c quota-enforcer-client.c
quota_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

quotad_la_SOURCES = quotad.c quotad-helpers.c quotad-aggregator.c
quotad_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = quota-mem-types.h quota.h quotad-aggregator.h \
	quotad-helpers.h quota-messages.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src/ -I$(top_builddir)/rpc/xdr/src/ \
	-I$(top_srcdir)/rpc/rpc-lib/src \
	-I$(top_srcdir)/xlators/cluster/dht/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =

EXTRA_DIST = quotad.sym

