auth_PROGRAMS = login.so
authdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/auth

login_so_SOURCES = login.c
#noinst_HEADERS = alu.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles

CLEANFILES = *~
