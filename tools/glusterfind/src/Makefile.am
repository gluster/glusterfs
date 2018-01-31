glusterfinddir = $(GLUSTERFS_LIBEXECDIR)/glusterfind

if WITH_SERVER
glusterfind_PYTHON = conf.py utils.py __init__.py \
	main.py libgfchangelog.py changelogdata.py

glusterfind_SCRIPTS = changelog.py nodeagent.py \
	brickfind.py

glusterfind_DATA = tool.conf
endif

EXTRA_DIST = changelog.py nodeagent.py brickfind.py \
	tool.conf changelogdata.py

CLEANFILES =
