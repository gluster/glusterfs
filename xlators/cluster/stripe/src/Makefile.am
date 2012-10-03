
xlator_LTLIBRARIES = stripe.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/cluster

stripe_la_LDFLAGS = -module -avoidversion

stripe_la_SOURCES = stripe.c stripe-helpers.c \
	$(top_builddir)/xlators/lib/src/libxlator.c

stripe_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = stripe.h stripe-mem-types.h $(top_builddir)/xlators/lib/src/libxlator.h

AM_CFLAGS = $(GF_CPPFLAGS) -Wall \
	-I$(top_srcdir)/libglusterfs/src $(GF_CFLAGS) \
	-I$(top_srcdir)/xlators/lib/src

CLEANFILES = 

