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

import requests
from eventsapiconf import (LOG_FILE,
                           WEBHOOKS_FILE,
                           DEFAULT_CONFIG_FILE,
                           CUSTOM_CONFIG_FILE,
                           UUID_FILE)
import eventtypes


# Webhooks list
_webhooks = {}
# Default Log Level
_log_level = "INFO"
# Config Object
_config = {}

# Init Logger instance
logger = logging.getLogger(__name__)
NodeID = None


def get_node_uuid():
    val = None
    with open(UUID_FILE) as f:
        for line in f:
            if line.startswith("UUID="):
                val = line.strip().split("=")[-1]
                break
    return val


def get_config(key):
    if not _config:
        load_config()
    return _config.get(key, None)


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
    new_log_level = _config.get("log_level", "INFO")
    if _log_level != new_log_level:
        logger.setLevel(getattr(logging, new_log_level.upper()))
        _log_level = new_log_level.upper()


def load_webhooks():
    """
    Load/Reload the webhooks list. This function will
    be triggered during init and when SIGUSR2.
    """
    global _webhooks
    _webhooks = {}
    if os.path.exists(WEBHOOKS_FILE):
        _webhooks = json.load(open(WEBHOOKS_FILE))


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

    message = {
        "nodeid": NodeID,
        "ts": int(ts),
        "event": get_event_type_name(event_key),
        "message": data
    }
    if _webhooks:
        plugin_webhook(message)
    else:
        # TODO: Default action?
        pass


def plugin_webhook(message):
    message_json = json.dumps(message, sort_keys=True)
    logger.debug("EVENT: {0}".format(message_json))
    for url, token in _webhooks.items():
        http_headers = {"Content-Type": "application/json"}
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

        if resp.status_code != 200:
            logger.warn("Event push failed to URL: {url}, "
                        "Event: {event}, "
                        "Status Code: {status_code}".format(
                            url=url,
                            event=message_json,
                            status_code=resp.status_code))
