# -*- coding: utf-8 -*-
#
#  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
#  This file is part of GlusterFS.
#
#  This file is licensed to you under your choice of the GNU Lesser
#  General Public License, version 3 or any later version (LGPLv3 or
#  later), or the GNU General Public License, version 2 (GPLv2), in all
#  cases as published by the Free Software Foundation.
#

import json
import os
import logging
import fcntl
from errno import ESRCH, EBADF
from threading import Thread
import multiprocessing
from Queue import Queue

from eventsapiconf import (LOG_FILE,
                           WEBHOOKS_FILE,
                           DEFAULT_CONFIG_FILE,
                           CUSTOM_CONFIG_FILE,
                           UUID_FILE)
import eventtypes


# Webhooks list
_webhooks = {}
_webhooks_file_mtime = 0
# Default Log Level
_log_level = "INFO"
# Config Object
_config = {}

# Init Logger instance
logger = logging.getLogger(__name__)
NodeID = None
webhooks_pool = None


def boolify(value):
    value = str(value)
    if value.lower() in ["1", "on", "true", "yes"]:
        return True
    else:
        return False


def log_event(data):
    # Log all published events unless it is disabled
    if not _config.get("disable-events-log", False):
        logger.info(repr(data))


def get_node_uuid():
    val = None
    with open(UUID_FILE) as f:
        for line in f:
            if line.startswith("UUID="):
                val = line.strip().split("=")[-1]
                break
    return val


def get_config(key, default_value=None):
    if not _config:
        load_config()
    return _config.get(key, default_value)


def get_event_type_name(idx):
    """
    Returns Event Type text from the index. For example, VOLUME_CREATE
    """
    return eventtypes.all_events[idx].replace("EVENT_", "")


def setup_logger():
    """
    Logging initialization, Log level by default will be INFO, once config
    file is read, respective log_level will be set.
    """
    global logger
    logger.setLevel(logging.INFO)

    # create the logging file handler
    fh = logging.FileHandler(LOG_FILE)

    formatter = logging.Formatter("[%(asctime)s] %(levelname)s "
                                  "[%(module)s - %(lineno)s:%(funcName)s] "
                                  "- %(message)s")

    fh.setFormatter(formatter)

    # add handler to logger object
    logger.addHandler(fh)


def load_config():
    """
    Load/Reload the config from REST Config files. This function will
    be triggered during init and when SIGUSR2.
    """
    global _config
    _config = {}
    if os.path.exists(DEFAULT_CONFIG_FILE):
        _config = json.load(open(DEFAULT_CONFIG_FILE))
    if os.path.exists(CUSTOM_CONFIG_FILE):
        _config.update(json.load(open(CUSTOM_CONFIG_FILE)))


def load_log_level():
    """
    Reads log_level from Config file and sets accordingly. This function will
    be triggered during init and when SIGUSR2.
    """
    global logger, _log_level
    new_log_level = _config.get("log-level", "INFO")
    if _log_level != new_log_level:
        logger.setLevel(getattr(logging, new_log_level.upper()))
        _log_level = new_log_level.upper()


def load_webhooks():
    """
    Load/Reload the webhooks list. This function will
    be triggered during init and when SIGUSR2.
    """
    global _webhooks, _webhooks_file_mtime
    _webhooks = {}
    if os.path.exists(WEBHOOKS_FILE):
        _webhooks = json.load(open(WEBHOOKS_FILE))
        st = os.lstat(WEBHOOKS_FILE)
        _webhooks_file_mtime = st.st_mtime


def load_all():
    """
    Wrapper function to call all load/reload functions. This function will
    be triggered during init and when SIGUSR2.
    """
    load_config()
    load_webhooks()
    load_log_level()


