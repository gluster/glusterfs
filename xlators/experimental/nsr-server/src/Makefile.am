xlator_LTLIBRARIES = nsr.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/experimental

nodist_nsr_la_SOURCES = nsr-cg.c
CLEANFILES = $(nodist_nsr_la_SOURCES)

nsr_la_LDFLAGS = -module -avoid-version
nsr_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
		$(top_builddir)/api/src/libgfapi.la

noinst_HEADERS = nsr-internal.h \
	$(top_srcdir)/xlators/lib/src/libxlator.h \
	$(top_srcdir)/glusterfsd/src/glusterfsd.h

AM_CPPFLAGS = $(GF_CPPFLAGS) \
	-I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/xlators/lib/src \
	-I$(top_srcdir)/rpc/rpc-lib/src -DSBIN_DIR=\"$(sbindir)\" \
	-I$(top_srcdir)/api/src -DNSR_SCRIPT_PREFIX=\"$(nsrdir)\" \
        -I$(top_srcdir)/xlators/experimental/nsr-client/src/

AM_CFLAGS = -Wall $(GF_CFLAGS)

NSR_PREFIX	= $(top_srcdir)/xlators/experimental/nsr-server/src
NSR_GEN_FOPS	= $(NSR_PREFIX)/gen-fops.py
NSR_TEMPLATES	= $(NSR_PREFIX)/all-templates.c
NSR_WRAPPER	= $(NSR_PREFIX)/nsr.c
noinst_PYTHON	= $(NSR_GEN_FOPS)
EXTRA_DIST	= $(NSR_TEMPLATES) $(NSR_WRAPPER)

nsr-cg.c: $(NSR_GEN_FOPS) $(NSR_TEMPLATES) $(NSR_WRAPPER)
	$(PYTHON) $(NSR_GEN_FOPS) $(NSR_TEMPLATES) $(NSR_WRAPPER) > $@

uninstall-local:
	rm -f $(DESTDIR)$(xlatordir)/nsr.so
