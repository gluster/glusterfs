# The bits needed for glfsxmp
noinst_PROGRAMS = glfsxmp
glfsxmp_SOURCES = glfsxmp.c
glfsxmp_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la $(GF_LDADD) \
		$(top_builddir)/api/src/libgfapi.la \
		$(RLLIBS) $(top_builddir)/rpc/xdr/src/libgfxdr.la \
		$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la \
		$(XML_LIBS)

glfsxmp_LDFLAGS = $(GF_LDFLAGS)
AM_CPPFLAGS = $(GF_CPPFLAGS) \
	-I$(top_srcdir)/api/src \
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/rpc/rpc-lib/src\
	-I$(top_srcdir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

