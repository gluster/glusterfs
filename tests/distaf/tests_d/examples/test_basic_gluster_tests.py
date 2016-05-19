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


from distaf.util import tc, testcase
from distaflibs.gluster.gluster_base_class import GlusterBaseClass
from distaflibs.gluster.mount_ops import mount_volume, umount_volume


@testcase("gluster_basic_test")
class gluster_basic_test(GlusterBaseClass):
    """
        runs_on_volumes: ALL
        runs_on_protocol: [ glusterfs, nfs ]
        reuse_setup: False
    """
    def run(self):
        _rc = True
        client = self.clients[0]
        tc.run(self.mnode, "gluster volume status %s" % self.volname)
        ret, _, _ = mount_volume(self.volname, self.mount_proto,
                                 self.mountpoint, mclient=client)
        if ret != 0:
            tc.logger.error("Unable to mount the volume %s in %s"
                            "Please check the logs" % (self.volname, client))
            return False
        ret, _, _ = tc.run(client, "cp -r /etc %s" % self.mountpoint)
        if ret != 0:
            tc.logger.error("cp failed in %s. Please check the logs" % client)
            _rc = False
        tc.run(client, "rm -rf %s/etc" % self.mountpoint)
        umount_volume(client, self.mountpoint)
        return _rc


@testcase("dummy_testcase")
class dummy_testcase(GlusterBaseClass):
    """
        runs_on_volumes: ALL
        runs_on_protocol: [ glusterfs, nfs ]
        reuse_setup: False
    """
    def run(self):
        _rc = True
        ret, _, _ = tc.run(self.mnode, "gluster volume status %s" % self.volname)
        if ret != 0:
            tc.logger.error("Unable to thet the status of %s", self.volname)
            _rc = False

        return _rc
