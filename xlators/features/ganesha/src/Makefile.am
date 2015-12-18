xlator_LTLIBRARIES = ganesha.la

xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

noinst_HEADERS = ganesha.h ganesha-mem-types.h

ganesha_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

ganesha_la_SOURCES = ganesha.c

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
        -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D$(GF_HOST_OS)\
        -DGANESHA_DIR=\"$(sysconfdir)/ganesha\" \
        -DGYSNCD_PREFIX=\"$(libexecdir)/glusterfs\"

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
