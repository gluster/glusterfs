/*
   Copyright (c) 2015-2016 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "leases.h"


/* Mutex locks used in this xlator and their order of acquisition:
 * Check lease conflict:
 *         lease_ctx lock
 *                 add_timer => internal timer locks
 *         lease_ctx unlock
 *
 * Add/remove lease:
 *         lease_ctx lock
 *                 add_timer => internal timer locks
 *                 OR
 *                 priv lock => Adding/removing to/from the cleanup client list
 *                 priv unlock
 *         lease_ctx unlock
 *
 * Timer thread:
 *         Timer internal lock
 *                 priv lock => By timer handler
 *                 priv unlock
 *         Timer internal unlock
 *
 * Expired recall cleanup thread:
 *         priv lock
 *                 priv condwait
 *         priv unlock
 *         lease_ctx lock
 *                 priv lock
 *                 priv unlock
 *         lease_ctx unlock
 */

/*
 * Check if lease_lk is enabled
 * Return Value:
 * _gf_true  - lease lock option enabled
 * _gf_false - lease lock option disabled
 */
gf_boolean_t
is_leases_enabled (xlator_t *this)
{
        leases_private_t    *priv       = NULL;
        gf_boolean_t         is_enabled = _gf_false;

        GF_VALIDATE_OR_GOTO ("leases", this, out);

        if (this->private) {
                priv = (leases_private_t *)this->private;
                is_enabled = priv->leases_enabled;
        }
out:
        return is_enabled;
}


/*
 * Get the recall_leaselk_timeout
 * Return Value:
 * timeout value(in seconds) set as an option to this xlator.
 * -1 error case
 */
int32_t
get_recall_lease_timeout (xlator_t *this)
{
        leases_private_t    *priv       = NULL;
        int32_t              timeout    = -1;

        GF_VALIDATE_OR_GOTO ("leases", this, out);

        if (this->private) {
                priv = (leases_private_t *)this->private;
                timeout = priv->recall_lease_timeout;
        }
out:
        return timeout;
}


static void
__dump_leases_info (xlator_t *this, lease_inode_ctx_t *lease_ctx)
{
        lease_id_entry_t   *lease_entry     = NULL;
        lease_id_entry_t   *tmp             = NULL;

        GF_VALIDATE_OR_GOTO ("leases", this, out);
        GF_VALIDATE_OR_GOTO ("leases", lease_ctx, out);

        gf_msg_debug (this->name, 0, "Lease held on this inode, lease_type: %d,"
                      " lease_cnt:%"PRIu64", RD lease:%d, RW lease:%d, "
                      "openfd cnt:%"PRIu64, lease_ctx->lease_type,
                      lease_ctx->lease_cnt,
                      lease_ctx->lease_type_cnt[GF_RD_LEASE],
                      lease_ctx->lease_type_cnt[GF_RW_LEASE],
                      lease_ctx->openfd_cnt);

        list_for_each_entry_safe (lease_entry, tmp,
                                  &lease_ctx->lease_id_list,
                                  lease_id_list) {
                gf_msg_debug (this->name, 0, "Leases held by client: %s, lease "
                              "ID:%s, RD lease:%d, RW lease:%d, lease_type: %d, "
                              "lease_cnt:%"PRIu64, lease_entry->client_uid,
                              lease_entry->lease_id,
                              lease_entry->lease_type_cnt[GF_RD_LEASE],
                              lease_entry->lease_type_cnt[GF_RW_LEASE],
                              lease_entry->lease_type, lease_entry->lease_cnt);
        }
out:
        return;
}


static int
__lease_ctx_set (inode_t *inode, xlator_t *this)
{
        lease_inode_ctx_t   *inode_ctx    = NULL;
        int                  ret          = -1;
        uint64_t             ctx          = 0;

        GF_VALIDATE_OR_GOTO ("leases", inode, out);
        GF_VALIDATE_OR_GOTO ("leases", this, out);

        ret = __inode_ctx_get (inode, this, &ctx);
        if (!ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, LEASE_MSG_INVAL_INODE_CTX,
                        "inode_ctx_get failed");
                goto out;
        }

        inode_ctx = GF_CALLOC (1, sizeof (*inode_ctx),
                               gf_leases_mt_lease_inode_ctx_t);
        GF_CHECK_ALLOC (inode_ctx, ret, out);

        pthread_mutex_init (&inode_ctx->lock, NULL);
        INIT_LIST_HEAD (&inode_ctx->lease_id_list);
        INIT_LIST_HEAD (&inode_ctx->blocked_list);

        inode_ctx->lease_cnt = 0;

        ret = __inode_ctx_set (inode, this, (uint64_t *) inode_ctx);
        if (ret) {
                GF_FREE (inode_ctx);
                gf_msg (this->name, GF_LOG_INFO, 0, LEASE_MSG_INVAL_INODE_CTX,
                        "failed to set inode ctx (%p)", inode);
        }
out:
        return ret;
}


static lease_inode_ctx_t *
__lease_ctx_get (inode_t *inode, xlator_t *this)
{
        lease_inode_ctx_t *inode_ctx    = NULL;
        uint64_t           ctx          = 0;
        int                ret          = 0;

        GF_VALIDATE_OR_GOTO ("leases", inode, out);
        GF_VALIDATE_OR_GOTO ("leases", this, out);

        ret = __inode_ctx_get (inode, this, &ctx);
        if (ret < 0) {
                ret = __lease_ctx_set (inode, this);
                if (ret < 0)
                        goto out;

                ret = __inode_ctx_get (inode, this, &ctx);
                if (ret < 0) {
                        gf_msg (this->name, GF_LOG_WARNING, 0, LEASE_MSG_INVAL_INODE_CTX,
                                "failed to get inode ctx (%p)", inode);
                        goto out;
                }
        }

        inode_ctx = (lease_inode_ctx_t *)(long) ctx;
out:
        return inode_ctx;
}


