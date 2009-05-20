xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/performance

ldpreload_PROGRAMS = glusterfs-booster.so
ldpreloaddir = $(libdir)/glusterfs/
glusterfs_booster_so_SOURCES = booster.c booster_stat.c booster_fstab.c
glusterfs_booster_so_CFLAGS = -I$(top_srcdir)/libglusterfsclient/src/ -D_GNU_SOURCE -D$(GF_HOST_OS) -fPIC -Wall \
	-pthread $(GF_BOOSTER_CFLAGS)
glusterfs_booster_so_CPPFLAGS = -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE  \
	-I$(top_srcdir)/libglusterfsclient/src \
	-I$(top_srcdir)/libglusterfs/src -DDATADIR=\"$(localstatedir)\" \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\"
glusterfs_booster_so_LDFLAGS = -shared -nostartfiles
glusterfs_booster_so_LDADD = -L$(top_builddir)/libglusterfs/src -lglusterfs \
	-L$(top_builddir)/libglusterfsclient/src -lglusterfsclient  

CLEANFILES = 

