xlator_LTLIBRARIES = glupy.la

# Ensure GLUSTER_PYTHON_PATH is passed to glupy.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features
glupydir = $(xlatordir)/glupy

AM_CPPFLAGS = $(GF_CPPFLAGS) \
	-I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall -fno-strict-aliasing \
	-DGLUSTER_PYTHON_PATH=\"$(glupydir)\" \
	-DPATH_GLUSTERFS_GLUPY_MODULE=\"${xlatordir}/glupy${shrext_cmds}\" \
	$(GF_CFLAGS) $(PYTHON_CFLAGS)

# Flags to build glupy.so with
glupy_la_LDFLAGS = -module -nostartfiles \
        -export-symbols $(top_srcdir)/xlators/features/glupy/src/glupy.sym \
        $(GF_XLATOR_LDFLAGS) $(PYTHON_LIBS)

glupy_la_SOURCES = glupy.c
glupy_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	-lpthread $(LIB_DL)

noinst_HEADERS = glupy.h

# Install __init__.py into the Python site-packages area
pyglupydir = @BUILD_PYTHON_SITE_PACKAGES@/gluster
pyglupy_PYTHON = __init__.py

# Install glupy/__init_-.py into the Python site-packages area
SUBDIRS = glupy

CLEANFILES =

EXTRA_DIST = glupy.sym
