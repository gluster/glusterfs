SUBDIRS = src tools
EXTRA_DIST = eventskeygen.py
noinst_PYTHON = eventskeygen.py

if BUILD_EVENTS
install-data-hook:
	$(INSTALL) -d -m 755 $(DESTDIR)@GLUSTERD_WORKDIR@/events
endif
