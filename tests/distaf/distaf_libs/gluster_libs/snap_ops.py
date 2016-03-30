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
from distaf.gluster_libs.volume_ops import start_volume, stop_volume


def snap_create(volname, snapname, server='', desc=''):
    """
        Runs snap create command and returns the output
    """
    if server == '':
        server = tc.nodes[0]
    if desc != '':
        desc = "description %s" % desc
    ret = tc.run(server, "gluster snapshot create %s %s %s" \
            % (volname, snapname, desc))
    return ret


def snap_activate(snapname, server=''):
    """
        Activate the snap and returns the output
    """
    if server == '':
        server = tc.nodes[0]
    return tc.run(server, "gluster snapshot activate %s" % snapname)


def snap_delete(snapname, server=''):
    """
        Deletes snapshot and returns the output
    """
    if server == '':
        server = tc.nodes[0]
    cmd = "gluster snapshot delete %s --mode=script" % snapname
    return tc.run(server, cmd)


def snap_delete_all(volname, server=''):
    """
        Deletes one or more snaps and returns the output
    """
    if server == '':
        server = tc.nodes[0]
    cmd = 'ret=0; for i in `gluster snapshot list %s`; do \
gluster snapshot delete $i --mode=script || ret=1; done ; exit $ret' % volname
    return tc.run(server, cmd)


def snap_restore(volname, snapname, server=''):
    """
        stops the volume restore the snapshot and starts the volume

        Returns True upon success, False on in any step
    """
    if server == '':
        server = tc.nodes[0]
    ret = stop_volume(volname, server)
    if not ret:
        return False
    ret = tc.run(server, "gluster snapshot restore %s" % snapname)
    if ret[0] != 0:
        tc.logger.error("snapshot restore failed")
        return False
    ret = start_volume(volname, server)
    if not ret:
        return False
    return True
