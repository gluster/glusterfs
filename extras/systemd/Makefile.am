
CLEANFILES =

SYSTEMD_DIR = @systemddir@

install-exec-local:
	@if [ -d $(SYSTEMD_DIR) ]; then \
		$(mkdir_p) $(DESTDIR)$(SYSTEMD_DIR); \
		$(INSTALL_PROGRAM) glusterd.service $(DESTDIR)$(SYSTEMD_DIR)/; \
	fi

