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
add_upcall_client (call_frame_t *frame, client_t *client,
                   upcall_inode_ctx_t *up_inode_ctx)
{
        upcall_client_t *up_client_entry = NULL;

        pthread_mutex_lock (&up_inode_ctx->client_list_lock);
        {
                up_client_entry = __add_upcall_client (frame,
                                                       client,
                                                       up_inode_ctx);
        }
        pthread_mutex_unlock (&up_inode_ctx->client_list_lock);

        return up_client_entry;
}

upcall_client_t*
__add_upcall_client (call_frame_t *frame, client_t *client,
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
 * Given client->uid, retrieve the corresponding upcall client entry.
 * If none found, create a new entry.
 */
upcall_client_t*
__get_upcall_client (call_frame_t *frame, client_t *client,
                     upcall_inode_ctx_t *up_inode_ctx)
{
        upcall_client_t *up_client_entry = NULL;
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
                up_client_entry = __add_upcall_client (frame, client,
                                                       up_inode_ctx);
        }

        return up_client_entry;
}

int
__upcall_inode_ctx_set (inode_t *inode, xlator_t *this)
{
        upcall_inode_ctx_t *inode_ctx   = NULL;
        upcall_private_t   *priv        = NULL;
        int                ret          = -1;
        uint64_t           ctx          = 0;

        priv = this->private;
        GF_ASSERT(priv);

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
        INIT_LIST_HEAD (&inode_ctx->inode_ctx_list);
        INIT_LIST_HEAD (&inode_ctx->client_list);
        inode_ctx->destroy = 0;
        gf_uuid_copy (inode_ctx->gfid, inode->gfid);

        ctx = (long) inode_ctx;
        ret = __inode_ctx_set (inode, this, &ctx);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to set inode ctx (%p)", inode);
                GF_FREE (inode_ctx);
                goto out;
        }

        /* add this inode_ctx to the global list */
        LOCK (&priv->inode_ctx_lk);
        {
                list_add_tail (&inode_ctx->inode_ctx_list,
                               &priv->inode_ctx_list);
        }
        UNLOCK (&priv->inode_ctx_lk);
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

        inode_ctx = (upcall_inode_ctx_t *) (long) (ctx);

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
upcall_cleanup_expired_clients (xlator_t *this,
                                upcall_inode_ctx_t *up_inode_ctx) {

        upcall_client_t *up_client      = NULL;
        upcall_client_t *tmp            = NULL;
        int             ret             = -1;
        time_t          timeout         = 0;
        time_t          t_expired       = 0;

        timeout = get_cache_invalidation_timeout(this);

        pthread_mutex_lock (&up_inode_ctx->client_list_lock);
        {
                list_for_each_entry_safe (up_client,
                                          tmp,
                                          &up_inode_ctx->client_list,
                                          client_list) {
                        t_expired = time(NULL) -
                                         up_client->access_time;

                        if (t_expired > (2*timeout)) {

                                gf_log (THIS->name, GF_LOG_TRACE,
                                        "Cleaning up client_entry(%s)",
                                        up_client->client_uid);

                                ret =
                                  __upcall_cleanup_client_entry (up_client);

                                if (ret) {
                                        gf_msg ("upcall", GF_LOG_WARNING, 0,
                                                UPCALL_MSG_INTERNAL_ERROR,
                                                "Client entry cleanup failed (%p)",
                                                up_client);
                                        goto out;
                                }
                        }
                }
        }
        pthread_mutex_unlock (&up_inode_ctx->client_list_lock);

        ret = 0;
out:
        return ret;
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
        upcall_private_t   *priv        = NULL;

        priv = this->private;
        GF_ASSERT(priv);

        ret = inode_ctx_del (inode, this, &ctx);

        if (ret < 0) {
                gf_msg ("upcall", GF_LOG_WARNING, 0,
                        UPCALL_MSG_INTERNAL_ERROR,
                        "Failed to del upcall_inode_ctx (%p)",
                        inode);
                goto out;
        }

        inode_ctx = (upcall_inode_ctx_t *)(long) ctx;

        if (inode_ctx) {

                /* Invalidate all the upcall cache entries */
                upcall_cache_forget (this, inode, inode_ctx);

                /* do we really need lock? yes now reaper thread
                 * may also be trying to cleanup the client entries.
                 */
                pthread_mutex_lock (&inode_ctx->client_list_lock);
                {
                        if (!list_empty (&inode_ctx->client_list)) {
                                __upcall_cleanup_inode_ctx_client_list (inode_ctx);
                        }
                }
                pthread_mutex_unlock (&inode_ctx->client_list_lock);

                /* Mark the inode_ctx to be destroyed */
                inode_ctx->destroy = 1;
                gf_msg_debug ("upcall", 0, "set upcall_inode_ctx (%p) to destroy mode",
                              inode_ctx);
        }

out:
        return ret;
}

