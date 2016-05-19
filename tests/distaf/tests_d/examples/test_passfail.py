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


@testcase("this_should_pass")
class GoingToPass(GlusterBaseClass):
    """ Testing connectivity and framework pass
    This is an example of a basic distaf test with mixed comment and config
    Any necessary description doc string text goes here and can include any
        plain text normally found in a docstring.
    Distaf specific config yaml can be included using the yaml standard
        document triple-dash separator below.
    ---
    runs_on_volumes: [ distributed ]
    runs_on_protocol: [ glusterfs ]
    reuse_setup: False
    tags:
      - tag1
      - tag2
      - tag3
    """
    def setup(self):
        return True

    def run(self):
        config = self.config_data
        tc.logger.info("Testing connection and command exec")
        tc.logger.debug("Tag 1 is %s" % config["tags"][0])
        ret, _, _ = tc.run(self.servers[0], "hostname")
        if ret != 0:
            tc.logger.error("hostname command failed")
            return False
        else:
            return True

    def setup(self):
        return True

    def cleanup(self):
        return True

    def teardown(self):
        return True


@testcase("this_should_fail")
class GoingToFail(GlusterBaseClass):
    """ Testing connectivity and fail
    ---
    runs_on_volumes: [ distributed ]
    runs_on_protocol: [ glusterfs, cifs ]
    reuse_setup: False
    tags:
      - tag1
      - tag2
      - tag3
    """
    def setup(self):
        return True

    def run(self):
        config = self.config_data
        tc.logger.info("Testing fail output")
        tc.logger.debug("Tag 1 is %s" % config["tags"][0])
        ret, _, _ = tc.run(self.servers[0], "false")
        if ret != 0:
            tc.logger.error("false command failed")
            return False
        else:
            return True

    def setup(self):
        return True

    def cleanup(self):
        return True

    def teardown(self):
        return True
