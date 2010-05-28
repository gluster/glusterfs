
EXTRA_DIST = glusterfsd-Debian glusterfsd-Redhat glusterfsd-SuSE glusterfs-server.plist

CLEANFILES = 

initdir = @initdir@
launchddir = @launchddir@

$(GF_DISTRIBUTION): 
	$(mkdir_p) $(DESTDIR)$(initdir)	
	$(INSTALL_PROGRAM) glusterfsd-$(GF_DISTRIBUTION) $(DESTDIR)$(initdir)/glusterfsd

install-exec-local: $(GF_DISTRIBUTION)

install-data-local: 
if GF_DARWIN_HOST_OS
	$(mkdir_p) $(DESTDIR)$(launchddir)
	$(INSTALL_PROGRAM) glusterfs-server.plist $(DESTDIR)$(launchddir)/com.gluster.glusterfs.plist
endif
