noinst_HEADERS = ib-verbs.h name.h

transport_LTLIBRARIES = ib-verbs.la
transportdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/transport/

ib_verbs_la_LDFLAGS = -module -avoidversion

ib_verbs_la_SOURCES = ib-verbs.c name.c
ib_verbs_la_LIBADD = -libverbs $(top_builddir)/libglusterfs/src/libglusterfs.la 

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -I$(top_srcdir)/transport/ib-verbs \
	-shared -nostartfiles

CLEANFILES = *~
