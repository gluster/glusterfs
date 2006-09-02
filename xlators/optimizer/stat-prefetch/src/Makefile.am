
xlator_PROGRAMS = stat.so
xlatordir = $(libdir)/glusterfs/xlator/optimizer

stat_so_SOURCES = stat-prefetch.c
stat_so_LDADD = $(libdir)/glusterfs/libglusterfs.so

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

$(libdir)/glusterfs/libglusterfs.so:
	$(MAKE) -C $(dir $@) $(notdir $@)
