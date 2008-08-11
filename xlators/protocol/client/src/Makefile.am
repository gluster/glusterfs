
xlator_PROGRAMS = client.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/protocol

client_so_SOURCES = client-protocol.c
client_so_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = client-protocol.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 

