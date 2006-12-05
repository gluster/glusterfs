
transport_PROGRAMS = client.so
transportdir = $(libdir)/glusterfs/transport/vapi

client_so_SOURCES = vapi-client.c $(top_srcdir)/transport/vapi/vapi.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/transport/vapi -shared -nostartfiles

CLEANFILES = *~

