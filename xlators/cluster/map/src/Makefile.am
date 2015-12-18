xlator_LTLIBRARIES = map.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/testing/cluster

map_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)


map_la_SOURCES = map.c map-helper.c
map_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = map.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = 

