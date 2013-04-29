xlator_LTLIBRARIES = glupy.la

xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

glupydir = $(xlatordir)/glupy

glupy_PYTHON = gluster.py negative.py helloworld.py debug-trace.py

glupy_la_LDFLAGS = -module -avoid-version -shared -nostartfiles
glupy_la_SOURCES = glupy.c
glupy_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	 -lpthread -lpython2.6

noinst_HEADERS = glupy.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall -fno-strict-aliasing -DGLUSTER_PYTHON_PATH=\"$(glupydir)\" $(GF_CFLAGS)

CLEANFILES =
