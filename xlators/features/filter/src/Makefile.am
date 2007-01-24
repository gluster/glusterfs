xlator_PROGRAMS = filter.so
xlatordir = $(libdir)/glusterfs/xlator/features

filter_so_SOURCES = filter.c
noinst_HEADERS = filter.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

