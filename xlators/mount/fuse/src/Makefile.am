noinst_HEADERS = $(CONTRIBDIR)/fuse-include/fuse_kernel.h\
	$(CONTRIBDIR)/fuse-include/fuse-mount.h\
	$(CONTRIBDIR)/fuse-include/fuse-misc.h fuse-mem-types.h \
	fuse-bridge.h

xlator_LTLIBRARIES = fuse.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/mount

if GF_DARWIN_HOST_OS
    mount_source=$(CONTRIBDIR)/macfuse/mount_darwin.c
else
    mount_source=$(CONTRIBDIR)/fuse-lib/mount.c
endif

fuse_la_SOURCES = fuse-helpers.c fuse-resolve.c fuse-bridge.c \
	$(CONTRIBDIR)/fuse-lib/misc.c $(mount_source)

fuse_la_LDFLAGS = -module -avoidversion -shared -nostartfiles

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D$(GF_HOST_OS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -I$(CONTRIBDIR)/fuse-include \
	 $(GF_CFLAGS) $(GF_FUSE_CFLAGS)


CLEANFILES =
