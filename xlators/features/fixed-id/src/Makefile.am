xlator_PROGRAMS = fixed-id.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

fixed_id_so_SOURCES = fixed-id.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

