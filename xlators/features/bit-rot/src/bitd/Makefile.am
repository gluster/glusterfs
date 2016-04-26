xlator_LTLIBRARIES = bit-rot.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

bit_rot_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src/ -I$(top_builddir)/rpc/xdr/src/ \
	-I$(top_srcdir)/rpc/rpc-lib/src -I$(CONTRIBDIR)/timer-wheel \
	-I$(top_srcdir)/xlators/features/bit-rot/src/stub

bit_rot_la_SOURCES = bit-rot.c bit-rot-scrub.c bit-rot-ssm.c \
		     bit-rot-scrub-status.c
bit_rot_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/xlators/features/changelog/lib/src/libgfchangelog.la

noinst_HEADERS = bit-rot.h bit-rot-scrub.h bit-rot-bitd-messages.h bit-rot-ssm.h \
		 bit-rot-scrub-status.h

AM_CFLAGS = -Wall -DBR_RATE_LIMIT_SIGNER $(GF_CFLAGS)

CLEANFILES =
