gfindmissingfilesdir = $(GLUSTERFS_LIBEXECDIR)/gfind_missing_files

if WITH_SERVER
gfindmissingfiles_SCRIPTS = gfind_missing_files.sh gfid_to_path.sh \
	gfid_to_path.py
endif

EXTRA_DIST = gfind_missing_files.sh gfid_to_path.sh \
	gfid_to_path.py

if WITH_SERVER
gfindmissingfiles_PROGRAMS = gcrawler
endif

gcrawler_SOURCES = gcrawler.c
gcrawler_LDADD = $(top_builddir)/libglusterfs/src/libglusterfs.la
gcrawler_LDFLAGS = $(GF_LDFLAGS)

AM_CPPFLAGS = $(GF_CPPFLAGS) -I$(top_srcdir)/libglusterfs/src

AM_CFLAGS = -Wall $(GF_CFLAGS)

if WITH_SERVER
uninstall-local:
	rm -f $(DESTDIR)$(sbindir)/gfind_missing_files

install-data-local:
	rm -f $(DESTDIR)$(sbindir)/gfind_missing_files
	ln -s $(GLUSTERFS_LIBEXECDIR)/gfind_missing_files/gfind_missing_files.sh $(DESTDIR)$(sbindir)/gfind_missing_files
endif

CLEANFILES =
