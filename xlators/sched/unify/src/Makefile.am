
xlator_PROGRAMS = unify.so
xlatordir = $(libdir)/glusterfs/xlator/sched

unify_so_SOURCES = unify.c
unify_so_LDADD = ../../../../libglusterfs/src/libglusterfs.a

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

../../../../libglusterfs/src/libglusterfs.a:
	$(MAKE) -C $(dir $@) $(notdir $@)
