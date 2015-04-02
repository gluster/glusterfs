/*
   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "compat.h"
#include "xlator.h"
#include "inode.h"
#include "logging.h"
#include "common-utils.h"

#include "statedump.h"
#include "syncop.h"

#include "upcall.h"
#include "upcall-mem-types.h"
#include "glusterfs3-xdr.h"
#include "protocol-common.h"
#include "defaults.h"

/*
 * Check if any of the upcall options are enabled:
 *     - cache_invalidation
 *     - XXX: lease_lk
 */
gf_boolean_t
is_upcall_enabled(xlator_t *this) {
        upcall_private_t *priv      = NULL;
        gf_boolean_t     is_enabled = _gf_false;

        if (this->private) {
                priv = (upcall_private_t *)this->private;

                if (priv->cache_invalidation_enabled) {
                        is_enabled = _gf_true;
                }
        }

        return is_enabled;
}

/*
 * Check if any of cache_invalidation is enabled
 */
gf_boolean_t
is_cache_invalidation_enabled(xlator_t *this) {
        upcall_private_t *priv      = NULL;
        gf_boolean_t     is_enabled = _gf_false;

        if (this->private) {
                priv = (upcall_private_t *)this->private;

                if (priv->cache_invalidation_enabled) {
                        is_enabled = _gf_true;
                }
        }

        return is_enabled;
}

/*
 * Get the cache_invalidation_timeout
 */
int32_t
get_cache_invalidation_timeout(xlator_t *this) {
        upcall_private_t *priv      = NULL;
        int32_t          timeout    = 0;

        if (this->private) {
                priv = (upcall_private_t *)this->private;
                timeout = priv->cache_invalidation_timeout;
        }

        return timeout;
}

/*
 * Allocate and add a new client entry to the given upcall entry
 */
upcall_client_t*
add_upcall_client (call_frame_t *frame, uuid_t gfid,
                   client_t *client,
                   upcall_inode_ctx_t *up_inode_ctx)
{
        upcall_client_t *up_client_entry = NULL;

        pthread_mutex_lock (&up_inode_ctx->client_list_lock);
        {
                up_client_entry = __add_upcall_client (frame,
                                                       gfid,
                                                       client,
                                                       up_inode_ctx);
        }
        pthread_mutex_unlock (&up_inode_ctx->client_list_lock);

        return up_client_entry;
}

upcall_client_t*
__add_upcall_client (call_frame_t *frame, uuid_t gfid,
                     client_t *client,
                     upcall_inode_ctx_t *up_inode_ctx)
{
        upcall_client_t *up_client_entry = NULL;

        up_client_entry = GF_CALLOC (1, sizeof(*up_client_entry),
                                     gf_upcall_mt_upcall_client_entry_t);
        if (!up_client_entry) {
                gf_msg ("upcall", GF_LOG_WARNING, 0,
                        UPCALL_MSG_NO_MEMORY,
                        "Memory allocation failed");
                return NULL;
        }
        INIT_LIST_HEAD (&up_client_entry->client_list);
        up_client_entry->client_uid = gf_strdup(client->client_uid);
        up_client_entry->access_time = time(NULL);
        up_client_entry->expire_time_attr =
                        get_cache_invalidation_timeout(frame->this);

        list_add_tail (&up_client_entry->client_list,
                       &up_inode_ctx->client_list);

        gf_log (THIS->name, GF_LOG_DEBUG, "upcall_entry_t client added - %s",
                up_client_entry->client_uid);

        return up_client_entry;
}

/*
 * Given gfid and client->uid, retrieve the corresponding upcall client entry.
 * If none found, create a new entry.
 */
upcall_client_t*
__get_upcall_client (call_frame_t *frame, uuid_t gfid, client_t *client,
                     upcall_inode_ctx_t *up_inode_ctx)
{
        upcall_client_t *up_client_entry = NULL;
        upcall_client_t *up_client       = NULL;
        upcall_client_t *tmp             = NULL;
        gf_boolean_t    found_client     = _gf_false;

        list_for_each_entry_safe (up_client_entry, tmp,
                                  &up_inode_ctx->client_list,
                                  client_list) {
                if (strcmp(client->client_uid,
                           up_client_entry->client_uid) == 0) {
                        /* found client entry. Update the access_time */
                        up_client_entry->access_time = time(NULL);
                        found_client = _gf_true;
                        gf_log (THIS->name, GF_LOG_DEBUG,
                                "upcall_entry_t client found - %s",
                                up_client_entry->client_uid);
                        break;
                }
        }

        if (!found_client) { /* create one */
                up_client_entry = __add_upcall_client (frame, gfid, client,
                                                       up_inode_ctx);
        }

        return up_client_entry;
}

