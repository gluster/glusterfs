
xlator_PROGRAMS = Getattr.so
xlatordir = $(libdir)/glusterfs

Getattr_so_SOURCES = getattr.c
Getattr_so_LDADD = ../../../libglusterfs/src/libglusterfs.a

AM_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

../../../libglusterfs/src/libglusterfs.a:
	$(MAKE) -C $(dir $@) $(notdir $@)
