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
    # user driven events
    #peer and volume managment events
    "EVENT_PEER_ATTACH",
    "EVENT_PEER_DETACH",
    "EVENT_VOLUME_CREATE",
    "EVENT_VOLUME_START",
    "EVENT_VOLUME_STOP",
    "EVENT_VOLUME_DELETE",
    "EVENT_VOLUME_SET",
    "EVENT_VOLUME_RESET",
    "EVENT_BRICK_RESET_START",
    "EVENT_BRICK_RESET_COMMIT",
    "EVENT_BRICK_REPLACE",

    #geo-rep events
    "EVENT_GEOREP_CREATE",
    "EVENT_GEOREP_START",
    "EVENT_GEOREP_STOP",
    "EVENT_GEOREP_PAUSE",
    "EVENT_GEOREP_RESUME",
    "EVENT_GEOREP_DELETE",
    "EVENT_GEOREP_CONFIG_SET",
    "EVENT_GEOREP_CONFIG_RESET",

    #bitrot events
    "EVENT_BITROT_ENABLE",
    "EVENT_BITROT_DISABLE",
    "EVENT_BITROT_SCRUB_THROTTLE",
    "EVENT_BITROT_SCRUB_FREQ",
    "EVENT_BITROT_SCRUB_OPTION",
    "EVENT_BITROT_SCRUB_ONDEMAND",

    #quota events
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

    #snapshot events
    "EVENT_SNAPSHOT_CREATED",
    "EVENT_SNAPSHOT_CREATE_FAILED",
    "EVENT_SNAPSHOT_ACTIVATED",
    "EVENT_SNAPSHOT_ACTIVATE_FAILED",
    "EVENT_SNAPSHOT_DEACTIVATED",
    "EVENT_SNAPSHOT_DEACTIVATE_FAILED",
    "EVENT_SNAPSHOT_SOFT_LIMIT_REACHED",
    "EVENT_SNAPSHOT_HARD_LIMIT_REACHED",
    "EVENT_SNAPSHOT_RESTORED",
    "EVENT_SNAPSHOT_RESTORE_FAILED",
    "EVENT_SNAPSHOT_DELETED",
    "EVENT_SNAPSHOT_DELETE_FAILED",
    "EVENT_SNAPSHOT_CLONED",
    "EVENT_SNAPSHOT_CLONE_FAILED",
    "EVENT_SNAPSHOT_CONFIG_UPDATED",
    "EVENT_SNAPSHOT_CONFIG_UPDATE_FAILED",
    "EVENT_SNAPSHOT_SCHEDULER_INITIALISED",
    "EVENT_SNAPSHOT_SCHEDULER_INIT_FAILED",
    "EVENT_SNAPSHOT_SCHEDULER_ENABLED",
    "EVENT_SNAPSHOT_SCHEDULER_ENABLE_FAILED",
    "EVENT_SNAPSHOT_SCHEDULER_DISABLED",
    "EVENT_SNAPSHOT_SCHEDULER_DISABLE_FAILED",
    "EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_ADDED",
    "EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_ADD_FAILED",
    "EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_EDITED",
    "EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_EDIT_FAILED",
    "EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_DELETED",
    "EVENT_SNAPSHOT_SCHEDULER_SCHEDULE_DELETE_FAILED",

    #async events
    #glusterd events
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
    #ec events
    "EVENT_EC_MIN_BRICKS_NOT_UP",
    "EVENT_EC_MIN_BRICKS_UP",
    #georep async events
    "EVENT_GEOREP_FAULTY",
    "EVENT_GEOREP_CHECKPOINT_COMPLETED",
    "EVENT_GEOREP_ACTIVE",
    "EVENT_GEOREP_PASSIVE",

    #quota async events
    "EVENT_QUOTA_CROSSED_SOFT_LIMIT",
    #bitrot async events
    "EVENT_BITROT_BAD_FILE",
    #protocol-server events
    "EVENT_CLIENT_CONNECT",
    "EVENT_CLIENT_AUTH_REJECT",
    "EVENT_CLIENT_DISCONNECT",
    #posix events
    "EVENT_POSIX_SAME_GFID",
    "EVENT_POSIX_ALREADY_PART_OF_VOLUME",
    "EVENT_POSIX_BRICK_NOT_IN_VOLUME",
    "EVENT_POSIX_BRICK_VERIFICATION_FAILED",
    "EVENT_POSIX_ACL_NOT_SUPPORTED",
    "EVENT_POSIX_HEALTH_CHECK_FAILED",
    #afr events
    "EVENT_AFR_QUORUM_MET",
    "EVENT_AFR_QUORUM_FAIL",
    "EVENT_AFR_SUBVOL_UP",
    "EVENT_AFR_SUBVOLS_DOWN",
    "EVENT_AFR_SPLIT_BRAIN",

    #tier events
    "EVENT_TIER_ATTACH",
    "EVENT_TIER_ATTACH_FORCE",
    "EVENT_TIER_DETACH_START",
    "EVENT_TIER_DETACH_STOP",
    "EVENT_TIER_DETACH_COMMIT",
    "EVENT_TIER_DETACH_FORCE",
    "EVENT_TIER_PAUSE",
    "EVENT_TIER_RESUME",
    "EVENT_TIER_WATERMARK_HI",
    "EVENT_TIER_WATERMARK_DROPPED_TO_MID",
    "EVENT_TIER_WATERMARK_RAISED_TO_MID",
    "EVENT_TIER_WATERMARK_DROPPED_TO_LOW",

    #dht events
    #add/remove brick events
    "EVENT_VOLUME_ADD_BRICK",
    "EVENT_VOLUME_ADD_BRICK_FAILED",
    "EVENT_VOLUME_REMOVE_BRICK_START",
    "EVENT_VOLUME_REMOVE_BRICK_START_FAILED",
    "EVENT_VOLUME_REMOVE_BRICK_COMMIT",
    "EVENT_VOLUME_REMOVE_BRICK_COMMIT_FAILED",
    "EVENT_VOLUME_REMOVE_BRICK_STOP",
    "EVENT_VOLUME_REMOVE_BRICK_STOP_FAILED",
    "EVENT_VOLUME_REMOVE_BRICK_FORCE",
    "EVENT_VOLUME_REMOVE_BRICK_FORCE_FAILED",
    "EVENT_VOLUME_REMOVE_BRICK_FAILED",

    #rebalance events
    "EVENT_VOLUME_REBALANCE_START",
    "EVENT_VOLUME_REBALANCE_STOP",
    "EVENT_VOLUME_REBALANCE_FAILED",
    "EVENT_VOLUME_REBALANCE_COMPLETE",

    #tier events
    "EVENT_TIER_START",
    "EVENT_TIER_START_FORCE",
)

LAST_EVENT = "EVENT_LAST"

ERRORS = (
    "EVENT_SEND_OK",
    "EVENT_ERROR_INVALID_INPUTS",
    "EVENT_ERROR_SOCKET",
    "EVENT_ERROR_CONNECT",
    "EVENT_ERROR_SEND",
    "EVENT_ERROR_RESOLVE",
    "EVENT_ERROR_MSG_FORMAT",
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
