
xlator_PROGRAMS = bdb.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/storage

bdb_so_SOURCES = bdb.c 
noinst_HEADERS = bdb.h 

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D$(GF_HOST_OS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

AM_LDFLAGS = -ldb

CLEANFILES = *~

