utildir = @mountutildir@

if GF_LINUX_HOST_OS
util_SCRIPTS = mount.glusterfs
if GF_FBEXTRAS
util_SCRIPTS += umount.fuse.glusterfs
endif
else
util_SCRIPTS = mount_glusterfs
endif

CLEANFILES =
