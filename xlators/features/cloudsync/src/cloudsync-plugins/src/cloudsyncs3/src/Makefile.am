csp_LTLIBRARIES = cloudsyncs3.la
cspdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/cloudsync-plugins

cloudsyncs3_la_SOURCES = libcloudsyncs3.c  $(top_srcdir)/xlators/features/cloudsync/src/cloudsync-common.c
cloudsyncs3_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la
cloudsyncs3_la_LDFLAGS = -module -export-symbols $(top_srcdir)/xlators/features/cloudsync/src/cloudsync-plugins/src/cloudsyncs3/src/libcloudsyncs3.sym $(GF_XLATOR_LDFLAGS)
AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src   -I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src -lcurlpp -lcryptopp
noinst_HEADERS = libcloudsyncs3.h libcloudsyncs3-mem-types.h
AM_CFLAGS = -Wall -fno-strict-aliasing $(GF_CFLAGS) -lcurl -lcrypto -I$(top_srcdir)/xlators/features/cloudsync/src
CLEANFILES =

EXTRA_DIST = libcloudsyncs3.sym
