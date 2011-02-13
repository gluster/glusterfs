xlator_LTLIBRARIES = glusterd.la
xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/mgmt
glusterd_la_LDFLAGS = -module -avoidversion
glusterd_la_SOURCES = glusterd.c glusterd-handler.c glusterd-sm.c glusterd-op-sm.c \
	glusterd-utils.c glusterd3_1-mops.c glusterd-store.c glusterd-handshake.c \
	glusterd-pmap.c glusterd-volgen.c glusterd-rebalance.c

glusterd_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la\
		     $(top_builddir)/rpc/xdr/src/libgfxdr.la\
		     $(top_builddir)/rpc/rpc-lib/src/libgfrpc.la

noinst_HEADERS = glusterd.h glusterd-utils.h glusterd-op-sm.h glusterd-sm.h \
	glusterd-store.h glusterd-mem-types.h glusterd-pmap.h glusterd-volgen.h

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
	-I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)\
	-I$(rpclibdir) -L$(xlatordir)/ -I$(CONTRIBDIR)/rbtree -I$(top_srcdir)/rpc/xdr/src\
	-I$(top_srcdir)/rpc/rpc-lib/src -I$(CONTRIBDIR)/uuid -DGFS_PREFIX=\"$(prefix)\" \
	-DDATADIR=\"$(localstatedir)\" -DGSYNC_PREFIX=\"$(libexecdir)\"


CLEANFILES =
