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
import os
import urllib

from syncdaemon.gstatus import GeorepStatus, set_monitor_status
from syncdaemon.gstatus import get_default_values
from syncdaemon.gstatus import MONITOR_STATUS, DEFAULT_STATUS
from syncdaemon.gstatus import STATUS_VALUES, CRAWL_STATUS_VALUES
from syncdaemon.gstatus import human_time, human_time_utc


class GeorepStatusTestCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work_dir = os.path.dirname(os.path.abspath(__file__))
        cls.monitor_status_file = os.path.join(cls.work_dir, "monitor.status")
        cls.brick = "/exports/bricks/b1"
        cls.status = GeorepStatus(cls.monitor_status_file, cls.brick)
        cls.statusfile = os.path.join(cls.work_dir,
                                      "brick_%s.status"
                                      % urllib.quote_plus(cls.brick))

    @classmethod
    def tearDownClass(cls):
        os.remove(cls.statusfile)
        os.remove(cls.monitor_status_file)

    def _filter_dict(self, inp, keys):
        op = {}
        for k in keys:
            op[k] = inp.get(k, None)
        return op

    def test_monitor_status_file_created(self):
        self.assertTrue(os.path.exists(self.monitor_status_file))

    def test_status_file_created(self):
        self.assertTrue(os.path.exists(self.statusfile))

    def test_set_monitor_status(self):
        for st in MONITOR_STATUS:
            set_monitor_status(self.monitor_status_file, st)
            self.assertTrue(self.status.get_monitor_status(), st)

    def test_default_values_test(self):
        self.assertTrue(get_default_values(), {
            "slave_node": DEFAULT_STATUS,
            "worker_status": DEFAULT_STATUS,
            "last_synced": 0,
            "last_synced_utc": 0,
            "crawl_status": DEFAULT_STATUS,
            "entry": 0,
            "data": 0,
            "metadata": 0,
            "failures": 0,
            "checkpoint_completed": False,
            "checkpoint_time": 0,
            "checkpoint_time_utc": 0,
            "checkpoint_completion_time": 0,
            "checkpoint_completion_time_utc": 0
        })

    def test_human_time(self):
        self.assertTrue(human_time(1429174398), "2015-04-16 14:23:18")

    def test_human_time_utc(self):
        self.assertTrue(human_time_utc(1429174398), "2015-04-16 08:53:18")

    def test_invalid_human_time(self):
        self.assertTrue(human_time(142917439), DEFAULT_STATUS)
        self.assertTrue(human_time("abcdef"), DEFAULT_STATUS)

    def test_invalid_human_time_utc(self):
        self.assertTrue(human_time_utc(142917439), DEFAULT_STATUS)
        self.assertTrue(human_time_utc("abcdef"), DEFAULT_STATUS)

    def test_worker_status(self):
        set_monitor_status(self.monitor_status_file, "Started")
        for st in STATUS_VALUES:
            self.status.set_worker_status(st)
            self.assertTrue(self.status.get_status()["worker_status"], st)

    def test_crawl_status(self):
        set_monitor_status(self.monitor_status_file, "Started")
        self.status.set_active()
        for st in CRAWL_STATUS_VALUES:
            self.status.set_worker_crawl_status(st)
            self.assertTrue(self.status.get_status()["crawl_status"], st)

    def test_slave_node(self):
        set_monitor_status(self.monitor_status_file, "Started")
        self.status.set_active()
        self.status.set_slave_node("fvm2")
        self.assertTrue(self.status.get_status()["slave_node"], "fvm2")

        self.status.set_worker_status("Passive")
        self.status.set_slave_node("fvm2")
        self.assertTrue(self.status.get_status()["slave_node"], "fvm2")

    def test_active_worker_status(self):
        set_monitor_status(self.monitor_status_file, "Started")
        self.status.set_active()
        self.assertTrue(self.status.get_status()["worker_status"], "Active")

    def test_passive_worker_status(self):
        set_monitor_status(self.monitor_status_file, "Started")
        self.status.set_passive()
        self.assertTrue(self.status.get_status()["worker_status"], "Passive")

    def test_set_field(self):
        set_monitor_status(self.monitor_status_file, "Started")
        self.status.set_active()
        self.status.set_field("entry", 42)
        self.assertTrue(self.status.get_status()["entry"], 42)

    def test_inc_value(self):
        set_monitor_status(self.monitor_status_file, "Started")
        self.status.set_active()
        self.status.set_field("entry", 0)
        self.status.inc_value("entry", 2)
        self.assertTrue(self.status.get_status()["entry"], 2)

        self.status.set_field("data", 0)
        self.status.inc_value("data", 2)
        self.assertTrue(self.status.get_status()["data"], 2)

        self.status.set_field("meta", 0)
        self.status.inc_value("meta", 2)
        self.assertTrue(self.status.get_status()["meta"], 2)

        self.status.set_field("failures", 0)
        self.status.inc_value("failures", 2)
        self.assertTrue(self.status.get_status()["failures"], 2)

    def test_dec_value(self):
        set_monitor_status(self.monitor_status_file, "Started")
        self.status.set_active()

        self.status.set_field("entry", 4)
        self.status.inc_value("entry", 2)
        self.assertTrue(self.status.get_status()["entry"], 2)

        self.status.set_field("data", 4)
        self.status.inc_value("data", 2)
        self.assertTrue(self.status.get_status()["data"], 2)

        self.status.set_field("meta", 4)
        self.status.inc_value("meta", 2)
        self.assertTrue(self.status.get_status()["meta"], 2)

        self.status.set_field("failures", 4)
        self.status.inc_value("failures", 2)
        self.assertTrue(self.status.get_status()["failures"], 2)

    def test_worker_status_when_monitor_status_created(self):
        set_monitor_status(self.monitor_status_file, "Created")
        for st in STATUS_VALUES:
            self.status.set_worker_status(st)
            self.assertTrue(self.status.get_status()["worker_status"],
                            "Created")

    def test_worker_status_when_monitor_status_paused(self):
        set_monitor_status(self.monitor_status_file, "Paused")
        for st in STATUS_VALUES:
            self.status.set_worker_status(st)
            self.assertTrue(self.status.get_status()["worker_status"],
                            "Paused")

    def test_worker_status_when_monitor_status_stopped(self):
        set_monitor_status(self.monitor_status_file, "Stopped")
        for st in STATUS_VALUES:
            self.status.set_worker_status(st)
            self.assertTrue(self.status.get_status()["worker_status"],
                            "Stopped")

    def test_status_when_worker_status_active(self):
        set_monitor_status(self.monitor_status_file, "Started")
        self.status.set_active()


if __name__ == "__main__":
    unittest.main()
