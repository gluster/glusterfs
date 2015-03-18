xlator_LTLIBRARIES = changetimerecorder.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

changetimerecorder_la_LDFLAGS = -module -avoid-version

changetimerecorder_la_SOURCES = changetimerecorder.c ctr-helper.c

changetimerecorder_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la\
			$(top_builddir)/libglusterfs/src/gfdb/libgfdb.la\
			$(top_builddir)/api/src/libgfapi.la\
			$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la\
			-lsqlite3

noinst_HEADERS = changetimerecorder.h ctr_mem_types.h ctr-helper.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
		-I$(top_srcdir)/libglusterfs/src/gfdb \
		-I$(top_srcdir)/api/src \
		-I$(top_srcdir)/rpc/rpc-lib/src \
		-I$(top_srcdir)/rpc/xdr/src \
		-DDATADIR=\"$(localstatedir)\"

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
