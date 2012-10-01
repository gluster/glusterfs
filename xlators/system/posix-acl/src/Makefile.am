xlator_LTLIBRARIES = posix-acl.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/system
posix_acl_la_LDFLAGS = -module -avoidversion
posix_acl_la_SOURCES = posix-acl.c posix-acl-xattr.c
posix_acl_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = posix-acl.h posix-acl-xattr.h

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)\
	-L$(xlatordir)/

CLEANFILES =

access-control-compat:
	mkdir -p $(DESTDIR)$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features
	rm -rf $(DESTDIR)$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features/access-control.so
	ln -s ../system/posix-acl.so $(DESTDIR)$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features/access-control.so


install-exec-local: access-control-compat
