
EXTRA_DIST = glusterfsd-Debian glusterfsd-Redhat glusterfsd-SuSE glusterfs-server.plist

CLEANFILES = 

initdir = /etc/init.d

$(GF_DISTRIBUTION): 
	$(mkdir_p) $(DESTDIR)$(initdir)	
	cp glusterfsd-$(GF_DISTRIBUTION) $(DESTDIR)$(initdir)/glusterfsd

install-data-local: $(GF_DISTRIBUTION)
if GF_DARWIN_HOST_OS
	cp glusterfs-server.plist /Library/LaunchDaemons/com.zresearch.glusterfs.plist 
endif
