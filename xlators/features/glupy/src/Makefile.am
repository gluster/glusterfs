xlator_LTLIBRARIES = glupy.la

# Ensure GLUSTER_PYTHON_PATH is passed to glupy.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features
glupydir = $(xlatordir)/glupy
AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src -isystem $(BUILD_PYTHON_INC)
AM_CFLAGS = -Wall -fno-strict-aliasing -DGLUSTER_PYTHON_PATH=\"$(glupydir)\" $(GF_CFLAGS)

# Flags to build glupy.so with
glupy_la_LDFLAGS = -module -avoid-version -shared -nostartfiles
glupy_la_SOURCES = glupy.c
glupy_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	 -lpthread -l$(BUILD_PYTHON_LIB)

noinst_HEADERS = glupy.h

# Install glupy.py into the Python site-packages area
pyglupydir = $(pythondir)/gluster
pyglupy_PYTHON = glupy.py

CLEANFILES =
