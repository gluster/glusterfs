lib_LTLIBRARIES = libposix2common.la

POSIX2_PREFIX = $(top_srcdir)/xlators/experimental/posix2/common/src

POSIX_SRC = $(top_srcdir)/xlators/storage/posix/src
POSIX_BLD = $(top_builddir)/xlators/storage/posix/src

posix_sources = $(POSIX_SRC)/posix-common.c
posix_sources += $(POSIX_SRC)/posix-helpers.c
posix_sources += $(POSIX_SRC)/posix-handle.c
posix_sources += $(POSIX_SRC)/posix-aio.c
posix_sources += $(POSIX_SRC)/posix-gfid-path.c
posix_sources += $(POSIX_SRC)/posix-inode-fd-ops.c

noinst_HEADERS = $(POSIX2_PREFIX)/posix2-messages.h \
                 $(POSIX2_PREFIX)/posix2-common.h \
                 $(POSIX2_PREFIX)/posix2-mem-types.h \
                 $(POSIX2_PREFIX)/posix2-inode-handle.h \
                 $(POSIX2_PREFIX)/posix2-helpers.h \
                 $(POSIX_BLD)/posix-mem-types.h \
                 $(POSIX_BLD)/posix-handle.h \
                 $(POSIX_BLD)/posix-aio.h \
                 $(POSIX_BLD)/posix-messages.h \
                 $(POSIX_BLD)/posix-gfid-path.h \
                 $(POSIX_BLD)/posix.h \
                 $(POSIX_BLD)/posix-inode-handle.h

libposix2common_la_SOURCES = posix2-helpers.c $(posix_sources)

nodist_libposix2common_la_SOURCES = posix2-common-fops.c posix2-common-fops.h
BUILT_SOURCES = posix2-common-fops.h

libposix2common_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
        $(LIBAIO) $(ACL_LIBS)

libposix2common_la_CFLAGS = -Wall -fno-strict-aliasing
libposix2common_la_CFLAGS += $(GF_CFLAGS) -I$(top_srcdir)/glusterfsd/src
libposix2common_la_CFLAGS += -DPOSIX2
libposix2common_la_CFLAGS += -I$(POSIX_SRC)

libposix2common_la_CPPFLAGS = $(GF_CPPFLAGS)
libposix2common_la_CPPFLAGS += -I$(top_srcdir)/libglusterfs/src
libposix2common_la_CPPFLAGS += -I$(top_srcdir)/rpc/xdr/src
libposix2common_la_CPPFLAGS += -I$(top_builddir)/rpc/xdr/src
libposix2common_la_CPPFLAGS += -I$(top_srcdir)/rpc/rpc-lib/src
libposix2common_la_CPPFLAGS += -I$(POSIX_SRC)

noinst_PYTHON = posix2-common-fops-c.py posix2-common-fops-h.py
EXTRA_DIST = posix2-common-fops-tmpl.c posix2-common-fops-tmpl.h

posix2-common-fops.c: posix2-common-fops-c.py posix2-common-fops-tmpl.c
	$(PYTHON) $(POSIX2_PREFIX)/posix2-common-fops-c.py \
        $(POSIX2_PREFIX)/posix2-common-fops-tmpl.c > $@

posix2-common-fops.h: posix2-common-fops-h.py posix2-common-fops-tmpl.h
	$(PYTHON) $(POSIX2_PREFIX)/posix2-common-fops-h.py \
        $(POSIX2_PREFIX)/posix2-common-fops-tmpl.h > $@

CLEANFILES = $(nodist_libposix2common_la_SOURCES)
