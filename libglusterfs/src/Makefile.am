libglusterfs_la_CFLAGS = -fPIC  -Wall -g -shared -nostartfiles $(GF_CFLAGS) $(GF_DARWIN_LIBGLUSTERFS_CFLAGS)

libglusterfs_la_CPPFLAGS = -D_FILE_OFFSET_BITS=64 -D__USE_FILE_OFFSET64 -D_GNU_SOURCE -DXLATORDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator\" -D$(GF_HOST_OS) -I$(CONTRIBDIR)/rbtree  -DSCHEDULERDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/scheduler\" -I$(CONTRIBDIR)/md5

libglusterfs_la_LIBADD = @LEXLIB@

lib_LTLIBRARIES = libglusterfs.la

libglusterfs_la_SOURCES = dict.c graph.lex.c y.tab.c xlator.c logging.c  hashfn.c defaults.c common-utils.c timer.c inode.c call-stub.c compat.c fd.c compat-errno.c event.c mem-pool.c gf-dirent.c syscall.c iobuf.c globals.c statedump.c stack.c checksum.c $(CONTRIBDIR)/md5/md5.c $(CONTRIBDIR)/rbtree/rb.c rbthash.c latency.c graph.c $(CONTRIBDIR)/uuid/clear.c $(CONTRIBDIR)/uuid/copy.c $(CONTRIBDIR)/uuid/gen_uuid.c $(CONTRIBDIR)/uuid/pack.c $(CONTRIBDIR)/uuid/parse.c $(CONTRIBDIR)/uuid/unparse.c $(CONTRIBDIR)/uuid/uuid_time.c $(CONTRIBDIR)/uuid/compare.c $(CONTRIBDIR)/uuid/isnull.c $(CONTRIBDIR)/uuid/unpack.c syncop.c graph-print.c trie.c

noinst_HEADERS = common-utils.h defaults.h dict.h glusterfs.h hashfn.h logging.h  xlator.h  stack.h timer.h list.h inode.h call-stub.h compat.h fd.h revision.h compat-errno.h event.h mem-pool.h byte-order.h gf-dirent.h locking.h syscall.h iobuf.h globals.h statedump.h checksum.h $(CONTRIBDIR)/md5/md5.h $(CONTRIBDIR)/rbtree/rb.h rbthash.h iatt.h latency.h mem-types.h $(CONTRIBDIR)/uuid/uuidd.h $(CONTRIBDIR)/uuid/uuid.h $(CONTRIBDIR)/uuid/uuidP.h $(CONTRIBDIR)/uuid/uuid_types.h syncop.h graph-utils.h graph-mem-types.h trie.h trie-mem-types.h

EXTRA_DIST = graph.l graph.y

graph.lex.c: graph.l y.tab.h
	$(LEX) -t $(srcdir)/graph.l > $@

y.tab.c y.tab.h: graph.y
	$(YACC) -d $(srcdir)/graph.y

CLEANFILES = graph.lex.c y.tab.c y.tab.h
