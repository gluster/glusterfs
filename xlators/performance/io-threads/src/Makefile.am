xlator_LTLIBRARIES = io-threads.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

io_threads_la_LDFLAGS = -module -avoidversion 

io_threads_la_SOURCES = io-threads.c
io_threads_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = io-threads.h iot-mem-types.h

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared $(GF_CFLAGS)

CLEANFILES = 
