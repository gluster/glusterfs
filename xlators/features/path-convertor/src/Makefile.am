
xlator_LTLIBRARIES = path-converter.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/testing/features

path_converter_la_LDFLAGS = -module -avoidversion 

path_converter_la_SOURCES = path.c
path_converter_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la 

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared $(GF_CFLAGS)

CLEANFILES = 

