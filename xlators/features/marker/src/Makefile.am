xlator_LTLIBRARIES = marker.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

marker_la_LDFLAGS = -module -avoid-version -shared

marker_la_SOURCES = marker.c marker-quota.c marker-quota-helper.c marker-common.c
marker_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = marker-mem-types.h marker.h marker-quota.h marker-quota-helper.h marker-common.h $(top_builddir)/xlators/lib/src/libxlator.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -fno-strict-aliasing -D$(GF_HOST_OS) \
        -I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/xlators/lib/src $(GF_CFLAGS) -shared -nostartfiles

CLEANFILES =

