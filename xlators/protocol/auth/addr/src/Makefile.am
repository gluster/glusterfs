auth_LTLIBRARIES = addr.la
authdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/auth

addr_la_LDFLAGS = -module -avoidversion

addr_la_SOURCES = addr.c
addr_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS) \
	-I$(top_srcdir)/xlators/protocol/server/src \
	-I$(top_srcdir)/rpc/rpc-lib/src/
