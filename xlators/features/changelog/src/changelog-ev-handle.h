/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef __CHANGELOG_EV_HANDLE_H
#define __CHANGELOG_EV_HANDLE_H

#include "list.h"
#include "xlator.h"
#include "rpc-clnt.h"

#include "rot-buffs.h"

struct changelog_clnt;

typedef struct changelog_rpc_clnt {
        xlator_t *this;

        gf_lock_t lock;

        unsigned long ref;
        gf_boolean_t  disconnected;

        unsigned int filter;
        char sock[UNIX_PATH_MAX];

        struct changelog_clnt *c_clnt;   /* back pointer to list holder */

        struct rpc_clnt *rpc;            /* RPC client endpoint */

        struct list_head list;           /* ->pending, ->waitq, ->active */

        void (*cleanup)
        (struct changelog_rpc_clnt *);   /* cleanup handler */
} changelog_rpc_clnt_t;

static inline void
changelog_rpc_clnt_ref (changelog_rpc_clnt_t *crpc)
{
        LOCK (&crpc->lock);
        {
                ++crpc->ref;
        }
        UNLOCK (&crpc->lock);
}

static inline void
changelog_set_disconnect_flag (changelog_rpc_clnt_t *crpc, gf_boolean_t flag)
{
        crpc->disconnected = flag;
}

static inline int
changelog_rpc_clnt_is_disconnected (changelog_rpc_clnt_t *crpc)
{
        return (crpc->disconnected == _gf_true);
}

static inline void
changelog_rpc_clnt_unref (changelog_rpc_clnt_t *crpc)
{
        gf_boolean_t gone = _gf_false;

        LOCK (&crpc->lock);
        {
                if (!(--crpc->ref)
                    && changelog_rpc_clnt_is_disconnected (crpc)) {
                        list_del (&crpc->list);
                        gone = _gf_true;
                }
        }
        UNLOCK (&crpc->lock);

        if (gone)
                crpc->cleanup (crpc);
}

/**
 * This structure holds pending and active clients. On probe RPC all
 * an instance of the above structure (@changelog_rpc_clnt) is placed
 * in ->pending and gets moved to ->active on a successful connect.
 *
 * locking rules:
 *
 * Manipulating ->pending
 * ->pending_lock
 *    ->pending
 *
 * Manipulating ->active
 * ->active_lock
 *    ->active
 *
 * Moving object from ->pending to ->active
 * ->pending_lock
 *   ->active_lock
 *
 * Objects are _never_ moved from ->active to ->pending, i.e., during
 * disconnection, the object is destroyed. Well, we could have tried
 * to reconnect, but that's pure waste.. let the other end reconnect.
 */

typedef struct changelog_clnt {
        xlator_t *this;

        /* pending connections */
        pthread_mutex_t pending_lock;
        pthread_cond_t pending_cond;
        struct list_head pending;

        /* current active connections */
        gf_lock_t active_lock;
        struct list_head active;

        gf_lock_t wait_lock;
        struct list_head waitq;

        /* consumer part of rot-buffs */
        rbuf_t *rbuf;
        unsigned long sequence;
} changelog_clnt_t;

void *changelog_ev_connector (void *);

void *changelog_ev_dispatch (void *);

/* APIs */
void
changelog_ev_queue_connection (changelog_clnt_t *, changelog_rpc_clnt_t *);

void
changelog_ev_cleanup_connections (xlator_t *, changelog_clnt_t *);

#endif

