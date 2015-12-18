xlator_LTLIBRARIES = ha.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/testing/cluster

ha_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)


ha_la_SOURCES = ha-helpers.c ha.c
ha_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = ha.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = 

