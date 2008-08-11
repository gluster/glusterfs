
xlator_PROGRAMS = path-converter.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

path_converter_so_SOURCES = path.c
path_converter_so_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la 

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 

