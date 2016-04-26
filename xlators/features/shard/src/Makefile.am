xlator_LTLIBRARIES = shard.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

shard_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)

shard_la_SOURCES = shard.c

shard_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la

noinst_HEADERS = shard.h shard-mem-types.h shard-messages.h

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/xdr/src -I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES =
