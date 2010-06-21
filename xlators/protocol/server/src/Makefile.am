xlator_LTLIBRARIES = server.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/protocol

server_la_LDFLAGS = -module -avoidversion

server_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/xlators/protocol/rpc/rpc-lib/src/libgfrpc.la \
	$(top_builddir)/xlators/protocol/lib/src/libgfproto1.la

server_la_SOURCES = server.c server-resolve.c server-helpers.c  \
	server3_1-fops.c server-handshake.c

noinst_HEADERS = server.h server-helpers.h server-mem-types.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall    \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles  \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\" -D$(GF_HOST_OS)   \
	$(GF_CFLAGS) -I$(top_srcdir)/xlators/protocol/lib/src   \
	-I$(top_srcdir)/xlators/protocol/rpc/rpc-lib/src/       \
	-I$(top_srcdir)/contrib/md5/

CLEANFILES = *~
