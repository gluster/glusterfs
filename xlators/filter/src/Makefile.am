xlator_PROGRAMS = Filter.so
xlatordir = $(libdir)/glusterfs/xlator

Filter_so_SOURCES = filter.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

