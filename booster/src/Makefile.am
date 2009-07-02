ldpreload_LTLIBRARIES = libglusterfs-booster.la
ldpreloaddir = $(libdir)/glusterfs
noinst_HEADERS = booster_fstab.h booster-fd.h
libglusterfs_booster_la_SOURCES = booster.c booster_stat.c booster_fstab.c booster-fd.c
libglusterfs_booster_la_CFLAGS = -I$(top_srcdir)/libglusterfsclient/src/ -D_GNU_SOURCE -D$(GF_HOST_OS) -fPIC -Wall \
	-pthread $(GF_BOOSTER_CFLAGS) -shared -nostartfiles
libglusterfs_booster_la_CPPFLAGS = -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE  \
	-I$(top_srcdir)/libglusterfsclient/src \
	-I$(top_srcdir)/libglusterfs/src -DDATADIR=\"$(localstatedir)\" \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\"

libglusterfs_booster_la_LDFLAGS = -module -avoidversion
libglusterfs_booster_la_LIBADD =  $(top_builddir)/libglusterfs/src/libglusterfs.la $(top_builddir)/libglusterfsclient/src/libglusterfsclient.la

CLEANFILES =

uninstall-local:
	rm -f $(DESTDIR)$(ldpreloaddir)/glusterfs-booster.so

install-data-hook:
	ln -sf libglusterfs-booster.so $(DESTDIR)$(ldpreloaddir)/glusterfs-booster.so
