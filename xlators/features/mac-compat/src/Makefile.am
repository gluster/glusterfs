xlator_LTLIBRARIES = mac-compat.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

mac_compat_la_LDFLAGS = -module -avoid-version

mac_compat_la_SOURCES = mac-compat.c
mac_compat_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = 

