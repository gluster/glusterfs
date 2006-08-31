AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -g -rdynamic -DXLATORDIR=\"$(libdir)/glusterfs/xlator\" -DSCHEDULERDIR=\"$(libdir)/glusterfs/scheduler\"

lib_LIBRARIES = libglusterfs.a

libglusterfs_a_SOURCES = dict.c spec.lex.c y.tab.c xlator.c logging.c loc_hint.c hashfn.c layout.c defaults.c scheduler.c common-utils.c

dict-test: dict-test.o 
	gcc -Wall -g -o dict-test -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE $(top_srcdir)/libglusterfs/src/dict-test.c $(top_srcdir)/libglusterfs/src/dict.c $(top_srcdir)/libglusterfs/src/protocol.c

spec.lex.c: spec.l y.tab.h
	$(LEX) -t $(srcdir)/spec.l > $@

y.tab.c y.tab.h: spec.y
	$(YACC) -d $(srcdir)/spec.y

CLEANFILES = spec.lex.c y.tab.c y.tab.h
