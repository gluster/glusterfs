lib_LTLIBRARIES = libglrpcsvc.la
libglrpcsvc_la_LDFLAGS = -module -avoidversion

libglrpcsvc_la_SOURCES = msg-nfs3.c xdr-nfs3.c xdr-rpc.c auth-unix.c rpcsvc-auth.c rpcsvc.c auth-null.c rpc-socket.c
libglrpcsvc_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = xdr-rpc.h msg-nfs3.h xdr-common.h xdr-nfs3.h rpc-socket.h rpcsvc.h
AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES =
