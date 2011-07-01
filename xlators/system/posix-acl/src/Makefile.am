xlator_LTLIBRARIES = posix-acl.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/system
posix_acl_la_LDFLAGS = -module -avoidversion
posix_acl_la_SOURCES = posix-acl.c posix-acl-xattr.c
posix_acl_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = posix-acl.h posix-acl-xattr.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)\
	-L$(xlatordir)/

CLEANFILES =
