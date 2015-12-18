
xlator_LTLIBRARIES = path-converter.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/testing/features

path_converter_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

path_converter_la_SOURCES = path.c
path_converter_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la 

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = 

