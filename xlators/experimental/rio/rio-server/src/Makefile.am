xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/experimental
xlator_LTLIBRARIES = rios.la

rios_sources = rio-server-main.c

RIOCOMMON_SRC = $(top_srcdir)/xlators/experimental/rio/rio-common/src
RIOCOMMON_BLD = $(top_builddir)/xlators/experimental/rio/rio-common/src

riocommon_sources = $(RIOCOMMON_SRC)/rio-common-map.c
riocommon_sources += $(RIOCOMMON_SRC)/rio-common-helpers.c
riocommon_sources += $(RIOCOMMON_SRC)/rio-common-init.c
riocommon_sources += $(RIOCOMMON_SRC)/layout.c
riocommon_sources += $(RIOCOMMON_SRC)/layout-static-bucket.c
riocommon_sources += $(RIOCOMMON_SRC)/layout-inodehash-bucket.c

noinst_HEADERS_riocommon = $(RIOCOMMON_BLD)/rio-common.h \
        $(RIOCOMMON_BLD)/rio-common.h \
        $(RIOCOMMON_BLD)/rio-common-helpers.h \
        $(RIOCOMMON_BLD)/rio-mem-types.h \
        $(RIOCOMMON_BLD)/rio-messages.h \
        $(RIOCOMMON_BLD)/layout.h

noinst_HEADERS = $(top_builddir)/xlators/lib/src/libxlator.h \
        $(noinst_HEADERS_riocommon)

rios_la_SOURCES = $(rios_sources) $(riocommon_sources)
rios_la_LDFLAGS = -module -avoid-version
rios_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CFLAGS = -DRIO_DEBUG -Wall $(GF_CFLAGS)

AM_CPPFLAGS = $(GF_CPPFLAGS)
AM_CPPFLAGS += -I$(RIOCOMMON_SRC)
AM_CPPFLAGS += -I$(top_srcdir)/libglusterfs/src
AM_CPPFLAGS += -I$(top_srcdir)/rpc/xdr/src
AM_CPPFLAGS += -I$(top_builddir)/rpc/xdr/src
AM_CPPFLAGS += -I$(top_srcdir)/xlators/lib/src

CLEANFILES =

uninstall-local:
	rm -f $(DESTDIR)$(xlatordir)/rios.so
