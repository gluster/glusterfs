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

# trick automake into doing BUILT_SOURCES magic
BUILT_SOURCES = $(XDRHEADERS) $(XDRSOURCES)

xdrsrc=$(top_srcdir)/rpc/xdr/src
xdrdst=$(top_builddir)/rpc/xdr/src

# make's dependency resolution may mean that it decides to run
# rpcgen again (unnecessarily), but as the .c file already exists,
# rpcgen will exit with an error, resulting in a build error. We
# could use a '-' (i.e. -@rpcgen ...) and suffer with noisy warnings
# in the build. Or we do this crufty thing instead.
$(XDRSOURCES): $(XDRGENFILES)
	@if [ ! -e $(xdrdst)/$@ -o $(@:.c=.x) -nt $(xdrdst)/$@ ]; then \
		rpcgen -c -o $(xdrdst)/$@ $(@:.c=.x) ;\
	fi

# d*mn sed in netbsd6 doesn't do -i (inline)
# (why are we still running smoke on netbsd6 and not netbsd7?)
$(XDRHEADERS): $(XDRGENFILES)
	@if [ ! -e $(xdrdst)/$@ -o $(@:.h=.x) -nt $(xdrdst)/$@ ]; then \
		rpcgen -h -o $(@:.h=.tmp) $(@:.h=.x) && \
		sed -e '/#ifndef/ s/-/_/g' -e '/#define/ s/-/_/g' \
			-e '/#endif/ s/-/_/' -e 's/TMP_/H_/g' \
			$(@:.h=.tmp) > $(xdrdst)/$@ && \
		rm -f $(@:.h=.tmp) ; \
	fi


# link .x files when doing out-of-tree builds
# have to use .PHONY here to force it; all versions of make
# will think the file already exists "here" by virtue of the
# VPATH. And we have to have the .x file in $cwd in order to
# have rpcgen generate "nice" #include directives
# i.e. (nice):
# #include "acl3-xdr.h"
# versus (not nice):
# #include "../../../../foo/src/rpc/xdr/src/acl3-xdr.h"
.PHONY : $(XDRGENFILES)
$(XDRGENFILES):
	@if [ ! -e $@ ]; then ln -s $(xdrsrc)/$@ . ; fi;

clean-local:
	@if [ $(top_builddir) != $(top_srcdir) ]; then \
		rm -f $(xdrdst)/*.x; \
	fi
