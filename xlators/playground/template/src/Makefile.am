xlator_LTLIBRARIES = template.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/testing/features

template_la_LDFLAGS = -module -avoid-version

template_la_SOURCES = template.c
template_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = template.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =

