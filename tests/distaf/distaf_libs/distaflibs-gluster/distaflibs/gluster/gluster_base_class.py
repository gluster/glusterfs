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
from distaflibs.gluster.volume_ops import (setup_vol, get_volume_info,
                                           cleanup_volume)


class GlusterBaseClass():
    """
        This is the base class for the distaf tests

        All tests can subclass this and then write test cases
    """

    def __init__(self, config_data):
        """
            Initialise the class with the config values
        """
        if config_data['global_mode']:
            self.volname = config_data['volumes'].keys()[0]
            self.voltype = config_data['volumes'][self.volname]['voltype']
            self.servers = config_data['volumes'][self.volname]['servers']
            self.peers = config_data['volumes'][self.volname]['peers']
            self.clients = config_data['volumes'][self.volname]['clients']
            self.mount_proto = (config_data['volumes'][self.volname]
                                ['mount_proto'])
            self.mountpoint = (config_data['volumes'][self.volname]
                               ['mountpoint'])
        else:
            self.voltype = config_data['voltype']
            self.volname = "%s-testvol" % self.voltype
            self.servers = config_data['servers'].keys()
            self.clients = config_data['clients'].keys()
            self.peers = []
            if config_data['peers'] is not None:
                self.peers = config_data['peers'].keys()
            self.mount_proto = config_data['mount_proto']
            self.mountpoint = "/mnt/%s_mount" % self.mount_proto
        self.mnode = self.servers[0]
        self.config_data = config_data

    def _create_volume(self):
        """
         Create the volume with proper configurations
        """
        dist = rep = dispd = red = stripe = 1
        trans = ''
        if self.voltype == 'distribute':
            dist = self.config_data[self.voltype]['dist_count']
            trans = self.config_data[self.voltype]['transport']
        elif self.voltype == 'replicate':
            rep = self.config_data[self.voltype]['replica']
            trans = self.config_data[self.voltype]['transport']
        elif self.voltype == 'dist_rep':
            dist = self.config_data[self.voltype]['dist_count']
            rep = self.config_data[self.voltype]['replica']
            trans = self.config_data[self.voltype]['transport']
        elif self.voltype == 'disperse':
            dispd = self.config_data[self.voltype]['disperse']
            red = self.config_data[self.voltype]['redundancy']
            trans = self.config_data[self.voltype]['transport']
        elif self.voltype == 'dist_disperse':
            dist = self.config_data[self.voltype]['dist_count']
            dispd = self.config_data[self.voltype]['disperse']
            red = self.config_data[self.voltype]['redundancy']
            trans = self.config_data[self.voltype]['transport']
        else:
            tc.logger.error("The volume type is not present")
            return False
        ret = setup_vol(self.volname, dist, rep, dispd, red, stripe, trans,
                        servers=self.servers)
        if not ret:
            tc.logger.error("Unable to setup volume %s", self.volname)
            return False
        return True

    def setup(self):
        """
            Function to setup the volume for testing.
        """
        volinfo = get_volume_info(server=self.servers[0])
        if volinfo is not None and self.volname in volinfo.keys():
            tc.logger.debug("The volume %s is already present in %s",
                            self.volname, self.mnode)
            if not self.config_data['reuse_setup']:
                ret = cleanup_volume(self.volname, self.mnode)
                if not ret:
                    tc.logger.error("Unable to cleanup the setup")
                    return False
                return self._create_volume()
        else:
            return self._create_volume()
        return True

    def teardown(self):
        """
            The function to cleanup the test setup
        """
        return True

    def cleanup(self):
        """
            The function to cleanup the volume
        """
        return cleanup_volume(self.volname, self.mnode)
