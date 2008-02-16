sched_PROGRAMS = switch.so
scheddir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/scheduler

switch_so_SOURCES = switch.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_DARWIN_BUNDLE_CFLAGS)

CLEANFILES = *~
