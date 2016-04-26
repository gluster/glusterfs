xlator_LTLIBRARIES = glupy.la

# Ensure GLUSTER_PYTHON_PATH is passed to glupy.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features
glupydir = $(xlatordir)/glupy

AM_CPPFLAGS = $(PYTHONDEV_CPPFLAGS) $(GF_CPPFLAGS) \
	-I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src \
	-isystem $(BUILD_PYTHON_INC)

AM_CFLAGS = $(PYTHONDEV_CPPFLAGS) -Wall -fno-strict-aliasing \
	-DGLUSTER_PYTHON_PATH=\"$(glupydir)\" \
	-DPATH_GLUSTERFS_GLUPY_MODULE=\"${xlatordir}/glupy${shrext_cmds}\" \
	$(GF_CFLAGS)

# Flags to build glupy.so with
glupy_la_LDFLAGS = $(PYTHONDEV_LDFLAGS) -module -avoid-version \
	-nostartfiles -export-symbols \
	$(top_srcdir)/xlators/features/glupy/src/glupy.sym

glupy_la_SOURCES = glupy.c
glupy_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	-lpthread -l$(BUILD_PYTHON_LIB)

noinst_HEADERS = glupy.h

# Install __init__.py into the Python site-packages area
pyglupydir = @BUILD_PYTHON_SITE_PACKAGES@/gluster
pyglupy_PYTHON = __init__.py

# Install glupy/__init_-.py into the Python site-packages area
SUBDIRS = glupy

CLEANFILES =

EXTRA_DIST = glupy.sym
