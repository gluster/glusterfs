noinst_PYTHON = $(top_srcdir)/events/eventskeygen.py
EXTRA_DIST = glustereventsd.py __init__.py  eventsapiconf.py.in \
	handlers.py utils.py peer_eventsapi.py eventsconfig.json gf_event.py

BUILT_SOURCES = eventtypes.py
CLEANFILES = eventtypes.py

eventsdir = $(libexecdir)/glusterfs/events
events_PYTHON = __init__.py gf_event.py eventsapiconf.py eventtypes.py \
	utils.py

eventtypes.py: $(top_srcdir)/events/eventskeygen.py
	$(PYTHON) $(top_srcdir)/events/eventskeygen.py PY_HEADER

if BUILD_EVENTS
eventspeerscriptdir = $(libexecdir)/glusterfs
eventsconfdir = $(sysconfdir)/glusterfs
eventsconf_DATA = eventsconfig.json

events_PYTHON += handlers.py
events_SCRIPTS = glustereventsd.py
eventspeerscript_SCRIPTS = peer_eventsapi.py

install-exec-hook:
	$(mkdir_p) $(DESTDIR)$(sbindir)
	rm -f $(DESTDIR)$(sbindir)/glustereventsd
	ln -s $(libexecdir)/glusterfs/events/glustereventsd.py \
		$(DESTDIR)$(sbindir)/glustereventsd
	rm -f $(DESTDIR)$(sbindir)/gluster-eventsapi
	ln -s $(libexecdir)/glusterfs/peer_eventsapi.py \
		$(DESTDIR)$(sbindir)/gluster-eventsapi

uninstall-hook:
	rm -f $(DESTDIR)$(sbindir)/glustereventsd
	rm -f $(DESTDIR)$(sbindir)/gluster-eventsapi

endif
