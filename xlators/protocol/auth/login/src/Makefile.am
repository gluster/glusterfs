auth_LTLIBRARIES = login.la
authdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/auth

login_la_LDFLAGS = -module -avoid-version

login_la_SOURCES = login.c
login_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/xlators/protocol/server/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)
