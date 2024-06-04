/*
 *   Copyright (c) 2017 Red Hat, Inc. <http://www.redhat.com>
 *   This file is part of GlusterFS.
 *
 *   This file is licensed to you under your choice of the GNU Lesser
 *   General Public License, version 3 or any later version (LGPLv3 or
 *   later), or the GNU General Public License, version 2 (GPLv2), in all
 *   cases as published by the Free Software Foundation.
 */

#include "nl-cache.h"
#include "timer-wheel.h"
#include <glusterfs/statedump.h>

/* Caching guidelines:
 * This xlator serves negative lookup(ENOENT lookups) from the cache,
 * there by making create faster.
 *   What is cached?
 *      Negative lookup cache is stored for each directory, and has 2 entries:
 *      - Negative entries: Populated only when lookup/stat returns ENOENT.
 *        Fuse mostly sends only one lookup before create, hence negative entry
 *        cache is almost useless. But for SMB access, multiple lookups/stats
 *        are sent before creating the file. Hence the negative entry cache.
 *        It can exist even when the positive entry cache is invalid. It also
 *        has the entries that were deleted from this directory.
 *        Freed on receiving upcall(with dentry change flag) or on expiring
 *        timeout of the cache.
 *
 *      - Positive entries: Populated as a part of readdirp, and as a part of
 *        mkdir followed by creates inside that directory. Lookups and other
 *        fops do not populate the positive entry (as it can grow long and is
 *        of no value add)
 *        Freed on receiving upcall(with dentry change flag) or on expiring
 *        timeout of the cache.
 *
 *   Data structures to store cache?
 *      The cache of any directory is stored in the inode_ctx of the directory.
 *      Negative entries are stored as list of strings.
 *             Search - O(n)
 *             Add    - O(1)
 *             Delete - O(n) - as it has to be searched before deleting
 *      Positive entries are stored as a list, each list node has a pointer
 *          to the inode of the positive entry or the name of the entry.
 *          Since the client side inode table already will have inodes for
 *          positive entries, we just take a ref of that inode and store as
 *          positive entry cache. In cases like hardlinks and readdirp where
 *          inode is NULL, we store the names.
 *          Name Search - O(n)
 *          Inode Search - O(1) - Actually complexity of inode_find()
 *          Name/inode Add - O(1)
 *          Name Delete - O(n)
 *          Inode Delete - O(1)
 *
 * Locking order:
 *
 * TODO:
 * - Fill Positive entries on readdir/p, after which in lookup_cbk check if the
 *   name is in PE and replace it with inode.
 * - fini, PARENET_DOWN, disable caching
 * - Virtual setxattr to dump the inode_ctx, to ease debugging
 * - Handle dht_nuke xattr: clear all cache
 * - Special handling for .meta and .trashcan?
 */

static int
__nlc_inode_ctx_timer_start(xlator_t *this, inode_t *inode, nlc_ctx_t *nlc_ctx);
static int
__nlc_add_to_lru(xlator_t *this, inode_t *inode, nlc_ctx_t *nlc_ctx);
static void
nlc_remove_from_lru(nlc_conf_t *conf, inode_t *inode);
static void
__nlc_inode_ctx_timer_delete(nlc_conf_t *conf, nlc_ctx_t *nlc_ctx);
static gf_boolean_t
__nlc_search_ne(nlc_ctx_t *nlc_ctx, const char *name, const size_t name_len0);
static void
__nlc_free_pe(xlator_t *this, nlc_ctx_t *nlc_ctx, nlc_pe_t *pe);
static void
__nlc_free_ne(nlc_conf_t *conf, nlc_ctx_t *nlc_ctx, nlc_ne_t *ne);

static gf_boolean_t
__nlc_is_cache_valid(xlator_t *this, nlc_ctx_t *nlc_ctx)
{
    nlc_conf_t *conf = NULL;
    time_t last_val_time;

    if (nlc_ctx->cache_time == 0)
        goto out;

    conf = this->private;

    LOCK(&conf->lock);
    {
        last_val_time = conf->last_child_down;
    }
    UNLOCK(&conf->lock);

    if (last_val_time <= nlc_ctx->cache_time)
        return _gf_true;
out:
    return _gf_false;
}

void
nlc_update_child_down_time(xlator_t *this, time_t now)
{
    nlc_conf_t *conf = NULL;

    conf = this->private;

    LOCK(&conf->lock);
    {
        conf->last_child_down = now;
    }
    UNLOCK(&conf->lock);

    return;
}

static nlc_ctx_t *
__nlc_inode_ctx_get(xlator_t *this, inode_t *inode)
{
    int ret = 0;
    uint64_t nlc_ctx_int = 0;

    ret = __inode_ctx_get2(inode, this, &nlc_ctx_int, NULL);
    if (ret == 0) {
        return (void *)(long)(nlc_ctx_int);
    }
    return NULL;
}

