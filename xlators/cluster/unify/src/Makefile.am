
xlator_PROGRAMS = unify.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/cluster

unify_so_SOURCES = unify.c unify-self-heal.c
noinst_HEADERS = unify.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_DARWIN_BUNDLE_CFLAGS)

CLEANFILES = *~

