xlator_LTLIBRARIES = jbrc.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/experimental

nodist_jbrc_la_SOURCES = jbrc-cg.c
CLEANFILES = $(nodist_jbrc_la_SOURCES)

jbrc_la_LDFLAGS = -module -avoid-version
jbrc_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = \
	 $(top_srcdir)/xlators/lib/src/libxlator.h \
	 $(top_srcdir)/glusterfsd/src/glusterfsd.h \
	 jbrc.h jbr-messages.h

AM_CPPFLAGS = $(GF_CPPFLAGS) \
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/xlators/lib/src \
	-I$(top_srcdir)/rpc/rpc-lib/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

JBRC_PREFIX	= $(top_srcdir)/xlators/experimental/jbr-client/src
JBRC_GEN_FOPS	= $(JBRC_PREFIX)/gen-fops.py
JBRC_TEMPLATES	= $(JBRC_PREFIX)/fop-template.c
JBRC_WRAPPER	= $(JBRC_PREFIX)/jbrc.c
noinst_PYTHON	= $(JBRC_GEN_FOPS)
EXTRA_DIST	= $(JBRC_TEMPLATES) $(JBRC_WRAPPER)

jbrc-cg.c: $(JBRC_GEN_FOPS) $(JBRC_TEMPLATES) $(JBRC_WRAPPER)
	$(PYTHON) $(JBRC_GEN_FOPS) $(JBRC_TEMPLATES) $(JBRC_WRAPPER) > $@

uninstall-local:
	rm -f $(DESTDIR)$(xlatordir)/jbr.so