static int
nlc_inode_ctx_set(xlator_t *this, inode_t *inode, nlc_ctx_t *nlc_ctx,
                  nlc_pe_t *nlc_pe_p)
{
    uint64_t ctx1, ctx2;
    int ret = -1;

    ctx1 = (uint64_t)(uintptr_t)nlc_ctx;
    ctx2 = (uint64_t)(uintptr_t)nlc_pe_p;

    /* The caller may choose to set one of the ctxs, hence check
     * if the ctx1/2 is non zero and then send the address. If we
     * blindly send the address of both the ctxs, it may reset the
     * ctx the caller had sent NULL(intended as leave untouched) for.*/
    LOCK(&inode->lock);
    {
        ret = __inode_ctx_set2(inode, this, ctx1 ? &ctx1 : 0, ctx2 ? &ctx2 : 0);
    }
    UNLOCK(&inode->lock);
    return ret;
}

static nlc_ctx_t *
nlc_inode_ctx_get(xlator_t *this, inode_t *inode)
{
    nlc_ctx_t *nlc_ctx = NULL;

    LOCK(&inode->lock);
    {
        nlc_ctx = __nlc_inode_ctx_get(this, inode);
    }
    UNLOCK(&inode->lock);

    if (nlc_ctx == NULL)
        gf_msg_debug(this->name, 0,
                     "inode ctx get failed for "
                     "inode:%p",
                     inode);

    return nlc_ctx;
}

static void
__nlc_inode_clear_entries(xlator_t *this, nlc_ctx_t *nlc_ctx)
{
    nlc_pe_t *pe = NULL;
    nlc_pe_t *tmp = NULL;
    nlc_ne_t *ne = NULL;
    nlc_ne_t *tmp1 = NULL;
    nlc_conf_t *conf = NULL;

    if (!nlc_ctx)
        goto out;

    if (IS_PE_VALID(nlc_ctx->state))
        list_for_each_entry_safe(pe, tmp, &nlc_ctx->pe, list)
        {
            __nlc_free_pe(this, nlc_ctx, pe);
        }

    if (IS_NE_VALID(nlc_ctx->state)) {
        conf = this->private;
        list_for_each_entry_safe(ne, tmp1, &nlc_ctx->ne, list)
        {
            __nlc_free_ne(conf, nlc_ctx, ne);
        }
    }

    nlc_ctx->cache_time = 0;
    nlc_ctx->state = 0;
    GF_ASSERT(nlc_ctx->cache_size == sizeof(*nlc_ctx));
    GF_ASSERT(nlc_ctx->refd_inodes == 0);
out:
    return;
}

static void
nlc_init_invalid_ctx(xlator_t *this, inode_t *inode, nlc_ctx_t *nlc_ctx)
{
    nlc_conf_t *conf = NULL;
    int ret = -1;

    conf = this->private;
    if (!nlc_ctx)
        goto out;

    LOCK(&nlc_ctx->lock);
    {
        if (__nlc_is_cache_valid(this, nlc_ctx))
            goto unlock;

        /* The cache/nlc_ctx can be invalid for 2 reasons:
         * - Because of a child-down/timer expiry, cache is
         *   invalid but the nlc_ctx is not yet cleaned up.
         * - nlc_ctx is cleaned up, because of invalidations
         *   or lru prune etc.*/

        /* If the cache is present but invalid, clear the cache and
         * reset the timer. */
        __nlc_inode_clear_entries(this, nlc_ctx);

        /* If timer is present, then it is already part of lru as well
         * Hence reset the timer and return.*/
        if (nlc_ctx->timer) {
            gf_tw_mod_timer_pending(conf->timer_wheel, nlc_ctx->timer,
                                    conf->cache_timeout);
            nlc_ctx->cache_time = gf_time();
            goto unlock;
        }

        /* If timer was NULL, the nlc_ctx is already cleanedup,
         * and we need to start timer and add to lru, so that it is
         * ready to cache entries a fresh */
        ret = __nlc_inode_ctx_timer_start(this, inode, nlc_ctx);
        if (ret < 0)
            goto unlock;

        ret = __nlc_add_to_lru(this, inode, nlc_ctx);
        if (ret < 0) {
            __nlc_inode_ctx_timer_delete(conf, nlc_ctx);
            goto unlock;
        }
    }
unlock:
    UNLOCK(&nlc_ctx->lock);
out:
    return;
}

