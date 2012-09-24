xlator_LTLIBRARIES = filter.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/testing/features

filter_la_LDFLAGS = -module -avoid-version -shared

filter_la_SOURCES = filter.c
filter_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la 

noinst_HEADERS = filter-mem-types.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 

