xlator_LTLIBRARIES = open-behind.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

open_behind_la_LDFLAGS = -module -avoid-version

open_behind_la_SOURCES = open-behind.c
open_behind_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = open-behind-mem-types.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
