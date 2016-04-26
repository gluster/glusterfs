libgfchangelog_la_CFLAGS = -Wall $(GF_CFLAGS) $(GF_DARWIN_LIBGLUSTERFS_CFLAGS) \
	-DDATADIR=\"$(localstatedir)\"

libgfchangelog_la_CPPFLAGS = $(GF_CPPFLAGS) -D__USE_FILE_OFFSET64 -fpic \
	-I../../../src/ -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/xlators/features/changelog/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-I$(top_srcdir)/rpc/rpc-lib/src \
	-I$(top_srcdir)/rpc/rpc-transport/socket/src \
	-DDATADIR=\"$(localstatedir)\"

libgfchangelog_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/rpc/xdr/src/libgfxdr.la \
	$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la

libgfchangelog_la_LDFLAGS = $(GF_LDFLAGS) -version-info \
	$(LIBGFCHANGELOG_LT_VERSION)

libgfchangelogdir = $(includedir)/glusterfs/gfchangelog
lib_LTLIBRARIES = libgfchangelog.la

CONTRIB_BUILDDIR = $(top_builddir)/contrib

libgfchangelog_la_SOURCES = gf-changelog.c gf-changelog-journal-handler.c \
	gf-changelog-helpers.c gf-changelog-api.c gf-history-changelog.c \
	gf-changelog-rpc.c gf-changelog-reborp.c \
	$(top_srcdir)/xlators/features/changelog/src/changelog-rpc-common.c

noinst_HEADERS = gf-changelog-helpers.h gf-changelog-rpc.h \
	gf-changelog-journal.h changelog-lib-messages.h

CLEANFILES =

$(top_builddir)/libglusterfs/src/libglusterfs.la:
	$(MAKE) -C $(top_builddir)/libglusterfs/src/ all
