/*
  Copyright (c) 2015, Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _UPCALL_UTILS_H
#define _UPCALL_UTILS_H

#include "iatt.h"
#include "compat-uuid.h"
#include "compat.h"

/* Used by GF_IPC_UPCALL_FEATURES in the ipc() FOP.
 *
 * Each feature is a bit in the uint32_t, this likely will match all the events
 * from the gf_upcall_event_t enum.
 *
 * When the bit for GF_UPCALL_EVENT_NULL is not set, upcall is loaded, but
 * disabled.
 */
#define GF_UPCALL_FEATURES "gluster.upcall.features"

typedef enum {
        GF_UPCALL_EVENT_NULL = 0,
        GF_UPCALL_CACHE_INVALIDATION = 1,
        /* add new events to the feature mask in up_ipc() */
} gf_upcall_event_t;

struct gf_upcall {
        char      *client_uid;
        uuid_t    gfid;
        uint32_t  event_type;
        void      *data;
};

struct gf_upcall_cache_invalidation {
        uint32_t flags;
        uint32_t expire_time_attr;
        struct iatt stat;
        struct iatt p_stat; /* parent dir stat */
        struct iatt oldp_stat; /* oldparent dir stat */
};

#endif /* _UPCALL_UTILS_H */
