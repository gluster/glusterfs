
xlator_LTLIBRARIES = client-old.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/legacy/protocol

client_old_la_LDFLAGS = -module -avoidversion

client_old_la_SOURCES = client-protocol.c saved-frames.c

client_old_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/xlators/protocol/legacy/lib/src/libgfproto.la

noinst_HEADERS = client-protocol.h saved-frames.h client-mem-types.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)  \
	-I$(top_srcdir)/xlators/protocol/legacy/lib/src

CLEANFILES = 

install-data-hook:
	ln -sf client.so $(DESTDIR)$(xlatordir)/client-old.so
