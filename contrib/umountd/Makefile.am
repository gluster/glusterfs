sbin_PROGRAMS = umountd
umountd_SOURCES = umountd.c
umountd_CFLAGS = $(GF_CFLAGS) -DDATADIR=\"$(localstatedir)\"
umountd_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la ${GF_LDADD}
umountd_LDFLAGS = $(GF_LDFLAGS)

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src
AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
