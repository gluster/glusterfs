
CLEANFILES =

SYSTEMD_DIR = @systemddir@

if GF_WITH_SYSTEMD
install-exec-local:
	@if [ -d $(SYSTEMD_DIR) ]; then \
		$(mkdir_p) $(DESTDIR)$(SYSTEMD_DIR); \
		$(INSTALL_PROGRAM) glusterd-mgmt.service $(DESTDIR)$(SYSTEMD_DIR)/; \
		$(INSTALL_PROGRAM) glusterd-nfsd.service $(DESTDIR)$(SYSTEMD_DIR)/; \
		$(INSTALL_PROGRAM) glusterd-gfproxyd.service $(DESTDIR)$(SYSTEMD_DIR)/; \
		$(INSTALL_PROGRAM) glusterd-shd.service $(DESTDIR)$(SYSTEMD_DIR)/; \
	fi
endif
