xlator_LTLIBRARIES = read-only.la worm.la

xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

noinst_HEADERS = read-only-common.h

read_only_la_LDFLAGS = -module -avoidversion

read_only_la_SOURCES = read-only.c
read_only_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la 

worm_la_LDFLAGS = -module -avoidversion

worm_la_SOURCES = read-only-common.c worm.c
worm_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 

