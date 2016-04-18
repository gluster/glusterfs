XDRSOURCES = glusterfs3-xdr.c cli1-xdr.c nlm4-xdr.c nsm-xdr.c \
	rpc-common-xdr.c glusterd1-xdr.c acl3-xdr.c portmap-xdr.c \
	mount3udp.c changelog-xdr.c glusterfs-fops.c
XDRHEADERS = $(XDRSOURCES:.c=.h)
XDRGENFILES = $(XDRSOURCES:.c=.x)

lib_LTLIBRARIES = libgfxdr.la

libgfxdr_la_CFLAGS = -Wall $(GF_CFLAGS) $(GF_DARWIN_LIBGLUSTERFS_CFLAGS)

libgfxdr_la_CPPFLAGS = $(GF_CPPFLAGS) -D__USE_FILE_OFFSET64 \
			-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/rpc/rpc-lib/src

libgfxdr_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

libgfxdr_la_LDFLAGS = -version-info $(LIBGFXDR_LT_VERSION)

libgfxdr_la_SOURCES = xdr-generic.c xdr-nfs3.c msg-nfs3.c
nodist_libgfxdr_la_SOURCES = $(XDRSOURCES)

libgfxdr_la_HEADERS = xdr-generic.h xdr-nfs3.h msg-nfs3.h glusterfs3.h \
	$(XDRHEADERS)

libgfxdr_ladir = $(includedir)/glusterfs/rpc

BUILT_SOURCES = $(XDRSOURCES) $(XDRHEADERS)

EXTRA_DIST = $(XDRGENFILES)

CLEANFILES = $(XDRSOURCES) $(XDRHEADERS)

xdrsrc=$(top_srcdir)/rpc/xdr/src
glusterfs-fops.c: glusterfs-fops.x glusterfs-fops.h
	$(top_srcdir)/build-aux/xdrgen source $(xdrsrc)/`basename ${@:.c=.x}`

glusterfs-fops.h: glusterfs-fops.x
	$(top_srcdir)/build-aux/xdrgen header $(xdrsrc)/`basename ${@:.h=.x}`

$(top_srcdir)/libglusterfs/src/glusterfs-fops.h: glusterfs-fops.h
	cp $(xdrsrc)/`basename $@` $(top_srcdir)/libglusterfs/src/

glusterfs3-xdr.c: glusterfs3-xdr.x glusterfs3-xdr.h
	$(top_srcdir)/build-aux/xdrgen source $(xdrsrc)/`basename ${@:.c=.x}`

glusterfs3-xdr.h: glusterfs3-xdr.x
	$(top_srcdir)/build-aux/xdrgen header $(xdrsrc)/`basename ${@:.h=.x}`

cli1-xdr.c: cli1-xdr.x cli1-xdr.h
	$(top_srcdir)/build-aux/xdrgen source $(xdrsrc)/`basename ${@:.c=.x}`

cli1-xdr.h: cli1-xdr.x
	$(top_srcdir)/build-aux/xdrgen header $(xdrsrc)/`basename ${@:.h=.x}`

nlm4-xdr.c: nlm4-xdr.x nlm4-xdr.h
	$(top_srcdir)/build-aux/xdrgen source $(xdrsrc)/`basename ${@:.c=.x}`

nlm4-xdr.h: nlm4-xdr.x
	$(top_srcdir)/build-aux/xdrgen header $(xdrsrc)/`basename ${@:.h=.x}`

nsm-xdr.c: nsm-xdr.x nsm-xdr.h
	$(top_srcdir)/build-aux/xdrgen source $(xdrsrc)/`basename ${@:.c=.x}`

nsm-xdr.h: nsm-xdr.x
	$(top_srcdir)/build-aux/xdrgen header $(xdrsrc)/`basename ${@:.h=.x}`

rpc-common-xdr.c: rpc-common-xdr.x rpc-common-xdr.h
	$(top_srcdir)/build-aux/xdrgen source $(xdrsrc)/`basename ${@:.c=.x}`

rpc-common-xdr.h: rpc-common-xdr.x
	$(top_srcdir)/build-aux/xdrgen header $(xdrsrc)/`basename ${@:.h=.x}`

glusterd1-xdr.c: glusterd1-xdr.x glusterd1-xdr.h
	$(top_srcdir)/build-aux/xdrgen source $(xdrsrc)/`basename ${@:.c=.x}`

glusterd1-xdr.h: glusterd1-xdr.x
	$(top_srcdir)/build-aux/xdrgen header $(xdrsrc)/`basename ${@:.h=.x}`

acl3-xdr.c: acl3-xdr.x acl3-xdr.h
	$(top_srcdir)/build-aux/xdrgen source $(xdrsrc)/`basename ${@:.c=.x}`

acl3-xdr.h: acl3-xdr.x
	$(top_srcdir)/build-aux/xdrgen header $(xdrsrc)/`basename ${@:.h=.x}`

portmap-xdr.c: portmap-xdr.x portmap-xdr.h
	$(top_srcdir)/build-aux/xdrgen source $(xdrsrc)/`basename ${@:.c=.x}`

portmap-xdr.h: portmap-xdr.x
	$(top_srcdir)/build-aux/xdrgen header $(xdrsrc)/`basename ${@:.h=.x}`

mount3udp.c: mount3udp.x mount3udp.h
	$(top_srcdir)/build-aux/xdrgen source $(xdrsrc)/`basename ${@:.c=.x}`

mount3udp.h: mount3udp.x
	$(top_srcdir)/build-aux/xdrgen header $(xdrsrc)/`basename ${@:.h=.x}`

changelog-xdr.c: changelog-xdr.x changelog-xdr.h
	$(top_srcdir)/build-aux/xdrgen source $(xdrsrc)/`basename ${@:.c=.x}`

changelog-xdr.h: changelog-xdr.x
	$(top_srcdir)/build-aux/xdrgen header $(xdrsrc)/`basename ${@:.h=.x}`
