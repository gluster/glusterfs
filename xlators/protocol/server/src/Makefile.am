
xlator_PROGRAMS = server.so
xlatordir = $(libdir)/glusterfs/protocol/server

server_so_SOURCES = proto-srv.c ns.c lock.c
noinst_HEADERS = lock.h ns.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

