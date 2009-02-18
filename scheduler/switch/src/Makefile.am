sched_LTLIBRARIES = switch.la
scheddir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/scheduler

switch_la_LDFLAGS = -module -avoidversion

switch_la_SOURCES = switch.c
switch_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 