lease_inode_ctx_t *
lease_ctx_get (inode_t *inode, xlator_t *this)
{
        lease_inode_ctx_t *inode_ctx = NULL;

        GF_VALIDATE_OR_GOTO ("leases", inode, out);
        GF_VALIDATE_OR_GOTO ("leases", this, out);

        LOCK (&inode->lock);
        {
                inode_ctx = __lease_ctx_get (inode, this);
        }
        UNLOCK (&inode->lock);
out:
        return inode_ctx;
}


static lease_id_entry_t *
new_lease_id_entry (call_frame_t *frame, const char *lease_id)
{
        lease_id_entry_t *lease_entry = NULL;

        GF_VALIDATE_OR_GOTO ("leases", frame, out);
        GF_VALIDATE_OR_GOTO ("leases", lease_id, out);

        lease_entry = GF_CALLOC (1, sizeof (*lease_entry),
                                  gf_leases_mt_lease_id_entry_t);
        if (!lease_entry) {
                gf_msg (frame->this->name, GF_LOG_ERROR, ENOMEM, LEASE_MSG_NO_MEM,
                        "Memory allocation for lease_entry failed");
                return NULL;
        }

        INIT_LIST_HEAD (&lease_entry->lease_id_list);
        lease_entry->lease_type = NONE;
        lease_entry->lease_cnt = 0;
        lease_entry->recall_time =
                        get_recall_lease_timeout (frame->this);
        lease_entry->client_uid = gf_strdup (frame->root->client->client_uid);
        if (!lease_entry->client_uid) {
                gf_msg (frame->this->name, GF_LOG_ERROR, ENOMEM, LEASE_MSG_NO_MEM,
                        "Memory allocation for client_uid failed");
                GF_FREE (lease_entry);
                lease_entry = NULL;
                goto out;
        }

        memcpy (lease_entry->lease_id, lease_id, LEASE_ID_SIZE);
out:
        return lease_entry;
}


static void
__destroy_lease_id_entry (lease_id_entry_t *lease_entry)
{
        GF_VALIDATE_OR_GOTO ("leases", lease_entry, out);

        list_del_init (&lease_entry->lease_id_list);
        GF_FREE (lease_entry->client_uid);
        GF_FREE (lease_entry);
out:
        return;
}


static inline gf_boolean_t
__is_same_lease_id (const char *k1, const char *k2)
{
        if (memcmp(k1, k2, LEASE_ID_SIZE) == 0)
                return _gf_true;

        return _gf_false;
}


/* Checks if there are any leases, other than the leases taken
 * by the given lease_id
 */
static gf_boolean_t
__another_lease_found (lease_inode_ctx_t *lease_ctx, const char *lease_id)
{
        lease_id_entry_t   *lease_entry     = NULL;
        lease_id_entry_t   *tmp             = NULL;
        gf_boolean_t        found_lease     = _gf_false;

        GF_VALIDATE_OR_GOTO ("leases", lease_id, out);
        GF_VALIDATE_OR_GOTO ("leases", lease_ctx, out);

        list_for_each_entry_safe (lease_entry, tmp,
                                  &lease_ctx->lease_id_list,
                                  lease_id_list) {

                if (!__is_same_lease_id (lease_id, lease_entry->lease_id)) {
                        if (lease_entry->lease_cnt > 0) {
                                found_lease = _gf_true;
                                break;
                        }
                }
        }
out:
        return found_lease;
}


/* Returns the lease_id_entry for a given lease_id and a given inode.
 * Return values:
 * NULL - If no client entry found
 * lease_id_entry_t* - a pointer to the client entry if found
 */
static lease_id_entry_t *
__get_lease_id_entry (lease_inode_ctx_t *lease_ctx, const char *lease_id)
{
        lease_id_entry_t   *lease_entry    = NULL;
        lease_id_entry_t   *tmp            = NULL;
        lease_id_entry_t   *found          = NULL;

        GF_VALIDATE_OR_GOTO ("leases", lease_id, out);
        GF_VALIDATE_OR_GOTO ("leases", lease_ctx, out);

        list_for_each_entry_safe (lease_entry, tmp,
                                  &lease_ctx->lease_id_list,
                                  lease_id_list) {

                if (__is_same_lease_id (lease_id, lease_entry->lease_id)) {
                        found = lease_entry;
                        gf_msg_debug ("leases", 0, "lease ID entry found "
                                      "Client UID:%s, lease id:%s",
                                      lease_entry->client_uid,
                                      leaseid_utoa (lease_entry->lease_id));
                        break;
                }
        }
out:
        return found;
}


/* Returns the lease_id_entry for a given lease_id and a given inode,
 * if none found creates one.
 * Return values:
 * lease_id_entry_t* - a pointer to the client entry
 */
