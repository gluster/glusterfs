sched_PROGRAMS = random.so
scheddir = $(libdir)/glusterfs/scheduler

random_so_SOURCES = random.c
noinst_HEADERS = random.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~
