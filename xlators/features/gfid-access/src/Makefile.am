xlator_LTLIBRARIES = gfid-access.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

gfid_access_la_LDFLAGS = -module -avoid-version

gfid_access_la_SOURCES = gfid-access.c
gfid_access_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = gfid-access.h gfid-access-mem-types.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
