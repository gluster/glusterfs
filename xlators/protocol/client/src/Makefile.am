
xlator_PROGRAMS = client.so
xlatordir = $(libdir)/glusterfs/xlator/protocol

client_so_SOURCES = client-protocol.c
noinst_HEADERS = client-protocol.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

