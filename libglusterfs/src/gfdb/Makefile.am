libgfdb_la_CFLAGS = -Wall $(GF_CFLAGS) $(GF_DARWIN_LIBGLUSTERFS_CFLAGS) \
	$(SQLITE_CFLAGS) -DDATADIR=\"$(localstatedir)\"

libgfdb_la_CPPFLAGS = $(GF_CPPFLAGS) -D__USE_FILE_OFFSET64 -fpic \
	-I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src \
	-I$(top_builddir)/rpc/xdr/src \
	-DDATADIR=\"$(localstatedir)\"

libgfdb_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(SQLITE_LIBS) $(UUID_LIBS)

libgfdb_la_LDFLAGS = $(GF_LDFLAGS) -version-info $(LIBGLUSTERFS_LT_VERSION)

libgfdbdir = $(includedir)/glusterfs/gfdb

if BUILD_GFDB
 lib_LTLIBRARIES = libgfdb.la
endif

CONTRIB_BUILDDIR = $(top_builddir)/contrib

libgfdb_la_SOURCES = gfdb_data_store.c gfdb_data_store_helper.c \
	gfdb_sqlite3_helper.c gfdb_sqlite3.c

noinst_HEADERS =  gfdb_data_store.h gfdb_data_store_types.h \
	gfdb_sqlite3_helper.h gfdb_sqlite3.h gfdb_mem-types.h \
	gfdb_data_store_helper.h

libgfdb_HEADERS = gfdb_data_store.h gfdb_data_store_types.h \
	gfdb_data_store_helper.h gfdb_sqlite3.h gfdb_mem-types.h \
	gfdb_sqlite3_helper.h

CLEANFILES =

$(top_builddir)/libglusterfs/src/libglusterfs.la:
	$(MAKE) -C $(top_builddir)/libglusterfs/src/ all
