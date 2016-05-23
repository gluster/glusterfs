lib_LTLIBRARIES = libposix2common.la

POSIX2_PREFIX = $(top_srcdir)/xlators/experimental/posix2/common/src

noinst_HEADERS = $(POSIX2_PREFIX)/posix2-messages.h \
                 $(POSIX2_PREFIX)/posix2-common.h \
                 $(POSIX2_PREFIX)/posix2-mem-types.h

libposix2common_la_SOURCES = posix2-common.c
nodist_libposix2common_la_SOURCES = posix2-common-fops.c posix2-common-fops.h
BUILT_SOURCES = posix2-common-fops.h

libposix2common_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

libposix2common_la_CFLAGS = -Wall $(GF_CFLAGS)

libposix2common_la_CPPFLAGS = $(GF_CPPFLAGS)
libposix2common_la_CPPFLAGS += -I$(top_srcdir)/libglusterfs/src
libposix2common_la_CPPFLAGS += -I$(top_srcdir)/rpc/xdr/src
libposix2common_la_CPPFLAGS += -I$(top_builddir)/rpc/xdr/src

noinst_PYTHON = posix2-common-fops-c.py posix2-common-fops-h.py
EXTRA_DIST = posix2-common-fops-tmpl.c posix2-common-fops-tmpl.h

posix2-common-fops.c: posix2-common-fops-c.py posix2-common-fops-tmpl.c
	$(PYTHON) $(POSIX2_PREFIX)/posix2-common-fops-c.py \
        $(POSIX2_PREFIX)/posix2-common-fops-tmpl.c > $@

posix2-common-fops.h: posix2-common-fops-h.py posix2-common-fops-tmpl.h
	$(PYTHON) $(POSIX2_PREFIX)/posix2-common-fops-h.py \
        $(POSIX2_PREFIX)/posix2-common-fops-tmpl.h > $@

CLEANFILES = $(nodist_libposix2common_la_SOURCES)