static nlc_ctx_t *
nlc_inode_ctx_get_set(xlator_t *this, inode_t *inode)
{
    uint64_t ctx;
    int ret = 0;
    nlc_ctx_t *nlc_ctx = NULL;
    nlc_conf_t *conf = NULL;

    conf = this->private;

    LOCK(&inode->lock);
    {
        nlc_ctx = __nlc_inode_ctx_get(this, inode);
        if (nlc_ctx)
            goto unlock;

        nlc_ctx = GF_CALLOC(sizeof(*nlc_ctx), 1, gf_nlc_mt_nlc_ctx_t);
        if (!nlc_ctx)
            goto unlock;

        INIT_LIST_HEAD(&nlc_ctx->pe);
        INIT_LIST_HEAD(&nlc_ctx->ne);
        LOCK_INIT(&nlc_ctx->lock);

        ret = __nlc_inode_ctx_timer_start(this, inode, nlc_ctx);
        if (ret < 0)
            goto unlock;

        ret = __nlc_add_to_lru(this, inode, nlc_ctx);
        if (ret < 0) {
            __nlc_inode_ctx_timer_delete(conf, nlc_ctx);
            goto unlock;
        }

        ctx = (uint64_t)(uintptr_t)nlc_ctx;
        ret = __inode_ctx_set2(inode, this, &ctx, NULL);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, ENOMEM, NLC_MSG_NO_MEMORY,
                   "inode ctx set failed");
            __nlc_inode_ctx_timer_delete(conf, nlc_ctx);
            nlc_remove_from_lru(conf, inode);
            goto unlock;
        }

        /*TODO: also sizeof (gf_tw_timer_list) + nlc_timer_data_t ?*/
        nlc_ctx->cache_size = sizeof(*nlc_ctx);
        GF_ATOMIC_ADD(conf->current_cache_size, nlc_ctx->cache_size);
    }
unlock:
    UNLOCK(&inode->lock);

    if (ret == 0) {
        nlc_init_invalid_ctx(this, inode, nlc_ctx);
    }

    if (ret < 0 && nlc_ctx) {
        LOCK_DESTROY(&nlc_ctx->lock);
        GF_FREE(nlc_ctx);
        nlc_ctx = NULL;
        goto out;
    }

out:
    return nlc_ctx;
}

nlc_local_t *
nlc_local_init(call_frame_t *frame, glusterfs_fop_t fop, loc_t *loc,
               loc_t *loc2)
{
    nlc_local_t *local = NULL;

    local = GF_CALLOC(sizeof(*local), 1, gf_nlc_mt_nlc_local_t);
    if (!local)
        goto out;

    if (loc)
        loc_copy(&local->loc, loc);
    if (loc2)
        loc_copy(&local->loc2, loc2);

    local->fop = fop;
    frame->local = local;
out:
    return local;
}

void
nlc_local_wipe(nlc_local_t *local)
{
    if (local) {
        loc_wipe(&local->loc);
        loc_wipe(&local->loc2);
        GF_FREE(local);
    }
}

static void
__nlc_set_dir_state(nlc_ctx_t *nlc_ctx, uint64_t new_state)
{
    nlc_ctx->state |= new_state;

    return;
}

void
nlc_set_dir_state(xlator_t *this, inode_t *inode, uint64_t state)
{
    nlc_ctx_t *nlc_ctx = NULL;

    if (inode->ia_type != IA_IFDIR) {
        gf_msg_callingfn(this->name, GF_LOG_ERROR, EINVAL, NLC_MSG_EINVAL,
                         "inode is not of type dir");
        goto out;
    }

    nlc_ctx = nlc_inode_ctx_get_set(this, inode);
    if (!nlc_ctx)
        goto out;

    LOCK(&nlc_ctx->lock);
    {
        __nlc_set_dir_state(nlc_ctx, state);
    }
    UNLOCK(&nlc_ctx->lock);
out:
    return;
}

static void
nlc_cache_timeout_handler(struct gf_tw_timer_list *timer, void *data,
                          unsigned long calltime)
{
    nlc_timer_data_t *tmp = data;
    nlc_ctx_t *nlc_ctx = NULL;

    nlc_ctx = nlc_inode_ctx_get(tmp->this, tmp->inode);
    if (nlc_ctx) {
        /* Taking nlc_ctx->lock will lead to deadlock, hence updating
         * the cache is invalid outside of lock, instead of clear_cache.
         * Since cache_time is assigned outside of lock, the value can
         * be invalid for short time, this may result in false negative
         * which is better than deadlock */
        nlc_ctx->cache_time = 0;
    }
}

static void
__nlc_inode_ctx_timer_delete(nlc_conf_t *conf, nlc_ctx_t *nlc_ctx)
{
    if (nlc_ctx->timer)
        gf_tw_del_timer(conf->timer_wheel, nlc_ctx->timer);

    if (nlc_ctx->timer_data) {
        inode_unref(nlc_ctx->timer_data->inode);
        GF_FREE(nlc_ctx->timer_data);
        nlc_ctx->timer_data = NULL;
    }

    GF_FREE(nlc_ctx->timer);
    nlc_ctx->timer = NULL;

    return;
}

