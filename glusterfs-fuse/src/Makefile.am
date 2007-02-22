sbin_PROGRAMS = glusterfs

glusterfs_SOURCES = glusterfs.c fuse-bridge.c fuse-internals.c
glusterfs_LDADD = ../../libglusterfs/src/libglusterfs.la

noinst_HEADERS = glusterfs.h fuse-internals.h

AM_CFLAGS = -fPIC -Wall

AM_CPPFLAGS = -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=26 -D_GNU_SOURCE \
	-I$(top_srcdir)/libglusterfs/src -DDATADIR=\"$(localstatedir)\" \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\"

logfiledir = $(datadir)/log/glusterfs


CLEANFILES = *~

install-data-local:
	mkdir -p $(localstatedir)/log/glusterfs/
