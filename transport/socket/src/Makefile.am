noinst_HEADERS = socket.h name.h

transport_PROGRAMS = socket.so
transportdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/transport/

socket_so_SOURCES = socket.c name.c
socket_so_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la 

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = *~
