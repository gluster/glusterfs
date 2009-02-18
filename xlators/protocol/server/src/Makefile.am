
xlator_LTLIBRARIES = server.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/protocol

server_la_LDFLAGS = -module -avoidversion

server_la_SOURCES = server-protocol.c server-dentry.c server-helpers.c
server_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = server-protocol.h server-helpers.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles \
	-DDATADIR=\"$(localstatedir)\" -DCONFDIR=\"$(sysconfdir)/glusterfs\"  \
	$(GF_CFLAGS)

CLEANFILES = 

