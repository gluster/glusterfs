
xlator_LTLIBRARIES = client.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/alpha/protocol

client_la_LDFLAGS = -module -avoidversion

client_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/xlators/protocol/rpc/rpc-lib/src/libgfrpc.la \
	$(top_builddir)/xlators/protocol/lib/src/libgfproto1.la

client_la_SOURCES = client.c client-helpers.c client3_1-fops.c client-handshake.c
noinst_HEADERS = client.h client-mem-types.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)  \
	-I$(top_srcdir)/xlators/protocol/lib/src -I$(top_srcdir)/xlators/protocol/rpc/rpc-lib/src/
