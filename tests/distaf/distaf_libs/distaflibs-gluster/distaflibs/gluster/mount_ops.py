#  This file is part of DiSTAF
#  Copyright (C) 2015-2016  Red Hat, Inc. <http://www.redhat.com>
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.


from distaf.util import tc

class GlusterMount():
    """Gluster Mount class

    Args:
        mount (dict): Mount dict with 'mount_protocol', 'mountpoint',
            'server', 'client', 'volname', 'options' as keys

    Returns:
        Instance of GlusterMount class
   """
    client_register = 0

    def __init__(self, mount):
        if mount['protocol']:
            self.mounttype = mount['protocol']
        else:
            self.mounttype = "glusterfs"

        if mount['mountpoint']:
            self.mountpoint = mount['mountpoint']
        else:
            self.mountpoint = "/mnt/%s" % self.mounttype

        self.server_system = mount['server']
        self.client_system = mount['client']
        self.volname = mount['volname']
        self.options = mount['options']

    def mount(self):
        """Mounts the volume

        Args:
            uses instance args passed at init

        Returns:
            bool: True on success and False on failure.
        """
        (_retcode, _, _) = mount_volume(self.volname,
                                        mtype=self.mounttype,
                                        mpoint=self.mountpoint,
                                        mserver=self.server_system,
                                        mclient=self.client_system,
                                        options=self.options)

        if _retcode == 0:
            return True
        else:
            return False

    def is_mounted(self):
        """Tests for mount on client

        Args:
            uses instance args passed at init

        Returns:
            bool: True on success and False on failure.
        """
        _retcode = is_mounted(self.volname,
                              mpoint=self.mountpoint,
                              mserver=self.server_system,
                              mclient=self.client_system)

        if _retcode:
            return True
        else:
            return False

    def unmount(self):
        """Unmounts the volume

        Args:
            uses instance args passed at init

        Returns:
            bool: True on success and False on failure.
        """
        (_retcode, _, _) = umount_volume(self.client_system,
                                         self.mountpoint)

        if _retcode == 0:
            return True
        else:
            return False

def is_mounted(volname, mpoint, mserver, mclient):
    """Check if mount exist.

    Args:
        volname (str): Name of the volume
        mpoint (str): Mountpoint dir
        mserver (str): Server to which it is mounted to
        mclient (str): Client from which it is mounted.

    Returns:
        bool: True if mounted and False otherwise.
    """
    # python will error on missing arg, so just checking for empty args here
    if not volname or not mpoint or not mserver or not mclient:
        tc.logger.error("Missing arguments for mount.")
        return False

    ret, _, _ = tc.run(mclient, "mount | grep %s | grep %s | grep \"%s\""
                       % (volname, mpoint, mserver), verbose=False)
    if ret == 0:
        tc.logger.debug("Volume %s is mounted at %s:%s" % (volname,
                                                           mclient,
                                                           mpoint))
        return True
    else:
        tc.logger.error("Volume %s is not mounted at %s:%s" % (volname,
                                                               mclient,
                                                               mpoint))
        return False

def mount_volume(volname, mtype='glusterfs', mpoint='/mnt/glusterfs',
                 mserver='', mclient='', options=''):
    """Mount the gluster volume with specified options.

    Args:
        volname (str): Name of the volume to mount.

    Kwargs:
        mtype (str): Protocol to be used to mount.
        mpoint (str): Mountpoint dir.
        mserver (str): Server to mount.
        mclient (str): Client from which it has to be mounted.
        option (str): Options for the mount command.

    Returns:
        tuple: Tuple containing three elements (ret, out, err).
            (0, '', '') if already mounted.
            (1, '', '') if setup_samba_service fails in case of smb.
            (ret, out, err) of mount commnd execution otherwise.
    """
    global tc
    if mserver == '':
        mserver = tc.servers[0]
    if mclient == '':
        mclient = tc.clients[0]
    if options != '':
        options = "-o %s" % options
    if mtype == 'nfs' and options != '':
        options = "%s" % options
    elif mtype == 'nfs' and options == '':
        options = '-o vers=3'

    if is_mounted(volname, mpoint, mserver, mclient):
        tc.logger.debug("Volume %s is already mounted at %s" %
                        (volname, mpoint))
        return (0, '', '')

    mcmd = ("mount -t %s %s %s:/%s %s" %
            (mtype, options, mserver, volname, mpoint))

    if mtype == 'cifs':
        from distaflibs.gluster.samba_ops import setup_samba_service
        smbuser = tc.global_config['gluster']['cluster_config']['smb']['user']
        smbpasswd = (tc.global_config['gluster']['cluster_config']['smb']
                     ['passwd'])

        if not setup_samba_service(volname, mserver, smbuser, smbpasswd):
            tc.logger.error("Failed to setup samba service %s" % mserver)
            return (1, '', '')

        mcmd = ("mount -t cifs -o username=root,password=%s "
                "\\\\\\\\%s\\\\gluster-%s %s" % (smbpasswd, mserver,
                                                 volname, mpoint))
    # Create mount dir
    _, _, _ = tc.run(mclient, "test -d %s || mkdir -p %s" % (mpoint, mpoint),
                     verbose=False)

    # Create mount
    return tc.run(mclient, mcmd)


def umount_volume(mclient, mpoint):
    """Unmounts the mountpoint.

    Args:
        mclient (str): Client from which it has to be mounted.
        mpoint (str): Mountpoint dir.

    Returns:
        tuple: Tuple containing three elements (ret, out, err) as returned by
            umount command execution.
    """
    cmd = ("umount %s || umount -f %s || umount -l %s" %
           (mpoint, mpoint, mpoint))
    return tc.run(mclient, cmd)
