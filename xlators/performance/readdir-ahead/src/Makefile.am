xlator_LTLIBRARIES = readdir-ahead.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

readdir_ahead_la_LDFLAGS = -module -avoid-version

readdir_ahead_la_SOURCES = readdir-ahead.c
readdir_ahead_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = readdir-ahead.h readdir-ahead-mem-types.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