static lease_id_entry_t *
__get_or_new_lease_entry (call_frame_t *frame, const char *lease_id,
                           lease_inode_ctx_t *lease_ctx)
{
        lease_id_entry_t *lease_entry    = NULL;

        GF_VALIDATE_OR_GOTO ("leases", frame, out);
        GF_VALIDATE_OR_GOTO ("leases", lease_id, out);
        GF_VALIDATE_OR_GOTO ("leases", lease_ctx, out);

        lease_entry = __get_lease_id_entry (lease_ctx, lease_id);
        if (!lease_entry) { /* create one */
                lease_entry = new_lease_id_entry (frame, lease_id);
                if (!lease_entry)
                        goto out;

                list_add_tail (&lease_entry->lease_id_list,
                               &lease_ctx->lease_id_list);

                gf_msg_debug (frame->this->name, 0, "lease ID entry added,"
                              " Client UID:%s, lease id:%s",
                              lease_entry->client_uid,
                              leaseid_utoa (lease_entry->lease_id));
        }
out:
        return lease_entry;
}


static lease_inode_t *
new_lease_inode (inode_t *inode)
{
        lease_inode_t *l_inode = NULL;

        l_inode = GF_CALLOC (1, sizeof (*l_inode), gf_leases_mt_lease_inode_t);
        if (!l_inode)
                goto out;

        INIT_LIST_HEAD (&l_inode->list);
        l_inode->inode = inode_ref (inode);
out:
        return l_inode;
}


static void
__destroy_lease_inode (lease_inode_t *l_inode)
{
        list_del_init (&l_inode->list);
        inode_unref (l_inode->inode);
        GF_FREE (l_inode);
}


static lease_client_t *
new_lease_client (const char *client_uid)
{
        lease_client_t  *clnt = NULL;

        clnt = GF_CALLOC (1, sizeof (*clnt), gf_leases_mt_lease_client_t);
        if (!clnt)
                goto out;

        INIT_LIST_HEAD (&clnt->client_list);
        INIT_LIST_HEAD (&clnt->inode_list);
        clnt->client_uid = gf_strdup (client_uid);
out:
        return clnt;
}


static void
__destroy_lease_client (lease_client_t *clnt)
{
         list_del_init (&clnt->inode_list);
         list_del_init (&clnt->client_list);
         GF_FREE (clnt);

         return;
}


static lease_client_t *
__get_lease_client (xlator_t *this, leases_private_t *priv,
                    const char *client_uid)
{
        lease_client_t  *clnt  = NULL;
        lease_client_t  *tmp   = NULL;
        lease_client_t  *found = NULL;

        list_for_each_entry_safe (clnt, tmp, &priv->client_list, client_list) {
                if ((strcmp (clnt->client_uid, client_uid) == 0)) {
                        found = clnt;
                        gf_msg_debug (this->name, 0, "Client:%s already found "
                                      "in the cleanup list", client_uid);
                        break;
                }
        }
        return found;
}


static lease_client_t *
__get_or_new_lease_client (xlator_t *this, leases_private_t *priv,
                           const char *client_uid)
{
        lease_client_t  *found = NULL;

        found = __get_lease_client (this, priv, client_uid);
        if (!found) {
                found = new_lease_client (client_uid);
                if (!found)
                        goto out;
                list_add_tail (&found->client_list, &priv->client_list);
                gf_msg_debug (this->name, 0, "Adding a new client:%s entry "
                              "to the cleanup list", client_uid);
        }
out:
        return found;
}


static int
add_inode_to_client_list (xlator_t *this, inode_t *inode, const char *client_uid)
{
        int               ret         = 0;
        leases_private_t *priv        = NULL;
        lease_client_t   *clnt        = NULL;
        lease_inode_t    *lease_inode = NULL;

        priv = this->private;
        pthread_mutex_lock (&priv->mutex);
        {
                clnt = __get_or_new_lease_client (this, priv, client_uid);
                GF_CHECK_ALLOC (clnt, ret, out);

                lease_inode = new_lease_inode (inode);
                GF_CHECK_ALLOC (lease_inode, ret, out);

                list_add_tail (&clnt->inode_list, &lease_inode->list);
                gf_msg_debug (this->name, 0,
                              "Added a new inode:%p to the client(%s) "
                              "cleanup list, gfid(%s)", inode, client_uid,
                              uuid_utoa (inode->gfid));
        }
        pthread_mutex_unlock (&priv->mutex);
out:
        return ret;
}


/* Add lease entry to the corresponding client entry.
 * Return values:
 * 0 Success
 * -1 Failure
 */
static int
__add_lease (call_frame_t *frame, inode_t *inode, lease_inode_ctx_t *lease_ctx,
             const char *client_uid, struct gf_lease *lease)
{
        lease_id_entry_t  *lease_entry  = NULL;
        int                ret          = -1;

        GF_VALIDATE_OR_GOTO ("leases", frame, out);
        GF_VALIDATE_OR_GOTO ("leases", client_uid, out);
        GF_VALIDATE_OR_GOTO ("leases", lease_ctx, out);
        GF_VALIDATE_OR_GOTO ("leases", inode, out);
        GF_VALIDATE_OR_GOTO ("leases", lease, out);

        gf_msg_trace (frame->this->name, 0,
                      "Granting lease lock to client %s with lease id %s"
                      " on gfid(%s)", client_uid, leaseid_utoa (lease->lease_id),
                      uuid_utoa (inode->gfid));

        lease_entry = __get_or_new_lease_entry (frame, lease->lease_id, lease_ctx);
        if (!lease_entry) {
                errno = ENOMEM;
                goto out;
        }

        lease_entry->lease_type_cnt[lease->lease_type]++;
        lease_entry->lease_cnt++;
        lease_entry->lease_type |= lease->lease_type;
        /* If this is the first lease taken by the client on the file, then
         * add this inode/file to the client disconnect cleanup list
         */
        if (lease_entry->lease_cnt == 1) {
                add_inode_to_client_list (frame->this, inode, client_uid);
        }

        lease_ctx->lease_cnt++;
        lease_ctx->lease_type_cnt[lease->lease_type]++;
        lease_ctx->lease_type |= lease->lease_type;

        /* Take a ref for the first lock taken on this inode. Corresponding
         * unref when all the leases are unlocked or during DISCONNECT
         * Ref is required because the inode on which lease is acquired should
         * not be deleted when lru cleanup kicks in*/
        if (lease_ctx->lease_cnt == 1) {
                lease_ctx->inode = inode_ref (inode);
        }

        ret = 0;
out:
        return ret;
}


