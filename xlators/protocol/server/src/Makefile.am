
xlator_LTLIBRARIES = server.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/protocol

server_la_LDFLAGS = -module -avoidversion

server_la_SOURCES = server-protocol.c server-resolve.c server-helpers.c
server_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/xlators/protocol/lib/src/libgfproto.la

noinst_HEADERS = server-protocol.h server-helpers.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles \
	-I$(top_srcdir)/contrib/md5/ \
	-DDATADIR=\"$(localstatedir)\" -DCONFDIR=\"$(sysconfdir)/glusterfs\"  \
	$(GF_CFLAGS) -I$(top_srcdir)/xlators/protocol/lib/src

CLEANFILES =

