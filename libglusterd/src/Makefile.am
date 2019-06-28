libglusterd_la_CFLAGS = $(GF_CFLAGS) $(GF_DARWIN_LIBGLUSTERFS_CFLAGS) \
	-DDATADIR=\"$(localstatedir)\"

libglusterd_la_CPPFLAGS = $(GF_CPPFLAGS) -D__USE_FILE_OFFSET64 \
	-DXLATORDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator\" \
	-DXLATORPARENTDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)\" \
	-DXXH_NAMESPACE=GF_ -D__USE_LARGEFILE64 \
	-I$(CONTRIBDIR)/rbtree \
	-I$(CONTRIBDIR)/libexecinfo ${ARGP_STANDALONE_CPPFLAGS} \
	-DSBIN_DIR=\"$(sbindir)\" -I$(CONTRIBDIR)/timer-wheel \
	-I$(CONTRIBDIR)/xxhash \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-I$(top_srcdir)/rpc/rpc-lib/src/

libglusterd_la_LIBADD = $(ZLIB_LIBS) $(MATH_LIB) $(UUID_LIBS)
libglusterd_la_LDFLAGS = -version-info $(LIBGLUSTERFS_LT_VERSION) $(GF_LDFLAGS) \
	-export-symbols $(top_srcdir)/libglusterd/src/libglusterd.sym

lib_LTLIBRARIES = libglusterd.la

libglusterd_la_SOURCES = gd-common-utils.c

libglusterd_la_HEADERS = gd-common-utils.h

libglusterd_ladir = $(includedir)/glusterfs

noinst_HEADERS = gd-common-utils.h

EXTRA_DIST = libglusterd.sym

CLEANFILES =
