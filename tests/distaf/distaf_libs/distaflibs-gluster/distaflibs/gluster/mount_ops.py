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


def mount_volume(volname, mtype='glusterfs', mpoint='/mnt/glusterfs', \
        mserver='', mclient='', options=''):
    """
        Mount the gluster volume with specified options
        Takes the volume name as mandatory argument

        Returns a tuple of (returncode, stdout, stderr)
        Returns (0, '', '') if already mounted
    """
    global tc
    if mserver == '':
        mserver = tc.servers[0]
    if mclient == '':
        mclient = tc.clients[0]
    if options != '':
        options = "-o %s" % options
    if mtype == 'nfs' and options != '':
        options = "%s,vers=3" % options
    elif mtype == 'nfs' and options == '':
        options = '-o vers=3'
    ret, _, _ = tc.run(mclient, "mount | grep %s | grep %s | grep \"%s\"" \
            % (volname, mpoint, mserver), verbose=False)
    if ret == 0:
        tc.logger.debug("Volume %s is already mounted at %s" \
        % (volname, mpoint))
        return (0, '', '')
    mcmd = "mount -t %s %s %s:%s %s" % \
            (mtype, options, mserver, volname, mpoint)
    tc.run(mclient, "test -d %s || mkdir -p %s" % (mpoint, mpoint), \
            verbose=False)
    return tc.run(mclient, mcmd)


def umount_volume(client, mountpoint):
    """
        unmounts the mountpoint
        Returns the output of umount command
    """
    cmd = "umount %s || umount -f %s || umount -l %s" \
            % (mountpoint, mountpoint, mountpoint)
    return tc.run(client, cmd)
