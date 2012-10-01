auth_LTLIBRARIES = login.la
authdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/auth

login_la_LDFLAGS = -module -avoidversion

login_la_SOURCES = login.c
login_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CFLAGS = -fPIC $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS) \
	-I$(top_srcdir)/xlators/protocol/server/src
