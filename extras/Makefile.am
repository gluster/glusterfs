EditorModedir = $(docdir)
EditorMode_DATA = glusterfs-mode.el glusterfs.vim

SUBDIRS = init.d systemd benchmarking hook-scripts $(OCF_SUBDIR) LinuxRPM geo-rep

confdir = $(sysconfdir)/glusterfs
conf_DATA = glusterfs-logrotate gluster-rsyslog-7.2.conf gluster-rsyslog-5.8.conf \
	logger.conf.example glusterfs-georep-logrotate

voldir = $(sysconfdir)/glusterfs
vol_DATA = glusterd.vol
scriptsdir = $(datadir)/glusterfs/scripts
scripts_DATA = post-upgrade-script-for-quota.sh pre-upgrade-script-for-quota.sh

EXTRA_DIST = specgen.scm MacOSX/Portfile glusterfs-mode.el glusterfs.vim  \
	migrate-unify-to-distribute.sh backend-xattr-sanitize.sh          \
	backend-cleanup.sh disk_usage_sync.sh glusterfs-logrotate         \
        clear_xattrs.sh group-virt.example glusterd-sysconfig             \
        gluster-rsyslog-7.2.conf gluster-rsyslog-5.8.conf                 \
        logger.conf.example glusterd.vol glusterfs-georep-logrotate       \
        post-upgrade-script-for-quota.sh pre-upgrade-script-for-quota.sh
