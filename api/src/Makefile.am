lib_LTLIBRARIES = libgfapi.la
noinst_HEADERS = glfs-mem-types.h glfs-internal.h
libgfapi_HEADERS = glfs.h
libgfapidir = $(includedir)/glusterfs/api

libgfapi_la_SOURCES = glfs.c glfs-mgmt.c glfs-fops.c glfs-resolve.c
libgfapi_la_CFLAGS = -fPIC -Wall
libgfapi_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la
	$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la \
	$(top_builddir)/rpc/xdr/src/libgfxdr.la \
	$(GF_LDADD)

libgfapi_la_CPPFLAGS = -D_FILE_OFFSET_BITS=64 -D__USE_FILE_OFFSET64 -D_GNU_SOURCE \
	-D$(GF_HOST_OS) $(GF_CFLAGS) \
	-I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/rpc-lib/src \
	-I$(top_srcdir)/rpc/xdr/src
libgfapi_la_LDFLAGS = -shared -nostartfiles


xlator_LTLIBRARIES = api.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/mount

api_la_SOURCES = glfs-master.c

api_la_LDFLAGS = -module -avoid-version -shared
api_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D$(GF_HOST_OS) -Wall \
	-I$(top_srcdir)/libglusterfs/src $(GF_CFLAGS)
