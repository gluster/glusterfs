xlator_LTLIBRARIES = rot-13.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/encryption

rot_13_la_LDFLAGS = -module -avoidversion

rot_13_la_SOURCES = rot-13.c
rot_13_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la 

noinst_HEADERS = rot-13.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 
