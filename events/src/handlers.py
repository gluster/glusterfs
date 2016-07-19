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


def handle_event_volume_set(ts, key, data):
    """
    Recieved data will have all the options as one string, split into
    list of options. "key1,value1,key2,value2" into
    [[key1, value1], [key2, value2]]
    """
    opts = data.get("options", "").strip(",").split(",")
    data["options"] = []
    for i, opt in enumerate(opts):
        if i % 2 == 0:
            # Add new array with key
            data["options"].append([opt])
        else:
            # Add to the last added array
            data["options"][-1].append(opt)

    utils.publish(ts, key, data)
