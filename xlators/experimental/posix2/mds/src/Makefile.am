xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/experimental
xlator_LTLIBRARIES = posix2-mds.la

POSIX_SRC = $(top_srcdir)/xlators/storage/posix/src
POSIX_BLD = $(top_builddir)/xlators/storage/posix/src

noinst_HEADERS = $(POSIX_BLD)/posix.h posix2-entry-ops.h

posix2_mds_sources = posix2-mds-main.c posix2-entry-ops.c

posix2_mds_la_SOURCES = $(posix2_mds_sources)
posix2_mds_la_LDFLAGS = -module -avoid-version
posix2_mds_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la
posix2_mds_la_LIBADD += $(top_builddir)/xlators/experimental/posix2/common/src/libposix2common.la

AM_CFLAGS = -Wall $(GF_CFLAGS) -I$(POSIX_SRC)

AM_CPPFLAGS = $(GF_CPPFLAGS)
AM_CPPFLAGS += -I$(top_srcdir)/xlators/experimental/posix2/common/src
AM_CPPFLAGS += -I$(top_builddir)/xlators/experimental/posix2/common/src
AM_CPPFLAGS += -I$(top_srcdir)/libglusterfs/src
AM_CPPFLAGS += -I$(top_srcdir)/xlators/lib/src
AM_CPPFLAGS += -I$(top_srcdir)/rpc/xdr/src
AM_CPPFLAGS += -I$(top_builddir)/rpc/xdr/src

CLEANFILES =
