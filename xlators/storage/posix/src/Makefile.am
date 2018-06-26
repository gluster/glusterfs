if WITH_SERVER
xlator_LTLIBRARIES = posix.la
endif
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/storage

posix_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

posix_la_SOURCES = posix.c posix-helpers.c posix-handle.c posix-aio.c \
	posix-gfid-path.c posix-entry-ops.c posix-inode-fd-ops.c \
        posix-common.c posix-metadata.c
posix_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la $(LIBAIO) \
	$(ACL_LIBS)

noinst_HEADERS = posix.h posix-mem-types.h posix-handle.h posix-aio.h \
	posix-messages.h posix-gfid-path.h posix-inode-handle.h \
	posix-metadata.h posix-metadata-disk.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-I$(top_srcdir)/rpc/rpc-lib/src

AM_CFLAGS = -fno-strict-aliasing -Wall $(GF_CFLAGS) -I$(top_srcdir)/glusterfsd/src

CLEANFILES =

