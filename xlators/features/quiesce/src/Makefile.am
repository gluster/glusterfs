xlator_LTLIBRARIES = quiesce.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

quiesce_la_LDFLAGS = -module -avoid-version

quiesce_la_SOURCES = quiesce.c
quiesce_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = quiesce.h quiesce-mem-types.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
