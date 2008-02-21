sbin_PROGRAMS = glusterfs-guts

glusterfs_guts_SOURCES = glusterfs-guts.c fuse-bridge.c guts-replay.c guts-trace.c  fuse-extra.c guts-extra.c\
	guts-parse.c guts-tables.c

glusterfs_guts_LDADD = ../../libglusterfs/src/libglusterfs.la -lfuse

AM_CFLAGS = -fPIC -Wall -pthread 

AM_CPPFLAGS = -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -DFUSE_USE_VERSION=26 \
	-I$(top_srcdir)/libglusterfs/src  -DDATADIR=\"$(localstatedir)\" \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\"

CLEANFILES = *~