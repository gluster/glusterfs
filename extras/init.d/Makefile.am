
EXTRA_DIST = glusterfsd glusterfs-server glusterfs-server.plist

CLEANFILES = 

install-data-am:
if GF_DARWIN_HOST_OS
	cp glusterfs-server.plist /Library/LaunchDaemons/com.zresearch.glusterfs.plist
endif
