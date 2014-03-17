libglusterfs_la_CFLAGS = -Wall $(GF_CFLAGS) \
	$(GF_DARWIN_LIBGLUSTERFS_CFLAGS)    \
        -DDATADIR=\"$(localstatedir)\"

libglusterfs_la_CPPFLAGS = $(GF_CPPFLAGS) -D__USE_FILE_OFFSET64 \
	-DXLATORDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator\" \
	-I$(top_srcdir)/rpc/rpc-lib/src/ -I$(CONTRIBDIR)/rbtree

libglusterfs_la_LIBADD = @LEXLIB@
libglusterfs_la_LDFLAGS = -version-info $(LIBGLUSTERFS_LT_VERSION)

lib_LTLIBRARIES = libglusterfs.la

CONTRIB_BUILDDIR = $(top_builddir)/contrib

libglusterfs_la_SOURCES = dict.c xlator.c logging.c \
	hashfn.c defaults.c common-utils.c timer.c inode.c call-stub.c \
	compat.c fd.c compat-errno.c event.c mem-pool.c gf-dirent.c syscall.c \
	iobuf.c globals.c statedump.c stack.c checksum.c daemon.c timespec.c \
	$(CONTRIBDIR)/rbtree/rb.c rbthash.c store.c latency.c \
	graph.c $(CONTRIBDIR)/uuid/clear.c $(CONTRIBDIR)/uuid/copy.c \
	$(CONTRIBDIR)/uuid/gen_uuid.c $(CONTRIBDIR)/uuid/pack.c \
	$(CONTRIBDIR)/uuid/parse.c $(CONTRIBDIR)/uuid/unparse.c \
	$(CONTRIBDIR)/uuid/uuid_time.c $(CONTRIBDIR)/uuid/compare.c \
	$(CONTRIBDIR)/uuid/isnull.c $(CONTRIBDIR)/uuid/unpack.c syncop.c \
	graph-print.c trie.c run.c options.c fd-lk.c circ-buff.c \
	event-history.c gidcache.c ctx.c client_t.c event-poll.c event-epoll.c \
	$(CONTRIBDIR)/libgen/basename_r.c $(CONTRIBDIR)/libgen/dirname_r.c \
	$(CONTRIBDIR)/stdlib/gf_mkostemp.c


nodist_libglusterfs_la_SOURCES = y.tab.c graph.lex.c gf-error-codes.h

BUILT_SOURCES = graph.lex.c

noinst_HEADERS = common-utils.h defaults.h dict.h glusterfs.h hashfn.h timespec.h \
	logging.h xlator.h stack.h timer.h list.h inode.h call-stub.h compat.h \
	fd.h revision.h compat-errno.h event.h mem-pool.h byte-order.h \
	gf-dirent.h locking.h syscall.h iobuf.h globals.h statedump.h \
	checksum.h daemon.h $(CONTRIBDIR)/rbtree/rb.h store.h\
	rbthash.h iatt.h latency.h mem-types.h $(CONTRIBDIR)/uuid/uuidd.h \
	$(CONTRIBDIR)/uuid/uuid.h $(CONTRIBDIR)/uuid/uuidP.h \
	$(CONTRIB_BUILDDIR)/uuid/uuid_types.h syncop.h graph-utils.h trie.h \
	run.h options.h lkowner.h fd-lk.h circ-buff.h event-history.h \
	gidcache.h client_t.h glusterfs-acl.h

EXTRA_DIST = graph.l graph.y

graph.lex.c: graph.l y.tab.h
	$(LEX) -Pgraphyy -t $(srcdir)/graph.l > $@

y.tab.c: y.tab.h
y.tab.h: graph.y
	$(YACC) -p graphyy -d $(srcdir)/graph.y

CLEANFILES = graph.lex.c y.tab.c y.tab.h
CONFIG_CLEAN_FILES = $(CONTRIB_BUILDDIR)/uuid/uuid_types.h
