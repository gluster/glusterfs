sbin_PROGRAMS = glusterfsd

glusterfsd_SOURCES = glusterfsd.c 
glusterfsd_LDADD = -L../../libglusterfs/src -lglusterfs -ldl -lm #-libverbs

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=25 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -DDATADIR=\"$(localstatedir)\" \
	-DCONFDIR=\"$(sysconfdir)/glusterfs\"

CLEANFILES = *~ 
