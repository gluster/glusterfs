xlator_LTLIBRARIES = md-cache.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

md_cache_la_LDFLAGS = -module -avoidversion

md_cache_la_SOURCES = md-cache.c
md_cache_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = md-cache-mem-types.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -I$(CONTRIBDIR)/rbtree -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES =


stat-prefetch-compat:
	mkdir -p $(DESTDIR)$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance
	rm -rf $(DESTDIR)$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance/stat-prefetch.so
	ln -s ./md-cache.so $(DESTDIR)$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance/stat-prefetch.so


install-exec-local: stat-prefetch-compat
