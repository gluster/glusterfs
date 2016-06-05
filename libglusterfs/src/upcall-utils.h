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

typedef enum {
        GF_UPCALL_EVENT_NULL,
        GF_UPCALL_CACHE_INVALIDATION,
        GF_UPCALL_RECALL_LEASE,
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

struct gf_upcall_recall_lease {
        uint32_t  lease_type; /* Lease type to which client can downgrade to*/
        uuid_t    tid;        /* transaction id of the fop that caused
                                 the recall */
        dict_t   *dict;
};

#endif /* _UPCALL_UTILS_H */
