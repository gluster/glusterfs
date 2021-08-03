xlator_LTLIBRARIES = tier.la

xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

tier_sources = tier.c

TIER_SRC = $(top_srcdir)/xlators/features/tier/src
TIER_BLD = $(top_builddir)/xlators/features/tier/src

noinst_HEADERS = $(TIER_BLD)/tier.h \
		 $(TIER_BLD)/tier-mem-types.h \
		 $(TIER_BLD)/tier-messages.h

tier_la_SOURCES = $(tier_sources)

nodist_tier_la_SOURCES = tier-autogen-fops.c tier-autogen-fops.h
BUILT_SOURCES = tier-autogen-fops.h

tier_la_LDFLAGS = $(LIB_DL) -module $(GF_XLATOR_DEFAULT_LDFLAGS)

tier_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
              -I$(top_builddir)/rpc/xdr/src \
	      -DTIER_PLUGINDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/tier-plugins\"
AM_CFLAGS = -Wall -fno-strict-aliasing $(GF_CFLAGS)

noinst_PYTHON = tier-fops-c.py tier-fops-h.py
EXTRA_DIST = tier-autogen-fops-tmpl.c tier-autogen-fops-tmpl.h

tier-autogen-fops.c: tier-fops-c.py tier-autogen-fops-tmpl.c
	$(PYTHON) $(TIER_SRC)/tier-fops-c.py \
	$(TIER_SRC)/tier-autogen-fops-tmpl.c > $@

tier-autogen-fops.h: tier-fops-h.py tier-autogen-fops-tmpl.h
	$(PYTHON) $(TIER_SRC)/tier-fops-h.py \
	$(TIER_SRC)/tier-autogen-fops-tmpl.h > $@

CLEANFILES = $(nodist_tier_la_SOURCES)

uninstall-local:
	rm -f $(DESTDIR)$(xlatordir)/tier.so
