xlator_LTLIBRARIES = stripe.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/cluster

stripe_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)


stripe_la_SOURCES = stripe.c stripe-helpers.c \
	$(top_builddir)/xlators/lib/src/libxlator.c

stripe_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = stripe.h stripe-mem-types.h \
	$(top_builddir)/xlators/lib/src/libxlator.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/xlators/lib/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = 

