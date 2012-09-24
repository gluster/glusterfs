xlator_LTLIBRARIES = index.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

index_la_LDFLAGS = -module -avoid-version -shared

index_la_SOURCES = index.c
index_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = index.h index-mem-types.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/rpc/xdr/src \
	-I$(top_srcdir)/rpc/rpc-lib/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES =
