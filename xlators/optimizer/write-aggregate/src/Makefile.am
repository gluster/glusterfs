xlator_PROGRAMS = write-aggregate.so
xlatordir = $(libdir)/glusterfs/xlator/optimizer

write_aggregate_so_SOURCES = write-aggregate.c
noinst_HEADERS = write-aggregate.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~
