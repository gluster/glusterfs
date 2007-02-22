transport_PROGRAMS = server.so
transportdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/transport/tcp/

server_so_SOURCES = tcp-server.c $(top_srcdir)/transport/tcp/tcp.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/transport/tcp/ -shared -nostartfiles

CLEANFILES = *~
