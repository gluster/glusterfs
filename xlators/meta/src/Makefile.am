xlator_PROGRAMS = meta.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/

meta_so_SOURCES = meta.c tree.c misc.c view.c
noinst_HEADERS = meta.h tree.h misc.h view.h

AM_CFLAGS = $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src

CLEANFILES = 
