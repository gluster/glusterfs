
EditorModedir = $(docdir)
EditorMode_DATA = glusterfs-mode.el glusterfs.vim

SUBDIRS = init.d systemd benchmarking hook-scripts $(OCF_SUBDIR) LinuxRPM

confdir = $(sysconfdir)/glusterfs
conf_DATA = glusterfs-logrotate

EXTRA_DIST = specgen.scm MacOSX/Portfile glusterfs-mode.el glusterfs.vim  \
	migrate-unify-to-distribute.sh backend-xattr-sanitize.sh          \
	backend-cleanup.sh disk_usage_sync.sh quota-remove-xattr.sh       \
	quota-metadata-cleanup.sh glusterfs-logrotate clear_xattrs.sh     \
	group-virt.example glusterd-sysconfig
