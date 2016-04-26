noinst_HEADERS_linux = $(CONTRIBDIR)/fuse-include/fuse_kernel.h\
	$(CONTRIBDIR)/fuse-include/mount_util.h\
	$(CONTRIBDIR)/fuse-lib/mount-gluster-compat.h
noinst_HEADERS_darwin = $(CONTRIBDIR)/fuse-include/fuse_kernel_macfuse.h\
	$(CONTRIBDIR)/macfuse/fuse_param.h\
	$(CONTRIBDIR)/macfuse/fuse_ioctl.h
noinst_HEADERS_common = $(CONTRIBDIR)/fuse-include/fuse-mount.h\
	$(CONTRIBDIR)/fuse-include/fuse-misc.h fuse-mem-types.h \
	fuse-bridge.h

if GF_DARWIN_HOST_OS
    noinst_HEADERS = $(noinst_HEADERS_common) $(noinst_HEADERS_darwin)
else
    noinst_HEADERS = $(noinst_HEADERS_common) $(noinst_HEADERS_linux)
endif

xlator_LTLIBRARIES = fuse.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/mount

if GF_DARWIN_HOST_OS
    mount_source=$(CONTRIBDIR)/macfuse/mount_darwin.c
else
    mount_source=$(CONTRIBDIR)/fuse-lib/mount.c $(CONTRIBDIR)/fuse-lib/mount-common.c
endif

fuse_la_SOURCES = fuse-helpers.c fuse-resolve.c fuse-bridge.c \
	$(CONTRIBDIR)/fuse-lib/misc.c $(mount_source)

fuse_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)
fuse_la_LIBADD = $(GF_LDADD) @GF_FUSE_LDADD@

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-I$(CONTRIBDIR)/fuse-include \
	-I$(CONTRIBDIR)/fuse-lib $(GF_FUSE_CFLAGS)

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
