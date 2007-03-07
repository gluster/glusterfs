xlator_PROGRAMS = trash.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

trash_so_SOURCES = trash.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

