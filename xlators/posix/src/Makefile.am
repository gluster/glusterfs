
xlator_PROGRAMS = Posix.so
xlatordir = $(libdir)/glusterfs

Posix_so_SOURCES = posix.c
Posix_so_LDADD = ../../../libglusterfs/src/libglusterfs.a

AM_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

../../../libglusterfs/src/libglusterfs.a:
	$(MAKE) -C $(dir $@) $(notdir $@)
