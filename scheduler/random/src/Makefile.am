sched_LTLIBRARIES = random.la
scheddir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/scheduler

random_la_LDFLAGS = -module -avoidversion

random_la_SOURCES = random.c
random_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = random.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 