def publish(ts, event_key, data):
    global NodeID
    if NodeID is None:
        NodeID = get_node_uuid()

    autoload_webhooks()

    message = {
        "nodeid": NodeID,
        "ts": int(ts),
        "event": get_event_type_name(event_key),
        "message": data
    }

    log_event(message)

    if _webhooks:
        plugin_webhook(message)
    else:
        # TODO: Default action?
        pass


def autoload_webhooks():
    global _webhooks_file_mtime
    try:
        st = os.lstat(WEBHOOKS_FILE)
    except OSError:
        st = None

    if st is not None:
        # If Stat is available and mtime is not matching with
        # previously recorded mtime, reload the webhooks file
        if st.st_mtime != _webhooks_file_mtime:
            load_webhooks()


def publish_to_webhook(url, token, message_queue):
    # Import requests here since not used in any other place
    import requests

    http_headers = {"Content-Type": "application/json"}
    while True:
        message_json = message_queue.get()
        if token != "" and token is not None:
            http_headers["Authorization"] = "Bearer " + token

        try:
            resp = requests.post(url, headers=http_headers, data=message_json)
        except requests.ConnectionError as e:
            logger.warn("Event push failed to URL: {url}, "
                        "Event: {event}, "
                        "Status: {error}".format(
                            url=url,
                            event=message_json,
                            error=e))
            continue
        finally:
            message_queue.task_done()

        if resp.status_code != 200:
            logger.warn("Event push failed to URL: {url}, "
                        "Event: {event}, "
                        "Status Code: {status_code}".format(
                            url=url,
                            event=message_json,
                            status_code=resp.status_code))


def plugin_webhook(message):
    message_json = json.dumps(message, sort_keys=True)
    logger.debug("EVENT: {0}".format(message_json))
    webhooks_pool.send(message_json)


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


class PidFileLockFailed(Exception):
    pass


class PidFile(object):
    def __init__(self, filename):
        self.filename = filename
        self.pid = os.getpid()
        self.fh = None

    def cleanup(self, remove_file=True):
        try:
            if self.fh is not None:
                self.fh.close()
        except IOError as exc:
            if exc.errno != EBADF:
                raise
        finally:
            if os.path.isfile(self.filename) and remove_file:
                os.remove(self.filename)

    def __enter__(self):
        self.fh = open(self.filename, 'a+')
        try:
            fcntl.flock(self.fh.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except IOError as exc:
            self.cleanup(remove_file=False)
            raise PidFileLockFailed(exc)

        self.fh.seek(0)
        self.fh.truncate()
        self.fh.write("%d\n" % self.pid)
        self.fh.flush()
        self.fh.seek(0)
        return self

    def __exit__(self, _exc_type, _exc_value, _traceback):
        self.cleanup()


def webhook_monitor(proc_queue, webhooks):
    queues = {}
    for url, token in webhooks.items():
        queues[url] = Queue()
        t = Thread(target=publish_to_webhook, args=(url, token, queues[url]))
        t.start()

    # Get the message sent to Process queue and distribute to all thread queues
    while True:
        message = proc_queue.get()
        for _, q in queues.items():
            q.put(message)


class WebhookThreadPool(object):
    def start(self):
        # Seperate process to emit messages to webhooks
        # which maintains one thread per webhook. Seperate
        # process is required since on reload we need to stop
        # and start the thread pool. In Python Threads can't be stopped
        # so terminate the process and start again. Note: In transit
        # events will be missed during reload
        self.queue = multiprocessing.Queue()
        self.proc = multiprocessing.Process(target=webhook_monitor,
                                            args=(self.queue, _webhooks))
        self.proc.start()

    def restart(self):
        # In transit messages are skipped, since webhooks monitor process
        # is terminated.
        self.proc.terminate()
        self.start()

    def send(self, message):
        self.queue.put(message)


def init_webhook_pool():
    global webhooks_pool
    webhooks_pool = WebhookThreadPool()
    webhooks_pool.start()


def restart_webhook_pool():
    global webhooks_pool
    if webhooks_pool is not None:
        webhooks_pool.restart()
