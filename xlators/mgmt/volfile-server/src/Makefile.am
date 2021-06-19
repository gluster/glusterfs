if WITH_SERVER
xlator_LTLIBRARIES = volfile-server.la
endif

xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/mgmt
volfile_server_la_CPPFLAGS = $(AM_CPPFLAGS) \
	-DFILTERDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/filter\" \
	-DXLATORDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator\" \
	-I$(top_srcdir)/libglusterd/src/

volfile_server_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)
volfile_server_la_SOURCES = glusterd.c glusterd-handshake.c

volfile_server_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/rpc/xdr/src/libgfxdr.la \
	$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la \
	$(XML_LIBS) -lcrypto $(URCU_LIBS) $(URCU_CDS_LIBS) $(LIB_DL) $(GF_XLATOR_MGNT_LIBADD)

noinst_HEADERS = glusterd.h glusterd-mem-types.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-I$(CONTRIBDIR)/rbtree -I$(top_srcdir)/rpc/rpc-lib/src


AM_CFLAGS = -Wall $(GF_CFLAGS) $(URCU_CFLAGS) $(URCU_CDS_CFLAGS) $(XML_CFLAGS)

AM_LDFLAGS = -L$(xlatordir) $(URCU_LIBS) $(URCU_CDS_LIBS)

CLEANFILES =
