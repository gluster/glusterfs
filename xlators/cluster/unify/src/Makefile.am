
xlator_PROGRAMS = unify.so
xlatordir = $(libdir)/glusterfs/xlator/cluster

unify_so_SOURCES = unify.c
noinst_HEADERS = unify.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

