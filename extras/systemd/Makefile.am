CLEANFILES = glusterd.service glustereventsd.service glusterfssharedstorage.service
EXTRA_DIST = glusterd.service.in glustereventsd.service.in glusterfssharedstorage.service.in

if WITH_SERVER
if USE_SYSTEMD
# systemddir is already defined through configure.ac
systemd_DATA = glusterd.service glusterfssharedstorage.service

if BUILD_EVENTS
systemd_DATA += glustereventsd.service
endif
endif
endif
