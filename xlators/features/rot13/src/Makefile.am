xlator_PROGRAMS = rot13.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

rot13_so_SOURCES = rot13.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~
