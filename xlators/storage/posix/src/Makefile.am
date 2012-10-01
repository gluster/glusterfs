
xlator_LTLIBRARIES = posix.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/storage

posix_la_LDFLAGS = -module -avoidversion

posix_la_SOURCES = posix.c posix-helpers.c posix-handle.c posix-aio.c
posix_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la $(LIBAIO)

noinst_HEADERS = posix.h posix-mem-types.h posix-handle.h posix-aio.h

AM_CFLAGS = -fPIC -fno-strict-aliasing $(GF_CPPFLAGS) \
            -Wall -I$(top_srcdir)/libglusterfs/src -shared \
            -nostartfiles -I$(top_srcdir)/rpc/xdr/src \
            -I$(top_srcdir)/rpc/rpc-lib/src $(GF_CFLAGS)

CLEANFILES =

