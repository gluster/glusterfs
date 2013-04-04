# Copyright (c) 2013 Red Hat, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
# implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import unittest
import gluster.swift.common.constraints
import swift.common.utils
from gluster.swift.common.ring import Ring


class TestRing(unittest.TestCase):
    """ Tests for common.utils """

    def setUp(self):
        swift.common.utils.HASH_PATH_SUFFIX = 'endcap'
        swiftdir = os.path.join(os.getcwd(), "common", "data")
        self.ring = Ring(swiftdir, ring_name='object')

    def test_first_device(self):
        part, node = self.ring.get_nodes('test')
        assert node[0]['device'] == 'test'
        node = self.ring.get_part_nodes(0)
        assert node[0]['device'] == 'test'
        for node in self.ring.get_more_nodes(0):
            assert node['device'] == 'volume_not_in_ring'

    def test_invalid_device(self):
        part, node = self.ring.get_nodes('test2')
        assert node[0]['device'] == 'volume_not_in_ring'
        node = self.ring.get_part_nodes(0)
        assert node[0]['device'] == 'volume_not_in_ring'

    def test_second_device(self):
        part, node = self.ring.get_nodes('iops')
        assert node[0]['device'] == 'iops'
        node = self.ring.get_part_nodes(0)
        assert node[0]['device'] == 'iops'
        for node in self.ring.get_more_nodes(0):
            assert node['device'] == 'volume_not_in_ring'

    def test_second_device_with_reseller_prefix(self):
        part, node = self.ring.get_nodes('AUTH_iops')
        assert node[0]['device'] == 'iops'
