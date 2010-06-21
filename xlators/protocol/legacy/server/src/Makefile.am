
xlator_LTLIBRARIES = server-old.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/legacy/protocol

server_old_la_LDFLAGS = -module -avoidversion

server_old_la_SOURCES = server-protocol.c server-resolve.c server-helpers.c
server_old_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la   \
	$(top_builddir)/xlators/protocol/legacy/lib/src/libgfproto.la \
	$(top_builddir)/xlators/protocol/lib/src/libgfproto1.la

noinst_HEADERS = server-protocol.h server-helpers.h server-mem-types.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles \
	-I$(top_srcdir)/contrib/md5/ \
	-DDATADIR=\"$(localstatedir)\" -DCONFDIR=\"$(sysconfdir)/glusterfs\"  \
	$(GF_CFLAGS) -I$(top_srcdir)/xlators/protocol/legacy/lib/src \
	-I$(top_srcdir)/xlators/protocol/lib/src

CLEANFILES =

install-data-hook:
	ln -sf server.so $(DESTDIR)$(xlatordir)/server-old.so
