xlator_PROGRAMS = aio.so
xlatordir = $(libdir)/glusterfs/xlator/performance

aio_so_SOURCES = aio.c
noinst_HEADERS = aio.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~
