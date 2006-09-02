sched_PROGRAMS = nufa.so
scheddir = $(libdir)/glusterfs/scheduler

nufa_so_SOURCES = nufa.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

	$(MAKE) -C $(dir $@) $(notdir $@)