static gf_boolean_t
__is_clnt_lease_none (const char *client_uid, lease_inode_ctx_t *lease_ctx)
{
        gf_boolean_t      lease_none  = _gf_true;
        lease_id_entry_t *lease_entry = NULL;
        lease_id_entry_t *tmp         = NULL;

        list_for_each_entry_safe (lease_entry, tmp,
                                  &lease_ctx->lease_id_list,
                                  lease_id_list) {
                if ((strcmp (client_uid, lease_entry->client_uid) == 0)
                    && (lease_entry->lease_cnt != 0)) {
                        lease_none = _gf_false;
                        break;
                }
        }

        return lease_none;
}

static int
__remove_inode_from_clnt_list (xlator_t *this, lease_client_t *clnt,
                               inode_t *inode)
{
        int               ret     = -1;
        lease_inode_t    *l_inode = NULL;
        lease_inode_t    *tmp1    = NULL;

        list_for_each_entry_safe (l_inode, tmp1,
                                  &clnt->inode_list,
                                  list) {
                if (l_inode->inode == inode) {
                        __destroy_lease_inode (l_inode);
                        gf_msg_debug (this->name, 0,
                                      "Removed the inode from the client cleanup list");
                        ret = 0;
                }
        }
        /* TODO: Remove the client entry from the cleanup list */

        return ret;
}


static int
remove_from_clnt_list (xlator_t *this, const char *client_uid, inode_t *inode)
{
        leases_private_t *priv    = NULL;
        int               ret     = -1;
        lease_client_t   *clnt    = NULL;

        priv = this->private;
        if (!priv)
                goto out;

        pthread_mutex_lock (&priv->mutex);
        {
                clnt = __get_lease_client (this, priv, client_uid);
                if (!clnt) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, LEASE_MSG_CLNT_NOTFOUND,
                                "There is no client entry found in the cleanup list");
                        pthread_mutex_unlock (&priv->mutex);
                        goto out;
                }
                ret = __remove_inode_from_clnt_list (this, clnt, inode);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, LEASE_MSG_INODE_NOTFOUND,
                                "There is no inode entry found in the cleanup list");
                }
        }
        pthread_mutex_unlock (&priv->mutex);
out:
        return ret;
}


/* Remove lease entry in the corresponding client entry.
 */
static int
__remove_lease (xlator_t *this, inode_t *inode, lease_inode_ctx_t *lease_ctx,
                const char *client_uid, struct gf_lease *lease)
{
        lease_id_entry_t    *lease_entry     = NULL;
        int                  ret             = 0;
        int32_t              lease_type      = 0;
        leases_private_t    *priv            = NULL;

        GF_VALIDATE_OR_GOTO ("leases", lease_ctx, out);
        GF_VALIDATE_OR_GOTO ("leases", lease, out);

        priv = this->private;

        gf_msg_trace (this->name, 0, "Removing lease entry for client: %s, "
                      "lease type:%d, lease id:%s", client_uid, lease->lease_type,
                      leaseid_utoa (lease->lease_id));

        lease_entry = __get_lease_id_entry (lease_ctx, lease->lease_id);
        if (!lease_entry) {
                gf_msg (this->name, GF_LOG_INFO, 0, LEASE_MSG_INVAL_UNLK_LEASE,
                        "Got unlock lease request from client:%s, but has no "
                        "corresponding lock", client_uid);
                ret = -EINVAL;
                errno = EINVAL;
                goto out;
        }

        lease_type = lease->lease_type;
        lease_entry->lease_type_cnt[lease_type]--;
        lease_entry->lease_cnt--;

        lease_ctx->lease_type_cnt[lease_type]--;
        lease_ctx->lease_cnt--;

        if (lease_entry->lease_type_cnt[lease_type] == 0)
                lease_entry->lease_type = lease_entry->lease_type & (~lease_type);

        if (lease_ctx->lease_type_cnt[lease_type] == 0)
                lease_ctx->lease_type = lease_ctx->lease_type & (~lease_type);

        if (lease_entry->lease_cnt == 0) {
                if (__is_clnt_lease_none (client_uid, lease_ctx)) {
                        gf_msg_debug (this->name, 0, "Client(%s) has no leases"
                                      " on gfid (%s), hence removing the inode"
                                      " from the client cleanup list",
                                      client_uid, uuid_utoa (inode->gfid));
                        remove_from_clnt_list (this, client_uid, lease_ctx->inode);
                }
                __destroy_lease_id_entry (lease_entry);
        }

        if (lease_ctx->lease_cnt == 0 && lease_ctx->timer) {
                ret = gf_tw_del_timer (priv->timer_wheel, lease_ctx->timer);
                lease_ctx->recall_in_progress = _gf_false;
        }
out:
        return ret;
}


static gf_boolean_t
__is_lease_grantable (xlator_t *this, lease_inode_ctx_t *lease_ctx,
                      struct gf_lease *lease, inode_t *inode)
{
        uint32_t        fd_count     = 0;
        int32_t         flags        = 0;
        fd_t           *iter_fd      = NULL;
        gf_boolean_t    grant        = _gf_false;
        int             ret          = 0;
        lease_fd_ctx_t *fd_ctx       = NULL;
        uint64_t        ctx          = 0;

