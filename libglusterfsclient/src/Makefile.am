lib_LTLIBRARIES = libglusterfsclient.la
noinst_HEADERS = libglusterfsclient-internals.h
libglusterfsclient_HEADERS = libglusterfsclient.h 
libglusterfsclientdir = $(includedir)

libglusterfsclient_la_SOURCES = libglusterfsclient.c libglusterfsclient-dentry.c
libglusterfsclient_la_CFLAGS =  -fPIC -Wall -pthread 
libglusterfsclient_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la
libglusterfsclient_la_CPPFLAGS = -D_FILE_OFFSET_BITS=64 -D$(GF_HOST_OS) -D__USE_FILE_OFFSET64 -D_GNU_SOURCE -I$(top_srcdir)/libglusterfs/src -DDATADIR=\"$(localstatedir)\" -DCONFDIR=\"$(sysconfdir)/glusterfs\" $(GF_CFLAGS)
libglusterfsclient_la_LDFLAGS = -shared -nostartfiles

CLEANFILES = 

$(top_builddir)/libglusterfs/src/libglusterfs.la:
	$(MAKE) -C $(top_builddir)/libglusterfs/src/ all

