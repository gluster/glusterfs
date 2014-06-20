# The bits needed for glfsxmp
EXTRA_PROGRAMS = glfsxmp
glfsxmp_SOURCES = glfsxmp.c
glfsxmp_CFLAGS = $(GLFS_CFLAGS) -Wall
glfsxmp_LDADD = $(GLFS_LIBS) -lrt

# Install __init__.py (a generated file), and gfapi.py into
# the Python site-packages area
pygfapidir = $(pythondir)/gluster
pygfapi_PYTHON = __init__.py gfapi.py
