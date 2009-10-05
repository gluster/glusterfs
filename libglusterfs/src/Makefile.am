libglusterfs_la_CFLAGS = -fPIC  -Wall -g -shared -nostartfiles $(GF_CFLAGS) $(GF_DARWIN_LIBGLUSTERFS_CFLAGS)

libglusterfs_la_CPPFLAGS = -D_FILE_OFFSET_BITS=64 -D__USE_FILE_OFFSET64 -D_GNU_SOURCE -DXLATORDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator\" -DSCHEDULERDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/scheduler\" -DTRANSPORTDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/transport\" -D$(GF_HOST_OS) -DLIBDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/auth\" -I$(CONTRIBDIR)/rbtree

libglusterfs_la_LIBADD = @LEXLIB@

lib_LTLIBRARIES = libglusterfs.la

libglusterfs_la_SOURCES = dict.c spec.lex.c y.tab.c xlator.c logging.c  hashfn.c defaults.c scheduler.c common-utils.c transport.c timer.c inode.c call-stub.c compat.c authenticate.c fd.c compat-errno.c event.c mem-pool.c gf-dirent.c syscall.c iobuf.c globals.c statedump.c stack.c checksum.c md5.c $(CONTRIBDIR)/rbtree/rb.c rbthash.c

noinst_HEADERS = common-utils.h defaults.h dict.h glusterfs.h hashfn.h logging.h protocol.h scheduler.h xlator.h transport.h stack.h timer.h list.h inode.h call-stub.h compat.h authenticate.h fd.h revision.h compat-errno.h event.h mem-pool.h byte-order.h gf-dirent.h locking.h syscall.h iobuf.h globals.h statedump.h checksum.h md5.h $(CONTRIBDIR)/rbtree/rb.h rbthash.h

EXTRA_DIST = spec.l spec.y

spec.lex.c: spec.l y.tab.h
	$(LEX) -t $(srcdir)/spec.l > $@

y.tab.c y.tab.h: spec.y
	$(YACC) -d $(srcdir)/spec.y

CLEANFILES = spec.lex.c y.tab.c y.tab.h
