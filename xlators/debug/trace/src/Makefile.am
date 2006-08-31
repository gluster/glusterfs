
xlator_PROGRAMS = trace.so
xlatordir = $(libdir)/glusterfs/xlator/debug

trace_so_SOURCES = trace.c
trace_so_LDADD = ../../../../libglusterfs/src/libglusterfs.a

AM_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

../../../libglusterfs/src/libglusterfs.a:
	$(MAKE) -C $(dir $@) $(notdir $@)
