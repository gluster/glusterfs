lib_LTLIBRARIES = libgfproto1.la

libgfproto1_la_CFLAGS = -fPIC  -Wall -g -shared -nostartfiles $(GF_CFLAGS) $(GF_DARWIN_LIBGLUSTERFS_CFLAGS)

libgfproto1_la_CPPFLAGS = -D_FILE_OFFSET_BITS=64 -D__USE_FILE_OFFSET64 -D_GNU_SOURCE  \
	-D$(GF_HOST_OS) -DLIBDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/auth\"      \
	-DTRANSPORTDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/transport\"           \
	-I$(CONTRIBDIR)/rbtree -I$(top_srcdir)/libglusterfs/src/                      \
        -I$(top_srcdir)/xlators/protocol/rpc/rpc-lib/src/

libgfproto1_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

libgfproto1_la_SOURCES = authenticate.c msg-xdr.c glusterfs-xdr.c

noinst_HEADERS = authenticate.h protocol-common.h msg-xdr.h glusterfs-xdr.h
