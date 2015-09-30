xlator_LTLIBRARIES = md-cache.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

md_cache_la_LDFLAGS = -module -avoid-version

md_cache_la_SOURCES = md-cache.c
md_cache_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = md-cache-mem-types.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(CONTRIBDIR)/rbtree

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =


stat-prefetch-compat:
	mkdir -p $(DESTDIR)$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance
	rm -rf $(DESTDIR)$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance/stat-prefetch.so
	ln -s ./md-cache.so $(DESTDIR)$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance/stat-prefetch.so


install-exec-local: stat-prefetch-compat
