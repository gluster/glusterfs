xlator_LTLIBRARIES = utime.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

UTIME_SRC = $(top_srcdir)/xlators/features/utime/src

utime_sources  = $(UTIME_SRC)/utime-helpers.c
utime_sources += $(UTIME_SRC)/utime.c

utime_la_SOURCES = $(utime_sources)
nodist_utime_la_SOURCES = utime-autogen-fops.c utime-autogen-fops.h
BUILT_SOURCES = utime-autogen-fops.h

utime_la_LDFLAGS = -module -avoid-version
utime_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS_utime  = $(UTIME_SRC)/utime-helpers.h
noinst_HEADERS_utime += $(UTIME_SRC)/utime.h
noinst_HEADERS = $(top_srcdir)/xlators/lib/src/libxlator.h
noinst_HEADERS += $(noinst_HEADERS_utime)

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-I$(top_srcdir)/xlators/lib/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

noinst_PYTHON	= utime-gen-fops-c.py utime-gen-fops-h.py
EXTRA_DIST	= utime-autogen-fops-tmpl.c utime-autogen-fops-tmpl.h

utime-autogen-fops.c: utime-gen-fops-c.py utime-autogen-fops-tmpl.c
	$(PYTHON) $(UTIME_SRC)/utime-gen-fops-c.py $(UTIME_SRC)/utime-autogen-fops-tmpl.c > $@

utime-autogen-fops.h: utime-gen-fops-h.py utime-autogen-fops-tmpl.h
	$(PYTHON) $(UTIME_SRC)/utime-gen-fops-h.py $(UTIME_SRC)/utime-autogen-fops-tmpl.h > $@

CLEANFILES = $(nodist_utime_la_SOURCES)

uninstall-local:
	rm -f $(DESTDIR)$(xlatordir)/utime.so
