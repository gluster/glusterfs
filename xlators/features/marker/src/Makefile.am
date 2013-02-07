xlator_LTLIBRARIES = marker.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

marker_la_LDFLAGS = -module -avoid-version

marker_la_SOURCES = marker.c marker-quota.c marker-quota-helper.c marker-common.c
marker_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = marker-mem-types.h marker.h marker-quota.h marker-quota-helper.h marker-common.h $(top_builddir)/xlators/lib/src/libxlator.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/xlators/lib/src

AM_CFLAGS = -Wall -fno-strict-aliasing $(GF_CFLAGS)

CLEANFILES =