static int
__nlc_inode_ctx_timer_start(xlator_t *this, inode_t *inode, nlc_ctx_t *nlc_ctx)
{
    struct gf_tw_timer_list *timer = NULL;
    nlc_timer_data_t *tmp = NULL;
    nlc_conf_t *conf = NULL;
    int ret = -1;

    conf = this->private;

    /* We are taking inode_table->lock within inode->lock
     * as the only other caller which takes inode->lock within
     * inode_table->lock and cause deadlock is inode_table_destroy.
     * Hopefully, there can be no fop when inode_table_destroy is
     * being called. */
    tmp = GF_CALLOC(1, sizeof(*tmp), gf_nlc_mt_nlc_timer_data_t);
    if (!tmp)
        goto out;
    tmp->inode = inode_ref(inode);
    tmp->this = this;

    timer = GF_CALLOC(1, sizeof(*timer), gf_common_mt_tw_timer_list);
    if (!timer)
        goto out;

    INIT_LIST_HEAD(&timer->entry);
    /* Cache timeout is generally not meant to be changed often,
     * once set, hence not within locks */
    timer->expires = conf->cache_timeout;
    timer->function = nlc_cache_timeout_handler;
    timer->data = tmp;
    nlc_ctx->timer = timer;
    nlc_ctx->timer_data = tmp;
    gf_tw_add_timer(conf->timer_wheel, timer);

    nlc_ctx->cache_time = gf_time();
    gf_msg_trace(this->name, 0,
                 "Registering timer:%p, inode:%p, "
                 "gfid:%s",
                 timer, inode, uuid_utoa(inode->gfid));

    ret = 0;

out:
    if (ret < 0) {
        if (tmp && tmp->inode)
            inode_unref(tmp->inode);
        GF_FREE(tmp);
        GF_FREE(timer);
    }

    return ret;
}

static int
__nlc_add_to_lru(xlator_t *this, inode_t *inode, nlc_ctx_t *nlc_ctx)
{
    nlc_lru_node_t *lru_ino = NULL;
    uint64_t nlc_pe_int = 0;
    nlc_conf_t *conf = NULL;
    int ret = -1;

    conf = this->private;

    lru_ino = GF_MALLOC(sizeof(*lru_ino), gf_nlc_mt_nlc_lru_node);
    if (!lru_ino)
        goto out;

    lru_ino->inode = inode_ref(inode);
    INIT_LIST_HEAD(&lru_ino->list);
    LOCK(&conf->lock);
    {
        list_add_tail(&lru_ino->list, &conf->lru);
    }
    UNLOCK(&conf->lock);

    nlc_ctx->refd_inodes = 0;
    ret = __inode_ctx_get2(inode, this, NULL, &nlc_pe_int);
    if (nlc_pe_int == 0)
        GF_ATOMIC_ADD(conf->refd_inodes, 1);

    ret = 0;

out:
    return ret;
}

static void
nlc_remove_from_lru(nlc_conf_t *conf, inode_t *inode)
{
    nlc_lru_node_t *lru_node = NULL;
    nlc_lru_node_t *tmp = NULL;
    nlc_lru_node_t *tmp1 = NULL;

    LOCK(&conf->lock);
    {
        list_for_each_entry_safe(lru_node, tmp, &conf->lru, list)
        {
            if (inode == lru_node->inode) {
                list_del(&lru_node->list);
                tmp1 = lru_node;
                break;
            }
        }
    }
    UNLOCK(&conf->lock);

    if (tmp1) {
        inode_unref(tmp1->inode);
        GF_FREE(tmp1);
    }

    return;
}

void
nlc_lru_prune(xlator_t *this, inode_t *inode)
{
    nlc_lru_node_t *lru_node = NULL;
    nlc_lru_node_t *prune_node = NULL;
    nlc_lru_node_t *tmp = NULL;
    nlc_conf_t *conf = NULL;

    conf = this->private;

    LOCK(&conf->lock);
    {
        if ((GF_ATOMIC_GET(conf->refd_inodes) < conf->inode_limit) &&
            (GF_ATOMIC_GET(conf->current_cache_size) < conf->cache_size))
            goto unlock;

        list_for_each_entry_safe(lru_node, tmp, &conf->lru, list)
        {
            list_del(&lru_node->list);
            prune_node = lru_node;
            goto unlock;
        }
    }
unlock:
    UNLOCK(&conf->lock);

    if (prune_node) {
        nlc_inode_clear_cache(this, prune_node->inode, NLC_LRU_PRUNE);
        inode_unref(prune_node->inode);
        GF_FREE(prune_node);
    }
    return;
}

