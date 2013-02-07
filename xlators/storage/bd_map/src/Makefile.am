
if ENABLE_BD_XLATOR
xlator_LTLIBRARIES = bd_map.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/storage

bd_map_la_LDFLAGS = -module -avoid-version
LIBBD = -llvm2app -lrt
bd_map_la_SOURCES = bd_map.c bd_map_help.c
bd_map_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la $(LIBBD)

noinst_HEADERS = bd_map.h bd_map_help.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
            -I$(top_srcdir)/rpc/xdr/src \
            -I$(top_srcdir)/rpc/rpc-lib/src

AM_CFLAGS = -fno-strict-aliasing -Wall $(GF_CFLAGS)

CLEANFILES =

endif
