xlator_LTLIBRARIES = glusterd.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/mgmt
glusterd_la_CPPFLAGS = $(GF_CPPFLAGS) "-DFILTERDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/filter\""
glusterd_la_LDFLAGS = -module -avoidversion $(LIBXML2_LIBS) -lcrypto
glusterd_la_SOURCES = glusterd.c glusterd-handler.c glusterd-sm.c \
	glusterd-op-sm.c glusterd-utils.c glusterd-rpc-ops.c \
	glusterd-store.c glusterd-handshake.c glusterd-pmap.c \
	glusterd-volgen.c glusterd-rebalance.c glusterd-quota.c \
	glusterd-geo-rep.c glusterd-replace-brick.c glusterd-log-ops.c \
	glusterd-volume-ops.c glusterd-brick-ops.c glusterd-mountbroker.c \
	glusterd-syncop.c glusterd-hooks.c

glusterd_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
		     $(top_builddir)/rpc/xdr/src/libgfxdr.la \
		     $(top_builddir)/rpc/rpc-lib/src/libgfrpc.la

noinst_HEADERS = glusterd.h glusterd-utils.h glusterd-op-sm.h \
	glusterd-sm.h glusterd-store.h glusterd-mem-types.h \
	glusterd-pmap.h glusterd-volgen.h glusterd-mountbroker.h \
	glusterd-syncop.h glusterd-hooks.h

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)\
	-I$(rpclibdir) -L$(xlatordir)/ -I$(CONTRIBDIR)/rbtree \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_srcdir)/rpc/rpc-lib/src \
	-I$(CONTRIBDIR)/uuid \
	-DSBIN_DIR=\"$(sbindir)\" -DDATADIR=\"$(localstatedir)\" \
	-DGSYNCD_PREFIX=\"$(libexecdir)/glusterfs\"\
        -DSYNCDAEMON_COMPILE=$(SYNCDAEMON_COMPILE) $(LIBXML2_CFLAGS)

>>>>>>> glusterd: prefix added to gluster commands

CLEANFILES =

install-data-hook:

if GF_INSTALL_VAR_LIB_GLUSTERD
	$(mkdir_p) $(localstatedir)/lib/
	(stat $(sysconfdir)/glusterd && \
	    mv $(sysconfdir)/glusterd $(localstatedir)/lib/) || true;
	(ln -sf $(localstatedir)/lib/glusterd $(sysconfdir)/glusterd) || true;
endif
