XDRSOURCES = glusterfs3-xdr.c cli1-xdr.c nlm4-xdr.c nsm-xdr.c \
	rpc-common-xdr.c glusterd1-xdr.c acl3-xdr.c portmap-xdr.c \
	mount3udp.c changelog-xdr.c glusterfs-fops.c
XDRHEADERS = $(XDRSOURCES:.c=.h)
XDRGENFILES = $(XDRSOURCES:.c=.x)

lib_LTLIBRARIES = libgfxdr.la

libgfxdr_la_CFLAGS = -Wall $(GF_CFLAGS) $(GF_DARWIN_LIBGLUSTERFS_CFLAGS)

libgfxdr_la_CPPFLAGS = $(GF_CPPFLAGS) -D__USE_FILE_OFFSET64 \
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/rpc/rpc-lib/src \
	-I$(top_builddir)/rpc/xdr/src

# libgfxdr_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

libgfxdr_la_LDFLAGS = -version-info $(LIBGFXDR_LT_VERSION)

libgfxdr_la_SOURCES = xdr-generic.c xdr-nfs3.c msg-nfs3.c
nodist_libgfxdr_la_SOURCES = $(XDRSOURCES)

libgfxdr_la_HEADERS = xdr-generic.h xdr-nfs3.h msg-nfs3.h glusterfs3.h \
	rpc-pragmas.h $(XDRHEADERS)

libgfxdr_ladir = $(includedir)/glusterfs/rpc

# trick automake into doing BUILT_SOURCES magic
BUILT_SOURCES = $(XDRHEADERS)

EXTRA_DIST = $(XDRGENFILES)

CLEANFILES = $(XDRSOURCES) $(XDRHEADERS)

xdrsrc=$(top_srcdir)/rpc/xdr/src

# make's dependency resolution may mean that it decides to run
# rpcgen again (unnecessarily), but as the .c file already exists,
# rpcgen will exit with an error, resulting in a build error. We
# could use a '-' (i.e. -@rpcgen ...) and suffer with noisy warnings
# in the build. Or we do this crufty thing instead.
$(XDRSOURCES): $(XDRHEADERS)
	@if [ ! -e $@ -o $(@:.c=.x) -nt $@ ]; then \
		rpcgen -c -o $(@:.c=.tmp) $(@:.c=.x) && mv $(@:.c=.tmp) $@ ; \
	fi

# d*mn sed in netbsd6 doesn't do -i (inline)
# (why are we still running smoke on netbsd6 and not netbsd7?)
$(XDRHEADERS): $(XDRGENFILES)
	@if [ ! -e $@ -o $(@:.h=.x) -nt $@ ]; then \
		rpcgen -h -o $(@:.h=.tmp) $(@:.h=.x) ; \
		sed -e '/#ifndef/ s/-/_/g' -e '/#define/ s/-/_/g' -e '/#endif/ s/-/_/' \
			$(@:.h=.tmp) > $@ && rm -f $(@:.h=.tmp) ; \
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

