/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _RPCSVC_COMMON_H
#define _RPCSVC_COMMON_H

#include <pthread.h>
#include "list.h"
#include "compat.h"
#include "glusterfs.h"
#include "dict.h"

typedef enum {
        RPCSVC_EVENT_ACCEPT,
        RPCSVC_EVENT_DISCONNECT,
        RPCSVC_EVENT_TRANSPORT_DESTROY,
        RPCSVC_EVENT_LISTENER_DEAD,
} rpcsvc_event_t;


struct rpcsvc_state;

typedef int (*rpcsvc_notify_t) (struct rpcsvc_state *, void *mydata,
                                rpcsvc_event_t, void *data);

struct drc_globals;
typedef struct drc_globals rpcsvc_drc_globals_t;

/* Contains global state required for all the RPC services.
 */
typedef struct rpcsvc_state {

        /* Contains list of (program, version) handlers.
         * other options.
         */

        pthread_mutex_t         rpclock;

        unsigned int            memfactor;

        /* List of the authentication schemes available. */
        struct list_head        authschemes;

        /* Reference to the options */
        dict_t                  *options;

        /* Allow insecure ports. */
        gf_boolean_t            allow_insecure;
        gf_boolean_t            register_portmap;
        gf_boolean_t            root_squash;
        uid_t                   anonuid;
        gid_t                   anongid;
        glusterfs_ctx_t         *ctx;

        /* list of connections which will listen for incoming connections */
        struct list_head        listeners;

        /* list of programs registered with rpcsvc */
        struct list_head        programs;

        /* list of notification callbacks */
        struct list_head        notify;
        int                     notify_count;

        void                    *mydata; /* This is xlator */
        rpcsvc_notify_t         notifyfn;
        struct mem_pool         *rxpool;
        rpcsvc_drc_globals_t    *drc;

	/* per-client limit of outstanding rpc requests */
        int                     outstanding_rpc_limit;
        gf_boolean_t            addr_namelookup;
} rpcsvc_t;

/* DRC START */
enum drc_op_type {
        DRC_NA              = 0,
        DRC_IDEMPOTENT      = 1,
        DRC_NON_IDEMPOTENT  = 2
};
typedef enum drc_op_type drc_op_type_t;

enum drc_type {
        DRC_TYPE_NONE        = 0,
        DRC_TYPE_IN_MEMORY   = 1
};
typedef enum drc_type drc_type_t;

enum drc_lru_factor {
        DRC_LRU_5_PC       = 20,
        DRC_LRU_10_PC      = 10,
        DRC_LRU_25_PC      = 4,
        DRC_LRU_50_PC      = 2
};
typedef enum drc_lru_factor drc_lru_factor_t;

enum drc_xid_state {
        DRC_XID_MONOTONOUS  = 0,
        DRC_XID_WRAPPED     = 1
};
typedef enum drc_xid_state drc_xid_state_t;

enum drc_op_state {
        DRC_OP_IN_TRANSIT    = 0,
        DRC_OP_CACHED        = 1
};
typedef enum drc_op_state drc_op_state_t;

enum drc_policy {
        DRC_LRU              = 0
};
typedef enum drc_policy drc_policy_t;

/* Default policies for DRC */
#define DRC_DEFAULT_TYPE               DRC_TYPE_IN_MEMORY
#define DRC_DEFAULT_CACHE_SIZE         0x20000
#define DRC_DEFAULT_LRU_FACTOR         DRC_LRU_25_PC

/* DRC END */

#endif /* #ifndef _RPCSVC_COMMON_H */
