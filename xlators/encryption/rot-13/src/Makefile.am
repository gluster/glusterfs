xlator_PROGRAMS = rot-13.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/encryption

rot_13_so_SOURCES = rot-13.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~
