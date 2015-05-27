EXTRA_DIST = S40ufo-stop.py S56glusterd-geo-rep-create-post.sh \
	S57glusterfind-delete-post.py
SUBDIRS = add-brick set start stop reset

scriptsdir = $(GLUSTERD_WORKDIR)/hooks/1/gsync-create/post/
if USE_GEOREP
scripts_SCRIPTS = S56glusterd-geo-rep-create-post.sh
endif

deletehookscriptsdir = $(GLUSTERD_WORKDIR)/hooks/1/delete/post/
deletehookscripts_SCRIPTS = S57glusterfind-delete-post.py
