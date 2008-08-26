
xlator_LTLIBRARIES = path-converter.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

path_converter_la_LDFLAGS = -module -avoidversion 

path_converter_la_SOURCES = path.c
path_converter_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la 

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 

