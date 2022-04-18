#ifndef _PROTOCOL_UTILS_H
#define _PROTOCOL_UTILS_H

#include "protocol-common.h"

static inline int
get_vol_type(int type, int dist_count, int brick_count)
{
    if ((type != GF_CLUSTER_TYPE_TIER) && (type > 0) &&
        (dist_count < brick_count))
        type = type + GF_CLUSTER_TYPE_MAX - 1;

    return type;
}

static inline char *
get_struct_variable(int mem_num, gf_gsync_status_t *sts_val)
{
    switch (mem_num) {
        case 0:
            return (sts_val->node);
        case 1:
            return (sts_val->primary);
        case 2:
            return (sts_val->brick);
        case 3:
            return (sts_val->secondary_user);
        case 4:
            return (sts_val->secondary);
        case 5:
            return (sts_val->secondary_node);
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
            return (sts_val->secondarykey);
        case 21:
            return (sts_val->session_secondary);
        default:
            return NULL;
    }
}

#endif /* _PROTOCOL_UTILS_H */
