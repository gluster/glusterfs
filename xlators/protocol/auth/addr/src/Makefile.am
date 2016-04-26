auth_LTLIBRARIES = addr.la
authdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/auth

addr_la_LDFLAGS = -module -avoid-version

addr_la_SOURCES = addr.c
addr_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/xlators/protocol/server/src \
	-I$(top_srcdir)/rpc/xdr/src/ -I$(top_builddir)/rpc/xdr/src/ \
	-I$(top_srcdir)/rpc/rpc-lib/src/

AM_CFLAGS = -Wall $(GF_CFLAGS)
