
SUBDIRS = stub

xlator_LTLIBRARIES = bit-rot.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

bit_rot_la_LDFLAGS = -module -avoid-version

bit_rot_la_SOURCES = bit-rot.c
bit_rot_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = bit-rot.h bit-rot-mem-types.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
