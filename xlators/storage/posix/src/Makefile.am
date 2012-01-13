
xlator_LTLIBRARIES = posix.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/storage

posix_la_LDFLAGS = -module -avoidversion

posix_la_SOURCES = posix.c posix-helpers.c posix-handle.c
posix_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = posix.h posix-mem-types.h posix-handle.h

AM_CFLAGS = -fPIC -fno-strict-aliasing -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE \
            -D$(GF_HOST_OS) -Wall -I$(top_srcdir)/libglusterfs/src -shared \
            -nostartfiles -I$(top_srcdir)/contrib/md5 -I$(top_srcdir)/rpc/xdr/src \
            -I$(top_srcdir)/rpc/rpc-lib/src $(GF_CFLAGS)

CLEANFILES =

