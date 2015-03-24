EXTRA_DIST = autogen.sh \
	COPYING-GPLV2 COPYING-LGPLV3 \
	INSTALL README.md AUTHORS THANKS NEWS \
	glusterfs.spec glusterfs-api.pc.in libgfchangelog.pc.in libgfdb.pc.in \
	run-tests.sh \
	build-aux/pkg-version \
	build-aux/xdrgen \
        contrib/argp-standalone \
        contrib/umountd \
	$(shell find $(top_srcdir)/tests -type f -print)

SUBDIRS = $(ARGP_STANDALONE_DIR) libglusterfs rpc api xlators glusterfsd \
	$(FUSERMOUNT_SUBDIR) doc extras cli heal @SYNCDAEMON_SUBDIR@ \
	@UMOUNTD_SUBDIR@ tools

pkgconfigdir = @pkgconfigdir@
pkgconfig_DATA = glusterfs-api.pc libgfchangelog.pc libgfdb.pc

CLEANFILES =

gitclean: distclean
	find . -name Makefile.in -exec rm -f {} \;
	find . -name Makefile -exec rm -f {} \;
	find . -name mount.glusterfs -exec rm -f {} \;
	rm -fr autom4te.cache
	rm -f missing aclocal.m4 config.h.in config.guess config.sub ltmain.sh install-sh configure depcomp
	-rm -fr $(CONTRIBDIR)/argp-standalone/autom4te.cache
	-rm -f $(CONTRIBDIR)/argp-standalone/aclocal.m4
	-rm -f $(CONTRIBDIR)/argp-standalone/config.h.in
	-rm -f $(CONTRIBDIR)/argp-standalone/configure
	-rm -f $(CONTRIBDIR)/argp-standalone/depcomp
	-rm -fr $(CONTRIBDIR)/argp-standalone/.deps
	-rm -f $(CONTRIBDIR)/argp-standalone/install-sh
	-rm -f $(CONTRIBDIR)/argp-standalone/missing

dist-hook: gen-VERSION gen-ChangeLog
	-rm -fr $(distdir)/contrib/argp-standalone/autom4te.cache
	-rm -fr $(distdir)/contrib/argp-standalone/.deps
	-rm -fr $(distdir)/contrib/umountd/.deps

.PHONY: gen-VERSION gen-ChangeLog

gen-ChangeLog:
	(cd $(srcdir) && git diff && echo ===== git log ==== && git log) > $(distdir)/ChangeLog

gen-VERSION:
	if test -d .git; then                           \
	  $(top_srcdir)/build-aux/pkg-version --full    \
	    > $(distdir)/VERSION;                       \
	fi
