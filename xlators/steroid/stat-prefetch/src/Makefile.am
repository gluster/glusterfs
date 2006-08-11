
xlator_PROGRAMS = stat-prefetch.so
xlatordir = $(libdir)/glusterfs/steroid

stat_prefetch_so_SOURCES = stat-prefetch.c
stat_prefetch_so_LDADD = ../../../../libglusterfs/src/libglusterfs.a

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

../../../../libglusterfs/src/libglusterfs.a:
	$(MAKE) -C $(dir $@) $(notdir $@)
