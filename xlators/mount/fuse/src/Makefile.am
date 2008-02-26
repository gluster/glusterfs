
noinst_HEADERS = fuse-extra.h 

xlator_PROGRAMS = fuse.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/mount
fuse_so_SOURCES = fuse-bridge.c fuse-extra.c
fuse_so_CFLAGS = -DFUSE_USE_VERSION=26 -fPIC -Wall -pthread $(GF_DARWIN_BOOSTER_CFLAGS)
fuse_so_CPPFLAGS = -D$(GF_HOST_OS) -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE  \
	-I$(top_srcdir)/libglusterfs/src -DDATADIR=\"$(localstatedir)\" \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\"
fuse_so_LDFLAGS = -shared -nostartfiles
fuse_so_LDADD = -lfuse


CLEANFILES = *~

