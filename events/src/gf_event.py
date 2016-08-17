#!/usr/bin/env python
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

import socket
import time

from eventsapiconf import SERVER_ADDRESS, EVENTS_ENABLED
from eventtypes import all_events

from utils import logger, setup_logger, get_config

# Run this when this lib loads
setup_logger()


def gf_event(event_type, **kwargs):
    if EVENTS_ENABLED == 0:
        return

    if not isinstance(event_type, int) or event_type >= len(all_events):
        logger.error("Invalid Event Type: {0}".format(event_type))
        return

    try:
        client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    except socket.error as e:
        logger.error("Unable to connect to events Server: {0}".format(e))
        return

    # Convert key value args into KEY1=VALUE1;KEY2=VALUE2;..
    msg = ""
    for k, v in kwargs.items():
        msg += "{0}={1};".format(k, v)

    # <TIMESTAMP> <EVENT_TYPE> <MSG>
    msg = "{0} {1} {2}".format(int(time.time()), event_type, msg.strip(";"))

    port = get_config("port")
    if port is None:
        logger.error("Unable to get eventsd port details")
        return

    try:
        sent = client.sendto(msg, (SERVER_ADDRESS, port))
        assert sent == len(msg)
    except socket.error as e:
        logger.error("Unable to Send message: {0}".format(e))
    except AssertionError:
        logger.error("Unable to send message. Sent: {0}, Actual: {1}".format(
            sent, len(msg)))
    finally:
        client.close()
