xlator_LTLIBRARIES = trash.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

trash_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

trash_la_SOURCES = trash.c
trash_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la 

noinst_HEADERS = trash.h trash-mem-types.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = 

