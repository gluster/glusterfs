sbin_PROGRAMS = glusterfsd gf_attach

glusterfsd_SOURCES = glusterfsd.c glusterfsd-mgmt.c
glusterfsd_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la \
	$(top_builddir)/rpc/xdr/src/libgfxdr.la ${GF_LDADD}
glusterfsd_LDFLAGS = $(GF_LDFLAGS)

gf_attach_SOURCES = gf_attach.c
gf_attach_LDADD   = $(top_builddir)/libglusterfs/src/libglusterfs.la \
		    $(top_builddir)/api/src/libgfapi.la \
		    $(top_builddir)/rpc/rpc-lib/src/libgfrpc.la \
		    $(top_builddir)/rpc/xdr/src/libgfxdr.la

noinst_HEADERS = glusterfsd.h glusterfsd-mem-types.h glusterfsd-messages.h

AM_CPPFLAGS = $(GF_CPPFLAGS) \
	-I$(top_srcdir)/libglusterfs/src -DDATADIR=\"$(localstatedir)\" \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\" $(GF_GLUSTERFS_CFLAGS) \
	-DXLATORDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator\" \
	-I$(top_srcdir)/rpc/rpc-lib/src \
	-I$(top_srcdir)/rpc/xdr/src \
	-I$(top_builddir)/rpc/xdr/src \
	-I$(top_srcdir)/xlators/nfs/server/src \
	-I$(top_srcdir)/api/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =

$(top_builddir)/libglusterfs/src/libglusterfs.la:
	$(MAKE) -C $(top_builddir)/libglusterfs/src/ all

uninstall-local:
	rm -f $(DESTDIR)$(sbindir)/glusterfs
	rm -f $(DESTDIR)$(sbindir)/glusterd

install-data-local:
	$(INSTALL) -d -m 755 $(DESTDIR)$(localstatedir)/run
	$(INSTALL) -d -m 755 $(DESTDIR)$(localstatedir)/run/gluster
	$(INSTALL) -d -m 755 $(DESTDIR)$(localstatedir)/log/glusterfs
	$(INSTALL) -d -m 755 $(DESTDIR)$(sbindir)
	rm -f $(DESTDIR)$(sbindir)/glusterfs
	rm -f $(DESTDIR)$(sbindir)/glusterd
	ln -s glusterfsd $(DESTDIR)$(sbindir)/glusterfs
	ln -s glusterfsd $(DESTDIR)$(sbindir)/glusterd
