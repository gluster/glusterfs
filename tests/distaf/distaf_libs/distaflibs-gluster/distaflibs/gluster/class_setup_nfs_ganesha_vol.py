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


import time
from distaflibs.gluster.mount_ops import mount_volume, umount_volume
from distaflibs.gluster.volume_ops import (setup_vol, get_volume_info,
                                           get_volume_status)
from distaf.util import testcase, tc
from distaflibs.gluster.ganesha import (setup_nfs_ganesha,
                                        teardown_nfs_ganesha_setup)
from distaflibs.gluster.gluster_base_class import GlusterBaseClass


class SetupNfsGaneshaVol(GlusterBaseClass):
    """
        This is the base class for the ganesha-gluster tests
        It is a subclass of GlusterBaseClass. All ganesha-gluster
        tests can subclass this and then write test cases
    """

    def __init__(self, config_data, nfs_options="vers=3"):
        """
            Initialise the class with the config values
            Kwargs:
                nfs_options (str): This argument takes the nfs options,
                    say vers=3 or vers=4.
                    Default value is vers=3.
        """
        GlusterBaseClass.__init__(self, config_data)
        self.options = nfs_options
        self.no_of_ganesha_nodes = (config_data["gluster"]
                                    ["cluster_config"]["nfs_ganesha"]
                                    ["num_of_nfs_ganesha_nodes"])
        self.no_of_ganesha_nodes = int(self.no_of_ganesha_nodes)
        self.vips = (config_data["gluster"]["cluster_config"]
                     ["nfs_ganesha"]["vips"])

    def setup(self):
        """
            Function to setup ganesha and create volume for testing.
        """
        ret = setup_nfs_ganesha(self.no_of_ganesha_nodes)
        if ret:
            tc.logger.info("setup of ganesha for %s node is successfull"
                           % self.no_of_ganesha_nodes)
        else:
            tc.logger.error("setup of ganesha for %s node is unsuccessfull"
                            % self.no_of_ganesha_nodes)
            return False
        ret = GlusterBaseClass.setup(self)
        if not ret:
            return False
        time.sleep(10)
        ret = get_volume_status(self.volname)
        if ret is None:
            return False
        ret = get_volume_info(self.volname)
        if ret is None:
            return False
        ret, out, err = tc.run(self.mnode, "showmount -e localhost")
        if ret != 0:
            return False
        ret, out, err = mount_volume(self.volname, self.mount_proto,
                                     self.mountpoint, self.vips[0],
                                     self.clients[0], self.options)
        if ret != 0:
            tc.logger.error("Mounting Volume %s failed on %s:%s" %
                            (self.volname, self.clients[0], self.mountpoint))
            return False

        return True

    def teardown(self, teardown_ganesha_setup=False):
        """
            The function to cleanup the test setup
            Kwargs:
                teardown_ganesha_setup (bool): If True teardowns ganesha setup,
                    else leaves the ganesha setup as it is.
                    Default value is False
        """
        umount_volume(tc.clients[0], self.mountpoint)
        ret, out, err = tc.run(tc.clients[0], "rm -rf %s/*" % self.mountpoint)
        time.sleep(5)
        if ret != 0:
            tc.logger.error("rm -rf command failed on the mountpoint %s"
                            % self.mountpoint)
            return False
        if teardown_ganesha_setup:
            ret = teardown_nfs_ganesha_setup()
            return ret
        return True

    def cleanup(Self, delete_vol=False):
        """
            The function to cleanup the volume
            Kwargs:
                delete_vol (bool): If True deletes the volume.
                    else leaves the volume as it is.
                    Defualt value is False
        """
        if not delete_vol:
            return True
        return GlusterBaseClass.cleanup(self)
