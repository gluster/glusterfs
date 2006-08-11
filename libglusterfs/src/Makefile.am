AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -g -rdynamic -DXLATORDIR=\"$(libdir)/glusterfs\"

lib_LIBRARIES = libglusterfs.a

libglusterfs_a_SOURCES = dict.c spec.lex.c y.tab.c xlator.c logging.c loc_hint.c hashfn.c

loc_hint_test: loc_hint.o 
	gcc -Wall -g -o loc_hint_test -fprofile-arcs -ftest-coverage -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -DLOC_HINT_TEST $(top_srcdir)/libglusterfs/src/loc_hint.c -lm

spec.lex.c: spec.l y.tab.h
	$(LEX) -t $(srcdir)/spec.l > $@

y.tab.c y.tab.h: spec.y
	$(YACC) -d $(srcdir)/spec.y

CLEANFILES = spec.lex.c y.tab.c y.tab.h
