lib_LTLIBRARIES = libgfapi.la
noinst_HEADERS = glfs-mem-types.h glfs-internal.h
libgfapi_HEADERS = glfs.h
libgfapidir = $(includedir)/glusterfs/api

libgfapi_la_SOURCES = glfs.c glfs-mgmt.c glfs-fops.c glfs-resolve.c
libgfapi_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la \
	$(top_builddir)/rpc/xdr/src/libgfxdr.la \
	$(GF_LDADD)

libgfapi_la_CPPFLAGS = $(GF_CPPFLAGS) -D__USE_FILE_OFFSET64 \
	-I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/rpc-lib/src \
	-I$(top_srcdir)/rpc/xdr/src

xlator_LTLIBRARIES = api.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/mount

api_la_SOURCES = glfs-master.c

api_la_LDFLAGS = -module -avoidversion
api_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall $(GF_CFLAGS)
