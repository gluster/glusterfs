bin_PROGRAMS = fusermount-glusterfs

fusermount_glusterfs_SOURCES = fusermount.c $(CONTRIBDIR)/fuse-lib/mount.c
noinst_HEADERS = mount_util.h

AM_CFLAGS = -Wall -D_FILE_OFFSET_BITS=64 -DFUSE_UTIL $(GF_CFLAGS)

install-exec-hook:
	-chown root $(DESTDIR)$(bindir)/fusermount-glusterfs
	chmod u+s $(DESTDIR)$(bindir)/fusermount-glusterfs

CLEANFILES =
