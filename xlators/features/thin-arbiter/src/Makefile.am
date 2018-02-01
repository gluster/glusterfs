if WITH_SERVER
xlator_LTLIBRARIES = thin-arbiter.la
endif

xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

thin_arbiter_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

thin_arbiter_la_SOURCES = thin-arbiter.c \
    $(top_builddir)/xlators/lib/src/libxlator.c

thin_arbiter_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = thin-arbiter.h thin-arbiter-mem-types.h thin-arbiter-messages.h \
    $(top_builddir)/xlators/lib/src/libxlator.h

AM_CPPFLAGS = $(GF_CPPFLAGS) \
    -I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/xlators/lib/src \
    -I$(top_srcdir)/rpc/rpc-lib/src \
    -I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
