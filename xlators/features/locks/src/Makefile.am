xlator_LTLIBRARIES = locks.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

locks_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

locks_la_SOURCES = common.c posix.c entrylk.c inodelk.c reservelk.c \
	clear.c

locks_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = locks.h common.h locks-mem-types.h clear.h pl-messages.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src


AM_CFLAGS = -Wall -fno-strict-aliasing $(GF_CFLAGS)

CLEANFILES = 

uninstall-local:
	rm -f $(DESTDIR)$(xlatordir)/posix-locks.so

install-data-hook:
	ln -sf locks.so $(DESTDIR)$(xlatordir)/posix-locks.so
