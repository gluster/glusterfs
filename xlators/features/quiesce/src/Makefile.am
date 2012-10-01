xlator_LTLIBRARIES = quiesce.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

quiesce_la_LDFLAGS = -module -avoidversion

quiesce_la_SOURCES = quiesce.c
quiesce_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = quiesce.h quiesce-mem-types.h

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES =
