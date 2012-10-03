xlator_LTLIBRARIES = write-behind.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

write_behind_la_LDFLAGS = -module -avoidversion 

write_behind_la_SOURCES = write-behind.c
write_behind_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = write-behind-mem-types.h

AM_CFLAGS = $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src $(GF_CFLAGS)

CLEANFILES = 
