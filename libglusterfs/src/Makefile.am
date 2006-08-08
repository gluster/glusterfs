
lib_LIBRARIES = libglusterfs.a

libglusterfs_a_SOURCES = dict.c spec.lex.c y.tab.c xlator.c logging.c loc_hint.c

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -g -rdynamic -DXLATORDIR=\"$(libdir)/glusterfs\"

spec.lex.c: spec.l y.tab.h
	$(LEX) -t $(srcdir)/spec.l > $@

y.tab.c y.tab.h: spec.y
	$(YACC) -d $(srcdir)/spec.y

CLEANFILES = spec.lex.c y.tab.c y.tab.h
