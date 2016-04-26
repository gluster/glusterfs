gsyncddir = $(libexecdir)/glusterfs

gsyncd_SCRIPTS = gverify.sh peer_gsec_create \
	set_geo_rep_pem_keys.sh peer_mountbroker peer_mountbroker.py \
	peer_georep-sshkey.py

# peer_gsec_create and peer_add_secret_pub are not added to
# EXTRA_DIST as it's derived from a .in file
EXTRA_DIST = gverify.sh set_geo_rep_pem_keys.sh peer_mountbroker.py.in \
	peer_georep-sshkey.py.in

gsyncd_PROGRAMS = gsyncd

gsyncd_SOURCES = gsyncd.c procdiggy.c

gsyncd_LDADD = $(GF_LDADD) $(top_builddir)/libglusterfs/src/libglusterfs.la

gsyncd_LDFLAGS = $(GF_LDFLAGS)

noinst_HEADERS = procdiggy.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-DGSYNCD_PREFIX=\"$(libexecdir)/glusterfs\" -DUSE_LIBGLUSTERFS \
	-DSBIN_DIR=\"$(sbindir)\" -DPYTHON=\"$(PYTHON)\"

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =

$(top_builddir)/libglusterfs/src/libglusterfs.la:
	$(MAKE) -C $(top_builddir)/libglusterfs/src/ all


install-exec-hook:
	$(mkdir_p) $(DESTDIR)$(sbindir)
	rm -f $(DESTDIR)$(sbindir)/gluster-mountbroker
	ln -s $(libexecdir)/glusterfs/peer_mountbroker.py \
		$(DESTDIR)$(sbindir)/gluster-mountbroker

	rm -f $(DESTDIR)$(sbindir)/gluster-georep-sshkey
	ln -s $(libexecdir)/glusterfs/peer_georep-sshkey.py \
		$(DESTDIR)$(sbindir)/gluster-georep-sshkey


uninstall-hook:
	rm -f $(DESTDIR)$(sbindir)/gluster-mountbroker
	rm -f $(DESTDIR)$(sbindir)/gluster-georep-sshkey
