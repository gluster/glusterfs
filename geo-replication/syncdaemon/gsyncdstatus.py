#!/usr/bin/env python
#
# Copyright (c) 2011-2014 Red Hat, Inc. <http://www.redhat.com>
# This file is part of GlusterFS.

# This file is licensed to you under your choice of the GNU Lesser
# General Public License, version 3 or any later version (LGPLv3 or
# later), or the GNU General Public License, version 2 (GPLv2), in all
# cases as published by the Free Software Foundation.
#

import fcntl
import os
import tempfile
import urllib
import json
import time
from datetime import datetime
from errno import EACCES, EAGAIN, ENOENT
import logging

from syncdutils import EVENT_GEOREP_ACTIVE, EVENT_GEOREP_PASSIVE, gf_event
from syncdutils import EVENT_GEOREP_CHECKPOINT_COMPLETED

DEFAULT_STATUS = "N/A"
MONITOR_STATUS = ("Created", "Started", "Paused", "Stopped")
STATUS_VALUES = (DEFAULT_STATUS,
                 "Initializing...",
                 "Active",
                 "Passive",
                 "Faulty")

CRAWL_STATUS_VALUES = (DEFAULT_STATUS,
                       "Hybrid Crawl",
                       "History Crawl",
                       "Changelog Crawl")


def human_time(ts):
    try:
        return datetime.fromtimestamp(float(ts)).strftime("%Y-%m-%d %H:%M:%S")
    except ValueError:
        return DEFAULT_STATUS


def human_time_utc(ts):
    try:
        return datetime.utcfromtimestamp(
            float(ts)).strftime("%Y-%m-%d %H:%M:%S")
    except ValueError:
        return DEFAULT_STATUS


def get_default_values():
    return {
        "slave_node": DEFAULT_STATUS,
        "worker_status": DEFAULT_STATUS,
        "last_synced": 0,
        "last_synced_entry": 0,
        "crawl_status": DEFAULT_STATUS,
        "entry": 0,
        "data": 0,
        "meta": 0,
        "failures": 0,
        "checkpoint_completed": DEFAULT_STATUS,
        "checkpoint_time": 0,
        "checkpoint_completion_time": 0}


class LockedOpen(object):

    def __init__(self, filename, *args, **kwargs):
        self.filename = filename
        self.open_args = args
        self.open_kwargs = kwargs
        self.fileobj = None

    def __enter__(self):
        """
        If two processes compete to update a file, The first process
        gets the lock and the second process is blocked in the fcntl.flock()
        call. When first process replaces the file and releases the lock,
        the already open file descriptor in the second process now points
        to a  "ghost" file(not reachable by any path name) with old contents.
        To avoid that conflict, check the fd already opened is same or
        not. Open new one if not same
        """
        f = open(self.filename, *self.open_args, **self.open_kwargs)
        while True:
            fcntl.flock(f, fcntl.LOCK_EX)
            fnew = open(self.filename, *self.open_args, **self.open_kwargs)
            if os.path.sameopenfile(f.fileno(), fnew.fileno()):
                fnew.close()
                break
            else:
                f.close()
                f = fnew
        self.fileobj = f
        return f

    def __exit__(self, _exc_type, _exc_value, _traceback):
        self.fileobj.close()


def set_monitor_status(status_file, status):
    fd = os.open(status_file, os.O_CREAT | os.O_RDWR)
    os.close(fd)
    with LockedOpen(status_file, 'r+'):
        with tempfile.NamedTemporaryFile('w', dir=os.path.dirname(status_file),
                                         delete=False) as tf:
            tf.write(status)
            tempname = tf.name

        os.rename(tempname, status_file)
        dirfd = os.open(os.path.dirname(os.path.abspath(status_file)),
                        os.O_DIRECTORY)
        os.fsync(dirfd)
        os.close(dirfd)


