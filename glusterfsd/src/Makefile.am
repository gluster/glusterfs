
sbin_PROGRAMS = glusterfsd

glusterfsd_SOURCES = glusterfsd.c glusterfsd-fops.c glusterfsd-mgmt.c conf.lex.c y.tab.c lock.c ns.c
glusterfsd_LDADD = -L../../libglusterfs/src -lglusterfs -ldl -lm

noinst_HEADERS = glusterfsd.h lock.h ns.h
EXTRA_DIST = conf.l conf.y

AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=25 -D_GNU_SOURCE -Wall \
	-I$(top_srcdir)/libglusterfs/src

conf.lex.c: conf.l y.tab.h
	$(LEX) -Pcc -t $(srcdir)/conf.l > $@

y.tab.c y.tab.h: conf.y
	$(YACC) -p cc -d $(srcdir)/conf.y

CLEANFILES = *~ y.tab.h y.tab.c conf.lex.c
