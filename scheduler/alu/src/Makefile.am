sched_PROGRAMS = alu.so
scheddir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/scheduler

alu_so_SOURCES = alu.c
noinst_HEADERS = alu.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_DARWIN_BUNDLE_CFLAGS)

CLEANFILES = *~