        GF_VALIDATE_OR_GOTO ("leases", lease_ctx, out);
        GF_VALIDATE_OR_GOTO ("leases", lease, out);
        GF_VALIDATE_OR_GOTO ("leases", inode, out);

        if (lease_ctx->recall_in_progress) {
                gf_msg_debug (this->name, 0, "Recall in progress, hence "
                              "failing the lease request");
                grant = _gf_false;
                goto out;
        }

        LOCK (&inode->lock);
        {
                list_for_each_entry (iter_fd, &inode->fd_list, inode_list) {
                        ret = fd_ctx_get (iter_fd, this, &ctx);
                        if (ret < 0) {
                                grant = _gf_false;
                                UNLOCK (&inode->lock);
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        LEASE_MSG_INVAL_FD_CTX,
                                        "Unable to get fd ctx");
                                goto out;
                        }
                        fd_ctx = (lease_fd_ctx_t *)(long) ctx;

                        /* Check for open fd conflict, note that open fds from
                         * the same lease id is not checked for conflict, as it is
                         * lease id based lease.
                         */
                        if (!__is_same_lease_id (fd_ctx->lease_id, lease->lease_id)) {
                                fd_count++;
                                flags |= iter_fd->flags;
                        }
                }
        }
        UNLOCK (&inode->lock);

        gf_msg_debug (this->name, 0, "open fd count:%d flags:%d",
                      fd_count, flags);

        __dump_leases_info (this, lease_ctx);

        switch (lease->lease_type) {
        case GF_RD_LEASE:
                /* check open fd conflict */
                if ((fd_count > 0) && ((flags & O_WRONLY) || (flags & O_RDWR))) {
                        grant = _gf_false;
                        break;
                }

                /* check for conflict with existing leases */
                if (lease_ctx->lease_type == NONE ||
                    lease_ctx->lease_type == GF_RD_LEASE ||
                    !(__another_lease_found (lease_ctx, lease->lease_id)))
                        grant = _gf_true;
                else
                        grant = _gf_false;
                break;

        case GF_RW_LEASE:
                /* check open fd conflict; conflict if there are any fds open
                 * other than the client on which the lease is requested. */
                if (fd_count > 0) {
                        grant = _gf_false;
                        break;
                }

                /* check existing lease conflict */
                if (lease_ctx->lease_type == NONE ||
                    !(__another_lease_found (lease_ctx, lease->lease_id)))
                        grant = _gf_true;
                else
                        grant = _gf_false;
                break;

        default:
                gf_msg (this->name, GF_LOG_ERROR, EINVAL, LEASE_MSG_INVAL_LEASE_TYPE,
                        "Invalid lease type specified");
                break;
        }
out:
        return grant;
}


static void
do_blocked_fops (xlator_t *this, lease_inode_ctx_t *lease_ctx)
{
        struct list_head   wind_list;
        fop_stub_t        *blk_fop  = NULL;
        fop_stub_t        *tmp      = NULL;

        INIT_LIST_HEAD (&wind_list);

        pthread_mutex_lock (&lease_ctx->lock);
        {
                list_for_each_entry_safe (blk_fop, tmp,
                                          &lease_ctx->blocked_list, list) {
                        list_del_init (&blk_fop->list);
                        list_add_tail (&blk_fop->list, &wind_list);
                }
        }
        pthread_mutex_unlock (&lease_ctx->lock);

        gf_msg_trace (this->name, 0, "Executing the blocked stubs on gfid(%s)",
                      uuid_utoa (lease_ctx->inode->gfid));

        list_for_each_entry_safe (blk_fop, tmp, &wind_list, list) {
                list_del_init (&blk_fop->list);
                gf_msg_trace (this->name, 0, "Executing fop:%d", blk_fop->stub->fop);
                call_resume (blk_fop->stub);
                GF_FREE (blk_fop);
        }

        pthread_mutex_lock (&lease_ctx->lock);
        {
                lease_ctx->lease_type = NONE;
                inode_unref (lease_ctx->inode);
                lease_ctx->inode = NULL;
        }
        pthread_mutex_unlock (&lease_ctx->lock);

        return;
}


void
recall_lease_timer_handler (struct gf_tw_timer_list *timer,
                            void *data, unsigned long calltime)
{
        inode_t                *inode       = NULL;
        lease_inode_t          *lease_inode = NULL;
        leases_private_t       *priv        = NULL;
        lease_timer_data_t     *timer_data    = NULL;

        timer_data = data;

        priv = timer_data->this->private;
        inode = timer_data->inode;
        pthread_mutex_lock (&priv->mutex);
        {
                lease_inode = new_lease_inode (inode);
                if (!lease_inode) {
                        errno = ENOMEM;
                        goto out;
                }
                list_add_tail (&lease_inode->list, &priv->recall_list);
                pthread_cond_broadcast (&priv->cond);
        }
out:
        pthread_mutex_unlock (&priv->mutex);

        GF_FREE (timer);
}


