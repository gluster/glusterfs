if ENABLE_BD_XLATOR
xlator_LTLIBRARIES = bd.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/storage

bd_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)
LIBBD = -llvm2app -lrt
bd_la_SOURCES = bd.c bd-helper.c bd-aio.c
bd_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(LIBBD) $(LIBAIO)

noinst_HEADERS = bd.h bd-aio.h bd-mem-types.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-I$(top_srcdir)/rpc/rpc-lib/src

AM_CFLAGS = -fno-strict-aliasing -Wall $(GF_CFLAGS)

CLEANFILES =

endif
