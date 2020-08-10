if WITH_SERVER
xlator_LTLIBRARIES = glusterd.la
endif

xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/mgmt
glusterd_la_CPPFLAGS = $(AM_CPPFLAGS) \
	-DFILTERDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/filter\" \
	-DXLATORDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator\" \
	-I$(top_srcdir)/libglusterd/src/

glusterd_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)
glusterd_la_SOURCES = glusterd.c glusterd-handler.c glusterd-sm.c \
	glusterd-op-sm.c glusterd-utils.c glusterd-rpc-ops.c \
	glusterd-store.c glusterd-handshake.c glusterd-pmap.c \
	glusterd-volgen.c glusterd-rebalance.c \
	glusterd-quota.c glusterd-bitrot.c glusterd-geo-rep.c \
	glusterd-replace-brick.c glusterd-log-ops.c \
	glusterd-volume-ops.c glusterd-brick-ops.c glusterd-mountbroker.c \
	glusterd-syncop.c glusterd-hooks.c glusterd-volume-set.c \
	glusterd-locks.c glusterd-snapshot.c glusterd-mgmt-handler.c \
	glusterd-mgmt.c glusterd-peer-utils.c glusterd-statedump.c \
	glusterd-snapshot-utils.c glusterd-conn-mgmt.c \
	glusterd-proc-mgmt.c glusterd-svc-mgmt.c \
	glusterd-nfs-svc.c glusterd-quotad-svc.c glusterd-svc-helper.c \
	glusterd-conn-helper.c glusterd-snapd-svc.c glusterd-snapd-svc-helper.c \
	glusterd-bitd-svc.c glusterd-scrub-svc.c glusterd-server-quorum.c \
	glusterd-reset-brick.c glusterd-shd-svc.c glusterd-shd-svc-helper.c \
        glusterd-gfproxyd-svc.c glusterd-gfproxyd-svc-helper.c glusterd-ganesha.c \
	$(CONTRIBDIR)/mount/mntent.c

glusterd_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/libglusterd/src/libglusterd.la \
	$(top_builddir)/rpc/xdr/src/libgfxdr.la \
	$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la \
	$(XML_LIBS) -lcrypto $(URCU_LIBS) $(URCU_CDS_LIBS) $(LIB_DL) $(GF_XLATOR_MGNT_LIBADD)

noinst_HEADERS = glusterd.h glusterd-utils.h glusterd-op-sm.h \
	glusterd-sm.h glusterd-store.h glusterd-mem-types.h \
	glusterd-pmap.h glusterd-volgen.h glusterd-mountbroker.h \
	glusterd-syncop.h glusterd-hooks.h glusterd-locks.h glusterd-quota.h \
	glusterd-mgmt.h glusterd-messages.h glusterd-peer-utils.h \
	glusterd-statedump.h glusterd-snapshot-utils.h glusterd-geo-rep.h \
	glusterd-conn-mgmt.h glusterd-conn-helper.h glusterd-proc-mgmt.h \
	glusterd-svc-mgmt.h glusterd-nfs-svc.h \
	glusterd-quotad-svc.h glusterd-svc-helper.h glusterd-snapd-svc.h \
	glusterd-snapd-svc-helper.h glusterd-rcu.h glusterd-bitd-svc.h \
	glusterd-scrub-svc.h glusterd-server-quorum.h glusterd-errno.h \
        glusterd-shd-svc.h glusterd-shd-svc-helper.h \
        glusterd-gfproxyd-svc.h glusterd-gfproxyd-svc-helper.h \
	$(CONTRIBDIR)/userspace-rcu/rculist-extra.h \
	$(CONTRIBDIR)/mount/mntent_compat.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-I$(CONTRIBDIR)/rbtree -I$(top_srcdir)/rpc/rpc-lib/src \
	-I$(CONTRIBDIR)/mount -I$(CONTRIBDIR)/userspace-rcu \
	-DSBIN_DIR=\"$(sbindir)\" -DDATADIR=\"$(localstatedir)\" \
	-DGSYNCD_PREFIX=\"$(GLUSTERFS_LIBEXECDIR)\" \
	-DCONFDIR=\"$(localstatedir)/run/gluster/shared_storage/nfs-ganesha\" \
	-DGANESHA_PREFIX=\"$(libexecdir)/ganesha\" \
	-DSYNCDAEMON_COMPILE=$(SYNCDAEMON_COMPILE) \
	-I$(top_srcdir)/libglusterd/src/


AM_CFLAGS = -Wall $(GF_CFLAGS) $(URCU_CFLAGS) $(URCU_CDS_CFLAGS) $(XML_CFLAGS)

AM_LDFLAGS = -L$(xlatordir) $(URCU_LIBS) $(URCU_CDS_LIBS)

CLEANFILES =

install-data-hook:
if WITH_SERVER
if GF_INSTALL_GLUSTERD_WORKDIR
	$(mkdir_p) $(DESTDIR)$(GLUSTERD_WORKDIR)
	(stat $(DESTDIR)$(sysconfdir)/glusterd && \
	    mv $(DESTDIR)$(sysconfdir)/glusterd $(DESTDIR)$(GLUSTERD_WORKDIR)) || true;
	(ln -sf $(DESTDIR)$(GLUSTERD_WORKDIR) $(sysconfdir)/glusterd) || true;
endif
endif
