xlator_LTLIBRARIES = read-ahead.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

read_ahead_la_LDFLAGS = -module -avoidversion

read_ahead_la_SOURCES = read-ahead.c page.c
read_ahead_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = read-ahead.h read-ahead-mem-types.h

AM_CFLAGS = $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src $(GF_CFLAGS)

CLEANFILES = 
