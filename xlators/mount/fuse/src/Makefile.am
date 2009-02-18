
noinst_HEADERS = fuse-extra.h

xlator_LTLIBRARIES = fuse.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/mount
fuse_la_SOURCES = fuse-bridge.c fuse-extra.c
fuse_la_LDFLAGS = -module -avoidversion -shared -nostartfiles $(GF_FUSE_LDADD) 

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D$(GF_HOST_OS) -Wall \
	-I$(top_srcdir)/libglusterfs/src $(GF_CFLAGS) -DFUSE_USE_VERSION=26


CLEANFILES = 

