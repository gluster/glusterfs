noinst_PYTHON = generator.py gen-defaults.py $(top_srcdir)/events/eventskeygen.py

libglusterfs_la_CFLAGS = $(GF_CFLAGS) $(GF_DARWIN_LIBGLUSTERFS_CFLAGS) \
	-DDATADIR=\"$(localstatedir)\"

libglusterfs_la_CPPFLAGS = $(GF_CPPFLAGS) -D__USE_FILE_OFFSET64 \
	-DXLATORDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator\" \
	-DXLATORPARENTDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)\" \
	-DXXH_NAMESPACE=GF_ -D__USE_LARGEFILE64 \
	-I$(CONTRIBDIR)/rbtree \
	-I$(CONTRIBDIR)/libexecinfo ${ARGP_STANDALONE_CPPFLAGS} \
	-DSBIN_DIR=\"$(sbindir)\" -I$(CONTRIBDIR)/timer-wheel \
	-I$(CONTRIBDIR)/xxhash

libglusterfs_la_LIBADD = $(ZLIB_LIBS) $(MATH_LIB) $(UUID_LIBS) $(LIB_DL) \
	$(URCU_LIBS) $(URCU_CDS_LIBS)
libglusterfs_la_LDFLAGS = -version-info $(LIBGLUSTERFS_LT_VERSION) $(GF_LDFLAGS) \
	-export-symbols $(top_srcdir)/libglusterfs/src/libglusterfs.sym

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
	$(CONTRIBDIR)/libgen/dirname_r.c \
	strfd.c parse-utils.c $(CONTRIBDIR)/mount/mntent.c \
	$(CONTRIBDIR)/libexecinfo/execinfo.c quota-common-utils.c rot-buffs.c \
	$(CONTRIBDIR)/timer-wheel/timer-wheel.c \
	$(CONTRIBDIR)/timer-wheel/find_last_bit.c default-args.c locking.c \
	$(CONTRIBDIR)/xxhash/xxhash.c \
	throttle-tbf.c monitoring.c async.c

nodist_libglusterfs_la_SOURCES = y.tab.c graph.lex.c defaults.c
nodist_libglusterfs_la_HEADERS = y.tab.h

BUILT_SOURCES = graph.lex.c defaults.c eventtypes.h

libglusterfs_la_HEADERS = glusterfs/common-utils.h glusterfs/defaults.h \
	glusterfs/default-args.h glusterfs/dict.h glusterfs/glusterfs.h \
	glusterfs/hashfn.h glusterfs/timespec.h glusterfs/logging.h \
	glusterfs/xlator.h glusterfs/stack.h glusterfs/timer.h glusterfs/list.h \
	glusterfs/inode.h glusterfs/call-stub.h glusterfs/compat.h glusterfs/fd.h \
	glusterfs/revision.h glusterfs/compat-errno.h glusterfs/gf-event.h \
	glusterfs/mem-pool.h glusterfs/byte-order.h glusterfs/gf-dirent.h \
	glusterfs/locking.h glusterfs/syscall.h glusterfs/iobuf.h \
	glusterfs/globals.h glusterfs/statedump.h glusterfs/checksum.h \
	glusterfs/daemon.h glusterfs/store.h glusterfs/rbthash.h glusterfs/iatt.h \
	glusterfs/latency.h glusterfs/mem-types.h glusterfs/syncop.h \
	glusterfs/cluster-syncop.h glusterfs/graph-utils.h glusterfs/trie.h \
	glusterfs/refcount.h glusterfs/run.h glusterfs/options.h \
	glusterfs/lkowner.h glusterfs/fd-lk.h glusterfs/circ-buff.h \
	glusterfs/event-history.h glusterfs/gidcache.h glusterfs/client_t.h \
	glusterfs/glusterfs-acl.h glusterfs/glfs-message-id.h \
	glusterfs/template-component-messages.h glusterfs/strfd.h \
	glusterfs/syncop-utils.h glusterfs/parse-utils.h \
	glusterfs/libglusterfs-messages.h glusterfs/lvm-defaults.h \
	glusterfs/quota-common-utils.h glusterfs/rot-buffs.h \
	glusterfs/compat-uuid.h glusterfs/upcall-utils.h glusterfs/throttle-tbf.h \
	glusterfs/events.h glusterfs/atomic.h glusterfs/monitoring.h \
	glusterfs/async.h glusterfs/glusterfs-fops.h

libglusterfs_ladir = $(includedir)/glusterfs

noinst_HEADERS = unittest/unittest.h \
	$(CONTRIBDIR)/rbtree/rb.h \
	$(CONTRIBDIR)/mount/mntent_compat.h \
	$(CONTRIBDIR)/libexecinfo/execinfo_compat.h \
	$(CONTRIBDIR)/timer-wheel/timer-wheel.h \
	$(CONTRIBDIR)/xxhash/xxhash.h \
	$(CONTRIBDIR)/userspace-rcu/wfcqueue.h \
	$(CONTRIBDIR)/userspace-rcu/wfstack.h \
	$(CONTRIBDIR)/userspace-rcu/static-wfcqueue.h \
	$(CONTRIBDIR)/userspace-rcu/static-wfstack.h

eventtypes.h: $(top_srcdir)/events/eventskeygen.py
	$(PYTHON) $(top_srcdir)/events/eventskeygen.py C_HEADER

if BUILD_EVENTS
libglusterfs_la_SOURCES += events.c
endif

libgfchangelog_HEADERS = changelog.h

EXTRA_DIST = graph.l graph.y defaults-tmpl.c libglusterfs.sym

graph.lex.c: graph.l y.tab.h
	$(LEX) -Pgraphyy -t $(srcdir)/graph.l > $@

y.tab.c: y.tab.h
y.tab.h: graph.y
	$(YACC) -p graphyy -d $(srcdir)/graph.y

defaults.c: defaults-tmpl.c generator.py gen-defaults.py
	$(PYTHON) $(srcdir)/gen-defaults.py $(srcdir)/defaults-tmpl.c > $@

CLEANFILES = $(nodist_libglusterfs_la_SOURCES) \
	$(nodist_libglusterfs_la_HEADERS) *.pyc

if UNITTEST
CLEANFILES += *.gcda *.gcno *_xunit.xml
noinst_PROGRAMS =
TESTS =
endif

if BUILD_EVENTS
CLEANFILES += eventtypes.h
endif
