
EXTRA_DIST = glusterd-Debian glusterd-FreeBSD glusterd-Redhat glusterd-SuSE glusterd.plist rhel5-load-fuse.modules

CLEANFILES =

INIT_DIR = @initdir@
SYSTEMD_DIR = @systemddir@
LAUNCHD_DIR = @launchddir@

install-glusterd:
	$(mkdir_p) $(DESTDIR)$(INIT_DIR)
	$(INSTALL_PROGRAM) glusterd-$(GF_DISTRIBUTION) $(DESTDIR)$(INIT_DIR)/glusterd

install-glusterd-functions:
if GF_FBEXTRAS
	$(INSTALL_PROGRAM) glusterd.functions-$(GF_DISTRIBUTION) $(DESTDIR)$(INIT_DIR)/glusterd.functions
endif

if GF_WITH_SYSTEMD
$(GF_DISTRIBUTION):
	@echo Using systemd, null installation
else
$(GF_DISTRIBUTION): install-glusterd install-glusterd-functions
endif

install-exec-local: $(GF_DISTRIBUTION)

install-data-local:
if GF_DARWIN_HOST_OS
	$(mkdir_p) $(DESTDIR)$(LAUNCHD_DIR)
	$(INSTALL_PROGRAM) glusterd.plist $(DESTDIR)$(LAUNCHD_DIR)/org.gluster.glusterd.plist
endif
