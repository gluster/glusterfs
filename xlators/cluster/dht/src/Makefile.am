
xlator_LTLIBRARIES = dht.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/cluster

dht_la_LDFLAGS = -module -avoidversion

dht_la_SOURCES = dht.c dht-layout.c dht-helper.c dht-hashfn.c dht-linkfile.c \
		dht-selfheal.c
dht_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = dht.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 