class GeorepStatus(object):
    def __init__(self, monitor_status_file, master_node, brick, master_node_id,
                 master, slave, monitor_pid_file=None):
        self.master = master
        slv_data = slave.split("::")
        self.slave_host = slv_data[0]
        self.slave_volume = slv_data[1].split(":")[0]  # Remove Slave UUID
        self.work_dir = os.path.dirname(monitor_status_file)
        self.monitor_status_file = monitor_status_file
        self.filename = os.path.join(self.work_dir,
                                     "brick_%s.status"
                                     % urllib.quote_plus(brick))

        fd = os.open(self.filename, os.O_CREAT | os.O_RDWR)
        os.close(fd)
        fd = os.open(self.monitor_status_file, os.O_CREAT | os.O_RDWR)
        os.close(fd)
        self.master_node = master_node
        self.master_node_id = master_node_id
        self.brick = brick
        self.default_values = get_default_values()
        self.monitor_pid_file = monitor_pid_file

    def send_event(self, event_type, **kwargs):
        gf_event(event_type,
                 master_volume=self.master,
                 master_node=self.master_node,
                 master_node_id=self.master_node_id,
                 slave_host=self.slave_host,
                 slave_volume=self.slave_volume,
                 brick_path=self.brick,
                 **kwargs)

    def _update(self, mergerfunc):
        with LockedOpen(self.filename, 'r+') as f:
            try:
                data = json.load(f)
            except ValueError:
                data = self.default_values

            data = mergerfunc(data)
            # If Data is not changed by merger func
            if not data:
                return False

            with tempfile.NamedTemporaryFile(
                    'w',
                    dir=os.path.dirname(self.filename),
                    delete=False) as tf:
                tf.write(data)
                tempname = tf.name

            os.rename(tempname, self.filename)
            dirfd = os.open(os.path.dirname(os.path.abspath(self.filename)),
                            os.O_DIRECTORY)
            os.fsync(dirfd)
            os.close(dirfd)
            return True

    def reset_on_worker_start(self):
        def merger(data):
            data["slave_node"] = DEFAULT_STATUS
            data["crawl_status"] = DEFAULT_STATUS
            data["entry"] = 0
            data["data"] = 0
            data["meta"] = 0
            return json.dumps(data)

        self._update(merger)

    def set_field(self, key, value):
        def merger(data):
            # Current data and prev data is same
            if data[key] == value:
                return {}

            data[key] = value
            return json.dumps(data)

        return self._update(merger)

    def trigger_gf_event_checkpoint_completion(self, checkpoint_time,
                                               checkpoint_completion_time):
        self.send_event(EVENT_GEOREP_CHECKPOINT_COMPLETED,
                        checkpoint_time=checkpoint_time,
                        checkpoint_completion_time=checkpoint_completion_time)

    def set_last_synced(self, value, checkpoint_time):
        def merger(data):
            data["last_synced"] = value[0]

            # If checkpoint is not set or reset
            # or if last set checkpoint is changed
            if checkpoint_time == 0 or \
               checkpoint_time != data["checkpoint_time"]:
                data["checkpoint_time"] = 0
                data["checkpoint_completion_time"] = 0
                data["checkpoint_completed"] = "No"

            # If checkpoint is completed and not marked as completed
            # previously then update the checkpoint completed time
            if checkpoint_time > 0 and checkpoint_time <= value[0]:
                if data["checkpoint_completed"] == "No":
                    curr_time = int(time.time())
                    data["checkpoint_time"] = checkpoint_time
                    data["checkpoint_completion_time"] = curr_time
                    data["checkpoint_completed"] = "Yes"
                    logging.info("Checkpoint completed. Checkpoint "
                                 "Time: %s, Completion Time: %s" % (
                                     human_time_utc(checkpoint_time),
                                     human_time_utc(curr_time)))
                    self.trigger_gf_event_checkpoint_completion(
                        checkpoint_time, curr_time)

            return json.dumps(data)

        self._update(merger)

    def set_worker_status(self, status):
        if self.set_field("worker_status", status):
            logging.info("Worker Status: %s" % status)

    def set_worker_crawl_status(self, status):
        if self.set_field("crawl_status", status):
            logging.info("Crawl Status: %s" % status)

    def set_slave_node(self, slave_node):
        def merger(data):
            data["slave_node"] = slave_node
            return json.dumps(data)

        self._update(merger)

    def inc_value(self, key, value):
        def merger(data):
            data[key] = data.get(key, 0) + value
            return json.dumps(data)

        self._update(merger)

    def dec_value(self, key, value):
        def merger(data):
            data[key] = data.get(key, 0) - value
            if data[key] < 0:
                data[key] = 0
            return json.dumps(data)

        self._update(merger)

    def set_active(self):
        if self.set_field("worker_status", "Active"):
            logging.info("Worker Status: Active")
            self.send_event(EVENT_GEOREP_ACTIVE)

    def set_passive(self):
        if self.set_field("worker_status", "Passive"):
            logging.info("Worker Status: Passive")
            self.send_event(EVENT_GEOREP_PASSIVE)

    def get_monitor_status(self):
        data = ""
        with open(self.monitor_status_file, "r") as f:
            data = f.read().strip()
        return data

    def get_status(self, checkpoint_time=0):
        """
        Monitor Status --->        Created    Started  Paused      Stopped
        ----------------------------------------------------------------------
        slave_node                 N/A        VALUE    VALUE       N/A
        status                     Created    VALUE    Paused      Stopped
        last_synced                N/A        VALUE    VALUE       VALUE
        last_synced_entry          N/A        VALUE    VALUE       VALUE
        crawl_status               N/A        VALUE    N/A         N/A
        entry                      N/A        VALUE    N/A         N/A
        data                       N/A        VALUE    N/A         N/A
        meta                       N/A        VALUE    N/A         N/A
        failures                   N/A        VALUE    VALUE       VALUE
        checkpoint_completed       N/A        VALUE    VALUE       VALUE
        checkpoint_time            N/A        VALUE    VALUE       VALUE
        checkpoint_completed_time  N/A        VALUE    VALUE       VALUE
        """
        data = self.default_values
        with open(self.filename) as f:
            try:
                data.update(json.load(f))
            except ValueError:
                pass
        monitor_status = self.get_monitor_status()

        # Verifying whether monitor process running and adjusting status
        if monitor_status in ["Started", "Paused"]:
            try:
                with open(self.monitor_pid_file, "r+") as f:
                    fcntl.lockf(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    monitor_status = "Stopped"
            except (IOError, OSError) as e:
                # If pid file not exists, either monitor died or Geo-rep
                # not even started once
                if e.errno == ENOENT:
                    monitor_status = "Stopped"
                elif e.errno in (EACCES, EAGAIN):
                    # cannot grab. so, monitor process still running..move on
                    pass
                else:
                    raise

        if monitor_status in ["Created", "Paused", "Stopped"]:
            data["worker_status"] = monitor_status

        if monitor_status == "":
            data["worker_status"] = "Stopped"

        # Checkpoint adjustments
        if checkpoint_time == 0:
            data["checkpoint_completed"] = DEFAULT_STATUS
            data["checkpoint_time"] = DEFAULT_STATUS
            data["checkpoint_completion_time"] = DEFAULT_STATUS
        else:
            if checkpoint_time != data["checkpoint_time"]:
                if checkpoint_time <= data["last_synced"]:
                    data["checkpoint_completed"] = "Yes"
                    data["checkpoint_time"] = checkpoint_time
                    data["checkpoint_completion_time"] = data["last_synced"]
                else:
                    data["checkpoint_completed"] = "No"
                    data["checkpoint_time"] = checkpoint_time
                    data["checkpoint_completion_time"] = DEFAULT_STATUS

        if data["checkpoint_time"] not in [0, DEFAULT_STATUS]:
            chkpt_time = data["checkpoint_time"]
            data["checkpoint_time"] = human_time(chkpt_time)
            data["checkpoint_time_utc"] = human_time_utc(chkpt_time)

        if data["checkpoint_completion_time"] not in [0, DEFAULT_STATUS]:
            chkpt_completion_time = data["checkpoint_completion_time"]
            data["checkpoint_completion_time"] = human_time(
                chkpt_completion_time)
            data["checkpoint_completion_time_utc"] = human_time_utc(
                chkpt_completion_time)

        if data["last_synced"] == 0:
            data["last_synced"] = DEFAULT_STATUS
            data["last_synced_utc"] = DEFAULT_STATUS
        else:
            last_synced = data["last_synced"]
            data["last_synced"] = human_time(last_synced)
            data["last_synced_utc"] = human_time_utc(last_synced)

        if data["worker_status"] != "Active":
            data["last_synced"] = DEFAULT_STATUS
            data["last_synced_utc"] = DEFAULT_STATUS
            data["crawl_status"] = DEFAULT_STATUS
            data["entry"] = DEFAULT_STATUS
            data["data"] = DEFAULT_STATUS
            data["meta"] = DEFAULT_STATUS
            data["failures"] = DEFAULT_STATUS
            data["checkpoint_completed"] = DEFAULT_STATUS
            data["checkpoint_time"] = DEFAULT_STATUS
            data["checkpoint_completed_time"] = DEFAULT_STATUS
            data["checkpoint_time_utc"] = DEFAULT_STATUS
            data["checkpoint_completion_time_utc"] = DEFAULT_STATUS

        if data["worker_status"] not in ["Active", "Passive"]:
            data["slave_node"] = DEFAULT_STATUS

        if data.get("last_synced_utc", 0) == 0:
            data["last_synced_utc"] = DEFAULT_STATUS

        if data.get("checkpoint_completion_time_utc", 0) == 0:
            data["checkpoint_completion_time_utc"] = DEFAULT_STATUS

        if data.get("checkpoint_time_utc", 0) == 0:
            data["checkpoint_time_utc"] = DEFAULT_STATUS

        return data

    def print_status(self, checkpoint_time=0):
        for key, value in self.get_status(checkpoint_time).items():
            print ("%s: %s" % (key, value))
