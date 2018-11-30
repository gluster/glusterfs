csp_LTLIBRARIES = cloudsynccvlt.la
cspdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/cloudsync-plugins

cloudsynccvlt_la_SOURCES = libcvlt.c  $(top_srcdir)/xlators/features/cloudsync/src/cloudsync-common.c
cloudsynccvlt_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la
cloudsynccvlt_la_LDFLAGS = -module -avoid-version -export-symbols $(top_srcdir)/xlators/features/cloudsync/src/cloudsync-plugins/src/cvlt/src/libcloudsynccvlt.sym
AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src   -I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src
noinst_HEADERS = archivestore.h libcvlt.h libcvlt-mem-types.h cvlt-messages.h
AM_CFLAGS = -Wall -fno-strict-aliasing $(GF_CFLAGS) -I$(top_srcdir)/xlators/features/cloudsync/src
CLEANFILES =

EXTRA_DIST = libcloudsynccvlt.sym
