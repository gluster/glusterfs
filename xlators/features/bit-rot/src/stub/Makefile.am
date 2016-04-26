xlator_LTLIBRARIES = bitrot-stub.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

bitrot_stub_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

bitrot_stub_la_SOURCES = bit-rot-stub-helpers.c bit-rot-stub.c
bitrot_stub_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = bit-rot-stub.h bit-rot-common.h bit-rot-stub-mem-types.h \
	bit-rot-object-version.h bit-rot-stub-messages.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-I$(top_srcdir)/rpc/rpc-lib/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
