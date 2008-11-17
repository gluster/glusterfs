
xlator_LTLIBRARIES = nufa.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/cluster

nufa_la_LDFLAGS = -module -avoidversion

nufa_la_SOURCES = nufa.c nufa-layout.c nufa-helper.c nufa-hashfn.c nufa-linkfile.c \
		nufa-selfheal.c nufa-rename.c
nufa_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = nufa.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 

