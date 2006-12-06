xlator_PROGRAMS = io-cache.so
xlatordir = $(libdir)/glusterfs/xlator/optimizer

io_cache_so_SOURCES = io-cache.c page.c

noinst_HEADERS = io-cache.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~
