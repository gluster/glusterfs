AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -g -shared -nostartfiles -rdynamic -DXLATORDIR=\"$(libdir)/glusterfs/xlator\" -DSCHEDULERDIR=\"$(libdir)/glusterfs/scheduler\" -DYY_NO_UNPUT

libglusterfs_PROGRAMS = libglusterfs.so
libglusterfsdir = $(libdir)

libglusterfs_so_SOURCES = dict.c spec.lex.c y.tab.c xlator.c logging.c loc_hint.c hashfn.c layout.c defaults.c scheduler.c common-utils.c protocol.c

noinst_HEADERS = common-utils.h defaults.h dict.h glusterfs.h hashfn.h layout.h loc_hint.h logging.h protocol.h scheduler.h sdp_inet.h xlator.h

EXTRA_DIST = spec.l spec.y

spec.lex.c: spec.l y.tab.h
	$(LEX) -t $(srcdir)/spec.l > $@

y.tab.c y.tab.h: spec.y
	$(YACC) -d $(srcdir)/spec.y

CLEANFILES = spec.lex.c y.tab.c y.tab.h
