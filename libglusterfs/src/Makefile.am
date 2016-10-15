noinst_PYTHON = generator.py gen-defaults.py $(top_srcdir)/events/eventskeygen.py

libglusterfs_la_CFLAGS = $(GF_CFLAGS) $(GF_DARWIN_LIBGLUSTERFS_CFLAGS) \
	-DDATADIR=\"$(localstatedir)\"

libglusterfs_la_CPPFLAGS = $(GF_CPPFLAGS) -D__USE_FILE_OFFSET64 \
	-DXLATORDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator\" \
	-DXLATORPARENTDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)\" \
	-I$(top_srcdir)/rpc/xdr/src/ -I$(top_builddir)/rpc/xdr/src/ \
	-I$(top_srcdir)/rpc/rpc-lib/src/ -I$(CONTRIBDIR)/rbtree \
	-I$(CONTRIBDIR)/libexecinfo ${ARGP_STANDALONE_CPPFLAGS} \
	-DSBIN_DIR=\"$(sbindir)\" -I$(CONTRIBDIR)/timer-wheel

libglusterfs_la_LIBADD = @LEXLIB@ $(ZLIB_LIBS) $(MATH_LIB) $(UUID_LIBS)
libglusterfs_la_LDFLAGS = -version-info $(LIBGLUSTERFS_LT_VERSION)

lib_LTLIBRARIES = libglusterfs.la
libgfchangelogdir = $(includedir)/glusterfs/gfchangelog

CONTRIB_BUILDDIR = $(top_builddir)/contrib

libglusterfs_la_SOURCES = dict.c xlator.c logging.c \
	hashfn.c common-utils.c timer.c inode.c call-stub.c \
	compat.c fd.c compat-errno.c event.c mem-pool.c gf-dirent.c syscall.c \
	iobuf.c globals.c statedump.c stack.c checksum.c daemon.c timespec.c \
	$(CONTRIBDIR)/rbtree/rb.c rbthash.c store.c latency.c \
	graph.c syncop.c graph-print.c trie.c run.c options.c fd-lk.c \
	circ-buff.c event-history.c gidcache.c ctx.c client_t.c event-poll.c \
	event-epoll.c syncop-utils.c cluster-syncop.c refcount.c \
	$(CONTRIBDIR)/libgen/basename_r.c \
	$(CONTRIBDIR)/libgen/dirname_r.c $(CONTRIBDIR)/stdlib/gf_mkostemp.c \
	strfd.c parse-utils.c $(CONTRIBDIR)/mount/mntent.c \
	$(CONTRIBDIR)/libexecinfo/execinfo.c quota-common-utils.c rot-buffs.c \
	$(CONTRIBDIR)/timer-wheel/timer-wheel.c \
	$(CONTRIBDIR)/timer-wheel/find_last_bit.c tw.c default-args.c locking.c \
	compound-fop-utils.c throttle-tbf.c

nodist_libglusterfs_la_SOURCES = y.tab.c graph.lex.c defaults.c
nodist_libglusterfs_la_HEADERS = y.tab.h

BUILT_SOURCES = graph.lex.c defaults.c eventtypes.h

libglusterfs_la_HEADERS = common-utils.h defaults.h default-args.h \
	dict.h glusterfs.h hashfn.h timespec.h logging.h xlator.h \
	stack.h timer.h list.h inode.h call-stub.h compat.h fd.h \
	revision.h compat-errno.h event.h mem-pool.h byte-order.h \
	gf-dirent.h locking.h syscall.h iobuf.h globals.h statedump.h \
	checksum.h daemon.h store.h rbthash.h iatt.h latency.h \
	mem-types.h syncop.h cluster-syncop.h graph-utils.h trie.h \
	refcount.h run.h options.h lkowner.h fd-lk.h circ-buff.h \
	event-history.h gidcache.h client_t.h glusterfs-acl.h \
	glfs-message-id.h template-component-messages.h strfd.h \
	syncop-utils.h parse-utils.h libglusterfs-messages.h tw.h \
	lvm-defaults.h quota-common-utils.h rot-buffs.h \
	compat-uuid.h upcall-utils.h throttle-tbf.h events.h\
	compound-fop-utils.h

libglusterfs_ladir = $(includedir)/glusterfs

noinst_HEADERS = unittest/unittest.h \
	$(CONTRIBDIR)/rbtree/rb.h \
	$(CONTRIBDIR)/mount/mntent_compat.h \
	$(CONTRIBDIR)/libexecinfo/execinfo_compat.h \
	$(CONTRIBDIR)/timer-wheel/timer-wheel.h \
	tier-ctr-interface.h

if !HAVE_LIBUUID
# FIXME: unbundle libuuid, see compat-uuid.h.
libglusterfs_la_SOURCES += $(CONTRIBDIR)/uuid/clear.c \
	$(CONTRIBDIR)/uuid/copy.c $(CONTRIBDIR)/uuid/gen_uuid.c \
	$(CONTRIBDIR)/uuid/pack.c $(CONTRIBDIR)/uuid/parse.c \
	$(CONTRIBDIR)/uuid/unparse.c $(CONTRIBDIR)/uuid/uuid_time.c \
	$(CONTRIBDIR)/uuid/compare.c $(CONTRIBDIR)/uuid/isnull.c \
	$(CONTRIBDIR)/uuid/unpack.c
endif


eventtypes.h: $(top_srcdir)/events/eventskeygen.py
	$(PYTHON) $(top_srcdir)/events/eventskeygen.py C_HEADER

if BUILD_EVENTS
libglusterfs_la_SOURCES += events.c
endif

libgfchangelog_HEADERS = changelog.h

EXTRA_DIST = graph.l graph.y defaults-tmpl.c

graph.lex.c: graph.l y.tab.h
	$(LEX) -Pgraphyy -t $(srcdir)/graph.l > $@

y.tab.c: y.tab.h
y.tab.h: graph.y
	$(YACC) -p graphyy -d $(srcdir)/graph.y

defaults.c: defaults-tmpl.c generator.py gen-defaults.py
	$(PYTHON) $(srcdir)/gen-defaults.py $(srcdir)/defaults-tmpl.c > $@

CLEANFILES = $(nodist_libglusterfs_la_SOURCES) $(nodist_libglusterfs_la_HEADERS)

if UNITTEST
CLEANFILES += *.gcda *.gcno *_xunit.xml
noinst_PROGRAMS =
TESTS =
endif

if BUILD_EVENTS
CLEANFILES += eventtypes.h
endif
