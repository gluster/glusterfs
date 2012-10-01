xlator_LTLIBRARIES = trash.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/testing/features

trash_la_LDFLAGS = -module -avoidversion 

trash_la_SOURCES = trash.c
trash_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la 

noinst_HEADERS = trash.h trash-mem-types.h

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 

