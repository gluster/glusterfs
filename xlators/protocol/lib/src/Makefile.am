libgfproto_la_CFLAGS = -fPIC  -Wall -g -shared -nostartfiles $(GF_CFLAGS) $(GF_DARWIN_LIBGLUSTERFS_CFLAGS)

libgfproto_la_CPPFLAGS = -D_FILE_OFFSET_BITS=64 -D__USE_FILE_OFFSET64 -D_GNU_SOURCE  \
	-D$(GF_HOST_OS) -DLIBDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/auth\"     \
	-DTRANSPORTDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/transport\"          \
	-I$(CONTRIBDIR)/rbtree -I$(top_srcdir)/libglusterfs/src/

libgfproto_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

lib_LTLIBRARIES = libgfproto.la 


libgfproto_la_SOURCES = transport.c authenticate.c

noinst_HEADERS =  transport.h  protocol.h authenticate.h
