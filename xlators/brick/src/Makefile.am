
xlator_PROGRAMS = Brick.so
xlatordir = $(libdir)/glusterfs

Brick_so_SOURCES = brick.c
Brick_so_LDADD = ../../../libglusterfs/src/libglusterfs.a

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

../../../libglusterfs/src/libglusterfs.a:
	$(MAKE) -C $(dir $@) $(notdir $@)
