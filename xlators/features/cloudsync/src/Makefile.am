xlator_LTLIBRARIES = cloudsync.la

xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

CLOUDSYNC_SRC = $(top_srcdir)/xlators/features/cloudsync/src

cloudsync_sources = cloudsync.c

CLOUDSYNC_SRC = $(top_srcdir)/xlators/features/cloudsync/src
CLOUDSYNC_BLD = $(top_builddir)/xlators/features/cloudsync/src

cloudsynccommon_sources = $(CLOUDSYNC_SRC)/cloudsync-common.c

noinst_HEADERS = $(CLOUDSYNC_BLD)/cloudsync.h \
	$(CLOUDSYNC_BLD)/cloudsync-mem-types.h \
	$(CLOUDSYNC_BLD)/cloudsync-messages.h \
	$(CLOUDSYNC_BLD)/cloudsync-common.h

cloudsync_la_SOURCES = $(cloudsync_sources) $(cloudsynccommon_sources)

nodist_cloudsync_la_SOURCES = cloudsync-autogen-fops.c cloudsync-autogen-fops.h
BUILT_SOURCES = cloudsync-autogen-fops.h

cloudsync_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

cloudsync_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall -fno-strict-aliasing $(GF_CFLAGS)

noinst_PYTHON = cloudsync-fops-c.py cloudsync-fops-h.py
EXTRA_DIST = cloudsync-autogen-fops-tmpl.c cloudsync-autogen-fops-tmpl.h

cloudsync-autogen-fops.c: cloudsync-fops-c.py cloudsync-autogen-fops-tmpl.c
	$(PYTHON) $(CLOUDSYNC_SRC)/cloudsync-fops-c.py \
	$(CLOUDSYNC_SRC)/cloudsync-autogen-fops-tmpl.c > $@

cloudsync-autogen-fops.h: cloudsync-fops-h.py cloudsync-autogen-fops-tmpl.h
	$(PYTHON) $(CLOUDSYNC_SRC)/cloudsync-fops-h.py \
	$(CLOUDSYNC_SRC)/cloudsync-autogen-fops-tmpl.h > $@

CLEANFILES = $(nodist_cloudsync_la_SOURCES)

uninstall-local:
	rm -f $(DESTDIR)$(xlatordir)/cloudsync.so

