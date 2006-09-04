
xlator_PROGRAMS = dummy.so
xlatordir = $(libdir)/glusterfs/xlator

dummy_so_SOURCES = dummy.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

