
sbin_PROGRAMS = glusterfsd

glusterfsd_SOURCES = glusterfsd.c glusterfsd-fops.c
glusterfsd_LDADD = ../../libglusterfs/src/libglusterfs.a -ldl

AM_CFLAGS = -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=25 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src

CLEANFILES = *~

../../libglusterfs/src/libglusterfs.a:
	$(MAKE) -C $(dir $@) $(notdir $@)
