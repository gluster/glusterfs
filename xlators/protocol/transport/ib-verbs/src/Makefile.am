# TODO : need to change transportdir

transport_LTLIBRARIES = ib-verbs.la
transportdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/transport

ib_verbs_la_LDFLAGS = -module -avoidversion

ib_verbs_la_SOURCES = ib-verbs.c name.c
ib_verbs_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la     \
	-libverbs $(top_builddir)/xlators/protocol/lib/src/libgfproto.la

noinst_HEADERS = ib-verbs.h name.h ib-verbs-mem-types.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)  \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)   \
	-I$(top_srcdir)/xlators/protocol/transport/ib-verbs                    \
	-I$(top_srcdir)/xlators/protocol/lib/src

CLEANFILES = *~
