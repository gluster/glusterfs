sbin_PROGRAMS = gluster

gluster_SOURCES = cli.c registry.c input.c cli-cmd.c cli-rl.c cli-cmd-global.c \
	 cli-cmd-volume.c cli-cmd-peer.c cli-rpc-ops.c cli-cmd-parser.c\
	 cli-cmd-system.c cli-cmd-misc.c cli-xml-output.c cli-quotad-client.c cli-cmd-snapshot.c

gluster_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la $(GF_LDADD) \
		$(RLLIBS) $(top_builddir)/rpc/xdr/src/libgfxdr.la \
		$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la \
		$(XML_LIBS)

gluster_LDFLAGS = $(GF_LDFLAGS)
noinst_HEADERS = cli.h cli-mem-types.h cli-cmd.h cli-quotad-client.h

AM_CPPFLAGS = $(GF_CPPFLAGS) \
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/rpc/rpc-lib/src\
	-I$(top_srcdir)/rpc/xdr/src\
	-I$(top_builddir)/rpc/xdr/src\
	-DDATADIR=\"$(localstatedir)\" \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\" \
	-DGSYNCD_PREFIX=\"$(libexecdir)/glusterfs\"\
	-DSYNCDAEMON_COMPILE=$(SYNCDAEMON_COMPILE) -DSBIN_DIR=\"$(sbindir)\"\
	$(XML_CPPFLAGS)

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =

$(top_builddir)/libglusterfs/src/libglusterfs.la:
	$(MAKE) -C $(top_builddir)/libglusterfs/src/ all

install-data-hook:
	$(mkdir_p) $(DESTDIR)$(localstatedir)/run/gluster
