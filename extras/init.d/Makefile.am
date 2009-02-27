
EXTRA_DIST = glusterfsd glusterfs-server glusterfs-server.plist

CLEANFILES = 

initdir = $(sysconfdir)/init.d
init_DATA = glusterfsd

install-data-local:
if GF_DARWIN_HOST_OS
	cp glusterfs-server.plist /Library/LaunchDaemons/com.zresearch.glusterfs.plist 
endif
