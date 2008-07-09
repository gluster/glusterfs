
xlator_PROGRAMS = path-converter.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

path_converter_so_SOURCES = path.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles \
	$(GF_DARWIN_BUNDLE_CFLAGS)

CLEANFILES = 
DISTCLEANFILES = .deps/*.P Makefile.in

