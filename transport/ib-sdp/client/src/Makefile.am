
transport_PROGRAMS = client.so
transportdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/transport/ib-sdp

client_so_SOURCES = ib-sdp-client.c $(top_srcdir)/transport/ib-sdp/ib-sdp.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/transport/ib-sdp -shared -nostartfiles

CLEANFILES = *~

