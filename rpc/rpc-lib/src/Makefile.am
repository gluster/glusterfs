lib_LTLIBRARIES = libgfrpc.la

libgfrpc_la_SOURCES = auth-unix.c rpcsvc-auth.c rpcsvc.c auth-null.c \
	rpc-transport.c xdr-rpc.c xdr-rpcclnt.c rpc-clnt.c auth-glusterfs.c

libgfrpc_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = rpcsvc.h rpc-transport.h xdr-common.h xdr-rpc.h xdr-rpcclnt.h \
	rpc-clnt.h rpcsvc-common.h protocol-common.h

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared $(GF_CFLAGS) \
	-I$(top_srcdir)/rpc/xdr/src \
	-DRPC_TRANSPORTDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/rpc-transport\"

CLEANFILES = *~
