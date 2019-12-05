if WITH_SERVER
scriptdir = $(GLUSTERFS_LIBEXECDIR)
script_PROGRAMS = glfsheal
endif

glfsheal_SOURCES = glfs-heal.c

glfsheal_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la $(GF_LDADD)\
		$(RLLIBS) $(top_builddir)/rpc/xdr/src/libgfxdr.la \
		$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la \
		$(top_builddir)/api/src/libgfapi.la \
		$(XML_LIBS) $(GFAPI_LIBS) $(UUID_LIBS)

glfsheal_LDFLAGS = $(GF_LDFLAGS)

AM_CPPFLAGS = $(GF_CPPFLAGS) \
	-I$(top_srcdir)/xlators/lib/src\
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/rpc/rpc-lib/src\
	-I$(top_srcdir)/rpc/xdr/src\
	-I$(top_builddir)/rpc/xdr/src\
	-I$(top_srcdir)/api/src\
	-DDATADIR=\"$(localstatedir)\"

AM_CFLAGS = -Wall $(GF_CFLAGS) $(XML_CFLAGS)

CLEANFILES =

$(top_builddir)/libglusterfs/src/libglusterfs.la:
	$(MAKE) -C $(top_builddir)/libglusterfs/src/ all