static void
__recall_lease (xlator_t *this, lease_inode_ctx_t *lease_ctx)
{
        lease_id_entry_t                  *lease_entry   = NULL;
        lease_id_entry_t                  *tmp           = NULL;
        struct gf_upcall                   up_req        = {0,};
        struct gf_upcall_recall_lease      recall_req    = {0,};
        int                                notify_ret    = -1;
        struct gf_tw_timer_list           *timer         = NULL;
        leases_private_t                  *priv          = NULL;
        lease_timer_data_t                *timer_data    = NULL;

        if (lease_ctx->recall_in_progress) {
                gf_msg_debug (this->name, 0, "Lease recall is already in "
                              "progress, hence not sending another recall");
                goto out;
        }

        priv = this->private;
        list_for_each_entry_safe (lease_entry, tmp,
                                  &lease_ctx->lease_id_list,
                                  lease_id_list) {
                gf_uuid_copy (up_req.gfid, lease_ctx->inode->gfid);
                up_req.client_uid = lease_entry->client_uid;
                up_req.event_type = GF_UPCALL_RECALL_LEASE;
                up_req.data = &recall_req;

                notify_ret = this->notify (this, GF_EVENT_UPCALL, &up_req);
                if (notify_ret < 0) {
                        gf_msg (this->name, GF_LOG_ERROR, 0, LEASE_MSG_RECALL_FAIL,
                                "Recall notification to client: %s failed",
                                lease_entry->client_uid);
                        /* Do not return from here, continue registering the timer,
                           this is required mostly o keep replicas in sync*/
                } else {
                        gf_msg_debug (this->name, 0, "Recall lease (all)"
                                      "notification sent to client %s",
                                      lease_entry->client_uid);
                }

                lease_ctx->recall_in_progress = _gf_true;
                lease_entry->recall_time = time (NULL);
        }
        timer = GF_CALLOC (1, sizeof (*timer),
                           gf_common_mt_tw_timer_list);
        if (!timer) {
                goto out;
        }
        timer_data = GF_CALLOC (1, sizeof (*timer_data),
                                gf_leases_mt_timer_data_t);
        if (!timer_data) {
                GF_FREE (timer);
                goto out;
        }

        timer_data->inode = inode_ref (lease_ctx->inode);
        timer_data->this = this;
        timer->data = timer_data;

        INIT_LIST_HEAD (&timer->entry);
        timer->expires = get_recall_lease_timeout (this);
        timer->function = recall_lease_timer_handler;
        lease_ctx->timer = timer;
        gf_tw_add_timer (priv->timer_wheel, timer);
        gf_msg_trace (this->name, 0, "Registering timer " "%p, after "
                      "sending recall", timer);
out:
        return;
}


/* ret = 0; STACK_UNWIND Success
 * ret = -1; STACK_UNWIND failure
 */
int
process_lease_req (call_frame_t *frame, xlator_t *this,
                   inode_t *inode, struct gf_lease *lease)
{
        int                 ret             = 0;
        char               *client_uid      = NULL;
        lease_inode_ctx_t  *lease_ctx       = NULL;

        GF_VALIDATE_OR_GOTO ("leases", frame, out);
        GF_VALIDATE_OR_GOTO ("leases", this, out);
        GF_VALIDATE_OR_GOTO ("leases", inode, out);
        GF_VALIDATE_OR_GOTO ("leases", lease, out);

        client_uid = frame->root->client->client_uid;

        if (!is_valid_lease_id (lease->lease_id)) {
                gf_msg (this->name, GF_LOG_ERROR, EINVAL,
                        LEASE_MSG_INVAL_LEASE_ID, "Invalid lease id, from"
                        "client:%s", client_uid);
                ret = -EINVAL;
                errno = EINVAL;
                goto out;
        }

        lease_ctx = lease_ctx_get (inode, this);
        if (!lease_ctx) {
                gf_msg (this->name, GF_LOG_WARNING, ENOMEM,
                        LEASE_MSG_NO_MEM, "Unable to create/get inode ctx, "
                        "inode:%p", inode);
                ret = -ENOMEM;
                errno = ENOMEM;
                goto out;
        }

        gf_msg_debug (this->name, 0, "Lease request from client: %s, "
                      "lease type:%d, lease cmd:%d, lease ID:%s, gfid:%s",
                      client_uid, lease->lease_type, lease->cmd,
                      leaseid_utoa (lease->lease_id), uuid_utoa (inode->gfid));

        pthread_mutex_lock (&lease_ctx->lock);
        {
                switch (lease->cmd) {
                case GF_GET_LEASE:
                        lease->lease_type = lease_ctx->lease_type;
                        gf_msg_debug (this->name, 0, "Get lease, existing lease"
                                      "type: %d", lease_ctx->lease_type);
                        /*TODO:Should it consider lease id or client_uid?*/
                        break;

                case GF_SET_LEASE:
                        if (__is_lease_grantable (this, lease_ctx, lease, inode)) {
                                __add_lease (frame, inode, lease_ctx,
                                             client_uid, lease);
                                ret = 0;
                        } else {
                                gf_msg_debug (this->name, GF_LOG_DEBUG,
                                              "Not granting the conflicting lease"
                                              " request from %s on gfid(%s)",
                                              client_uid, uuid_utoa (inode->gfid));
                                __recall_lease (this, lease_ctx);
                                ret = -1;
                        }
                        break;
                case GF_UNLK_LEASE:
                        ret = __remove_lease (this, inode, lease_ctx,
                                              client_uid, lease);
                        if ((ret == 0) && (lease_ctx->lease_cnt == 0)) {
                                pthread_mutex_unlock (&lease_ctx->lock);
                                goto unblock;
                        }
                        break;
                default:
                        ret = -EINVAL;
                        break;
                }
        }
        pthread_mutex_unlock (&lease_ctx->lock);

        return ret;

unblock:
        do_blocked_fops (this, lease_ctx);
out:
        return ret;
}


/* ret = 1 conflict
 * ret = 0 no conflict
 */
