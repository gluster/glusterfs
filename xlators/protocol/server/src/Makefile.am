xlator_LTLIBRARIES = server.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/protocol

server_la_LDFLAGS = -module -avoidversion

server_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la \
	$(top_builddir)/rpc/xdr/src/libgfxdr.la

server_la_SOURCES = server.c server-resolve.c server-helpers.c  \
	server3_1-fops.c server-handshake.c authenticate.c

noinst_HEADERS = server.h server-helpers.h server-mem-types.h authenticate.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall    \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles  \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\" -D$(GF_HOST_OS)   \
	-DLIBDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/auth\" \
	$(GF_CFLAGS) -I$(top_srcdir)/xlators/protocol/lib/src   \
	-I$(top_srcdir)/rpc/rpc-lib/src/       \
	-I$(top_srcdir)/rpc/xdr/src/

CLEANFILES = *~
