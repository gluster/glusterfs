
xlator_LTLIBRARIES = delay-gen.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/debug
delay_gen_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)
delay_gen_la_SOURCES = delay-gen.c
delay_gen_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la
noinst_HEADERS = delay-gen.h delay-gen-mem-types.h delay-gen-messages.h
AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	   -I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src
AM_CFLAGS = -Wall -fno-strict-aliasing $(GF_CFLAGS)
CLEANFILES =
