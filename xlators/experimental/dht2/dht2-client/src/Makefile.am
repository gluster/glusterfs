xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/experimental
xlator_LTLIBRARIES = dht2c.la

dht2c_sources = dht2-client-main.c

dht2common_sources = $(top_srcdir)/xlators/experimental/dht2/dht2-common/src/dht2-common-map.c

dht2c_la_SOURCES = $(dht2c_sources) $(dht2common_sources)
dht2c_la_LDFLAGS = -module -avoid-version
dht2c_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CFLAGS = -Wall $(GF_CFLAGS)

AM_CPPFLAGS = $(GF_CPPFLAGS)
AM_CPPFLAGS += -I$(top_srcdir)/xlators/experimental/dht2/dht2-common/src/
AM_CPPFLAGS += -I$(top_srcdir)/libglusterfs/src
AM_CPPFLAGS += -I$(top_srcdir)/xlators/lib/src

CLEANFILES =
