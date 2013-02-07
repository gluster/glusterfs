xlator_LTLIBRARIES = quota.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

quota_la_LDFLAGS = -module -avoid-version

quota_la_SOURCES = quota.c
quota_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = quota-mem-types.h quota.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/xlators/cluster/dht/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = 

