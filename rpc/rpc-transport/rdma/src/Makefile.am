# TODO : need to change transportdir

transport_LTLIBRARIES = rdma.la
transportdir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/rpc-transport

rdma_la_LDFLAGS = -module -avoid-version -nostartfiles

rdma_la_SOURCES = rdma.c name.c
rdma_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la     \
	-libverbs -lrdmacm

noinst_HEADERS = rdma.h name.h rpc-trans-rdma-messages.h

AM_CPPFLAGS = $(GF_CPPFLAGS) \
	-I$(top_srcdir)/libglusterfs/src \
	-I$(top_srcdir)/rpc/rpc-lib/src/ \
	-I$(top_srcdir)/rpc/xdr/src \
	-I$(top_builddir)/rpc/xdr/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = *~
