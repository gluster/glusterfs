xlator_LTLIBRARIES = nsrc.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/experimental

nodist_nsrc_la_SOURCES = nsrc-cg.c
CLEANFILES = $(nodist_nsrc_la_SOURCES)

nsrc_la_LDFLAGS = -module -avoid-version
nsrc_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = \
	 $(top_srcdir)/xlators/lib/src/libxlator.h \
	 $(top_srcdir)/glusterfsd/src/glusterfsd.h \
	 nsrc.h nsr-messages.h

AM_CPPFLAGS = $(GF_CPPFLAGS) \
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/xlators/lib/src \
	-I$(top_srcdir)/rpc/rpc-lib/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

NSRC_PREFIX	= $(top_srcdir)/xlators/experimental/nsr-client/src
NSRC_GEN_FOPS	= $(NSRC_PREFIX)/gen-fops.py
NSRC_TEMPLATES	= $(NSRC_PREFIX)/fop-template.c
NSRC_WRAPPER	= $(NSRC_PREFIX)/nsrc.c
noinst_PYTHON	= $(NSRC_GEN_FOPS)
EXTRA_DIST	= $(NSRC_TEMPLATES) $(NSRC_WRAPPER)

nsrc-cg.c: $(NSRC_GEN_FOPS) $(NSRC_TEMPLATES) $(NSRC_WRAPPER)
	$(PYTHON) $(NSRC_GEN_FOPS) $(NSRC_TEMPLATES) $(NSRC_WRAPPER) > $@

uninstall-local:
	rm -f $(DESTDIR)$(xlatordir)/nsr.so
