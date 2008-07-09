sbin_PROGRAMS = glusterfs-guts

glusterfs_guts_SOURCES = glusterfs-guts.c fuse-bridge.c guts-replay.c guts-trace.c  \
	fuse-extra.c guts-extra.c guts-parse.c guts-tables.c

noinst_HEADERS = fuse_kernel.h fuse-extra.h glusterfs-guts.h glusterfs-fuse.h guts-lowlevel.h \
	guts-parse.h guts-replay.h guts-tables.h guts-trace.h

glusterfs_guts_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la -lfuse

AM_CFLAGS = -fPIC -Wall -pthread 

AM_CPPFLAGS = -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -DFUSE_USE_VERSION=26 \
	-I$(top_srcdir)/libglusterfs/src  -DDATADIR=\"$(localstatedir)\" \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\"

CLEANFILES = 
DISTCLEANFILES = .deps/*.P Makefile.in
