libglusterfs_la_CFLAGS = -fPIC  -Wall -g -shared -nostartfiles $(GF_HOST_OS_FLAGS)

libglusterfs_la_CPPFLAGS = -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -DXLATORDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator\" -DSCHEDULERDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/scheduler\" -DTRANSPORTDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/transport\" -DYY_NO_UNPUT -D$(GF_HOST_OS) -DLIBDIR=\"$(libdir)/glusterfs/$(PACKAGE_VERSION)/auth\"

libglusterfs_la_LIBADD = @LEXLIB@

lib_LTLIBRARIES = libglusterfs.la

libglusterfs_la_SOURCES = dict.c spec.lex.c y.tab.c xlator.c logging.c  hashfn.c defaults.c scheduler.c common-utils.c protocol.c transport.c poll.c epoll.c timer.c lock.c inode.c call-stub.c compat.c authenticate.c fd.c

noinst_HEADERS = common-utils.h defaults.h dict.h glusterfs.h hashfn.h logging.h protocol.h scheduler.h xlator.h transport.h stack.h timer.h lock.h list.h inode.h call-stub.h compat.h  authenticate.h fd.h revision.h

EXTRA_DIST = spec.l spec.y

spec.lex.c: spec.l y.tab.h
	$(LEX) -t $(srcdir)/spec.l > $@

y.tab.c y.tab.h: spec.y
	$(YACC) -d $(srcdir)/spec.y

CLEANFILES = spec.lex.c y.tab.c y.tab.h *~
