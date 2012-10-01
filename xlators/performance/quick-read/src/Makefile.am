xlator_LTLIBRARIES = quick-read.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

quick_read_la_LDFLAGS = -module -avoidversion 

quick_read_la_SOURCES = quick-read.c
quick_read_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = quick-read.h quick-read-mem-types.h

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 
