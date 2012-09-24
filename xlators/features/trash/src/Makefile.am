xlator_LTLIBRARIES = trash.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/testing/features

trash_la_LDFLAGS = -module -avoid-version -shared 

trash_la_SOURCES = trash.c
trash_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la 

noinst_HEADERS = trash.h trash-mem-types.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 

