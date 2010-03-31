xlator_LTLIBRARIES = access-control.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features
access_control_la_LDFLAGS = -module -avoidversion
access_control_la_SOURCES = access-control.c
access_control_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = access-control.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)\
	-L$(xlatordir)/

CLEANFILES =
