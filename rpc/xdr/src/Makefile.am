XDRSOURCES = glusterfs3-xdr.c cli1-xdr.c nlm4-xdr.c nsm-xdr.c \
	rpc-common-xdr.c glusterd1-xdr.c acl3-xdr.c portmap-xdr.c mount3udp.c
XDRHEADERS = $(XDRSOURCES:.c=.h)
XDRGENFILES = $(XDRSOURCES:.c=.x)

lib_LTLIBRARIES = libgfxdr.la

libgfxdr_la_CFLAGS = -Wall $(GF_CFLAGS) $(GF_DARWIN_LIBGLUSTERFS_CFLAGS)

libgfxdr_la_CPPFLAGS = $(GF_CPPFLAGS) -D__USE_FILE_OFFSET64 \
			-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/rpc/rpc-lib/src

libgfxdr_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

libgfxdr_la_LDFLAGS = -version-info $(LIBGFXDR_LT_VERSION)

libgfxdr_la_SOURCES =  $(XDRSOURCES) xdr-generic.c xdr-nfs3.c msg-nfs3.c

noinst_HEADERS = $(XDRHEADERS) xdr-generic.h xdr-nfs3.h msg-nfs3.h glusterfs3.h

CLEANFILES = $(XDRSOURCES) $(XDRHEADERS)

EXTRA_DIST = $(XDRGENFILES)

glusterfs3-xdr.c: glusterfs3-xdr.x glusterfs3-xdr.h
	@if test -f $(top_srcdir)/rpc/xdr/src/${@:.c=.x}; then \
		$(top_srcdir)/build-aux/xdrgen source $(top_srcdir)/rpc/xdr/src/${@:.c=.x} ; \
	fi

glusterfs3-xdr.h: glusterfs3-xdr.x
	@if test -f $(top_srcdir)/rpc/xdr/src/${@:.h=.x} ; then \
		$(top_srcdir)/build-aux/xdrgen header $(top_srcdir)/rpc/xdr/src/${@:.h=.x} ; \
	fi

cli1-xdr.c: cli1-xdr.x cli1-xdr.h
	@if test -f $(top_srcdir)/rpc/xdr/src/${@:.c=.x} ; then \
		$(top_srcdir)/build-aux/xdrgen source $(top_srcdir)/rpc/xdr/src/${@:.c=.x} ; \
	fi

cli1-xdr.h: cli1-xdr.x
	@if test -f $(top_srcdir)/rpc/xdr/src/${@:.h=.x} ; then \
		$(top_srcdir)/build-aux/xdrgen header $(top_srcdir)/rpc/xdr/src/${@:.h=.x} ; \
	fi

nlm4-xdr.c: nlm4-xdr.x nlm4-xdr.h
	@if test -f $(top_srcdir)/rpc/xdr/src/${@:.c=.x} ; then \
		$(top_srcdir)/build-aux/xdrgen source $(top_srcdir)/rpc/xdr/src/${@:.c=.x} ; \
	fi

nlm4-xdr.h: nlm4-xdr.x
	@if test -f $(top_srcdir)/rpc/xdr/src/${@:.h=.x} ; then \
		$(top_srcdir)/build-aux/xdrgen header $(top_srcdir)/rpc/xdr/src/${@:.h=.x} ; \
	fi

nsm-xdr.c: nsm-xdr.x nsm-xdr.h
	@if test -f $(top_srcdir)/rpc/xdr/src/${@:.c=.x} ; then \
		$(top_srcdir)/build-aux/xdrgen source $(top_srcdir)/rpc/xdr/src/${@:.c=.x} ; \
	fi

nsm-xdr.h: nsm-xdr.x
	@if test -f $(top_srcdir)/rpc/xdr/src/${@:.h=.x} ; then \
		$(top_srcdir)/build-aux/xdrgen header $(top_srcdir)/rpc/xdr/src/${@:.h=.x} ; \
	fi

rpc-common-xdr.c: rpc-common-xdr.x rpc-common-xdr.h
	@if test -f $(top_srcdir)/rpc/xdr/src/${@:.c=.x} ; then \
		$(top_srcdir)/build-aux/xdrgen source $(top_srcdir)/rpc/xdr/src/${@:.c=.x} ; \
	fi

rpc-common-xdr.h: rpc-common-xdr.x
	@if test -f $(top_srcdir)/rpc/xdr/src/${@:.h=.x} ; then \
		$(top_srcdir)/build-aux/xdrgen header $(top_srcdir)/rpc/xdr/src/${@:.h=.x} ; \
	fi

glusterd1-xdr.c: glusterd1-xdr.x glusterd1-xdr.h
	@if test -f $(top_srcdir)/rpc/xdr/src/${@:.c=.x} ; then \
		$(top_srcdir)/build-aux/xdrgen source $(top_srcdir)/rpc/xdr/src/${@:.c=.x} ; \
	fi

glusterd1-xdr.h: glusterd1-xdr.x
	@if test -f $(top_srcdir)/rpc/xdr/src/${@:.h=.x} ; then \
		$(top_srcdir)/build-aux/xdrgen header $(top_srcdir)/rpc/xdr/src/${@:.h=.x} ; \
	fi

acl3-xdr.c: acl3-xdr.x acl3-xdr.h
	@if test -f $(top_srcdir)/rpc/xdr/src/${@:.c=.x} ; then \
		$(top_srcdir)/build-aux/xdrgen source $(top_srcdir)/rpc/xdr/src/${@:.c=.x} ; \
	fi

acl3-xdr.h: acl3-xdr.x
	@if test -f $(top_srcdir)/rpc/xdr/src/${@:.h=.x} ; then \
		$(top_srcdir)/build-aux/xdrgen header $(top_srcdir)/rpc/xdr/src/${@:.h=.x} ; \
	fi

portmap-xdr.c: portmap-xdr.x portmap-xdr.h
	@if test -f $(top_srcdir)/rpc/xdr/src/${@:.c=.x} ; then \
		$(top_srcdir)/build-aux/xdrgen source $(top_srcdir)/rpc/xdr/src/${@:.c=.x} ; \
	fi

portmap-xdr.h: portmap-xdr.x
	@if test -f $(top_srcdir)/rpc/xdr/src/${@:.h=.x} ; then \
		$(top_srcdir)/build-aux/xdrgen header $(top_srcdir)/rpc/xdr/src/${@:.h=.x} ; \
	fi

mount3udp.c: mount3udp.x mount3udp.h
	@if test -f $(top_srcdir)/rpc/xdr/src/${@:.c=.x} ; then \
		$(top_srcdir)/build-aux/xdrgen source $(top_srcdir)/rpc/xdr/src/${@:.c=.x} ; \
	fi

mount3udp.h: mount3udp.x
	@if test -f $(top_srcdir)/rpc/xdr/src/${@:.h=.x} ; then \
		$(top_srcdir)/build-aux/xdrgen header $(top_srcdir)/rpc/xdr/src/${@:.h=.x} ; \
	fi
