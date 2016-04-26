xlator_LTLIBRARIES = snapview-server.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

snapview_server_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

snapview_server_la_SOURCES = snapview-server.c snapview-server-mgmt.c \
	snapview-server-helpers.c

snapview_server_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/api/src/libgfapi.la \
	$(RLLIBS) $(top_builddir)/rpc/xdr/src/libgfxdr.la \
	$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la

noinst_HEADERS = snapview-server.h snapview-server-mem-types.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/api/src -I$(top_srcdir)/rpc/rpc-lib/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-DDATADIR=\"$(localstatedir)\"

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
