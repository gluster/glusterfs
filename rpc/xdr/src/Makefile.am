lib_LTLIBRARIES = libgfxdr.la

libgfxdr_la_CFLAGS = -fPIC  -Wall -g -shared -nostartfiles $(GF_CFLAGS) $(GF_DARWIN_LIBGLUSTERFS_CFLAGS)

libgfxdr_la_CPPFLAGS = -D_FILE_OFFSET_BITS=64 -D__USE_FILE_OFFSET64 \
			-D_GNU_SOURCE -D$(GF_HOST_OS) \
			-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/rpc/rpc-lib/src

libgfxdr_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
		$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la

libgfxdr_la_SOURCES =  xdr-generic.c \
			glusterfs3-xdr.c \
			cli1-xdr.c \
			glusterd1-xdr.c \
			portmap-xdr.c \
			xdr-nfs3.c msg-nfs3.c

noinst_HEADERS = xdr-generic.h \
		glusterfs3-xdr.h glusterfs3.h \
		cli1-xdr.h \
		glusterd1-xdr.h \
		portmap-xdr.h \
		xdr-nfs3.h msg-nfs3.h
