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

import unittest
import gluster.swift.common.constraints
from gluster.swift.common.ring import *
from gluster.swift.common.Glusterfs import SWIFT_DIR

def _mock_ring_data():
    return [{'zone': 1, 'weight': 100.0, 'ip': '127.0.0.1', 'port': 6012, \
                 'meta': '', 'device': 'test', 'id': 0},
            {'zone': 2, 'weight': 100.0, 'ip': '127.0.0.1', 'id': 1, \
                 'meta': '', 'device': 'iops', 'port': 6012}]

class TestRing(unittest.TestCase):
    """ Tests for common.utils """

    def setUp(self):
        self.ring = Ring(SWIFT_DIR, ring_name='object')

    def test_get_notes(self):
        try:
            __devs = self.ring._devs
            self.ring._devs = _mock_ring_data()
            part, node = self.ring.get_nodes('test')
            assert node[0]['device'] == 'test'
            part, node = self.ring.get_nodes('test2')
            assert node
            assert node[0]['device'] == 'volume_not_in_ring'
        finally:
            self.ring._devs = __devs
