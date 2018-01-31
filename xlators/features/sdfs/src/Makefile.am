if WITH_SERVER
xlator_LTLIBRARIES = sdfs.la
endif
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

sdfs_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

sdfs_la_SOURCES = sdfs.c
sdfs_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = sdfs.h sdfs-messages.h $(top_builddir)/xlators/lib/src/libxlator.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
                                 -I$(top_srcdir)/xlators/lib/src \
        -I$(top_srcdir)/rpc/xdr/src/ -I$(top_builddir)/rpc/xdr/src/

AM_CFLAGS = -Wall -fno-strict-aliasing $(GF_CFLAGS)

CLEANFILES =