/*
 * Traverse through the list of upcall_inode_ctx(s),
 * cleanup the expired client entries and destroy the ctx
 * which is no longer valid and has destroy bit set.
 */
void *
upcall_reaper_thread (void *data)
{
        upcall_private_t *priv          = NULL;
        upcall_inode_ctx_t *inode_ctx   = NULL;
        upcall_inode_ctx_t *tmp         = NULL;
        xlator_t           *this        = NULL;
        time_t              timeout     = 0;

        this = (xlator_t *)data;
        GF_ASSERT (this);

        priv = this->private;
        GF_ASSERT (priv);


        while (!priv->fini) {
                list_for_each_entry_safe (inode_ctx, tmp,
                                          &priv->inode_ctx_list,
                                          inode_ctx_list) {

                        /* cleanup expired clients */
                        upcall_cleanup_expired_clients (this, inode_ctx);

                        if (!inode_ctx->destroy) {
                                continue;
                        }

                        LOCK (&priv->inode_ctx_lk);
                        {
                                /* client list would have been cleaned up*/
                                gf_msg_debug ("upcall", 0, "Freeing upcall_inode_ctx (%p)",
                                              inode_ctx);
                                list_del_init (&inode_ctx->inode_ctx_list);
                                pthread_mutex_destroy (&inode_ctx->client_list_lock);
                                GF_FREE (inode_ctx);
                                inode_ctx = NULL;
                        }
                        UNLOCK (&priv->inode_ctx_lk);
                }

                /* don't do a very busy loop */
                timeout = get_cache_invalidation_timeout (this);
                sleep (timeout / 2);
        }

        return NULL;
}

/*
 * Initialize upcall reaper thread.
 */
int
upcall_reaper_thread_init (xlator_t *this)
{
        upcall_private_t *priv  = NULL;
        int              ret    = -1;

        priv = this->private;
        GF_ASSERT (priv);

        ret = pthread_create (&priv->reaper_thr, NULL,
                              upcall_reaper_thread, this);

        return ret;
}


int
up_compare_afr_xattr (dict_t *d, char *k, data_t *v, void *tmp)
{
        dict_t *dict = tmp;

        if (!strncmp (k, AFR_XATTR_PREFIX, strlen (AFR_XATTR_PREFIX))
            && (!is_data_equal (v, dict_get (dict, k))))
                return -1;

        return 0;
}


static void
up_filter_afr_xattr (dict_t *xattrs, char *xattr, data_t *v)
{
        /* Filter the afr pending xattrs, with value 0. Ideally this should
         * be executed only in case of xattrop and not in set and removexattr,
         * butset and remove xattr fops do not come with keys AFR_XATTR_PREFIX
         */
        if (!strncmp (xattr, AFR_XATTR_PREFIX, strlen (AFR_XATTR_PREFIX))
            && (mem_0filled (v->data, v->len) == 0)) {
                dict_del (xattrs, xattr);
        }
        return;
}


static int
up_filter_unregd_xattr (dict_t *xattrs, char *xattr, data_t *v,
                        void *regd_xattrs)
{
        if (dict_get ((dict_t *)regd_xattrs, xattr) == NULL) {
                /* xattr was not found in the registered xattr, hence do not
                 * send notification for its change
                 */
                dict_del (xattrs, xattr);
                goto out;
        }
        up_filter_afr_xattr (xattrs, xattr, v);
out:
        return 0;
}


int
up_filter_xattr (dict_t *xattr, dict_t *regd_xattrs)
{
        int ret = 0;

        /* Remove the xattrs from the dict, if they are not registered for
         * cache invalidation */
        ret = dict_foreach (xattr, up_filter_unregd_xattr, regd_xattrs);
        return ret;
}


gf_boolean_t
up_invalidate_needed (dict_t *xattrs)
{
        if (dict_key_count (xattrs) == 0) {
                gf_msg_trace ("upcall", 0, "None of xattrs requested for"
                              " invalidation, were changed. Nothing to "
                              "invalidate");
                return _gf_false;
        }

        return _gf_true;
}


