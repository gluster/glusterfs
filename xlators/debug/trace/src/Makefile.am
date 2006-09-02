
xlator_PROGRAMS = trace.so
xlatordir = $(libdir)/glusterfs/xlator/debug

trace_so_SOURCES = trace.c
trace_so_LDADD = $(libdir)/glusterfs/libglusterfs.so

AM_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

$(libdir)/glusterfs/libglusterfs.so:
	$(MAKE) -C $(dir $@) $(notdir $@)
