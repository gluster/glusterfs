auth_PROGRAMS = ip.so
authdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/auth

ip_so_SOURCES = ip.c
#noinst_HEADERS = alu.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~
