lib_LTLIBRARIES = libgfxdr.la

libgfxdr_la_CFLAGS = -Wall $(GF_CFLAGS) $(GF_DARWIN_LIBGLUSTERFS_CFLAGS)

libgfxdr_la_CPPFLAGS = $(GF_CPPFLAGS) -D__USE_FILE_OFFSET64 \
			-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/rpc/rpc-lib/src

libgfxdr_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

libgfxdr_la_LDFLAGS = -version-info $(LIBGFXDR_LT_VERSION)

libgfxdr_la_SOURCES =  $(XDRSOURCES) xdr-generic.c xdr-nfs3.c msg-nfs3.c

noinst_HEADERS = $(XDRHEADERS) xdr-generic.h xdr-nfs3.h msg-nfs3.h glusterfs3.h

XDRSOURCES = glusterfs3-xdr.c cli1-xdr.c nlm4-xdr.c nsm-xdr.c \
	rpc-common-xdr.c glusterd1-xdr.c acl3-xdr.c portmap-xdr.c
XDRHEADERS = glusterfs3-xdr.h cli1-xdr.h nlm4-xdr.h nsm-xdr.h \
	rpc-common-xdr.h glusterd1-xdr.h acl3-xdr.h portmap-xdr.h

%.h:%.x
	$(top_srcdir)/build-aux/xdrgen $(top_srcdir)/rpc/xdr/src/$*.x
%.c:%.x
	$(top_srcdir)/build-aux/xdrgen $(top_srcdir)/rpc/xdr/src/$*.x

CLEANFILES = $(XDRSOURCES) $(XDRHEADERS)
