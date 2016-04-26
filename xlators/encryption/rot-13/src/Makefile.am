xlator_LTLIBRARIES = rot-13.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/encryption

rot_13_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

rot_13_la_SOURCES = rot-13.c
rot_13_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la 

noinst_HEADERS = rot-13.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = 