int
__upcall_inode_ctx_set (inode_t *inode, xlator_t *this)
{
        upcall_inode_ctx_t *inode_ctx   = NULL;
        int                ret          = -1;
        uint64_t           ctx          = 0;

        ret = __inode_ctx_get (inode, this, &ctx);

        if (!ret)
                goto out;

        inode_ctx = GF_CALLOC (1, sizeof (upcall_inode_ctx_t),
                               gf_upcall_mt_upcall_inode_ctx_t);

        if (!inode_ctx) {
                ret = -ENOMEM;
                goto out;
        }

        pthread_mutex_init (&inode_ctx->client_list_lock, NULL);
        INIT_LIST_HEAD (&inode_ctx->client_list);

        ret = __inode_ctx_set (inode, this, (uint64_t *) inode_ctx);
        if (ret)
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to set inode ctx (%p)", inode);
out:
        return ret;
}

upcall_inode_ctx_t *
__upcall_inode_ctx_get (inode_t *inode, xlator_t *this)
{
        upcall_inode_ctx_t *inode_ctx   = NULL;
        uint64_t           ctx          = 0;
        int                ret          = 0;

        ret = __inode_ctx_get (inode, this, &ctx);

        if (ret < 0) {
                ret = __upcall_inode_ctx_set (inode, this);
                if (ret < 0)
                        goto out;

                ret = __inode_ctx_get (inode, this, &ctx);
                if (ret < 0)
                        goto out;
        }

        inode_ctx = (upcall_inode_ctx_t *)(long) ctx;

out:
        return inode_ctx;
}

upcall_inode_ctx_t *
upcall_inode_ctx_get (inode_t *inode, xlator_t *this)
{
        upcall_inode_ctx_t *inode_ctx = NULL;

        LOCK (&inode->lock);
        {
                inode_ctx = __upcall_inode_ctx_get (inode, this);
        }
        UNLOCK (&inode->lock);

        return inode_ctx;
}

int
__upcall_cleanup_client_entry (upcall_client_t *up_client)
{
        list_del_init (&up_client->client_list);

        GF_FREE (up_client->client_uid);
        GF_FREE (up_client);

        return 0;
}

/*
 * Free Upcall inode_ctx client list
 */
int
__upcall_cleanup_inode_ctx_client_list (upcall_inode_ctx_t *inode_ctx)
{
        upcall_client_t *up_client        = NULL;
        upcall_client_t *tmp              = NULL;

        list_for_each_entry_safe (up_client, tmp,
                                  &inode_ctx->client_list,
                                  client_list) {
                __upcall_cleanup_client_entry (up_client);
        }

        return 0;
}

/*
 * Free upcall_inode_ctx
 */
int
upcall_cleanup_inode_ctx (xlator_t *this, inode_t *inode)
{
        uint64_t           ctx          = 0;
        upcall_inode_ctx_t *inode_ctx   = NULL;
        int                ret          = 0;

        ret = inode_ctx_get (inode, this, &ctx);

        if (ret < 0) {
                gf_log (THIS->name, GF_LOG_TRACE,
                        "Failed to get upcall_inode_ctx (%p)",
                        inode);
                goto out;
        }

        /* Invalidate all the upcall cache entries */
        upcall_cache_forget (this, inode, inode_ctx);

        /* Set inode context to NULL */
        ret = __inode_ctx_set (inode, this, NULL);

        if (!ret) {
                gf_log (this->name, GF_LOG_WARNING,
                        "_inode_ctx_set to NULL failed (%p)",
                        inode);
        }
        inode_ctx = (upcall_inode_ctx_t *)(long) ctx;

        if (inode_ctx) {
                /* do we really need lock? */
                pthread_mutex_lock (&inode_ctx->client_list_lock);
                {
                        if (!list_empty (&inode_ctx->client_list)) {
                                __upcall_cleanup_inode_ctx_client_list (inode_ctx);
                        }
                }
                pthread_mutex_unlock (&inode_ctx->client_list_lock);

                pthread_mutex_destroy (&inode_ctx->client_list_lock);

                GF_FREE (inode_ctx);
        }

out:
        return ret;
}

/*
 * Given a gfid, client, first fetch upcall_entry_t based on gfid.
 * Later traverse through the client list of that upcall entry. If this client
 * is not present in the list, create one client entry with this client info.
 * Also check if there are other clients which need to be notified of this
 * op. If yes send notify calls to them.
 *
 * Since sending notifications for cache_invalidation is a best effort,
 * any errors during the process are logged and ignored.
 */
