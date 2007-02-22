xlator_PROGRAMS = stat-prefetch.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

stat_prefetch_so_SOURCES = stat-prefetch.c
noinst_HEADERS = stat-prefetch.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

