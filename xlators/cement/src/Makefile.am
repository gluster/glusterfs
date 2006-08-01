
xlator_PROGRAMS = Cement.so
xlatordir = $(libdir)/glusterfs

Cement_so_SOURCES = cement.c
Cement_so_LDADD = ../../../libglusterfs/src/libglusterfs.a

AM_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

../../../libglusterfs/src/libglusterfs.a:
	$(MAKE) -C $(dir $@) $(notdir $@)
