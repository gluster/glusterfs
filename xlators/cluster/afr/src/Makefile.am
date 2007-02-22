xlator_PROGRAMS = afr.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/cluster

afr_so_SOURCES = afr.c
noinst_HEADERS = afr.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	            -I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~

