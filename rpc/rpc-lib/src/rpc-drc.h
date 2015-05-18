/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef RPC_DRC_H
#define RPC_DRC_H

#include "rpcsvc-common.h"
#include "rpcsvc.h"
#include "locking.h"
#include "dict.h"
#include "rb.h"

/* per-client cache structure */
struct drc_client {
        uint32_t                   ref;
        union gf_sock_union        sock_union;
        /* pointers to the cache */
        struct rb_table           *rbtree;
        /* no. of ops currently cached */
        uint32_t                   op_count;
        struct list_head           client_list;
};

struct drc_cached_op {
        drc_op_state_t                 state;
        uint32_t                       xid;
        int                            prognum;
        int                            progversion;
        int                            procnum;
        rpc_transport_msg_t            msg;
        drc_client_t                  *client;
        struct list_head               client_list;
        struct list_head               global_list;
        int32_t                        ref;
};

/* global drc definitions */
enum drc_status {
        DRC_UNINITIATED,
        DRC_INITIATED
};
typedef enum drc_status drc_status_t;

struct drc_globals {
        /* allocator must be the first member since
         * it is used so in gf_libavl_allocator
         */
        struct libavl_allocator   allocator;
        drc_type_t                type;
        /* configurable size parameter */
        uint32_t                  global_cache_size;
        drc_lru_factor_t          lru_factor;
        gf_lock_t                 lock;
        drc_status_t              status;
        uint32_t                  op_count;
        uint64_t                  cache_hits;
        uint64_t                  intransit_hits;
        struct mem_pool          *mempool;
        struct list_head          cache_head;
        uint32_t                  client_count;
        struct list_head          clients_head;
};

int
rpcsvc_need_drc (rpcsvc_request_t *req);

drc_cached_op_t *
rpcsvc_drc_lookup (rpcsvc_request_t *req);

int
rpcsvc_send_cached_reply (rpcsvc_request_t *req, drc_cached_op_t *reply);

int
rpcsvc_cache_reply (rpcsvc_request_t *req, struct iobref *iobref,
                    struct iovec *rpchdr, int rpchdrcount,
                    struct iovec *proghdr, int proghdrcount,
                    struct iovec *payload, int payloadcount);

int
rpcsvc_cache_request (rpcsvc_request_t *req);

int32_t
rpcsvc_drc_priv (rpcsvc_drc_globals_t *drc);

int
rpcsvc_drc_init (rpcsvc_t *svc, dict_t *options);

int
rpcsvc_drc_deinit (rpcsvc_t *svc);

int
rpcsvc_drc_reconfigure (rpcsvc_t *svc, dict_t *options);

#endif /* RPC_DRC_H */
