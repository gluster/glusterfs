sched_PROGRAMS = rr.so
scheddir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/scheduler

rr_so_SOURCES = rr.c
noinst_HEADERS = rr.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~
