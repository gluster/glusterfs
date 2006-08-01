
xlator_PROGRAMS = Sample.so
xlatordir = $(libdir)/glusterfs

Sample_so_SOURCES = sample.c
Sample_so_LDADD = ../../../libglusterfs/src/libglusterfs.a

AM_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

../../../libglusterfs/src/libglusterfs.a:
	$(MAKE) -C $(dir $@) $(notdir $@)
