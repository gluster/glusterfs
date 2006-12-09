xlator_PROGRAMS = read-ahead.so
xlatordir = $(libdir)/glusterfs/xlator/performance

read_ahead_so_SOURCES = read-ahead.c page.c

noinst_HEADERS = read-ahead.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~
