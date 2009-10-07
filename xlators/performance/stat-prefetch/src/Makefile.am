xlator_LTLIBRARIES = stat-prefetch.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

stat_prefetch_la_LDFLAGS = -module -avoidversion
stat_prefetch_la_SOURCES = stat-prefetch.c
noinst_HEADERS = stat-prefetch.h

stat_prefetch_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/contrib/rbtree/ -nostartfiles $(GF_CFLAGS)

CLEANFILES = 

