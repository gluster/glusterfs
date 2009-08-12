noinst_HEADERS = $(CONTRIBDIR)/fuse-include/fuse_kernel.h

xlator_LTLIBRARIES = fuse.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/mount
fuse_la_SOURCES = fuse-bridge.c $(CONTRIBDIR)/fuse-lib/misc.c \
	$(CONTRIBDIR)/fuse-lib/mount.c
fuse_la_LDFLAGS = -module -avoidversion -shared -nostartfiles

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D$(GF_HOST_OS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -I$(CONTRIBDIR)/fuse-include \
	$(GF_CFLAGS) -DFUSERMOUNT_DIR=\"$(bindir)\"


CLEANFILES = 

