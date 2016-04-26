xlator_LTLIBRARIES = changelog.la

xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

noinst_HEADERS = changelog-helpers.h changelog-mem-types.h changelog-rt.h \
	changelog-rpc-common.h changelog-misc.h changelog-encoders.h \
	changelog-rpc-common.h changelog-rpc.h changelog-ev-handle.h \
	changelog-messages.h

changelog_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

changelog_la_SOURCES = changelog.c changelog-rt.c changelog-helpers.c \
	changelog-encoders.c changelog-rpc.c changelog-barrier.c \
	changelog-rpc-common.c changelog-ev-handle.c
changelog_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/rpc/xdr/src/libgfxdr.la \
	$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-I$(top_srcdir)/rpc/rpc-lib/src \
	-I$(top_srcdir)/rpc/rpc-transport/socket/src \
	-I$(top_srcdir)/xlators/features/changelog/lib/src/ \
	-fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D$(GF_HOST_OS) \
	-DDATADIR=\"$(localstatedir)\"

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
