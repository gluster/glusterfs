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


from distaf.util import tc, testcase
from distaflibs.gluster.class_setup_nfs_ganesha_vol import SetupNfsGaneshaVol


class TestSetupNfsGaneshaVol(SetupNfsGaneshaVol):
    """
        Test case to setup NFS-Ganesha
    """
    def __init__(self, config_data, nfs_options="vers=3"):
        """
            Initialise the class with the config values
            Kwargs:
                nfs_options (str): This argument takes the nfs options,
                    say vers=3 or vers=4.
                    Default value is vers=3
        """
        tc.logger.info("Testcase to setup NFS-Ganesha volume %s"
                       % nfs_options)
        SetupNfsGaneshaVol.__init__(self, config_data, nfs_options)

    def run(self):
        return True


@testcase("test_setup_nfs_ganesha_vol_v3")
class TestSetupNfsGaneshaVolV3(TestSetupNfsGaneshaVol):
    """
        Test case to setup NFS-Ganesha and
        export volume with vers=3
    """
    def ___init__(self, config_data):
        TestSetupNfsGaneshaVol.__init__(self, config_data,
                                        nfs_options="vers=3")


@testcase("test_setup_nfs_ganesha_vol_v4")
class TestSetupNfsGaneshaVolV4(TestSetupNfsGaneshaVol):
    """
        Test case to setup NFS-Ganesha and
        export volume with vers=3
    """
    def ___init__(self, config_data):
        TestSetupNfsGaneshaVol.__init__(self, config_data,
                                        nfs_options="vers=4")
