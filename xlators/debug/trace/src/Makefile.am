
xlator_LTLIBRARIES = trace.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/debug

trace_la_LDFLAGS = -module -avoidversion

trace_la_SOURCES = trace.c
trace_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 

