xlator_LTLIBRARIES = mac-compat.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

mac_compat_la_LDFLAGS = -module -avoid-version -shared

mac_compat_la_SOURCES = mac-compat.c
mac_compat_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 

