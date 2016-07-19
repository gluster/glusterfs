CLEANFILES = glusterd.service glustereventsd.service
EXTRA_DIST = glusterd.service.in glustereventsd.service.in

if USE_SYSTEMD
# systemddir is already defined through configure.ac
systemd_DATA = glusterd.service

if BUILD_EVENTS
systemd_DATA += glustereventsd.service
endif
endif
