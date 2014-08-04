xlator_LTLIBRARIES = ec.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/cluster

ec_sources := ec.c
ec_sources += ec-data.c
ec_sources += ec-helpers.c
ec_sources += ec-common.c
ec_sources += ec-generic.c
ec_sources += ec-locks.c
ec_sources += ec-dir-read.c
ec_sources += ec-dir-write.c
ec_sources += ec-inode-read.c
ec_sources += ec-inode-write.c
ec_sources += ec-combine.c
ec_sources += ec-gf.c
ec_sources += ec-method.c
ec_sources += ec-heal.c

ec_headers := ec.h
ec_headers += ec-mem-types.h
ec_headers += ec-helpers.h
ec_headers += ec-data.h
ec_headers += ec-fops.h
ec_headers += ec-common.h
ec_headers += ec-combine.h
ec_headers += ec-gf.h
ec_headers += ec-method.h

ec_ext_sources = $(top_builddir)/xlators/lib/src/libxlator.c

ec_ext_headers = $(top_builddir)/xlators/lib/src/libxlator.h

ec_la_LDFLAGS = -module -avoid-version
ec_la_SOURCES = $(ec_sources) $(ec_headers) $(ec_ext_sources) $(ec_ext_headers)
ec_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CPPFLAGS  = $(GF_CPPFLAGS)
AM_CPPFLAGS += -I$(top_srcdir)/libglusterfs/src
AM_CPPFLAGS += -I$(top_srcdir)/xlators/lib/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =

install-data-hook:
	ln -sf ec.so $(DESTDIR)$(xlatordir)/disperse.so

uninstall-local:
	rm -f $(DESTDIR)$(xlatordir)/disperse.so
