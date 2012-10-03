xlator_PROGRAMS = meta.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/

meta_so_SOURCES = meta.c tree.c misc.c view.c
noinst_HEADERS = meta.h tree.h misc.h view.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall

CLEANFILES = 