void
nlc_clear_all_cache(xlator_t *this)
{
    nlc_conf_t *conf = NULL;
    struct list_head clear_list;
    nlc_lru_node_t *prune_node = NULL;
    nlc_lru_node_t *tmp = NULL;

    conf = this->private;

    INIT_LIST_HEAD(&clear_list);

    LOCK(&conf->lock);
    {
        list_replace_init(&conf->lru, &clear_list);
    }
    UNLOCK(&conf->lock);

    list_for_each_entry_safe(prune_node, tmp, &clear_list, list)
    {
        list_del(&prune_node->list);
        nlc_inode_clear_cache(this, prune_node->inode, NLC_LRU_PRUNE);
        inode_unref(prune_node->inode);
        GF_FREE(prune_node);
    }

    return;
}

static void
__nlc_free_pe(xlator_t *this, nlc_ctx_t *nlc_ctx, nlc_pe_t *pe)
{
    uint64_t pe_int = 0;
    nlc_conf_t *conf = NULL;
    uint64_t nlc_ctx_int = 0;
    const size_t len = sizeof(*pe) + pe->name_len0;

    conf = this->private;

    if (pe->inode) {
        inode_ctx_reset1(pe->inode, this, &pe_int);
        inode_ctx_get2(pe->inode, this, &nlc_ctx_int, NULL);
        inode_unref(pe->inode);
    }
    list_del(&pe->list);

    nlc_ctx->cache_size -= len;
    GF_ATOMIC_SUB(conf->current_cache_size, len);

    nlc_ctx->refd_inodes -= 1;
    if (nlc_ctx_int == 0)
        GF_ATOMIC_SUB(conf->refd_inodes, 1);

    GF_FREE(pe);

    return;
}

static void
__nlc_free_ne(nlc_conf_t *conf, nlc_ctx_t *nlc_ctx, nlc_ne_t *ne)
{
    const size_t len = sizeof(*ne) + ne->name_len0;

    list_del(&ne->list);
    GF_FREE(ne);

    nlc_ctx->cache_size -= len;
    GF_ATOMIC_SUB(conf->current_cache_size, len);

    return;
}

void
nlc_inode_clear_cache(xlator_t *this, inode_t *inode, int reason)
{
    nlc_ctx_t *nlc_ctx = NULL;
    nlc_conf_t *conf = NULL;

    conf = this->private;

    nlc_ctx = nlc_inode_ctx_get(this, inode);
    if (!nlc_ctx)
        goto out;

    LOCK(&nlc_ctx->lock);
    {
        __nlc_inode_ctx_timer_delete(conf, nlc_ctx);

        __nlc_inode_clear_entries(this, nlc_ctx);
    }
    UNLOCK(&nlc_ctx->lock);

    if (reason != NLC_LRU_PRUNE)
        nlc_remove_from_lru(conf, inode);

out:
    return;
}

static void
__nlc_del_pe(xlator_t *this, nlc_ctx_t *nlc_ctx, inode_t *entry_ino,
             const char *name, gf_boolean_t multilink, const size_t name_len0)
{
    nlc_pe_t *pe = NULL;
    nlc_pe_t *tmp = NULL;
    gf_boolean_t found = _gf_false;
    uint64_t pe_int = 0;

    if (!IS_PE_VALID(nlc_ctx->state))
        goto out;

    if (!entry_ino)
        goto name_search;

    /* If there are hardlinks first search names, followed by inodes */
    if (multilink) {
        list_for_each_entry_safe(pe, tmp, &nlc_ctx->pe, list)
        {
            if (pe->name_len0 && (name_len0 == pe->name_len0) &&
                (strcmp(pe->name, name) == 0)) {
                found = _gf_true;
                goto out;
            }
        }
        inode_ctx_reset1(entry_ino, this, &pe_int);
        if (pe_int) {
            pe = (void *)(long)(pe_int);
            found = _gf_true;
            goto out;
        }
        goto out;
    }

    inode_ctx_reset1(entry_ino, this, &pe_int);
    if (pe_int) {
        pe = (void *)(long)(pe_int);
        found = _gf_true;
        goto out;
    }

name_search:
    list_for_each_entry_safe(pe, tmp, &nlc_ctx->pe, list)
    {
        if (pe->name_len0 && (name_len0 == pe->name_len0) &&
            (strcmp(pe->name, name) == 0)) {
            found = _gf_true;
            break;
            /* TODO: can there be duplicates? */
        }
    }

out:
    if (found)
        __nlc_free_pe(this, nlc_ctx, pe);

    return;
}

