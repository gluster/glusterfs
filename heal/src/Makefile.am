sbin_PROGRAMS = glfsheal

glfsheal_SOURCES = glfs-heal.c

glfsheal_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la $(GF_LDADD)\
		$(RLLIBS) $(top_builddir)/rpc/xdr/src/libgfxdr.la \
		$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la \
		$(top_builddir)/api/src/libgfapi.la\
		$(GF_GLUSTERFS_LIBS) $(XML_LIBS)

glfsheal_LDFLAGS = $(GF_LDFLAGS)

AM_CPPFLAGS = $(GF_CPPFLAGS) \
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/rpc/rpc-lib/src\
	-I$(top_srcdir)/rpc/xdr/src\
	-I$(top_srcdir)/api/src\
	-DDATADIR=\"$(localstatedir)\" \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\" \
	-DGSYNCD_PREFIX=\"$(libexecdir)/glusterfs\"\
	-DSYNCDAEMON_COMPILE=$(SYNCDAEMON_COMPILE) -DSBIN_DIR=\"$(sbindir)\"\
	$(XML_CPPFLAGS)

AM_CFLAGS = -Wall $(GF_GLUSTERFS_CFLAGS)

CLEANFILES =

$(top_builddir)/libglusterfs/src/libglusterfs.la:
	$(MAKE) -C $(top_builddir)/libglusterfs/src/ all
