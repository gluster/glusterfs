sbin_PROGRAMS = glusterfs

glusterfs_SOURCES = glusterfs.c fuse-bridge.c fuse-internals.c
glusterfs_LDADD = -lfuse -lpthread -L../../libglusterfs/src -lglusterfs -ldl -lm #-libverbs

noinst_HEADERS = glusterfs.h fuse-internals.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=26 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -DDATADIR=\"$(localstatedir)\"

logfiledir = $(datadir)/log/glusterfs


CLEANFILES = *~

install-data-local:
	mkdir -p $(localstatedir)/log/glusterfs/
