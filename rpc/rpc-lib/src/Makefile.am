lib_LTLIBRARIES = libgfrpc.la

libgfrpc_la_SOURCES = auth-unix.c rpcsvc-auth.c rpcsvc.c auth-null.c \
	rpc-transport.c xdr-rpc.c xdr-rpcclnt.c rpc-clnt.c auth-glusterfs.c \
	rpc-drc.c rpc-clnt-ping.c \
        autoscale-threads.c mgmt-pmap.c

EXTRA_DIST = libgfrpc.sym

libgfrpc_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
                     $(top_builddir)/rpc/xdr/src/libgfxdr.la
libgfrpc_la_LDFLAGS = -version-info $(LIBGFRPC_LT_VERSION) $(GF_LDFLAGS) \
		      -export-symbols $(top_srcdir)/rpc/rpc-lib/src/libgfrpc.sym

libgfrpc_la_HEADERS = rpcsvc.h rpc-transport.h xdr-common.h xdr-rpc.h xdr-rpcclnt.h \
	rpc-clnt.h rpcsvc-common.h protocol-common.h protocol-utils.h \
	rpc-drc.h rpc-clnt-ping.h rpc-lib-messages.h

libgfrpc_ladir = $(includedir)/glusterfs/rpc

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src \
	-I$(top_builddir)/rpc/xdr/src \
	-DRPC_TRANSPORTDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/rpc-transport\" \
	-I$(top_srcdir)/contrib/rbtree

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = *~
