xlator_LTLIBRARIES = jbr.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/experimental

nodist_jbr_la_SOURCES = jbr-cg.c
CLEANFILES = $(nodist_jbr_la_SOURCES)

jbr_la_LDFLAGS = -module -avoid-version
jbr_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
		$(top_builddir)/api/src/libgfapi.la

noinst_HEADERS = jbr-internal.h \
	$(top_srcdir)/xlators/lib/src/libxlator.h \
	$(top_srcdir)/glusterfsd/src/glusterfsd.h

AM_CPPFLAGS = $(GF_CPPFLAGS) \
	-I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/xlators/lib/src \
	-I$(top_srcdir)/rpc/rpc-lib/src -DSBIN_DIR=\"$(sbindir)\" \
	-I$(top_srcdir)/api/src -DJBR_SCRIPT_PREFIX=\"$(jbrdir)\" \
        -I$(top_srcdir)/xlators/experimental/jbr-client/src/

AM_CFLAGS = -Wall $(GF_CFLAGS)

JBR_PREFIX	= $(top_srcdir)/xlators/experimental/jbr-server/src
JBR_GEN_FOPS	= $(JBR_PREFIX)/gen-fops.py
JBR_TEMPLATES	= $(JBR_PREFIX)/all-templates.c
JBR_WRAPPER	= $(JBR_PREFIX)/jbr.c
noinst_PYTHON	= $(JBR_GEN_FOPS)
EXTRA_DIST	= $(JBR_TEMPLATES) $(JBR_WRAPPER)

jbr-cg.c: $(JBR_GEN_FOPS) $(JBR_TEMPLATES) $(JBR_WRAPPER)
	$(PYTHON) $(JBR_GEN_FOPS) $(JBR_TEMPLATES) $(JBR_WRAPPER) > $@

uninstall-local:
	rm -f $(DESTDIR)$(xlatordir)/jbr.so
