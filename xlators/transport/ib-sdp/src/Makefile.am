
xlator_PROGRAMS = ib-sdp.so
xlatordir = $(libdir)/glusterfs/xlator/transport

ib_sdp_so_SOURCES = ib-sdp.c
noinst_HEADERS = transport-socket.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