void
upcall_cache_invalidate (call_frame_t *frame, xlator_t *this, client_t *client,
                         inode_t *inode, uint32_t flags)
{
        upcall_client_t *up_client       = NULL;
        upcall_client_t *up_client_entry = NULL;
        upcall_client_t *tmp             = NULL;
        upcall_inode_ctx_t *up_inode_ctx = NULL;
        gf_boolean_t     found           = _gf_false;

        up_inode_ctx = ((upcall_local_t *)frame->local)->upcall_inode_ctx;

        if (!up_inode_ctx)
                up_inode_ctx = upcall_inode_ctx_get (inode, this);

        if (!up_inode_ctx) {
                gf_log (this->name, GF_LOG_WARNING,
                        "upcall_inode_ctx_get failed (%p)",
                        inode);
                return;
        }

        pthread_mutex_lock (&up_inode_ctx->client_list_lock);
        {
                list_for_each_entry_safe (up_client_entry, tmp,
                                          &up_inode_ctx->client_list,
                                          client_list) {

                        if (!strcmp(client->client_uid,
                                   up_client_entry->client_uid)) {
                                up_client_entry->access_time = time(NULL);
                                found = _gf_true;
                        }

                        /*
                         * Ignore sending notifications in case of only UP_ATIME
                         */
                        if (!(flags & ~(UP_ATIME))) {
                                if (found)
                                        break;
                                else /* we still need to find current client entry*/
                                        continue;
                        }

                        /* any other client */

                        /* XXX: Send notifications asynchrounously
                         * instead of in the I/O path - BZ 1200264
                         *  Also if the file is frequently accessed, set
                         *  expire_time_attr to 0.
                         */
                        upcall_client_cache_invalidate(this,
                                                       inode->gfid,
                                                       up_client_entry,
                                                       flags);
                }

                if (!found) {
                        up_client_entry = __add_upcall_client (frame,
                                                               inode->gfid,
                                                               client,
                                                               up_inode_ctx);
                }
        }
        pthread_mutex_unlock (&up_inode_ctx->client_list_lock);
}

/*
 * If the upcall_client_t has recently accessed the file (i.e, within
 * priv->cache_invalidation_timeout), send a upcall notification.
 */
void
upcall_client_cache_invalidate (xlator_t *this, uuid_t gfid,
                                upcall_client_t *up_client_entry,
                                uint32_t flags)
{
        notify_event_data_t n_event_data;
        time_t timeout   = 0;
        time_t t_expired = time(NULL) - up_client_entry->access_time;

        timeout = get_cache_invalidation_timeout(this);

        if (t_expired < timeout) {
                /* Send notify call */
                gf_uuid_copy(n_event_data.gfid, gfid);
                n_event_data.client_entry = up_client_entry;
                n_event_data.event_type = CACHE_INVALIDATION;
                n_event_data.invalidate_flags = flags;

                /* Need to send inode flags */
                this->notify (this, GF_EVENT_UPCALL, &n_event_data);

                gf_log (THIS->name, GF_LOG_TRACE,
                        "Cache invalidation notification sent to %s",
                        up_client_entry->client_uid);

        } else {
                if (t_expired > (2*timeout)) {
                        /* Cleanup the entry */
                        __upcall_cleanup_client_entry (up_client_entry);
                }

                gf_log (THIS->name, GF_LOG_TRACE,
                        "Cache invalidation notification NOT sent to %s",
                        up_client_entry->client_uid);
        }
}

/*
 * This is called during upcall_inode_ctx cleanup incase of 'inode_forget'.
 * Send "UP_FORGET" to all the clients so that they invalidate their cache
 * entry and do a fresh lookup next time when any I/O comes in.
 */
void
upcall_cache_forget (xlator_t *this, inode_t *inode, upcall_inode_ctx_t *up_inode_ctx)
{
        upcall_client_t *up_client       = NULL;
        upcall_client_t *up_client_entry = NULL;
        upcall_client_t *tmp             = NULL;
        uint32_t        flags            = 0;

        if (!up_inode_ctx) {
                return;
        }

        pthread_mutex_lock (&up_inode_ctx->client_list_lock);
        {
                list_for_each_entry_safe (up_client_entry, tmp,
                                          &up_inode_ctx->client_list,
                                          client_list) {
                        flags = UP_FORGET;

                        /* Set the access time to time(NULL)
                         * to send notify */
                        up_client_entry->access_time = time(NULL);

                        upcall_client_cache_invalidate(this,
                                                       inode->gfid,
                                                       up_client_entry,
                                                       flags);
                }

        }
        pthread_mutex_unlock (&up_inode_ctx->client_list_lock);
}
