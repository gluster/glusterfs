sbin_PROGRAMS = glusterfs

glusterfs_SOURCES = glusterfs.c fetch-spec.c
glusterfs_LDADD = ../../libglusterfs/src/libglusterfs.la
glusterfs_LDFLAGS = $(GF_HOST_OS_FLAGS) $(GF_SOLARIS_GLUSTERFSD_LDFLAGS)

noinst_HEADERS = glusterfsd.h

AM_CFLAGS = -fPIC -Wall -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -DDATADIR=\"$(localstatedir)\" \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\"

CLEANFILES = *~ 

install-data-local:
	$(INSTALL) -d -m 755 $(DESTDIR)$(localstatedir)/run
	$(INSTALL) -d -m 755 $(DESTDIR)$(localstatedir)/log/glusterfs
	rm -f $(DESTDIR)$(sbindir)/glusterfsd
	ln -s glusterfs $(DESTDIR)$(sbindir)/glusterfsd
