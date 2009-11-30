GLUSTERFS_BOOSTER_FSTAB = "booster.fstab"
GLUSTERFS_UNFS3_EXPORTS = "boosterexports"
GLUSTERFS_CIFS_CONFIG = "boostersmb.conf"
LOGDIR = "/var/log/glusterfs"
fstype = "glusterfs"

class CreateBooster:

    def __init__ (self, options, transports):

        self.volume_name = options.volume_name
        self.need_nfs = options.need_nfs
        self.need_cifs = options.need_cifs
        self.username = options.cifs_username
        self.enable_guest = options.enable_guest
        self.conf_dir = options.conf_dir
        self.transports = transports

    def configure_booster_fstab (self):

        _fstab = ""
        _options = ""
        _options_log = ""
        _options_ext = ""

        if self.conf_dir:
            booster_fstab_fd = file ("%s/%s" % (str(self.conf_dir),
                                                GLUSTERFS_BOOSTER_FSTAB), "a")
        else:
            booster_fstab_fd = file (GLUSTERFS_BOOSTER_FSTAB, "a")

        if self.need_nfs:
            for transport in self.transports:
                if self.conf_dir:
                    _fstab = "%s/%s-%s.vol  %s" % (str(self.conf_dir),
                                                   self.volume_name,
                                                   transport,
                                                   str("/nfs/" +
                                                       self.volume_name))
                else:
                    _fstab = "%s-%s.vol  %s" % (self.volume_name,
                                                transport,
                                                str("/nfs/" +
                                                    self.volume_name))

            _options = "%s" % fstype
            _options_log = "logfile=%s/%s-nfs.log" % (LOGDIR, self.volume_name)
            _options_ext = "loglevel=ERROR,attr_timeout=0"
            booster_fstab_fd.write ("%s %s %s,%s\n" %
                                    (_fstab,
                                     _options,
                                     _options_log,
                                     _options_ext))

        if self.need_cifs:
            for transport in self.transports:
                if self.conf_dir:
                    _fstab = "%s/%s-%s.vol  %s" % (self.conf_dir,
                                                   self.volume_name,
                                                   transport,
                                                   str("/cifs/" +
                                                       self.volume_name))
                else:
                    _fstab = "%s-%s.vol  %s" % (self.volume_name,
                                                transport,
                                                str("/cifs/" +
                                                    self.volume_name))

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

        if self.conf_dir:
            nfs_exports_fd = file ("%s/%s" % (str(self.conf_dir),
                                              GLUSTERFS_UNFS3_EXPORTS), "a")
        else:
            nfs_exports_fd = file (GLUSTERFS_UNFS3_EXPORTS, "a")

        nfs_exports_fd.write ("%s  0.0.0.0/0(rw,no_root_squash)\n" %
                              str("/nfs/" + self.volume_name))
        return

    def configure_cifs_booster (self):

        if self.conf_dir:
            cifs_config_fd = file ("%s/%s" % (str(self.conf_dir),
                                              GLUSTERFS_CIFS_CONFIG), "a")
        else:
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
