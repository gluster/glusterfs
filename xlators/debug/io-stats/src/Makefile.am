
xlator_LTLIBRARIES = io-stats.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/debug

io_stats_la_LDFLAGS = -module -avoidversion

io_stats_la_SOURCES = io-stats.c
io_stats_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES =
