xlator_PROGRAMS = posix-locks.so
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

posix_locks_so_SOURCES = posix-locks.c
noinst_HEADERS = posix-locks.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS) \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_DARWIN_BUNDLE_CFLAGS)

CLEANFILES = *~

