noinst_PYTHON = gen-fops.py

EXTRA_DIST = fops-tmpl.c

xlator_LTLIBRARIES = metadisp.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

nodist_metadisp_la_SOURCES = fops.c

BUILT_SOURCES = fops.c

metadisp_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

metadisp_la_SOURCES = metadisp.c \
		metadisp-unlink.c \
		metadisp-stat.c \
		metadisp-lookup.c \
		metadisp-readdir.c \
		metadisp-create.c \
		metadisp-open.c \
		metadisp-fsync.c \
		metadisp-setattr.c \
		backend.c

metadisp_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = metadisp.h metadisp-fops.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

fops.c: fops-tmpl.c $(top_srcdir)/libglusterfs/src/generator.py gen-fops.py
	PYTHONPATH=$(top_srcdir)/libglusterfs/src \
	$(PYTHON) $(srcdir)/gen-fops.py $(srcdir)/fops-tmpl.c > $@

CLEANFILES = $(nodist_metadisp_la_SOURCES)
