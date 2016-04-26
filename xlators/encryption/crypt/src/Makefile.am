if ENABLE_CRYPT_XLATOR

xlator_LTLIBRARIES = crypt.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/encryption

crypt_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

crypt_la_SOURCES = keys.c data.c metadata.c atom.c crypt.c
crypt_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	-lssl -lcrypto

noinst_HEADERS = crypt-common.h crypt-mem-types.h crypt.h metadata.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =

else

noinst_DIST = keys.c data.c metadata.c atom.c crypt.c
noinst_HEADERS = crypt-common.h crypt-mem-types.h crypt.h metadata.h

endif
