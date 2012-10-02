
xlator_LTLIBRARIES = io-stats.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/debug

io_stats_la_LDFLAGS = -module -avoidversion

io_stats_la_SOURCES = io-stats.c
io_stats_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = io-stats-mem-types.h

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared $(GF_CFLAGS)

CLEANFILES =
