bin_PROGRAMS = fusermount-glusterfs

fusermount_glusterfs_SOURCES = fusermount.c mount_util.c $(CONTRIBDIR)/fuse-lib/mount-common.c
noinst_HEADERS = $(CONTRIBDIR)/fuse-include/mount_util.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -DFUSE_UTIL -I$(CONTRIBDIR)/fuse-include -I$(CONTRIBDIR)/fuse-lib

AM_CFLAGS = -Wall $(GF_CFLAGS)

install-exec-hook:
	-chown root $(DESTDIR)$(bindir)/fusermount-glusterfs
	chmod u+s $(DESTDIR)$(bindir)/fusermount-glusterfs

CLEANFILES =
