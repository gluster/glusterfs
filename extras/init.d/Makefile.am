
EXTRA_DIST = glusterd-Debian glusterd-FreeBSD glusterd-Redhat glusterd-SuSE \
	glusterd.plist rhel5-load-fuse.modules \
	glustereventsd-FreeBSD glustereventsd-Redhat glustereventsd-Debian

CLEANFILES =

INIT_DIR = @initdir@
SYSTEMD_DIR = @systemddir@
LAUNCHD_DIR = @launchddir@

$(GF_DISTRIBUTION):
if WITH_SERVER
	@if [ ! -d $(SYSTEMD_DIR) ]; then \
		$(mkdir_p) $(DESTDIR)$(INIT_DIR); \
		$(INSTALL_PROGRAM) glusterd-$(GF_DISTRIBUTION) $(DESTDIR)$(INIT_DIR)/glusterd; \
	fi
endif
if BUILD_EVENTS
	@if [ ! -d $(SYSTEMD_DIR) ]; then \
		$(mkdir_p) $(DESTDIR)$(INIT_DIR); \
		$(INSTALL_PROGRAM) glustereventsd-$(GF_DISTRIBUTION) $(DESTDIR)$(INIT_DIR)/glustereventsd; \
	fi
endif

install-exec-local: $(GF_DISTRIBUTION)

install-data-local:
if GF_DARWIN_HOST_OS
	$(mkdir_p) $(DESTDIR)$(LAUNCHD_DIR)
	$(INSTALL_PROGRAM) glusterd.plist $(DESTDIR)$(LAUNCHD_DIR)/org.gluster.glusterd.plist
endif
