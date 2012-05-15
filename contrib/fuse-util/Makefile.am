bin_PROGRAMS = fusermount-glusterfs

fusermount_glusterfs_SOURCES = fusermount.c mount_util.c $(CONTRIBDIR)/fuse-lib/mount-common.c
noinst_HEADERS = $(CONTRIBDIR)/fuse-include/mount_util.h

AM_CFLAGS = -Wall -D_FILE_OFFSET_BITS=64 -DFUSE_UTIL $(GF_CFLAGS) -D_GNU_SOURCE -I$(CONTRIBDIR)/fuse-include

install-exec-hook:
	-chown root $(DESTDIR)$(bindir)/fusermount-glusterfs
	chmod u+s $(DESTDIR)$(bindir)/fusermount-glusterfs

CLEANFILES =
