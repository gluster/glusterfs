
xlator_LTLIBRARIES = bdb.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/storage

bdb_la_LDFLAGS = -module -avoidversion

bdb_la_SOURCES = bctx.c bdb-ll.c bdb.c
bdb_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la 

noinst_HEADERS = bdb.h 

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D__USE_FILE_OFFSET64 -D_GNU_SOURCE -D$(GF_HOST_OS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

AM_LDFLAGS = -ldb

CLEANFILES = 

