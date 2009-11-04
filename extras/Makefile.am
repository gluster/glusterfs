
docdir = $(datadir)/doc/glusterfs/
EditorModedir = $(docdir)/
EditorMode_DATA = glusterfs-mode.el glusterfs.vim

SUBDIRS = init.d benchmarking

EXTRA_DIST = specgen.scm MacOSX/Portfile glusterfs-mode.el glusterfs.vim migrate-unify-to-distribute.sh backend-xattr-sanitize.sh
dist_bin_SCRIPTS = glusterfs-volgen

install-exec-hook:
	chmod 755 $(DESTDIR)$(bindir)/glusterfs-volgen

CLEANFILES = 

