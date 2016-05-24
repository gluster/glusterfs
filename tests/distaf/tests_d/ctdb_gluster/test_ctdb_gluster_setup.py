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
from distaflibs.gluster.gluster_base_class import GlusterBaseClass
from distaflibs.gluster.ctdb_libs import ctdb_gluster_setup


@testcase("test_ctdb_gluster_setup")
class TestCtdbGlusterSetup(GlusterBaseClass):
    """
        Test case to setup CTDB on gluster setup
    """
    def __init__(self, config_data):
        """
            Initialise the class with the config values
        """
        tc.logger.info("Starting testcase for CTDB gluster setup")
        GlusterBaseClass.__init__(self, config_data)

    def setup(self):
        """
           The function to setup the CTDB setup
        """
        ret = ctdb_gluster_setup()
        return ret

    def run(self):
        return True

    def cleanup(self):
        """
            The function to cleanup the test setup
        """
        return True
