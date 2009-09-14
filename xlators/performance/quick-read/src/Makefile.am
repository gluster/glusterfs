xlator_LTLIBRARIES = quick-read.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

quick_read_la_LDFLAGS = -module -avoidversion 

quick_read_la_SOURCES = quick-read.c
quick_read_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = quick-read.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 
