gfindmissingfilesdir = $(libexecdir)/glusterfs/gfind_missing_files

gfindmissingfiles_SCRIPTS = gfind_missing_files.sh gfid_to_path.sh \
	gfid_to_path.py

EXTRA_DIST = gfind_missing_files.sh gfid_to_path.sh \
	gfid_to_path.py

gfindmissingfiles_PROGRAMS = gcrawler

gcrawler_SOURCES = gcrawler.c
gcrawler_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la
gcrawler_LDFLAGS = $(GF_LDFLAGS)

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

uninstall-local:
	rm -f $(DESTDIR)$(sbindir)/gfind_missing_files

install-data-local:
	rm -f $(DESTDIR)$(sbindir)/gfind_missing_files
	ln -s $(libexecdir)/glusterfs/gfind_missing_files/gfind_missing_files.sh $(DESTDIR)$(sbindir)/gfind_missing_files

CLEANFILES =
