xlator_LTLIBRARIES = glusterd.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/mgmt
glusterd_la_CPPFLAGS = $(AM_CPPFLAGS) "-DFILTERDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/filter\""
glusterd_la_LDFLAGS = -module -avoid-version
if ENABLE_BD_XLATOR
glusterd_la_LDFLAGS += -llvm2app
endif
glusterd_la_SOURCES = glusterd.c glusterd-handler.c glusterd-sm.c \
	glusterd-op-sm.c glusterd-utils.c glusterd-rpc-ops.c \
	glusterd-store.c glusterd-handshake.c glusterd-pmap.c \
	glusterd-volgen.c glusterd-rebalance.c glusterd-quota.c \
	glusterd-geo-rep.c glusterd-replace-brick.c glusterd-log-ops.c \
	glusterd-volume-ops.c glusterd-brick-ops.c glusterd-mountbroker.c \
	glusterd-syncop.c glusterd-hooks.c glusterd-volume-set.c \
	glusterd-locks.c glusterd-snapshot.c glusterd-mgmt-handler.c \
	glusterd-mgmt.c glusterd-peer-utils.c

glusterd_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
		     $(top_builddir)/rpc/xdr/src/libgfxdr.la \
		     $(top_builddir)/rpc/rpc-lib/src/libgfrpc.la \
		     $(XML_LIBS) -lcrypto

noinst_HEADERS = glusterd.h glusterd-utils.h glusterd-op-sm.h \
	glusterd-sm.h glusterd-store.h glusterd-mem-types.h \
	glusterd-pmap.h glusterd-volgen.h glusterd-mountbroker.h \
	glusterd-syncop.h glusterd-hooks.h glusterd-locks.h \
	glusterd-mgmt.h glusterd-messages.h glusterd-peer-utils.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(rpclibdir) -I$(CONTRIBDIR)/rbtree \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_srcdir)/rpc/rpc-lib/src \
	-I$(CONTRIBDIR)/uuid -I$(CONTRIBDIR)/mount \
	-DSBIN_DIR=\"$(sbindir)\" -DDATADIR=\"$(localstatedir)\" \
	-DGSYNCD_PREFIX=\"$(libexecdir)/glusterfs\"\
        -DSYNCDAEMON_COMPILE=$(SYNCDAEMON_COMPILE) $(XML_CPPFLAGS)

AM_CFLAGS = -Wall $(GF_CFLAGS)

AM_LDFLAGS = -L$(xlatordir)

CLEANFILES =

install-data-hook:
if GF_INSTALL_GLUSTERD_WORKDIR
	$(mkdir_p) $(DESTDIR)$(GLUSTERD_WORKDIR)
	(stat $(DESTDIR)$(sysconfdir)/glusterd && \
	    mv $(DESTDIR)$(sysconfdir)/glusterd $(DESTDIR)$(GLUSTERD_WORKDIR)) || true;
	(ln -sf $(DESTDIR)$(GLUSTERD_WORKDIR) $(sysconfdir)/glusterd) || true;
endif
