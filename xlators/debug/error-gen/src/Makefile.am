
xlator_LTLIBRARIES = error-gen.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/debug

error_gen_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

error_gen_la_SOURCES = error-gen.c
error_gen_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = error-gen.h error-gen-mem-types.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = 

