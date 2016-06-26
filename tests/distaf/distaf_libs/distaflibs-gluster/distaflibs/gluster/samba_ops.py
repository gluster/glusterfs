#!/usr/bin/env python
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


def setup_samba_service(volname, mnode, username, smbpasswd):
    """This module Sets up the servers for samba mount
       Takes care of editing the glusterd-volfile and
       starting the samba services
    Args:
        volname(str): Volume name
        mnode(str): where the samba setup will be done
        username: samba username
        smbpasswd: samba password
    Returns:
        bool:
            True: On Success
            False: On Failure
    Example:
        setup_samba_service("testvol", "tc.servers[0]", "Bond", "redhat")
    """
    try:
        if tc.global_flag['SAMBA_SETUP_DONE'] == True:
            tc.logger.error("samba is already setup for the cluster")
            return True
    except KeyError:
        pass

    glusterd_volfile = "/etc/glusterfs/glusterd.vol"
    glstrd_edit_cmd = ("grep -F 'option rpc-auth-allow-insecure on' %s > "
                       "/dev/null || (cp %s %s.orig && "
                       "sed -i '/^end-volume/d' %s && "
                       "echo '    option rpc-auth-allow-insecure on' >> %s && "
                       "echo 'end-volume' >> %s && "
                       "service glusterd restart)"
                       % (glusterd_volfile, glusterd_volfile,
                          glusterd_volfile, glusterd_volfile,
                          glusterd_volfile, glusterd_volfile))

    ret, _ = tc.run_servers(glstrd_edit_cmd)
    if not ret:
        tc.logger.error("Unable to edit glusterd in all servers")
        return False

    ret, _, _ = tc.run(mnode, "chkconfig smb on")

    if ret != 0:
        tc.logger.error("Unable to set chkconfig smb on")
        return False

    tc.logger.info("chkconfig smb on successfull")

    ret, _, _ = tc.run(mnode, "service smb start")
    if ret != 0:
        tc.logger.error("Unable to start the smb service")
        return False

    smbpasswd_cmd = ("(echo \"%s\"; echo \"%s\") | smbpasswd -a %s" %
                     (smbpasswd, smbpasswd, username))

    ret, _, _ = tc.run(mnode, smbpasswd_cmd)
    if ret != 0:
        tc.logger.error("Unable to set the smb password")
        return False

    time.sleep(20)

    ret, out, err = tc.run(mnode, "gluster volume set %s "
                           "server.allow-insecure on" % volname)
    if ret != 0:
        tc.logger.error("Failed to set the volume option "
                        "server-allow-insecure")
        return False

    ret, out, err = tc.run(mnode, "gluster volume set %s stat-prefetch off"
                           % volname)
    if ret != 0:
        tc.logger.error("Failed to set the volume option stat-prefetch off")
        return False

    set_volume_cmd = ("gluster volume set %s storage.batch-fsync-delay-usec 0"
                      % volname)
    ret, out, err = tc.run(mnode, set_volume_cmd)
    if ret != 0:
        tc.logger.error("Failed to set the volume option storage"
                        "batch-fsync-delay-usec 0")
        return False

    smb_cmd = "smbclient -L localhost -U " + username + "%" + smbpasswd

    ret, out, err = tc.run(mnode, smb_cmd + "| grep -i -Fw gluster-%s "
                           % volname)
    if ret != 0:
        tc.logger.error("unable to find share entry")
        return False
    tc.logger.info("Share entry present")

    tc.global_flag['SAMBA_SETUP_DONE'] = True
    return True
