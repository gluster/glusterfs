CLEANFILES = glusterd.service glustereventsd.service glusterfssharedstorage.service gluster-ta-volume.service
EXTRA_DIST = glusterd.service.in glustereventsd.service.in glusterfssharedstorage.service.in gluster-ta-volume.service.in

if USE_SYSTEMD
systemd_DATA = gluster-ta-volume.service
endif

if WITH_SERVER
if USE_SYSTEMD
# systemddir is already defined through configure.ac
systemd_DATA += glusterd.service glusterfssharedstorage.service

if BUILD_EVENTS
systemd_DATA += glustereventsd.service
endif
endif
endif
