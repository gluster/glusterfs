
transport_PROGRAMS = server.so
transportdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/transport/ib-sdp

server_so_SOURCES = ib-sdp-server.c $(top_srcdir)/transport/ib-sdp/ib-sdp.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/transport/ib-sdp -shared -nostartfiles $(GF_DARWIN_BUNDLE_CFLAGS)

CLEANFILES = *~

