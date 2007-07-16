sbin_PROGRAMS = glusterfs

glusterfs_SOURCES = glusterfs.c fuse-bridge.c fetch-spec.c fuse-extra.c

glusterfs_LDADD = ../../libglusterfs/src/libglusterfs.la @FUSE_CLIENT_LIBS@

noinst_HEADERS = glusterfs-fuse.h fuse-extra.h

AM_CFLAGS = -fPIC -Wall -pthread

AM_CPPFLAGS = -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=26 -D_GNU_SOURCE \
	-I$(top_srcdir)/libglusterfs/src -DDATADIR=\"$(localstatedir)\" \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\"

CLEANFILES = *~

install-data-local:
	$(INSTALL) -d -m 755 $(DESTDIR)$(localstatedir)/log/glusterfs/
