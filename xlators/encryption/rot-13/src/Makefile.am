xlator_LTLIBRARIES = rot-13.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/encryption

rot_13_la_LDFLAGS = -module -avoidversion

rot_13_la_SOURCES = rot-13.c
rot_13_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la 

noinst_HEADERS = rot-13.h

AM_CFLAGS = $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src $(GF_CFLAGS)

CLEANFILES = 
