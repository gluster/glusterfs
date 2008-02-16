xlator_PROGRAMS = rot-13.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/encryption

rot_13_so_SOURCES = rot-13.c
noinst_HEADERS = rot-13.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_DARWIN_BUNDLE_CFLAGS)

CLEANFILES = *~
