
xlator_LTLIBRARIES = client.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/protocol

client_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

client_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la \
	$(top_builddir)/rpc/xdr/src/libgfxdr.la

client_la_SOURCES = client.c client-helpers.c client-rpc-fops.c  \
	client-handshake.c client-callback.c client-lk.c client-common.c

noinst_HEADERS = client.h client-mem-types.h client-messages.h client-common.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-I$(top_srcdir)/rpc/rpc-lib/src/

AM_CFLAGS = -Wall $(GF_CFLAGS)
