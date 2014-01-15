EditorModedir = $(docdir)
EditorMode_DATA = glusterfs-mode.el glusterfs.vim

SUBDIRS = init.d systemd benchmarking hook-scripts $(OCF_SUBDIR) LinuxRPM geo-rep

confdir = $(sysconfdir)/glusterfs
conf_DATA = glusterfs-logrotate gluster-rsyslog-7.2.conf gluster-rsyslog-5.8.conf \
	logger.conf.example glusterfs-georep-logrotate group-virt.example

voldir = $(sysconfdir)/glusterfs
vol_DATA = glusterd.vol

EXTRA_DIST = $(conf_DATA) specgen.scm MacOSX/Portfile glusterfs-mode.el        \
	glusterfs.vim migrate-unify-to-distribute.sh backend-xattr-sanitize.sh \
	backend-cleanup.sh disk_usage_sync.sh clear_xattrs.sh                  \
	glusterd-sysconfig glusterd.vol

install-data-local:
	$(mkdir_p) $(DESTDIR)$(localstatedir)/lib/glusterd/groups
	$(INSTALL_DATA) $(top_srcdir)/extras/group-virt.example \
		$(DESTDIR)$(localstatedir)/lib/glusterd/groups/virt
