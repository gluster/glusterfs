transport_PROGRAMS = tcp.so
transportdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/transport/

tcp_so_SOURCES = tcp.c
noinst_HEADERS = tcp.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D$(GF_HOST_OS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/transport/tcp/ -shared -nostartfiles $(GF_DARWIN_BUNDLE_CFLAGS)

CLEANFILES = 
DISTCLEANFILES = .deps/*.P Makefile.in