gf_boolean_t
__check_lease_conflict (call_frame_t *frame, lease_inode_ctx_t *lease_ctx,
                        const char *lease_id, gf_boolean_t is_write)
{
        gf_lease_types_t   lease_type      = {0,};
        gf_boolean_t       conflicts       = _gf_false;
        lease_id_entry_t  *lease_entry     = NULL;

        GF_VALIDATE_OR_GOTO ("leases", frame, out);
        GF_VALIDATE_OR_GOTO ("leases", lease_ctx, out);
        GF_VALIDATE_OR_GOTO ("leases", lease_id, out);

        lease_type = lease_ctx->lease_type;

        /* If the fop is rename or unlink conflict the lease even if its
         * from the same client??
         */
        if ((frame->root->op == GF_FOP_RENAME) ||
            (frame->root->op == GF_FOP_UNLINK)) {
                conflicts = _gf_true;
                goto recall;
        }

        /* TODO: If lease_id is not sent, fall back to client uid conflict check?
         * Or set conflicts = true if lease_id is 0 when there is an existing
         * lease */
        switch (lease_type) {
        case (GF_RW_LEASE | GF_RD_LEASE):
        case GF_RW_LEASE:
                lease_entry = __get_lease_id_entry (lease_ctx, lease_id);
                if (lease_entry && (lease_entry->lease_type & GF_RW_LEASE))
                        conflicts = _gf_false;
                else
                        conflicts = _gf_true;
                break;
        case GF_RD_LEASE:
                if (is_write && __another_lease_found(lease_ctx, lease_id))
                        conflicts = _gf_true;
                else
                        conflicts = _gf_false;
                break;
        default:
                break;
        }

recall:
        /* If there is a conflict found and recall is not already sent to all
         * the clients, then send recall to each of the client holding lease.
         */
        if (conflicts)
                __recall_lease (frame->this, lease_ctx);
out:
        return conflicts;
}


/* Return values:
 * -1 : error, unwind the fop
 * WIND_FOP: No conflict, wind the fop
 * BLOCK_FOP: Found a conflicting lease, block the fop
 */
