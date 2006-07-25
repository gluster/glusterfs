
xlator_LIBRARIES = libbrick.so
xlatordir = $(libdir)/glusterfs

libbrick_so_SOURCES = brick.c
#libbrick_so_LIBADD = ../../../libglusterfs/src/libglusterfs.a

AM_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src

CLEANFILES = *~

../../../libglusterfs/src/libglusterfs.a:
	$(MAKE) -C $(dir $@) $(notdir $@)
