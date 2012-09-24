xlator_LTLIBRARIES = write-behind.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

write_behind_la_LDFLAGS = -module -avoid-version -shared 

write_behind_la_SOURCES = write-behind.c
write_behind_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = write-behind-mem-types.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)

CLEANFILES = 
