xlator_LTLIBRARIES = cdc.la

xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

noinst_HEADERS = cdc.h cdc-mem-types.h

cdc_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

cdc_la_SOURCES = cdc.c cdc-helper.c
cdc_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la $(ZLIB_LIBS)

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D$(GF_HOST_OS) \
	$(LIBZ_CFLAGS)

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
