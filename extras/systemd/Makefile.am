CLEANFILES = glustereventsd.service
EXTRA_DIST = glustereventsd.service.in

SYSTEMD_DIR = @systemddir@

install-exec-local:
	@if [ -d $(SYSTEMD_DIR) ]; then \
		$(mkdir_p) $(DESTDIR)$(SYSTEMD_DIR); \
		$(INSTALL_PROGRAM) glusterd.service $(DESTDIR)$(SYSTEMD_DIR)/; \
	fi
	@if [ @EVENTS_ENABLED@ = 1 ] && [ -d $(SYSTEMD_DIR) ]; then \
		$(mkdir_p) $(DESTDIR)$(SYSTEMD_DIR); \
		$(INSTALL_PROGRAM) glustereventsd.service $(DESTDIR)$(SYSTEMD_DIR)/; \
	fi
