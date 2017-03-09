SUBDIRS = src

EXTRA_DIST = S57glusterfind-delete-post.py

bin_SCRIPTS = glusterfind

CLEANFILES = $(bin_SCRIPTS)

deletehookscriptsdir = $(GLUSTERFS_LIBEXECDIR)/glusterfind/
deletehookscripts_SCRIPTS = S57glusterfind-delete-post.py

uninstall-local:
	rm -f $(DESTDIR)$(GLUSTERD_WORKDIR)/hooks/1/delete/post/S57glusterfind-delete-post

install-data-local:
	$(mkdir_p) $(DESTDIR)$(GLUSTERD_WORKDIR)/glusterfind/.keys
	$(mkdir_p) $(DESTDIR)$(GLUSTERD_WORKDIR)/hooks/1/delete/post/
	rm -f $(DESTDIR)$(GLUSTERD_WORKDIR)/hooks/1/delete/post/S57glusterfind-delete-post
	ln -s $(GLUSTERFS_LIBEXECDIR)/glusterfind/S57glusterfind-delete-post.py \
		$(DESTDIR)$(GLUSTERD_WORKDIR)/hooks/1/delete/post/S57glusterfind-delete-post
