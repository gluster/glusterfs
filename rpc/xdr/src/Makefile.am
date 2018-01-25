XDRGENFILES = glusterfs3-xdr.x glusterfs4-xdr.x cli1-xdr.x nlm4-xdr.x nsm-xdr.x \
	rpc-common-xdr.x glusterd1-xdr.x acl3-xdr.x portmap-xdr.x \
	mount3udp.x changelog-xdr.x glusterfs-fops.x
XDRHEADERS = $(XDRGENFILES:.x=.h)
XDRSOURCES = $(XDRGENFILES:.x=.c)

EXTRA_DIST = $(XDRGENFILES) libgfxdr.sym

lib_LTLIBRARIES = libgfxdr.la

libgfxdr_la_CFLAGS = -Wall $(GF_CFLAGS) $(GF_DARWIN_LIBGLUSTERFS_CFLAGS)

libgfxdr_la_CPPFLAGS = $(GF_CPPFLAGS) -D__USE_FILE_OFFSET64 \
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/rpc/rpc-lib/src \
	-I$(top_builddir)/rpc/xdr/src

libgfxdr_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

libgfxdr_la_LDFLAGS = -version-info $(LIBGFXDR_LT_VERSION) $(GF_LDFLAGS) \
		      -export-symbols $(top_srcdir)/rpc/xdr/src/libgfxdr.sym

libgfxdr_la_SOURCES = xdr-generic.c xdr-nfs3.c msg-nfs3.c
nodist_libgfxdr_la_SOURCES = $(XDRSOURCES)

libgfxdr_la_HEADERS = xdr-generic.h xdr-nfs3.h msg-nfs3.h glusterfs3.h \
	rpc-pragmas.h
nodist_libgfxdr_la_HEADERS = $(XDRHEADERS)

libgfxdr_ladir = $(includedir)/glusterfs/rpc

CLEANFILES = $(XDRSOURCES) $(XDRHEADERS)

# Generate the .c and .h symlinks from the ../gen/*.x files
$(XDRSOURCES) $(XDRHEADERS):
	$(MAKE) -C ../gen $^
