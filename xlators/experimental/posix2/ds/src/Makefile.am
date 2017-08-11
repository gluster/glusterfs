xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/experimental
xlator_LTLIBRARIES = posix2-ds.la

POSIX_SRC = $(top_srcdir)/xlators/storage/posix/src
POSIX_BLD = $(top_builddir)/xlators/storage/posix/src

noinst_HEADERS = $(POSIX_BLD)/posix.h

posix2_ds_sources = posix2-ds-main.c

posix2_ds_la_SOURCES = $(posix2_ds_sources)
posix2_ds_la_LDFLAGS = -module -avoid-version
posix2_ds_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la
posix2_ds_la_LIBADD += $(top_builddir)/xlators/experimental/posix2/common/src/libposix2common.la

AM_CFLAGS = -Wall $(GF_CFLAGS) -I$(POSIX_SRC)

AM_CPPFLAGS = $(GF_CPPFLAGS)
AM_CPPFLAGS += -I$(top_srcdir)/xlators/experimental/posix2/common/src
AM_CPPFLAGS += -I$(top_builddir)/xlators/experimental/posix2/common/src
AM_CPPFLAGS += -I$(top_srcdir)/libglusterfs/src
AM_CPPFLAGS += -I$(top_srcdir)/xlators/lib/src
AM_CPPFLAGS += -I$(top_srcdir)/rpc/xdr/src
AM_CPPFLAGS += -I$(top_builddir)/rpc/xdr/src

CLEANFILES =
