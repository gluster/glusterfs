transport_PROGRAMS = server.so
transportdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/transport/unix/

server_so_SOURCES = unix-server.c $(top_srcdir)/transport/unix/unix.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/transport/unix/ -shared -nostartfiles

CLEANFILES = *~
