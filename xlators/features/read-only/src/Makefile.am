xlator_LTLIBRARIES = read-only.la worm.la

xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

noinst_HEADERS = read-only-common.h

read_only_la_LDFLAGS = -module -avoid-version

read_only_la_SOURCES = read-only.c read-only-common.c
read_only_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la 

worm_la_LDFLAGS = -module -avoid-version

worm_la_SOURCES = read-only-common.c worm.c
worm_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = 

