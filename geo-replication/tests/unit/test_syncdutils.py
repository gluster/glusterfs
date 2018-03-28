#!/usr/bin/python2
#
# Copyright (c) 2011-2014 Red Hat, Inc. <http://www.redhat.com>
# This file is part of GlusterFS.

# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.
#

import unittest

from syncdaemon import syncdutils


class SyncdutilsTestCase(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    def test_escape(self):
        self.assertEqual(syncdutils.escape("http://gluster.org"),
                         "http%3A%2F%2Fgluster.org")

    def test_unescape(self):
        self.assertEqual(syncdutils.unescape("http%3A%2F%2Fgluster.org"),
                         "http://gluster.org")
