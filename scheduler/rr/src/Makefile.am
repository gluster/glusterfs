sched_LTLIBRARIES = rr.la
scheddir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/scheduler

rr_la_LDFLAGS = -module -avoidversion

rr_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la
rr_la_SOURCES = rr.c rr-options.c
noinst_HEADERS = rr.h rr-options.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 
