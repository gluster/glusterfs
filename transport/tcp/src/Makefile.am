transport_PROGRAMS = tcp.so
transportdir = $(libdir)/glusterfs/transport/

tcp_so_SOURCES = tcp.c
noinst_HEADERS = tcp.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~
