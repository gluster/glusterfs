xlator_LTLIBRARIES = changelog.la

xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

noinst_HEADERS = changelog-helpers.h changelog-mem-types.h changelog-rt.h \
	changelog-misc.h changelog-encoders.h changelog-notifier.h

changelog_la_LDFLAGS = -module -avoid-version

changelog_la_SOURCES = changelog.c changelog-rt.c changelog-helpers.c \
	changelog-encoders.c changelog-notifier.c
changelog_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src -fPIC -D_FILE_OFFSET_BITS=64 \
	-D_GNU_SOURCE -D$(GF_HOST_OS) -shared -nostartfiles -DDATADIR=\"$(localstatedir)\"

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
