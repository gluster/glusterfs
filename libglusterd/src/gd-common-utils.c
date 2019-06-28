/*
  Copyright (c) 2019 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "gd-common-utils.h"
#include "cli1-xdr.h"

int
get_vol_type(int type, int dist_count, int brick_count)
{
    if ((type != GF_CLUSTER_TYPE_TIER) && (type > 0) &&
        (dist_count < brick_count))
        type = type + GF_CLUSTER_TYPE_MAX - 1;

    return type;
}

char *
get_struct_variable(int mem_num, gf_gsync_status_t *sts_val)
{
    switch (mem_num) {
        case 0:
            return (sts_val->node);
        case 1:
            return (sts_val->master);
        case 2:
            return (sts_val->brick);
        case 3:
            return (sts_val->slave_user);
        case 4:
            return (sts_val->slave);
        case 5:
            return (sts_val->slave_node);
        case 6:
            return (sts_val->worker_status);
        case 7:
            return (sts_val->crawl_status);
        case 8:
            return (sts_val->last_synced);
        case 9:
            return (sts_val->entry);
        case 10:
            return (sts_val->data);
        case 11:
            return (sts_val->meta);
        case 12:
            return (sts_val->failures);
        case 13:
            return (sts_val->checkpoint_time);
        case 14:
            return (sts_val->checkpoint_completed);
        case 15:
            return (sts_val->checkpoint_completion_time);
        case 16:
            return (sts_val->brick_host_uuid);
        case 17:
            return (sts_val->last_synced_utc);
        case 18:
            return (sts_val->checkpoint_time_utc);
        case 19:
            return (sts_val->checkpoint_completion_time_utc);
        case 20:
            return (sts_val->slavekey);
        case 21:
            return (sts_val->session_slave);
        default:
            goto out;
    }

out:
    return NULL;
}
