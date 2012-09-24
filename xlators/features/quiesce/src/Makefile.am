xlator_LTLIBRARIES = quiesce.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

quiesce_la_LDFLAGS = -module -avoid-version -shared

quiesce_la_SOURCES = quiesce.c
quiesce_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = quiesce.h quiesce-mem-types.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES =
