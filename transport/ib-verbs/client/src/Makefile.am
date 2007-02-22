
transport_PROGRAMS = client.so
transportdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/transport/ib-verbs

client_so_SOURCES = ib-verbs-client.c $(top_srcdir)/transport/ib-verbs/ib-verbs.c
client_so_LDADD = -libverbs

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/transport/ib-verbs -shared -nostartfiles

CLEANFILES = *~

