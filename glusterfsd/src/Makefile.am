sbin_PROGRAMS = glusterfsd

glusterfsd_SOURCES = glusterfsd.c glusterfsd-mgmt.c
if GF_DARWIN_HOST_OS
glusterfsd_SOURCES += $(CONTRIBDIR)/apple/daemon.c
endif
glusterfsd_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la \
	$(top_builddir)/rpc/xdr/src/libgfxdr.la \
	$(GF_LDADD)
glusterfsd_LDFLAGS = $(GF_LDFLAGS) $(GF_GLUSTERFS_LDFLAGS)
noinst_HEADERS = glusterfsd.h glusterfsd-mem-types.h

AM_CFLAGS = -fPIC -Wall -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -DDATADIR=\"$(localstatedir)\" \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\" $(GF_GLUSTERFS_CFLAGS) \
	-I$(top_srcdir)/rpc/rpc-lib/src -I$(top_srcdir)/rpc/xdr/src
if GF_DARWIN_HOST_OS
AM_CFLAGS += -I$(CONTRIBDIR)/apple
endif

CLEANFILES = 

$(top_builddir)/libglusterfs/src/libglusterfs.la:
	$(MAKE) -C $(top_builddir)/libglusterfs/src/ all

uninstall-local:
	rm -f $(DESTDIR)$(sbindir)/glusterfs
	rm -f $(DESTDIR)$(sbindir)/glusterd

install-data-local:
	$(INSTALL) -d -m 755 $(DESTDIR)$(localstatedir)/run
	$(INSTALL) -d -m 755 $(DESTDIR)$(localstatedir)/log/glusterfs
	rm -f $(DESTDIR)$(sbindir)/glusterfs
	rm -f $(DESTDIR)$(sbindir)/glusterd
	ln -s glusterfsd $(DESTDIR)$(sbindir)/glusterfs
	ln -s glusterfsd $(DESTDIR)$(sbindir)/glusterd
