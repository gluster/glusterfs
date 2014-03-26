xlator_LTLIBRARIES = meta.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator

meta_la_LDFLAGS = -module -avoid-version

meta_la_SOURCES = meta.c meta-helpers.c meta-defaults.c \
	plugins/root-dir.c \
	plugins/graphs-dir.c \
	plugins/frames-file.c \
	plugins/graph-dir.c \
	plugins/active-link.c \
	plugins/xlator-dir.c \
	plugins/top-link.c \
	plugins/logging-dir.c \
	plugins/logfile-link.c \
	plugins/loglevel-file.c \
	plugins/process_uuid-file.c \
	plugins/volfile-file.c \
	plugins/view-dir.c \
	plugins/subvolumes-dir.c \
	plugins/subvolume-link.c \
	plugins/type-file.c \
	plugins/version-file.c \
	plugins/options-dir.c \
	plugins/option-file.c \
	plugins/cmdline-file.c \
	plugins/name-file.c

meta_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = meta.h meta-hooks.h meta-mem-types.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
