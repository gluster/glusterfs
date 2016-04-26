xlator_LTLIBRARIES = dht.la nufa.la switch.la
if BUILD_GFDB
   xlator_LTLIBRARIES += tier.la
endif

AM_CFLAGS = -Wall $(GF_CFLAGS)

xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/cluster

dht_common_source = dht-layout.c dht-helper.c dht-linkfile.c dht-rebalance.c \
	dht-selfheal.c dht-rename.c dht-hashfn.c dht-diskusage.c \
	dht-common.c dht-inode-write.c dht-inode-read.c dht-shared.c \
	$(top_builddir)/xlators/lib/src/libxlator.c

dht_la_SOURCES = $(dht_common_source) dht.c

nufa_la_SOURCES = $(dht_common_source) nufa.c
switch_la_SOURCES = $(dht_common_source) switch.c
tier_la_SOURCES = $(dht_common_source) tier.c tier-common.c

dht_la_LDFLAGS = -module -avoid-version -export-symbols \
	$(top_srcdir)/xlators/cluster/dht/src/dht.sym
dht_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

nufa_la_LDFLAGS = -module -avoid-version -export-symbols \
	$(top_srcdir)/xlators/cluster/dht/src/nufa.sym
nufa_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

switch_la_LDFLAGS = -module -avoid-version -export-symbols \
	$(top_srcdir)/xlators/cluster/dht/src/switch.sym
switch_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

tier_la_LDFLAGS = -module -avoid-version -export-symbols \
	$(top_srcdir)/xlators/cluster/dht/src/tier.sym
tier_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = dht-common.h dht-mem-types.h dht-messages.h \
	dht-helper.h tier-common.h tier.h \
	$(top_builddir)/xlators/lib/src/libxlator.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/libglusterfs/src/gfdb \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-I$(top_srcdir)/rpc/rpc-lib/src \
	-I$(top_srcdir)/xlators/lib/src \
	-DDATADIR=\"$(localstatedir)\" \
	-DLIBDIR=\"$(libdir)\" \
	-DLIBGFDB_VERSION=\"$(LIBGFDB_VERSION)\"

CLEANFILES =

EXTRA_DIST = dht.sym nufa.sym switch.sym tier.sym

uninstall-local:
	rm -f $(DESTDIR)$(xlatordir)/distribute.so

install-data-hook:
	ln -sf dht.so $(DESTDIR)$(xlatordir)/distribute.so

if UNITTEST
CLEANFILES += *.gcda *.gcno *_xunit.xml
noinst_PROGRAMS =
TESTS =
endif
