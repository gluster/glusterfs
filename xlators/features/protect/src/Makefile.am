xlator_LTLIBRARIES = prot_dht.la prot_client.la prot_server.la

xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/features

prot_dht_la_LDFLAGS = -module -avoid-version
prot_dht_la_SOURCES = prot_dht.c
prot_dht_la_LIBADD  = $(top_builddir)/libglusterfs/src/libglusterfs.la

prot_client_la_LDFLAGS = -module -avoid-version
prot_client_la_SOURCES = prot_client.c
prot_client_la_LIBADD  = $(top_builddir)/libglusterfs/src/libglusterfs.la

prot_server_la_LDFLAGS = -module -avoid-version
prot_server_la_SOURCES = prot_server.c
prot_server_la_LIBADD  = $(top_builddir)/libglusterfs/src/libglusterfs.la

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src
AM_CFLAGS = -Wall $(GF_CFLAGS)

CLEANFILES = 