/*
 * Given a client, first fetch upcall_entry_t from the inode_ctx client list.
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
                         inode_t *inode, uint32_t flags, struct iatt *stbuf,
                         struct iatt *p_stbuf, struct iatt *oldp_stbuf,
                         dict_t *xattr)
{
        upcall_client_t *up_client_entry = NULL;
        upcall_client_t *tmp             = NULL;
        upcall_inode_ctx_t *up_inode_ctx = NULL;
        gf_boolean_t     found           = _gf_false;

        if (!is_upcall_enabled(this))
                return;

        /* server-side generated fops like quota/marker will not have any
         * client associated with them. Ignore such fops.
         */
        if (!client) {
                gf_msg_debug ("upcall", 0, "Internal fop - client NULL");
                return;
        }

        up_inode_ctx = ((upcall_local_t *)frame->local)->upcall_inode_ctx;

        if (!up_inode_ctx)
                up_inode_ctx = upcall_inode_ctx_get (inode, this);

        if (!up_inode_ctx) {
                gf_msg ("upcall", GF_LOG_WARNING, 0,
                        UPCALL_MSG_INTERNAL_ERROR,
                        "upcall_inode_ctx_get failed (%p)",
                        inode);
                return;
        }

        /* In case of LOOKUP, if first time, inode created shall be
         * invalid till it gets linked to inode table. Read gfid from
         * the stat returned in such cases.
         */
        if (gf_uuid_is_null (up_inode_ctx->gfid)) {
                /* That means inode must have been invalid when this inode_ctx
                 * is created. Copy the gfid value from stbuf instead.
                 */
                gf_uuid_copy (up_inode_ctx->gfid, stbuf->ia_gfid);
        }

        if (gf_uuid_is_null (up_inode_ctx->gfid)) {
                gf_msg_debug (this->name, 0, "up_inode_ctx->gfid and "
                              "stbuf->ia_gfid is NULL, fop:%s",
                              gf_fop_list[frame->root->op]);
                goto out;
        }

        pthread_mutex_lock (&up_inode_ctx->client_list_lock);
        {
                list_for_each_entry_safe (up_client_entry, tmp,
                                          &up_inode_ctx->client_list,
                                          client_list) {

                        /* Do not send UPCALL event if same client. */
                        if (!strcmp(client->client_uid,
                                   up_client_entry->client_uid)) {
                                up_client_entry->access_time = time(NULL);
                                found = _gf_true;
                                continue;
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
                        upcall_client_cache_invalidate (this,
                                                       up_inode_ctx->gfid,
                                                       up_client_entry,
                                                       flags, stbuf,
                                                       p_stbuf, oldp_stbuf,
                                                       xattr);
                }

                if (!found) {
                        up_client_entry = __add_upcall_client (frame,
                                                               client,
                                                               up_inode_ctx);
                }
        }
        pthread_mutex_unlock (&up_inode_ctx->client_list_lock);
out:
        return;
}

/*
 * If the upcall_client_t has recently accessed the file (i.e, within
 * priv->cache_invalidation_timeout), send a upcall notification.
 */
void
upcall_client_cache_invalidate (xlator_t *this, uuid_t gfid,
                                upcall_client_t *up_client_entry,
                                uint32_t flags, struct iatt *stbuf,
                                struct iatt *p_stbuf,
                                struct iatt *oldp_stbuf, dict_t *xattr)
{
        struct gf_upcall                    up_req  = {0,};
        struct gf_upcall_cache_invalidation ca_req  = {0,};
        time_t                              timeout = 0;
        int                                 ret     = -1;
        time_t t_expired = time(NULL) - up_client_entry->access_time;

        GF_VALIDATE_OR_GOTO ("upcall_client_cache_invalidate",
                             !(gf_uuid_is_null (gfid)), out);
        timeout = get_cache_invalidation_timeout(this);

        if (t_expired < timeout) {
                /* Send notify call */
                up_req.client_uid = up_client_entry->client_uid;
                gf_uuid_copy (up_req.gfid, gfid);

                ca_req.flags = flags;
                ca_req.expire_time_attr =
                                 up_client_entry->expire_time_attr;
                if (stbuf)
                        ca_req.stat = *stbuf;
                if (p_stbuf)
                        ca_req.p_stat = *p_stbuf;
                if (oldp_stbuf)
                        ca_req.oldp_stat = *oldp_stbuf;
                ca_req.dict = xattr;

                up_req.data = &ca_req;
                up_req.event_type = GF_UPCALL_CACHE_INVALIDATION;

                gf_log (THIS->name, GF_LOG_TRACE,
                        "Cache invalidation notification sent to %s",
                        up_client_entry->client_uid);

                /* Need to send inode flags */
                ret = this->notify (this, GF_EVENT_UPCALL, &up_req);

                /*
                 * notify may fail as the client could have been
                 * dis(re)connected. Cleanup the client entry.
                 */
                if (ret < 0)
                        __upcall_cleanup_client_entry (up_client_entry);

        } else {
                gf_log (THIS->name, GF_LOG_TRACE,
                        "Cache invalidation notification NOT sent to %s",
                        up_client_entry->client_uid);

                if (t_expired > (2*timeout)) {
                        /* Cleanup the entry */
                        __upcall_cleanup_client_entry (up_client_entry);
                }
        }
out:
        return;
}

/*
 * This is called during upcall_inode_ctx cleanup incase of 'inode_forget'.
 * Send "UP_FORGET" to all the clients so that they invalidate their cache
 * entry and do a fresh lookup next time when any I/O comes in.
 */
void
upcall_cache_forget (xlator_t *this, inode_t *inode, upcall_inode_ctx_t *up_inode_ctx)
{
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
                                                       up_inode_ctx->gfid,
                                                       up_client_entry,
                                                       flags, NULL,
                                                       NULL, NULL, NULL);
                }

        }
        pthread_mutex_unlock (&up_inode_ctx->client_list_lock);
}
