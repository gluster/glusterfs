xlator_LTLIBRARIES = quota.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

quota_la_LDFLAGS = -module -avoid-version -shared

quota_la_SOURCES = quota.c
quota_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = quota-mem-types.h quota.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS) \
	-I$(top_srcdir)/xlators/cluster/dht/src

CLEANFILES = 

