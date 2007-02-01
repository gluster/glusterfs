xlator_PROGRAMS = io-threads.so
xlatordir = $(libdir)/glusterfs/xlator/performance

io_threads_so_SOURCES = io-threads.c
noinst_HEADERS = io-threads.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~
