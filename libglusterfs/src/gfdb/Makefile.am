libgfdb_la_CFLAGS = -Wall $(GF_CFLAGS) $(GF_DARWIN_LIBGLUSTERFS_CFLAGS) \
  -DDATADIR=\"$(localstatedir)\"

libgfdb_la_CPPFLAGS = $(GF_CPPFLAGS) -D__USE_FILE_OFFSET64 -fpic \
  -I$(top_srcdir)/libglusterfs/src \
  -DDATADIR=\"$(localstatedir)\"

libgfdb_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la -lsqlite3

libgfdb_la_LDFLAGS = $(GF_LDFLAGS) -version-info $(LIBGLUSTERFS_LT_VERSION)

libgfdbdir = $(includedir)/glusterfs/gfdb

if BUILD_GFDB
 lib_LTLIBRARIES = libgfdb.la
endif

CONTRIB_BUILDDIR = $(top_builddir)/contrib

libgfdb_la_SOURCES = gfdb_data_store.c gfdb_sqlite3_helper.c\
   gfdb_sqlite3.c \
   $(CONTRIBDIR)/uuid/clear.c \
   $(CONTRIBDIR)/uuid/copy.c $(CONTRIBDIR)/uuid/gen_uuid.c \
   $(CONTRIBDIR)/uuid/pack.c $(CONTRIBDIR)/uuid/parse.c \
   $(CONTRIBDIR)/uuid/unparse.c $(CONTRIBDIR)/uuid/uuid_time.c \
   $(CONTRIBDIR)/uuid/compare.c $(CONTRIBDIR)/uuid/isnull.c \
   $(CONTRIBDIR)/uuid/unpack.c

noinst_HEADERS =  gfdb_data_store.h gfdb_data_store_types.h gfdb_sqlite3_helper.h\
   gfdb_sqlite3.h gfdb_mem-types.h \
   $(CONTRIBDIR)/uuid/uuidd.h \
   $(CONTRIBDIR)/uuid/uuid.h $(CONTRIBDIR)/uuid/uuid.h \
   $(CONTRIB_BUILDDIR)/uuid/uuid_types.h

libgfdb_HEADERS = gfdb_data_store.h gfdb_data_store_types.h \
                gfdb_sqlite3.h gfdb_mem-types.h gfdb_sqlite3_helper.c

CLEANFILES =
CONFIG_CLEAN_FILES = $(CONTRIB_BUILDDIR)/uuid/uuid_types.h

$(top_builddir)/libglusterfs/src/libglusterfs.la:
	$(MAKE) -C $(top_builddir)/libglusterfs/src/ all
