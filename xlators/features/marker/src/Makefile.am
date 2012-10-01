xlator_LTLIBRARIES = marker.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

marker_la_LDFLAGS = -module -avoidversion

marker_la_SOURCES = marker.c marker-quota.c marker-quota-helper.c marker-common.c
marker_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = marker-mem-types.h marker.h marker-quota.h marker-quota-helper.h marker-common.h $(top_builddir)/xlators/lib/src/libxlator.h

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall -fno-strict-aliasing \
        -I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/xlators/lib/src $(GF_CFLAGS) -shared -nostartfiles

CLEANFILES =

