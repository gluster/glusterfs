noinst_HEADERS = socket.h name.h

rpctransport_LTLIBRARIES = socket.la
rpctransportdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/rpc-transport

socket_la_LDFLAGS = -module -avoidversion

socket_la_SOURCES = socket.c name.c
socket_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/xlators/protocol/rpc/rpc-lib/src/ \
	-I$(top_srcdir)/xlators/protocol/lib/src/ -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = *~