static void
__nlc_del_ne(nlc_conf_t *conf, nlc_ctx_t *nlc_ctx, const char *name,
             const size_t name_len0)
{
    nlc_ne_t *ne = NULL;
    nlc_ne_t *tmp = NULL;

    if (!IS_NE_VALID(nlc_ctx->state))
        goto out;

    list_for_each_entry_safe(ne, tmp, &nlc_ctx->ne, list)
    {
        if (ne->name_len0 && (name_len0 == ne->name_len0) &&
            (strcmp(ne->name, name) == 0)) {
            __nlc_free_ne(conf, nlc_ctx, ne);
            break;
        }
    }
out:
    return;
}

static void
__nlc_add_pe(xlator_t *this, nlc_ctx_t *nlc_ctx, inode_t *entry_ino,
             const char *name, const size_t name_len0)
{
    nlc_pe_t *pe = NULL;
    nlc_conf_t *conf = NULL;
    uint64_t nlc_ctx_int = 0;

    conf = this->private;

    /* TODO: There can be no duplicate entries, as it is added only
    during create. In case there arises duplicate entries, search PE
    found = __nlc_search (entries, name, _gf_false);
    can use bit vector to have simple search than sequential search */

    pe = GF_CALLOC(sizeof(*pe) + name_len0, 1, gf_nlc_mt_nlc_pe_t);
    if (!pe)
        goto out;

    pe->name_len0 = name_len0;
    if (entry_ino) {
        pe->inode = inode_ref(entry_ino);
        nlc_inode_ctx_set(this, entry_ino, NULL, pe);
    } else if (name_len0) {
        memcpy(pe->name, name, name_len0);
    }

    list_add(&pe->list, &nlc_ctx->pe);

    nlc_ctx->cache_size += (sizeof(*pe) + name_len0);
    GF_ATOMIC_ADD(conf->current_cache_size, (sizeof(*pe) + name_len0));

    nlc_ctx->refd_inodes += 1;
    inode_ctx_get2(entry_ino, this, &nlc_ctx_int, NULL);
    if (nlc_ctx_int == 0)
        GF_ATOMIC_ADD(conf->refd_inodes, 1);
out:
    return;
}

static void
__nlc_add_ne(xlator_t *this, nlc_ctx_t *nlc_ctx, const char *name,
             const size_t name_len0)
{
    nlc_ne_t *ne = NULL;
    nlc_conf_t *conf = NULL;

    /* TODO: search ne before adding to get rid of duplicate entries
    found = __nlc_search (entries, name, _gf_false);
    can use bit vector to have faster search than sequential search */

    ne = GF_MALLOC(sizeof(*ne) + name_len0, gf_nlc_mt_nlc_ne_t);
    if (!ne)
        goto out;

    list_add(&ne->list, &nlc_ctx->ne);
    ne->name_len0 = name_len0;
    if (name_len0 > 0)
        memcpy(ne->name, name, name_len0);

    nlc_ctx->cache_size += (sizeof(*ne) + name_len0);
    conf = this->private;
    GF_ATOMIC_ADD(conf->current_cache_size, (sizeof(*ne) + name_len0));
out:
    return;
}

void
nlc_dir_add_ne(xlator_t *this, inode_t *inode, const char *name)
{
    nlc_ctx_t *nlc_ctx = NULL;
    size_t name_len0;

    if (inode->ia_type != IA_IFDIR) {
        gf_msg_callingfn(this->name, GF_LOG_ERROR, EINVAL, NLC_MSG_EINVAL,
                         "inode is not of type dir");
        goto out;
    }

    nlc_ctx = nlc_inode_ctx_get_set(this, inode);
    if (!nlc_ctx)
        goto out;

    name_len0 = name ? strlen(name) + 1 : 0; /* account for '\0' */
    LOCK(&nlc_ctx->lock);
    {
        /* There is one possibility where we need to search before
         * adding NE: when there are two parallel lookups on a non
         * existent file */
        if (!__nlc_search_ne(nlc_ctx, name, name_len0)) {
            __nlc_add_ne(this, nlc_ctx, name, name_len0);
            __nlc_set_dir_state(nlc_ctx, NLC_NE_VALID);
        }
    }
    UNLOCK(&nlc_ctx->lock);
out:
    return;
}

void
nlc_dir_remove_pe(xlator_t *this, inode_t *parent, inode_t *entry_ino,
                  const char *name, gf_boolean_t multilink)
{
    nlc_ctx_t *nlc_ctx = NULL;
    size_t name_len0;

    if (parent->ia_type != IA_IFDIR) {
        gf_msg_callingfn(this->name, GF_LOG_ERROR, EINVAL, NLC_MSG_EINVAL,
                         "inode is not of type dir");
        goto out;
    }

    nlc_ctx = nlc_inode_ctx_get(this, parent);
    if (!nlc_ctx)
        goto out;

    name_len0 = name ? strlen(name) + 1 : 0; /* account for '\0' */
    LOCK(&nlc_ctx->lock);
    {
        if (!__nlc_is_cache_valid(this, nlc_ctx))
            goto unlock;

        __nlc_del_pe(this, nlc_ctx, entry_ino, name, multilink, name_len0);
        __nlc_add_ne(this, nlc_ctx, name, name_len0);
        __nlc_set_dir_state(nlc_ctx, NLC_NE_VALID);
    }
unlock:
    UNLOCK(&nlc_ctx->lock);
out:
    return;
}

