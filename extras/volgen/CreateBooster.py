GLUSTERFS_BOOSTER_FSTAB = "/etc/gluster/booster.fstab"
GLUSTERFS_UNFS3_EXPORTS = "/etc/gluster/boosterexports"
GLUSTERFS_CIFS_CONFIG = "/etc/gluster/boostersmb.conf"
LOGDIR = "/var/log/glusterfs"
CONFDIR = "/etc/gluster"
fstype = "glusterfs"

class CreateBooster:

    def __init__ (self, options):

        self.volume_name = options.volume_name
        self.need_nfs = options.need_nfs
        self.need_cifs = options.need_cifs
        self.username = options.cifs_username
        self.enable_guest = options.enable_guest

    def configure_booster_fstab (self):

        booster_fstab_fd = file (GLUSTERFS_BOOSTER_FSTAB, "a")
        if self.need_nfs:
            _fstab = "%s/%s.vol  %s" % (CONFDIR, self.volume_name, str("/nfs/" + self.volume_name))
            _options = "%s" % fstype
            _options_log = "logfile=%s/%s-nfs.log" % (LOGDIR, self.volume_name)
            _options_ext = "loglevel=ERROR,attr_timeout=0"
            booster_fstab_fd.write ("%s %s %s,%s\n" %
                                    (_fstab,
                                     _options,
                                     _options_log,
                                     _options_ext))

        if self.need_cifs:
            _fstab = "%s/%s.vol  %s" % (CONFDIR, self.volume_name, str("/cifs/" + self.volume_name))
            _options = "%s" % fstype
            _options_log = "logfile=%s/%s-cifs.log" % (LOGDIR, self.volume_name)
            _options_ext = "loglevel=ERROR,attr_timeout=0"
            booster_fstab_fd.write ("%s %s %s,%s\n" %
                                    (_fstab,
                                     _options,
                                     _options_log,
                                     _options_ext))

        return

    def configure_nfs_booster (self):

        nfs_exports_fd = file (GLUSTERFS_UNFS3_EXPORTS, "a")
        nfs_exports_fd.write ("%s  0.0.0.0/0(rw,no_root_squash)\n" %
                              str("/nfs/" + self.volume_name))
        return

    def configure_cifs_booster (self):

        cifs_config_fd = file (GLUSTERFS_CIFS_CONFIG, "a")
        cifs_config_fd.write ("[%s]\n" % self.volume_name)
        cifs_config_fd.write ("comment = %s volume served by Gluster\n" %
                              self.volume_name)
        cifs_config_fd.write ("path = %s\n" % str("/cifs/" + self.volume_name))
        if self.enable_guest:
            cifs_config_fd.write ("guest ok = yes\n")
        cifs_config_fd.write ("public = yes\n")
        cifs_config_fd.write ("writable = yes\n")
        cifs_config_fd.write ("users = %s\n" % self.username)
        cifs_config_fd.close()
        return

    def configure_booster (self):

        self.configure_booster_fstab()
        if self.need_nfs:
            self.configure_nfs_booster()
            print "Generating booster configuration for NFS reexport"
        if self.need_cifs:
            self.configure_cifs_booster()
            print "Generating booster configuration for CIFS reexport"

        return
