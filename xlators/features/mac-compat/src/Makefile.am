xlator_LTLIBRARIES = mac-compat.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

mac_compat_la_LDFLAGS = -module -avoidversion

mac_compat_la_SOURCES = mac-compat.c
mac_compat_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CFLAGS = $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src $(GF_CFLAGS)

CLEANFILES = 

