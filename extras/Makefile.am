addonexecdir = $(libexecdir)/glusterfs
addonexec_SCRIPTS = peer_add_secret_pub

EditorModedir = $(docdir)
EditorMode_DATA = glusterfs-mode.el glusterfs.vim

SUBDIRS = init.d systemd benchmarking hook-scripts $(OCF_SUBDIR) LinuxRPM \
          $(GEOREP_EXTRAS_SUBDIR) ganesha snap_scheduler firewalld cliutils

confdir = $(sysconfdir)/glusterfs
conf_DATA = glusterfs-logrotate gluster-rsyslog-7.2.conf gluster-rsyslog-5.8.conf \
	logger.conf.example glusterfs-georep-logrotate group-virt.example group-metadata-cache

voldir = $(sysconfdir)/glusterfs
vol_DATA = glusterd.vol
scriptsdir = $(datadir)/glusterfs/scripts
scripts_SCRIPTS = post-upgrade-script-for-quota.sh \
	pre-upgrade-script-for-quota.sh stop-all-gluster-processes.sh

EXTRA_DIST = $(conf_DATA) specgen.scm glusterfs-mode.el glusterfs.vim \
	migrate-unify-to-distribute.sh backend-xattr-sanitize.sh backend-cleanup.sh \
	disk_usage_sync.sh clear_xattrs.sh glusterd-sysconfig glusterd.vol \
	post-upgrade-script-for-quota.sh pre-upgrade-script-for-quota.sh \
	command-completion/gluster.bash command-completion/Makefile \
	command-completion/README stop-all-gluster-processes.sh clang-checker.sh

install-data-local:
	if [ -n "$(tmpfilesdir)" ]; then \
		$(mkdir_p) $(DESTDIR)$(tmpfilesdir); \
		$(INSTALL_DATA) run-gluster.tmpfiles \
			$(DESTDIR)$(tmpfilesdir)/gluster.conf; \
	fi
	$(mkdir_p) $(DESTDIR)$(GLUSTERD_WORKDIR)/groups
	$(INSTALL_DATA) $(top_srcdir)/extras/group-virt.example \
		$(DESTDIR)$(GLUSTERD_WORKDIR)/groups/virt
	$(INSTALL_DATA) $(top_srcdir)/extras/group-metadata-cache \
		$(DESTDIR)$(GLUSTERD_WORKDIR)/groups/metadata-cache
