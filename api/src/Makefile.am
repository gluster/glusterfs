lib_LTLIBRARIES = libgfapi.la
noinst_HEADERS = glfs-mem-types.h glfs-internal.h gfapi-messages.h
libgfapi_HEADERS = glfs.h glfs-handles.h
libgfapidir = $(includedir)/glusterfs/api

EXTRA_DIST = gfapi.map gfapi.aliases

libgfapi_la_SOURCES = glfs.c glfs-mgmt.c glfs-fops.c glfs-resolve.c \
	glfs-handleops.c
libgfapi_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la \
	$(top_builddir)/rpc/xdr/src/libgfxdr.la \
	$(GF_LDADD)

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src \
        -I$(top_srcdir)/rpc/rpc-lib/src \
        -I$(top_srcdir)/rpc/xdr/src \
        -I$(top_builddir)/rpc/xdr/src \
        -DDATADIR=\"$(localstatedir)\" \
        -D__USE_FILE_OFFSET64

AM_CFLAGS = -Wall $(GF_CFLAGS)

libgfapi_la_LDFLAGS = -version-info $(GFAPI_LT_VERSION) \
	$(GFAPI_EXTRA_LDFLAGS) $(ACL_LIBS)

xlator_LTLIBRARIES = api.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/mount
# workaround for broken parallel install support in automake with LTLIBRARIES
# http://debbugs.gnu.org/cgi/bugreport.cgi?bug=7328
install_xlatorLTLIBRARIES = install-xlatorLTLIBRARIES
$(install_xlatorLTLIBRARIES): install-libLTLIBRARIES

api_la_SOURCES = glfs-master.c
api_la_DEPENDENCIES = libgfapi.la
api_la_LDFLAGS = -module $(GF_XLATOR_DEFAULT_LDFLAGS)
api_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la \
	$(top_builddir)/rpc/rpc-lib/src/libgfrpc.la \
	$(top_builddir)/rpc/xdr/src/libgfxdr.la \
	$(top_builddir)/api/src/libgfapi.la

