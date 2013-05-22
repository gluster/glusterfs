
EXTRA_DIST = glusterd-Debian glusterd-Redhat glusterd-SuSE glusterd.plist rhel5-load-fuse.modules

CLEANFILES = 

initdir = @initdir@
launchddir = @launchddir@

$(GF_DISTRIBUTION): 
	$(mkdir_p) $(DESTDIR)$(initdir)	
	$(INSTALL_PROGRAM) glusterd-$(GF_DISTRIBUTION) $(DESTDIR)$(initdir)/glusterd

install-exec-local: $(GF_DISTRIBUTION)

install-data-local: 
if GF_DARWIN_HOST_OS
	$(mkdir_p) $(DESTDIR)$(launchddir)
	$(INSTALL_PROGRAM) glusterd.plist $(DESTDIR)$(launchddir)/com.gluster.glusterd.plist
endif
