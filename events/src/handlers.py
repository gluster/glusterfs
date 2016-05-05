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

import utils


def generic_handler(ts, key, data):
    """
    Generic handler to broadcast message to all peers, custom handlers
    can be created by func name handler_<event_name>
    Ex: handle_event_volume_create(ts, key, data)
    """
    utils.publish(ts, key, data)
