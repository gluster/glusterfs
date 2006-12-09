xlator_PROGRAMS = write-back.so
xlatordir = $(libdir)/glusterfs/xlator/performance

write_back_so_SOURCES = write-back.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~
