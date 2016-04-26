xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

# changetimerecorder can only get build when libgfdb is enabled
if BUILD_GFDB
   xlator_LTLIBRARIES = changetimerecorder.la
endif

changetimerecorder_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

changetimerecorder_la_SOURCES = changetimerecorder.c \
	ctr-helper.c ctr-xlator-ctx.c

changetimerecorder_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la\
	$(top_builddir)/libglusterfs/src/gfdb/libgfdb.la

noinst_HEADERS = ctr-messages.h changetimerecorder.h ctr_mem_types.h \
		ctr-helper.h ctr-xlator-ctx.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/libglusterfs/src/gfdb \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-DDATADIR=\"$(localstatedir)\"

AM_CFLAGS = -Wall $(GF_CFLAGS) $(SQLITE_CFLAGS)

CLEANFILES =
