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

import os
import sys

GLUSTER_SRC_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
eventtypes_h = os.path.join(GLUSTER_SRC_ROOT, "libglusterfs/src/eventtypes.h")
eventtypes_py = os.path.join(GLUSTER_SRC_ROOT, "events/src/eventtypes.py")

gen_header_type = sys.argv[1]

# When adding new keys add it to the END
keys = (
    "EVENT_PEER_ATTACH",
    "EVENT_PEER_DETACH",

    "EVENT_VOLUME_CREATE",
    "EVENT_VOLUME_START",
    "EVENT_VOLUME_STOP",
    "EVENT_VOLUME_DELETE",
    "EVENT_VOLUME_SET",
    "EVENT_VOLUME_RESET",

    "EVENT_GEOREP_CREATE",
    "EVENT_GEOREP_START",
    "EVENT_GEOREP_STOP",
    "EVENT_GEOREP_PAUSE",
    "EVENT_GEOREP_RESUME",
    "EVENT_GEOREP_DELETE",
    "EVENT_GEOREP_CONFIG_SET",
    "EVENT_GEOREP_CONFIG_RESET",

    "EVENT_BITROT_ENABLE",
    "EVENT_BITROT_DISABLE",
    "EVENT_BITROT_SCRUB_THROTTLE",
    "EVENT_BITROT_SCRUB_FREQ",
    "EVENT_BITROT_SCRUB_OPTION",
    "EVENT_BITROT_BAD_FILE",

    "EVENT_SVC_MANAGER_FAILED",
    "EVENT_SVC_RECONFIGURE_FAILED",
    "EVENT_SVC_CONNECTED",
    "EVENT_SVC_DISCONNECTED",


    "EVENT_PEER_STORE_FAILURE",
    "EVENT_PEER_RPC_CREATE_FAILED",
    "EVENT_PEER_REJECT",
    "EVENT_PEER_CONNECT",
    "EVENT_PEER_DISCONNECT",
    "EVENT_PEER_NOT_FOUND",
    "EVENT_UNKNOWN_PEER",

    "EVENT_BRICK_START_FAILED",
    "EVENT_BRICK_STOP_FAILED",
    "EVENT_BRICK_DISCONNECTED",
    "EVENT_BRICK_CONNECTED",
    "EVENT_BRICKS_START_FAILED",
    "EVENT_BRICKPATH_RESOLVE_FAILED",

    "EVENT_NOTIFY_UNKNOWN_OP",

    "EVENT_QUORUM_LOST",
    "EVENT_QUORUM_REGAINED",
    "EVENT_REBALANCE_START_FAILED",
    "EVENT_REBALANCE_STATUS_UPDATE_FAILED",
    "EVENT_IMPORT_QUOTA_CONF_FAILED",
    "EVENT_IMPORT_VOLUME_FAILED",
    "EVENT_IMPORT_BRICK_FAILED",
    "EVENT_COMPARE_FRIEND_VOLUME_FAILED",
    "EVENT_NFS_GANESHA_EXPORT_FAILED",

    "EVENT_QUOTA_ENABLE",
    "EVENT_QUOTA_DISABLE",
    "EVENT_QUOTA_SET_USAGE_LIMIT",
    "EVENT_QUOTA_SET_OBJECTS_LIMIT",
    "EVENT_QUOTA_REMOVE_USAGE_LIMIT",
    "EVENT_QUOTA_REMOVE_OBJECTS_LIMIT",
    "EVENT_QUOTA_ALERT_TIME",
    "EVENT_QUOTA_SOFT_TIMEOUT",
    "EVENT_QUOTA_HARD_TIMEOUT",
    "EVENT_QUOTA_DEFAULT_SOFT_LIMIT",
    "EVENT_QUOTA_CROSSED_SOFT_LIMIT",
)

LAST_EVENT = "EVENT_LAST"

ERRORS = (
    "EVENT_SEND_OK",
    "EVENT_ERROR_INVALID_INPUTS",
    "EVENT_ERROR_SOCKET",
    "EVENT_ERROR_CONNECT",
    "EVENT_ERROR_SEND"
)

if gen_header_type == "C_HEADER":
    # Generate eventtypes.h
    with open(eventtypes_h, "w") as f:
        f.write("#ifndef __EVENTTYPES_H__\n")
        f.write("#define __EVENTTYPES_H__\n\n")
        f.write("typedef enum {\n")
        for k in ERRORS:
            f.write("    {0},\n".format(k))
        f.write("} event_errors_t;\n")

        f.write("\n")

        f.write("typedef enum {\n")
        for k in keys:
            f.write("    {0},\n".format(k))

        f.write("    {0}\n".format(LAST_EVENT))
        f.write("} eventtypes_t;\n")
        f.write("\n#endif /* __EVENTTYPES_H__ */\n")

if gen_header_type == "PY_HEADER":
    # Generate eventtypes.py
    with open(eventtypes_py, "w") as f:
        f.write("# -*- coding: utf-8 -*-\n")
        f.write("all_events = [\n")
        for ev in keys:
            f.write('    "{0}",\n'.format(ev))

        f.write("]\n\n")

        for idx, ev in enumerate(keys):
            f.write("{0} = {1}\n".format(ev.replace("EVENT_", ""), idx))
