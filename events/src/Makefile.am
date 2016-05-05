EXTRA_DIST = glustereventsd.py __init__.py  eventsapiconf.py.in eventtypes.py \
	handlers.py utils.py peer_eventsapi.py eventsconfig.json

eventsdir = $(libexecdir)/glusterfs/events
eventspeerscriptdir = $(libexecdir)/glusterfs
eventsconfdir = $(sysconfdir)/glusterfs
eventsconf_DATA = eventsconfig.json

events_PYTHON = __init__.py eventsapiconf.py eventtypes.py handlers.py utils.py
events_SCRIPTS = glustereventsd.py
eventspeerscript_SCRIPTS = peer_eventsapi.py

install-exec-hook:
	$(mkdir_p) $(DESTDIR)$(sbindir)
	rm -f $(DESTDIR)$(sbindir)/glustereventsd
	ln -s $(libexecdir)/glusterfs/events/glustereventsd.py \
		$(DESTDIR)$(sbindir)/glustereventsd
	rm -f $(DESTDIR)$(sbindir)/gluster-eventing
	ln -s $(libexecdir)/glusterfs/peer_eventsapi.py \
		$(DESTDIR)$(sbindir)/gluster-eventsapi

uninstall-hook:
	rm -f $(DESTDIR)$(sbindir)/glustereventsd
	rm -f $(DESTDIR)$(sbindir)/gluster-eventsapi
