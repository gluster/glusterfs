EXTRA_DIST = autogen.sh \
	COPYING-GPLV2 COPYING-LGPLV3 \
	INSTALL README AUTHORS THANKS NEWS \
	glusterfs.spec glusterfs-api.pc.in \
	argp-standalone

SUBDIRS = $(ARGP_STANDALONE_DIR) libglusterfs rpc api xlators glusterfsd \
	$(FUSERMOUNT_SUBDIR) doc extras cli

pkgconfigdir = @pkgconfigdir@
pkgconfig_DATA = glusterfs-api.pc

CLEANFILES =

gitclean: distclean
	find . -name Makefile.in -exec rm -f {} \;
	find . -name Makefile -exec rm -f {} \;
	find . -name mount.glusterfs -exec rm -f {} \;
	rm -fr autom4te.cache
	rm -f missing aclocal.m4 config.h.in config.guess config.sub ltmain.sh install-sh configure depcomp
	rm -fr argp-standalone/autom4te.cache
	rm -f argp-standalone/aclocal.m4
	rm -f argp-standalone/config.h.in
	rm -f argp-standalone/configure
	rm -f argp-standalone/depcomp
	rm -f argp-standalone/install-sh
	rm -f argp-standalone/missing

dist-hook:
	(cd $(srcdir) && git diff && echo ===== git log ==== && git log) > $(distdir)/ChangeLog
	tar czf gluster-swift-ufo-$(VERSION).tar.gz ufo
