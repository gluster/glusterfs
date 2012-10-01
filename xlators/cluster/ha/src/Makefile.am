xlator_LTLIBRARIES = ha.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/testing/cluster

ha_la_LDFLAGS = -module -avoidversion 

ha_la_SOURCES = ha-helpers.c ha.c
ha_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = ha.h

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	    -I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 

