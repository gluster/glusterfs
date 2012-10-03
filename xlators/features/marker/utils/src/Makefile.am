gsyncddir = $(libexecdir)/glusterfs

gsyncd_PROGRAMS = gsyncd

gsyncd_SOURCES = gsyncd.c procdiggy.c

gsyncd_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

gsyncd_LDFLAGS = $(GF_LDFLAGS) $(GF_GLUSTERFS_LDFLAGS)

noinst_HEADERS = procdiggy.h

AM_CFLAGS = -Wall $(GF_CPPFLAGS) \
	-I$(top_srcdir)/libglusterfs/src\
	-DGSYNCD_PREFIX=\"$(libexecdir)/glusterfs\"\
	-DUSE_LIBGLUSTERFS\
	-DSBIN_DIR=\"$(sbindir)\" -DPYTHON=\"$(PYTHON)\" $(GF_CFLAGS)


CLEANFILES =

$(top_builddir)/libglusterfs/src/libglusterfs.la:
	$(MAKE) -C $(top_builddir)/libglusterfs/src/ all
