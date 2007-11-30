sbin_PROGRAMS = glusterfsd

glusterfsd_SOURCES = glusterfsd.c 
glusterfsd_LDADD = ../../libglusterfs/src/libglusterfs.la
glusterfsd_LDFLAGS = $(GF_HOST_OS_FLAGS) $(GF_SOLARIS_GLUSTERFSD_LDFLAGS)

AM_CFLAGS = -fPIC -Wall

AM_CPPFLAGS = -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=25 -D_GNU_SOURCE \
	-I$(top_srcdir)/libglusterfs/src -DDATADIR=\"$(localstatedir)\" \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\"

CLEANFILES = *~ 

install-data-local:
	$(INSTALL) -d -m 755 $(DESTDIR)$(localstatedir)/run
