
sbin_PROGRAMS = glusterfsd

glusterfsd_SOURCES = glusterfs.c glusterfs-fops.c
glusterfsd_LDADD = ../../libglusterfs/src/libglusterfs.a

AM_CFLAGS = -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=25 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/src/libglusterfs/src

CLEANFILES = *~

../../libglusterfs/src/libglusterfs.a:
	$(MAKE) -C $(dir $@) $(notdir $@)
