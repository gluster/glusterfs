GLUSTERFS_BOOSTER_FSTAB = "/etc/gluster/booster.fstab"
GLUSTERFS_UNFS3_EXPORTS = "/etc/gluster/boosterexports"
GLUSTERFS_CIFS_CONFIG = "/etc/gluster/boostersmb.conf"
LOGDIR = "/var/log/glusterfs"
CONFDIR = "/etc/gluster"
fstype = "glusterfs"

class CreateBooster:

    def __init__ (self, main_name, export_dir):

        self.volume_name = main_name
        self.export = export_dir

    def configure_booster_fstab (self):
        if self.volume_name is None or self.export is None:
            sys.exit(1)

        booster_fstab_fd = file (GLUSTERFS_BOOSTER_FSTAB, "a")
        _fstab = "%s/%s.vol  %s" % (CONFDIR, self.volume_name, self.export)
        _options = "%s subvolume=io-cache" % fstype
        _options_log = "logfile=%s/%s.log" % (LOGDIR, self.volume_name)
        _options_ext = "loglevel=ERROR,attr_timeout=0"
        booster_fstab_fd.write ("%s %s,%s,%s\n" %
                                (_fstab,
                                 _options,
                                 _options_log,
                                 _options_ext))

        return

    def configure_nfs_booster (self):
        if self.volume_name is None or self.export is None:
            sys.exit(1)

        nfs_exports_fd = file (GLUSTERFS_UNFS3_EXPORTS, "a")
        nfs_exports_fd.write ("%s  0.0.0.0/0(rw,no_root_squash)\n" %
                              self.export)
        return

    def configure_cifs_booster (self):

        if self.volume_name is None or self.export is None:
            sys.exit(1)

        cifs_config_fd = file (GLUSTERFS_CIFS_CONFIG, "a")
        cifs_config_fd.write ("[%s]\n" % self.volume_name)
        cifs_config_fd.write ("comment = %s volume served by Gluster\n" %
                              self.volume_name)
        cifs_config_fd.write ("path = %s\n" % self.export)
        cifs_config_fd.write ("guest ok = yes\n")
        cifs_config_fd.write ("public = yes\n")
        cifs_config_fd.write ("writable = yes\n")
        cifs_config_fd.write ("admin users = gluster\n")
        cifs_config_fd.close()
        return
