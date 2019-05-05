if BUILD_GNFS
    NFS_XDRS = nlm4-xdr.x nsm-xdr.x acl3-xdr.x mount3udp.x
    NFS_SRCS = xdr-nfs3.c msg-nfs3.c
    NFS_HDRS = xdr-nfs3.h msg-nfs3.h
else
    NFS_EXTRA_XDRS = nlm4-xdr.x nsm-xdr.x acl3-xdr.x mount3udp.x
endif

XDRGENFILES = glusterfs3-xdr.x glusterfs4-xdr.x cli1-xdr.x \
	rpc-common-xdr.x glusterd1-xdr.x changelog-xdr.x \
	portmap-xdr.x ${NFS_XDRS}

XDRHEADERS = $(XDRGENFILES:.x=.h)
XDRSOURCES = $(XDRGENFILES:.x=.c)

EXTRA_DIST = $(XDRGENFILES) libgfxdr.sym ${NFS_EXTRA_XDRS}

lib_LTLIBRARIES = libgfxdr.la

libgfxdr_la_CFLAGS = -Wall $(GF_CFLAGS) $(GF_DARWIN_LIBGLUSTERFS_CFLAGS)

libgfxdr_la_CPPFLAGS = $(GF_CPPFLAGS) -D__USE_FILE_OFFSET64 \
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/rpc/rpc-lib/src \
	-I$(top_builddir)/rpc/xdr/src

libgfxdr_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

libgfxdr_la_LDFLAGS = -version-info $(LIBGFXDR_LT_VERSION) $(GF_LDFLAGS) \
		      -export-symbols $(top_srcdir)/rpc/xdr/src/libgfxdr.sym

libgfxdr_la_SOURCES = xdr-generic.c ${NFS_SRCS}
nodist_libgfxdr_la_SOURCES = $(XDRSOURCES)

libgfxdr_la_HEADERS = xdr-generic.h glusterfs3.h rpc-pragmas.h ${NFS_HDRS}
nodist_libgfxdr_la_HEADERS = $(XDRHEADERS)

libgfxdr_ladir = $(includedir)/glusterfs/rpc

CLEANFILES = $(XDRSOURCES) $(XDRHEADERS)

# Generate the .c and .h symlinks from the ../gen/*.x files
$(XDRSOURCES) $(XDRHEADERS):
	$(MAKE) -C ../gen $^
