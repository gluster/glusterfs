xlator_LTLIBRARIES = map.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/testing/cluster

map_la_LDFLAGS = -module -avoidversion 

map_la_SOURCES = map.c map-helper.c
map_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = map.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	    -I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 

