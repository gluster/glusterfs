
xlator_PROGRAMS = posix.so
xlatordir = $(libdir)/glusterfs/xlator/storage

posix_so_SOURCES = posix.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

