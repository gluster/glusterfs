
transport_PROGRAMS = client.so
transportdir = $(libdir)/glusterfs/transport/ib-verbs

client_so_SOURCES = ib-verbs-client.c $(top_srcdir)/transport/ib-verbs/ib-verbs.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/transport/ib-verbs -shared -nostartfiles

CLEANFILES = *~

