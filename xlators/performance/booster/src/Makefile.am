xlator_PROGRAMS = booster.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

booster_so_SOURCES = booster.c

booster_so_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles -D$(GF_HOST_OS)
booster_so_LDFLAGS = -shared

noinst_HEADERS = booster.h

bin_PROGRAMS = glusterfs-booster.so
glusterfs_booster_so_SOURCES = entry.c bridge.c
glusterfs_booster_so_CFLAGS = -D_GNU_SOURCE -D$(GF_HOST_OS) -fPIC -Wall -pthread
glusterfs_booster_so_CPPFLAGS = -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE  \
	-I$(top_srcdir)/libglusterfs/src -DDATADIR=\"$(localstatedir)\" \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\"
glusterfs_booster_so_LDFLAGS = -shared -nostartfiles
glusterfs_booster_so_LDADD = -L$(top_builddir)/libglusterfs/src -lglusterfs


CLEANFILES = *~

