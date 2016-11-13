xlator_LTLIBRARIES = posix-acl.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/system
posix_acl_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)
posix_acl_la_SOURCES = posix-acl.c posix-acl-xattr.c
posix_acl_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = posix-acl.h posix-acl-xattr.h posix-acl-mem-types.h \
		 posix-acl-messages.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

AM_LDFLAGS = -L$(xlatordir)

CLEANFILES =

access-control-compat:
	mkdir -p $(DESTDIR)$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features
	rm -rf $(DESTDIR)$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features/access-control.so
	ln -s ../system/posix-acl.so $(DESTDIR)$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features/access-control.so


install-exec-local: access-control-compat

uninstall-local:
	rm -f $(DESTDIR)$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features/access-control.so
