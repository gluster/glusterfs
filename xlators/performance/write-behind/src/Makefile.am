xlator_PROGRAMS = write-behind.so
xlatordir = $(libdir)/glusterfs/xlator/performance

write_behind_so_SOURCES = write-behind.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~
