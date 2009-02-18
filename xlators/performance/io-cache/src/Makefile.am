xlator_LTLIBRARIES = io-cache.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

io_cache_la_LDFLAGS = -module -avoidversion 

io_cache_la_SOURCES = io-cache.c page.c ioc-inode.c
io_cache_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = io-cache.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 
