xlator_PROGRAMS = meta.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/

meta_so_SOURCES = meta.c tree.c misc.c view.c
noinst_HEADERS = meta.h tree.h misc.h view.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~
