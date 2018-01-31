gluster_setgfid2pathdir = $(sbindir)

if WITH_SERVER
gluster_setgfid2path_PROGRAMS = gluster-setgfid2path
endif

gluster_setgfid2path_SOURCES = main.c

gluster_setgfid2path_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

gluster_setgfid2path_LDFLAGS = $(GF_LDFLAGS)

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)