int
check_lease_conflict (call_frame_t *frame, inode_t *inode,
                      const char *lease_id, uint32_t fop_flags)
{
        lease_inode_ctx_t       *lease_ctx  = NULL;
        gf_boolean_t       is_blocking_fop  = _gf_false;
        gf_boolean_t       is_write_fop     = _gf_false;
        gf_boolean_t       conflicts        = _gf_false;
        int                ret              = -1;

        lease_ctx = lease_ctx_get (inode, frame->this);
        if (!lease_ctx) {
                gf_msg (frame->this->name, GF_LOG_WARNING, ENOMEM,
                        LEASE_MSG_NO_MEM,
                        "Unable to create/get inode ctx");
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        is_blocking_fop = ((fop_flags & BLOCKING_FOP) != 0);
        is_write_fop = ((fop_flags & DATA_MODIFY_FOP) != 0);

        pthread_mutex_lock (&lease_ctx->lock);
        {
                if (lease_ctx->lease_type == NONE) {
                        gf_msg_debug (frame->this->name, 0,
                                      "No leases found continuing with the"
                                      " fop:%s", gf_fop_list[frame->root->op]);
                        ret = WIND_FOP;
                        goto unlock;
                }
                conflicts = __check_lease_conflict (frame, lease_ctx,
                                lease_id, is_write_fop);
                if (conflicts) {
                        if (is_blocking_fop) {
                                gf_msg_debug (frame->this->name, 0, "Fop: %s "
                                              "conflicting existing "
                                              "lease: %d, blocking the"
                                              "fop", gf_fop_list[frame->root->op],
                                              lease_ctx->lease_type);
                                ret = BLOCK_FOP;
                        } else {
                                gf_msg_debug (frame->this->name, 0, "Fop: %s "
                                              "conflicting existing "
                                              "lease: %d, sending "
                                              "EAGAIN",
                                              gf_fop_list[frame->root->op],
                                              lease_ctx->lease_type);
                                errno = EAGAIN;
                                ret = -1;
                        }
                }
        }
unlock:
        pthread_mutex_unlock (&lease_ctx->lock);
out:
        return ret;
}


static int
remove_clnt_leases (const char *client_uid, inode_t *inode, xlator_t *this)
{
        lease_inode_ctx_t  *lease_ctx      = NULL;
        lease_id_entry_t   *lease_entry    = NULL;
        lease_id_entry_t   *tmp            = NULL;
        int                 ret            = 0;
        int                 i              = 0;

        lease_ctx = lease_ctx_get (inode, this);
        if (!lease_ctx) {
                gf_msg (this->name, GF_LOG_WARNING, ENOMEM,
                        LEASE_MSG_INVAL_INODE_CTX,
                        "Unable to create/get inode ctx");
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        pthread_mutex_lock (&lease_ctx->lock);
        {
                list_for_each_entry_safe (lease_entry, tmp,
                                          &lease_ctx->lease_id_list,
                                          lease_id_list) {
                        if (strcmp (client_uid, lease_entry->client_uid) == 0) {
                                for (i = 0; i < GF_LEASE_MAX_TYPE; i++) {
                                        lease_ctx->lease_type_cnt[i] -= lease_entry->lease_type_cnt[i];
                                }
                                lease_ctx->lease_cnt -= lease_entry->lease_cnt;
                                __destroy_lease_id_entry (lease_entry);
                                if (lease_ctx->lease_cnt == 0) {
                                        pthread_mutex_unlock (&lease_ctx->lock);
                                        goto unblock;
                                }
                        }
                }
        }
        pthread_mutex_unlock (&lease_ctx->lock);
out:
        return ret;

unblock:
        do_blocked_fops (this, lease_ctx);
        return ret;
}


int
cleanup_client_leases (xlator_t *this, const char *client_uid)
{
        lease_client_t    *clnt         = NULL;
        lease_client_t    *tmp          = NULL;
        struct list_head   cleanup_list = {0, };
        lease_inode_t     *l_inode      = NULL;
        lease_inode_t     *tmp1         = NULL;
        leases_private_t  *priv         = NULL;
        int                ret          = 0;

        priv = this->private;
        if (!priv) {
                ret = -1;
                errno = EINVAL;
                goto out;
        }

        INIT_LIST_HEAD (&cleanup_list);
        pthread_mutex_lock (&priv->mutex);
        {
                list_for_each_entry_safe (clnt, tmp, &priv->client_list, client_list) {
                        if ((strcmp (clnt->client_uid, client_uid) == 0)) {
                                list_for_each_entry_safe (l_inode, tmp1,
                                                          &clnt->inode_list, list) {
                                        list_del_init (&l_inode->list);
                                        list_add_tail (&l_inode->list, &cleanup_list);
                                }
                                break;
                        }
                        __destroy_lease_client (clnt);
                }
        }
        pthread_mutex_unlock (&priv->mutex);

        l_inode = tmp1 = NULL;
        list_for_each_entry_safe (l_inode, tmp1, &cleanup_list, list) {
                remove_clnt_leases (client_uid, l_inode->inode, this);
        }
out:
        return ret;
}


static void
__remove_all_leases (xlator_t *this, lease_inode_ctx_t *lease_ctx)
{
        int                 i              = 0;
        lease_id_entry_t   *lease_entry    = NULL;
        lease_id_entry_t   *tmp            = NULL;

        __dump_leases_info (this, lease_ctx);

        list_for_each_entry_safe (lease_entry, tmp,
                                  &lease_ctx->lease_id_list,
                                  lease_id_list) {
                lease_entry->lease_cnt = 0;
                remove_from_clnt_list (this, lease_entry->client_uid, lease_ctx->inode);
                __destroy_lease_id_entry (lease_entry);
        }
        INIT_LIST_HEAD (&lease_ctx->lease_id_list);
        for (i = 0; i <= GF_LEASE_MAX_TYPE; i++)
                lease_ctx->lease_type_cnt[i] = 0;
        lease_ctx->lease_type = 0;
        lease_ctx->lease_cnt = 0;
        lease_ctx->recall_in_progress = _gf_false;
        inode_unref (lease_ctx->inode);
        lease_ctx->timer = NULL;

        /* TODO:
         * - Mark the corresponding fd bad. Could be done on client side
         * as a result of recall
         * - Free the lease_ctx
         */
        return;
}


static int
remove_all_leases (xlator_t *this, inode_t *inode)
{
        lease_inode_ctx_t  *lease_ctx       = NULL;
        int                 ret             = 0;

        GF_VALIDATE_OR_GOTO ("leases", inode, out);

        lease_ctx = lease_ctx_get (inode, this);
        if (!lease_ctx) {
                gf_msg (this->name, GF_LOG_WARNING, ENOMEM,
                        LEASE_MSG_INVAL_INODE_CTX,
                        "Unable to create/get inode ctx");
                ret = -1;
                errno = ENOMEM;
                goto out;
        }

        pthread_mutex_lock (&lease_ctx->lock);
        {
                __remove_all_leases (this, lease_ctx);
        }
        pthread_mutex_unlock (&lease_ctx->lock);

        do_blocked_fops (this, lease_ctx);
out:
        return ret;
}


void *
expired_recall_cleanup (void *data)
{
        struct timespec      sleep_till      = {0, };
        struct list_head     recall_cleanup_list;
        lease_inode_t       *recall_entry    = NULL;
        lease_inode_t       *tmp             = NULL;
        leases_private_t    *priv            = NULL;
        xlator_t            *this            = NULL;

        GF_VALIDATE_OR_GOTO ("leases", data, out);

        this = data;
        priv = this->private;

        gf_msg_debug (this->name, 0, "Started the expired_recall_cleanup thread");

        while (1) {
                pthread_mutex_lock (&priv->mutex);
                {
                        if (priv->fini) {
                                pthread_mutex_unlock (&priv->mutex);
                                goto out;
                        }
                        INIT_LIST_HEAD (&recall_cleanup_list);
                        if (list_empty (&priv->recall_list)) {
                                sleep_till.tv_sec = time (NULL) + 600;
                                pthread_cond_timedwait (&priv->cond, &priv->mutex,
                                                        &sleep_till);
                        }
                        if (!list_empty (&priv->recall_list)) {
                                gf_msg_debug (this->name, 0, "Found expired recalls");
                                list_for_each_entry_safe (recall_entry, tmp,
                                                          &priv->recall_list, list) {
                                        list_del_init (&recall_entry->list);
                                        list_add_tail (&recall_entry->list, &recall_cleanup_list);
                                }
                        }
                }
                pthread_mutex_unlock (&priv->mutex);

                recall_entry = tmp = NULL;
                list_for_each_entry_safe (recall_entry, tmp, &recall_cleanup_list, list) {
                        gf_msg_debug (this->name, 0, "Recall lease was sent on"
                                      " inode:%p, recall timer has expired"
                                      " and clients haven't unlocked the lease"
                                      " hence cleaning up leases on the inode",
                                      recall_entry->inode);
                        remove_all_leases (this, recall_entry->inode);
                        list_del_init (&recall_entry->list);
                }
        }

out:
        return NULL;
}
