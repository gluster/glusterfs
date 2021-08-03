tierplugin_LTLIBRARIES = filesystem.la
tierplugindir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/tier-plugins

filesystem_la_SOURCES = filesystem.c
filesystem_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la
filesystem_la_LDFLAGS = -module -avoid-version -export-symbols $(top_srcdir)/xlators/features/tier/plugins/libtier.sym
AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src   -I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src
noinst_HEADERS = filesystem.h filesystem-mem-types.h filesystem-messages.h
AM_CFLAGS = -Wall -fno-strict-aliasing $(GF_CFLAGS) -I$(top_srcdir)/xlators/features/tier/src
CLEANFILES =
