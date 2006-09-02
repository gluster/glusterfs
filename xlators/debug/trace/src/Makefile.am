
xlator_PROGRAMS = trace.so
xlatordir = $(libdir)/glusterfs/xlator/debug

trace_so_SOURCES = trace.c

AM_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

