xlator_LTLIBRARIES = meta.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator

meta_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

meta_la_SOURCES = meta.c meta-helpers.c meta-defaults.c \
	root-dir.c \
	graphs-dir.c \
	frames-file.c \
	graph-dir.c \
	active-link.c \
	xlator-dir.c \
	top-link.c \
	logging-dir.c \
	logfile-link.c \
	loglevel-file.c \
	process_uuid-file.c \
	volfile-file.c \
	view-dir.c \
	subvolumes-dir.c \
	subvolume-link.c \
	type-file.c \
	version-file.c \
	options-dir.c \
	option-file.c \
	cmdline-file.c \
	name-file.c \
	private-file.c \
	history-file.c \
	mallinfo-file.c \
	meminfo-file.c \
	measure-file.c \
	profile-file.c

meta_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = meta.h meta-hooks.h meta-mem-types.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
