if WITH_SERVER
xlator_LTLIBRARIES = simple-quota.la
endif

xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

simple_quota_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

simple_quota_la_SOURCES = simple-quota.c
simple_quota_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = simple-quota.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
