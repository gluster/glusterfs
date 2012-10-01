xlator_LTLIBRARIES = quota.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

quota_la_LDFLAGS = -module -avoidversion

quota_la_SOURCES = quota.c
quota_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = quota-mem-types.h quota.h

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS) \
	-I$(top_srcdir)/xlators/cluster/dht/src

CLEANFILES = 

