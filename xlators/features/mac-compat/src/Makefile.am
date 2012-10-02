xlator_LTLIBRARIES = mac-compat.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

mac_compat_la_LDFLAGS = -module -avoidversion

mac_compat_la_SOURCES = mac-compat.c
mac_compat_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared $(GF_CFLAGS)

CLEANFILES = 

