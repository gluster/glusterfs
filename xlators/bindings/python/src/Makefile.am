
xlator_PROGRAMS = python.so

xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/bindings

python_PYTHON = gluster.py glustertypes.py glusterstack.py

pythondir = $(xlatordir)/python

python_so_SOURCES = python.c

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles \
	$(PYTHON_CPPLAGS) -DGLUSTER_PYTHON_PATH=\"$(pythondir)\"

AM_LDFLAGS = $(PYTHON_LDFLAGS)

CLEANFILES = 

