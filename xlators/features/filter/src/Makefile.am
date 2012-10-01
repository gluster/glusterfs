xlator_LTLIBRARIES = filter.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/testing/features

filter_la_LDFLAGS = -module -avoidversion

filter_la_SOURCES = filter.c
filter_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la 

noinst_HEADERS = filter-mem-types.h

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 