void
nlc_dir_add_pe(xlator_t *this, inode_t *inode, inode_t *entry_ino,
               const char *name)
{
    nlc_ctx_t *nlc_ctx = NULL;
    size_t name_len0;
    nlc_conf_t *conf = NULL;

    if (inode->ia_type != IA_IFDIR) {
        gf_msg_callingfn(this->name, GF_LOG_ERROR, EINVAL, NLC_MSG_EINVAL,
                         "inode is not of type dir");
        goto out;
    }

    nlc_ctx = nlc_inode_ctx_get_set(this, inode);
    if (!nlc_ctx)
        goto out;

    name_len0 = name ? strlen(name) + 1 : 0;
    conf = this->private;
    LOCK(&nlc_ctx->lock);
    {
        __nlc_del_ne(conf, nlc_ctx, name, name_len0);
        __nlc_add_pe(this, nlc_ctx, entry_ino, name, name_len0);
        if (!IS_PE_VALID(nlc_ctx->state))
            __nlc_set_dir_state(nlc_ctx, NLC_PE_PARTIAL);
    }
    UNLOCK(&nlc_ctx->lock);
out:
    return;
}

static gf_boolean_t
__nlc_search_ne(nlc_ctx_t *nlc_ctx, const char *name, const size_t name_len0)
{
    gf_boolean_t found = _gf_false;
    nlc_ne_t *ne = NULL;
    nlc_ne_t *tmp = NULL;

    if (!IS_NE_VALID(nlc_ctx->state))
        goto out;

    list_for_each_entry_safe(ne, tmp, &nlc_ctx->ne, list)
    {
        if (name_len0 && (name_len0 == ne->name_len0) &&
            (strcmp(ne->name, name) == 0)) {
            found = _gf_true;
            break;
        }
    }
out:
    return found;
}

static gf_boolean_t
__nlc_search_pe(nlc_ctx_t *nlc_ctx, const char *name, const size_t name_len0)
{
    gf_boolean_t found = _gf_false;
    nlc_pe_t *pe = NULL;
    nlc_pe_t *tmp = NULL;

    if (!IS_PE_VALID(nlc_ctx->state))
        goto out;

    list_for_each_entry_safe(pe, tmp, &nlc_ctx->pe, list)
    {
        if (pe->name_len0 > 0 && (name_len0 == pe->name_len0) &&
            (strcmp(pe->name, name) == 0)) {
            found = _gf_true;
            break;
        }
    }
out:
    return found;
}

static nlc_pe_t *
__nlc_get_pe(nlc_ctx_t *nlc_ctx, const char *name,
             gf_boolean_t case_insensitive)
{
    nlc_pe_t *pe = NULL;
    nlc_pe_t *tmp = NULL;

    if (!IS_PE_VALID(nlc_ctx->state))
        goto out;

    if (case_insensitive) {
        list_for_each_entry_safe(pe, tmp, &nlc_ctx->pe, list)
        {
            if (pe->name_len0 && (strcasecmp(pe->name, name) == 0)) {
                return pe;
                break;
            }
        }
    } else {
        list_for_each_entry_safe(pe, tmp, &nlc_ctx->pe, list)
        {
            if (pe->name_len0 && (strcmp(pe->name, name) == 0)) {
                return pe;
                break;
            }
        }
    }
out:
    return NULL;
}

gf_boolean_t
nlc_is_negative_lookup(xlator_t *this, loc_t *loc)
{
    nlc_ctx_t *nlc_ctx = NULL;
    inode_t *inode = NULL;
    gf_boolean_t neg_entry = _gf_false;
    size_t name_len0;

    inode = loc->parent;
    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    if (inode->ia_type != IA_IFDIR) {
        gf_msg_callingfn(this->name, GF_LOG_ERROR, EINVAL, NLC_MSG_EINVAL,
                         "inode is not of type dir");
        goto out;
    }

    nlc_ctx = nlc_inode_ctx_get(this, inode);
    if (!nlc_ctx)
        goto out;

    name_len0 = loc->name ? strlen(loc->name) + 1 : 0;

    LOCK(&nlc_ctx->lock);
    {
        if (!__nlc_is_cache_valid(this, nlc_ctx))
            goto unlock;

        if (__nlc_search_ne(nlc_ctx, loc->name, name_len0)) {
            neg_entry = _gf_true;
            goto unlock;
        }
        if ((nlc_ctx->state & NLC_PE_FULL) &&
            !__nlc_search_pe(nlc_ctx, loc->name, name_len0)) {
            neg_entry = _gf_true;
            goto unlock;
        }
    }
unlock:
    UNLOCK(&nlc_ctx->lock);

out:
    return neg_entry;
}

