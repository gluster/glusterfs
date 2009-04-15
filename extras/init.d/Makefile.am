
EXTRA_DIST = glusterfsd-Debian glusterfsd-Redhat glusterfsd-SuSE glusterfs-server.plist

CLEANFILES = 

initdir = /etc/init.d

$(GF_DISTRIBUTION): 
	$(mkdir_p) $(DESTDIR)$(initdir)	
	$(INSTALL_PROGRAM) $(abs_top_srcdir)/extras/init.d/glusterfsd-$(GF_DISTRIBUTION) $(DESTDIR)$(initdir)/glusterfsd

install-exec-local: $(GF_DISTRIBUTION)

install-data-local: 
if GF_DARWIN_HOST_OS
	cp glusterfs-server.plist /Library/LaunchDaemons/com.zresearch.glusterfs.plist 
endif
