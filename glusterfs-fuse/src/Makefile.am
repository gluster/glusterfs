
sbin_PROGRAMS = glusterfs

glusterfs_SOURCES = glusterfs.c glusterfs-fops.c
glusterfs_LDADD = -lfuse -lpthread ../../libglusterfs/src/libglusterfs.a -ldl

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=25 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -DDATADIR=\"$(localstatedir)\"

logfiledir = $(datadir)/log/glusterfs


CLEANFILES = *~

install-data-local:
	mkdir -p $(localstatedir)/log/glusterfs/

../../libglusterfs/src/libglusterfs.a:
	$(MAKE) -C $(dir $@) $(notdir $@)