int
nlc_get_real_file_name(xlator_t *this, loc_t *loc, const char *fname,
                       int32_t *op_errno, dict_t *dict)
{
    nlc_ctx_t *nlc_ctx = NULL;
    inode_t *inode = NULL;
    nlc_pe_t *found_file = NULL;
    int ret = -1;

    GF_VALIDATE_OR_GOTO(this->name, loc, out);
    GF_VALIDATE_OR_GOTO(this->name, fname, out);
    GF_VALIDATE_OR_GOTO(this->name, op_errno, out);
    GF_VALIDATE_OR_GOTO(this->name, dict, out);

    inode = loc->inode;
    GF_VALIDATE_OR_GOTO(this->name, inode, out);

    if (inode->ia_type != IA_IFDIR) {
        gf_msg_callingfn(this->name, GF_LOG_ERROR, EINVAL, NLC_MSG_EINVAL,
                         "inode is not of type dir");
        goto out;
    }

    nlc_ctx = nlc_inode_ctx_get(this, inode);
    if (!nlc_ctx)
        goto out;

    LOCK(&nlc_ctx->lock);
    {
        if (!__nlc_is_cache_valid(this, nlc_ctx))
            goto unlock;

        found_file = __nlc_get_pe(nlc_ctx, fname, _gf_true);
        if (found_file) {
            ret = dict_set_dynstrn(
                dict, GF_XATTR_GET_REAL_FILENAME_KEY,
                SLEN(GF_XATTR_GET_REAL_FILENAME_KEY),
                gf_strndup(found_file->name, found_file->name_len0 - 1));
            if (ret < 0)
                goto unlock;
            ret = found_file->name_len0;
        } else if ((nlc_ctx->state & NLC_PE_FULL)) {
            ret = 0;
            *op_errno = ENOENT;
        }
    }
unlock:
    UNLOCK(&nlc_ctx->lock);

out:
    return ret;
}

void
nlc_dump_inodectx(xlator_t *this, inode_t *inode)
{
    int32_t ret = -1;
    char *path = NULL;
    char key_prefix[GF_DUMP_MAX_BUF_LEN] = {
        0,
    };
    char uuid_str[64] = {
        0,
    };
    nlc_ctx_t *nlc_ctx = NULL;
    nlc_pe_t *pe = NULL;
    nlc_pe_t *tmp = NULL;
    nlc_ne_t *ne = NULL;
    nlc_ne_t *tmp1 = NULL;

    nlc_ctx = nlc_inode_ctx_get(this, inode);

    if (!nlc_ctx)
        goto out;

    ret = TRY_LOCK(&nlc_ctx->lock);
    if (!ret) {
        gf_proc_dump_build_key(key_prefix, "xlator.performance.nl-cache",
                               "nlc_inode");
        gf_proc_dump_add_section("%s", key_prefix);

        __inode_path(inode, NULL, &path);
        if (path != NULL) {
            gf_proc_dump_write("path", "%s", path);
            GF_FREE(path);
        }

        uuid_utoa_r(inode->gfid, uuid_str);

        gf_proc_dump_write("inode", "%p", inode);
        gf_proc_dump_write("gfid", "%s", uuid_str);

        gf_proc_dump_write("state", "%" PRIu64, nlc_ctx->state);
        gf_proc_dump_write("timer", "%p", nlc_ctx->timer);
        gf_proc_dump_write("cache-time", "%ld", nlc_ctx->cache_time);
        gf_proc_dump_write("cache-size", "%zu", nlc_ctx->cache_size);
        gf_proc_dump_write("refd-inodes", "%" PRIu64, nlc_ctx->refd_inodes);

        if (IS_PE_VALID(nlc_ctx->state))
            list_for_each_entry_safe(pe, tmp, &nlc_ctx->pe, list)
            {
                gf_proc_dump_write("pe", "%p, %p, %s", pe, pe->inode, pe->name);
            }

        if (IS_NE_VALID(nlc_ctx->state))
            list_for_each_entry_safe(ne, tmp1, &nlc_ctx->ne, list)
            {
                gf_proc_dump_write("ne", "%s", ne->name);
            }

        UNLOCK(&nlc_ctx->lock);
    }

    if (ret && nlc_ctx)
        gf_proc_dump_write("Unable to dump the inode information",
                           "(Lock acquisition failed) %p (gfid: %s)", nlc_ctx,
                           uuid_str);
out:
    return;
}
