/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "afr.h"
#include "afr-self-heal.h"
#include "afr-self-heald.h"
#include "protocol-common.h"
#include <glusterfs/syncop-utils.h>
#include "afr-messages.h"
#include <glusterfs/byte-order.h>

#define AFR_EH_SPLIT_BRAIN_LIMIT 1024
#define AFR_STATISTICS_HISTORY_SIZE 50

#define ASSERT_LOCAL(this, healer)                                             \
    if (!afr_shd_is_subvol_local(this, healer->subvol)) {                      \
        healer->local = _gf_false;                                             \
        if (safe_break(healer)) {                                              \
            break;                                                             \
        } else {                                                               \
            continue;                                                          \
        }                                                                      \
    } else {                                                                   \
        healer->local = _gf_true;                                              \
    }

#define NTH_INDEX_HEALER(this, n)                                              \
    &((((afr_private_t *)this->private))->shd.index_healers[n])
#define NTH_FULL_HEALER(this, n)                                               \
    &((((afr_private_t *)this->private))->shd.full_healers[n])

char *
afr_subvol_name(xlator_t *this, int subvol)
{
    afr_private_t *priv = NULL;

    priv = this->private;
    if (subvol < 0 || subvol > priv->child_count)
        return NULL;

    return priv->children[subvol]->name;
}

void
afr_destroy_crawl_event_data(void *data)
{
    return;
}

void
afr_destroy_shd_event_data(void *data)
{
    shd_event_t *shd_event = data;

    if (!shd_event)
        return;
    GF_FREE(shd_event->path);

    return;
}

gf_boolean_t
afr_shd_is_subvol_local(xlator_t *this, int subvol)
{
    afr_private_t *priv = NULL;
    gf_boolean_t is_local = _gf_false;
    loc_t loc = {
        0,
    };

    loc.inode = this->itable->root;
    gf_uuid_copy(loc.gfid, loc.inode->gfid);
    priv = this->private;
    syncop_is_subvol_local(priv->children[subvol], &loc, &is_local);
    return is_local;
}

int
__afr_shd_healer_wait(struct subvol_healer *healer)
{
    afr_private_t *priv = NULL;
    struct timespec wait_till = {
        0,
    };
    int ret = 0;

    priv = healer->this->private;

disabled_loop:
    wait_till.tv_sec = gf_time() + priv->shd.timeout;

    while (!healer->rerun) {
        ret = pthread_cond_timedwait(&healer->cond, &healer->mutex, &wait_till);
        if (ret == ETIMEDOUT)
            break;
    }

    ret = healer->rerun;
    healer->rerun = 0;

    if (!priv->shd.enabled)
        goto disabled_loop;

    return ret;
}

int
afr_shd_healer_wait(struct subvol_healer *healer)
{
    int ret = 0;

    pthread_mutex_lock(&healer->mutex);
    {
        ret = __afr_shd_healer_wait(healer);
    }
    pthread_mutex_unlock(&healer->mutex);

    return ret;
}

gf_boolean_t
safe_break(struct subvol_healer *healer)
{
    gf_boolean_t ret = _gf_false;

    pthread_mutex_lock(&healer->mutex);
    {
        if (healer->rerun)
            goto unlock;

        healer->running = _gf_false;
        ret = _gf_true;
    }
unlock:
    pthread_mutex_unlock(&healer->mutex);

    return ret;
}

inode_t *
afr_shd_inode_find(xlator_t *this, xlator_t *subvol, uuid_t gfid)
{
    int ret = 0;
    uint64_t val = IA_INVAL;
    dict_t *xdata = NULL;
    dict_t *rsp_dict = NULL;
    inode_t *inode = NULL;

    xdata = dict_new();
    if (!xdata)
        goto out;

    ret = dict_set_int8(xdata, GF_INDEX_IA_TYPE_GET_REQ, 1);
    if (ret)
        goto out;

    ret = syncop_inode_find(this, subvol, gfid, &inode, xdata, &rsp_dict);
    if (ret < 0)
        goto out;

    if (rsp_dict) {
        ret = dict_get_uint64(rsp_dict, GF_INDEX_IA_TYPE_GET_RSP, &val);
        if (ret)
            goto out;
    }
    ret = inode_ctx_set2(inode, subvol, 0, &val);
out:
    if (ret && inode) {
        inode_unref(inode);
        inode = NULL;
    }
    if (xdata)
        dict_unref(xdata);
    if (rsp_dict)
        dict_unref(rsp_dict);
    return inode;
}

inode_t *
afr_shd_index_inode(xlator_t *this, xlator_t *subvol, char *vgfid)
{
    loc_t rootloc = {
        0,
    };
    inode_t *inode = NULL;
    int ret = 0;
    dict_t *xattr = NULL;
    void *index_gfid = NULL;

    rootloc.inode = inode_ref(this->itable->root);
    gf_uuid_copy(rootloc.gfid, rootloc.inode->gfid);

    ret = syncop_getxattr(subvol, &rootloc, &xattr, vgfid, NULL, NULL);
    if (ret || !xattr) {
        errno = -ret;
        goto out;
    }

    ret = dict_get_ptr(xattr, vgfid, &index_gfid);
    if (ret)
        goto out;

    gf_msg_debug(this->name, 0, "%s dir gfid for %s: %s", vgfid, subvol->name,
                 uuid_utoa(index_gfid));

    inode = afr_shd_inode_find(this, subvol, index_gfid);

out:
    loc_wipe(&rootloc);

    if (xattr)
        dict_unref(xattr);

    return inode;
}

int
afr_shd_entry_purge(xlator_t *subvol, inode_t *inode, char *name,
                    ia_type_t type)
{
    int ret = 0;
    loc_t loc = {
        0,
    };

    loc.parent = inode_ref(inode);
    loc.name = name;

    if (IA_ISDIR(type))
        ret = syncop_rmdir(subvol, &loc, 1, NULL, NULL);
    else
        ret = syncop_unlink(subvol, &loc, NULL, NULL);

    loc_wipe(&loc);
    return ret;
}

void
afr_shd_zero_xattrop(xlator_t *this, uuid_t gfid)
{
    call_frame_t *frame = NULL;
    inode_t *inode = NULL;
    afr_private_t *priv = NULL;
    dict_t *xattr = NULL;
    int ret = 0;
    int i = 0;
    int raw[AFR_NUM_CHANGE_LOGS] = {0};

    priv = this->private;
    frame = afr_frame_create(this, NULL);
    if (!frame)
        goto out;
    inode = afr_inode_find(this, gfid);
    if (!inode)
        goto out;
    xattr = dict_new();
    if (!xattr)
        goto out;
    ret = dict_set_static_bin(xattr, AFR_DIRTY, raw,
                              sizeof(int) * AFR_NUM_CHANGE_LOGS);
    if (ret)
        goto out;
    for (i = 0; i < priv->child_count; i++) {
        ret = dict_set_static_bin(xattr, priv->pending_key[i], raw,
                                  sizeof(int) * AFR_NUM_CHANGE_LOGS);
        if (ret)
            goto out;
    }

    /*Send xattrop to all bricks. Doing a lookup to see if bricks are up or
     * has valid repies for this gfid seems a bit of an overkill.*/
    for (i = 0; i < priv->child_count; i++)
        afr_selfheal_post_op(frame, this, inode, i, xattr, NULL);

out:
    if (frame)
        AFR_STACK_DESTROY(frame);
    if (inode)
        inode_unref(inode);
    if (xattr)
        dict_unref(xattr);
    return;
}

int
afr_shd_selfheal_name(struct subvol_healer *healer, int child, uuid_t parent,
                      const char *bname)
{
    int ret = -1;

    ret = afr_selfheal_name(THIS, parent, bname, NULL, NULL);

    return ret;
}

int
afr_shd_selfheal(struct subvol_healer *healer, int child, uuid_t gfid)
{
    int ret = 0;
    eh_t *eh = NULL;
    afr_private_t *priv = NULL;
    afr_self_heald_t *shd = NULL;
    shd_event_t *shd_event = NULL;
    char *path = NULL;
    xlator_t *subvol = NULL;
    xlator_t *this = NULL;
    crawl_event_t *crawl_event = NULL;

    this = healer->this;
    priv = this->private;
    shd = &priv->shd;
    crawl_event = &healer->crawl_event;

    subvol = priv->children[child];

    // If this fails with ENOENT/ESTALE index is stale
    ret = syncop_gfid_to_path(this->itable, subvol, gfid, &path);
    if (ret < 0)
        return ret;

    ret = afr_selfheal(this, gfid);

    LOCK(&priv->lock);
    {
        if (ret == -EIO) {
            eh = shd->split_brain;
            crawl_event->split_brain_count++;
        } else if (ret < 0) {
            crawl_event->heal_failed_count++;
        } else if (ret == 0) {
            crawl_event->healed_count++;
        }
    }
    UNLOCK(&priv->lock);

    if (eh) {
        shd_event = GF_CALLOC(1, sizeof(*shd_event), gf_afr_mt_shd_event_t);
        if (!shd_event)
            goto out;

        shd_event->child = child;
        shd_event->path = path;

        if (eh_save_history(eh, shd_event) < 0)
            goto out;

        shd_event = NULL;
        path = NULL;
    }
out:
    GF_FREE(shd_event);
    GF_FREE(path);
    return ret;
}

void
afr_shd_sweep_prepare(struct subvol_healer *healer)
{
    crawl_event_t *event = NULL;

    event = &healer->crawl_event;

    event->healed_count = 0;
    event->split_brain_count = 0;
    event->heal_failed_count = 0;

    event->start_time = gf_time();
    event->end_time = 0;
    _mask_cancellation();
}

void
afr_shd_sweep_done(struct subvol_healer *healer)
{
    crawl_event_t *event = NULL;
    crawl_event_t *history = NULL;
    afr_self_heald_t *shd = NULL;

    event = &healer->crawl_event;
    shd = &(((afr_private_t *)healer->this->private)->shd);

    event->end_time = gf_time();
    history = gf_memdup(event, sizeof(*event));
    event->start_time = 0;

    if (!history)
        return;

    if (eh_save_history(shd->statistics[healer->subvol], history) < 0)
        GF_FREE(history);
    _unmask_cancellation();
}

int
afr_shd_index_heal(xlator_t *subvol, gf_dirent_t *entry, loc_t *parent,
                   void *data)
{
    struct subvol_healer *healer = data;
    afr_private_t *priv = NULL;
    uuid_t gfid = {0};
    int ret = 0;
    uint64_t val = IA_INVAL;

    priv = healer->this->private;
    if (!priv->shd.enabled)
        return -EBUSY;

    gf_msg_debug(healer->this->name, 0, "got entry: %s from %s", entry->d_name,
                 priv->children[healer->subvol]->name);

    ret = gf_uuid_parse(entry->d_name, gfid);
    if (ret)
        return 0;

    inode_ctx_get2(parent->inode, subvol, NULL, &val);

    ret = afr_shd_selfheal(healer, healer->subvol, gfid);

    if (ret == -ENOENT || ret == -ESTALE)
        afr_shd_entry_purge(subvol, parent->inode, entry->d_name, val);

    if (ret == 2)
        /* If bricks crashed in pre-op after creating indices/xattrop
         * link but before setting afr changelogs, we end up with stale
         * xattrop links but zero changelogs. Remove such entries by
         * sending a post-op with zero changelogs.
         */
        afr_shd_zero_xattrop(healer->this, gfid);

    return 0;
}

int
afr_shd_index_sweep(struct subvol_healer *healer, char *vgfid)
{
    loc_t loc = {0};
    afr_private_t *priv = NULL;
    int ret = 0;
    xlator_t *subvol = NULL;
    dict_t *xdata = NULL;
    call_frame_t *frame = NULL;

    priv = healer->this->private;
    subvol = priv->children[healer->subvol];

    frame = afr_frame_create(healer->this, &ret);
    if (!frame) {
        ret = -ret;
        goto out;
    }

    loc.inode = afr_shd_index_inode(healer->this, subvol, vgfid);
    if (!loc.inode) {
        gf_msg(healer->this->name, GF_LOG_WARNING, 0,
               AFR_MSG_INDEX_DIR_GET_FAILED, "unable to get index-dir on %s",
               subvol->name);
        ret = -errno;
        goto out;
    }

    xdata = dict_new();
    if (!xdata || dict_set_int32_sizen(xdata, "get-gfid-type", 1)) {
        ret = -ENOMEM;
        goto out;
    }

    ret = syncop_mt_dir_scan(frame, subvol, &loc, GF_CLIENT_PID_SELF_HEALD,
                             healer, afr_shd_index_heal, xdata,
                             priv->shd.max_threads, priv->shd.wait_qlength);

    if (ret == 0)
        ret = healer->crawl_event.healed_count;

out:
    loc_wipe(&loc);

    if (xdata)
        dict_unref(xdata);
    if (frame)
        AFR_STACK_DESTROY(frame);
    return ret;
}

int
afr_shd_index_sweep_all(struct subvol_healer *healer)
{
    int ret = 0;
    int count = 0;

    ret = afr_shd_index_sweep(healer, GF_XATTROP_INDEX_GFID);
    if (ret < 0)
        goto out;
    count = ret;

    ret = afr_shd_index_sweep(healer, GF_XATTROP_DIRTY_GFID);
    if (ret < 0)
        goto out;
    count += ret;

    ret = afr_shd_index_sweep(healer, GF_XATTROP_ENTRY_CHANGES_GFID);
    if (ret < 0)
        goto out;
    count += ret;
out:
    if (ret < 0)
        return ret;
    else
        return count;
}

int
afr_shd_full_heal(xlator_t *subvol, gf_dirent_t *entry, loc_t *parent,
                  void *data)
{
    struct subvol_healer *healer = data;
    xlator_t *this = healer->this;
    afr_private_t *priv = NULL;

    priv = this->private;

    if (this->cleanup_starting) {
        return -ENOTCONN;
    }

    if (!priv->shd.enabled)
        return -EBUSY;

    afr_shd_selfheal_name(healer, healer->subvol, parent->inode->gfid,
                          entry->d_name);

    afr_shd_selfheal(healer, healer->subvol, entry->d_stat.ia_gfid);

    return 0;
}

int
afr_shd_full_sweep(struct subvol_healer *healer, inode_t *inode)
{
    afr_private_t *priv = NULL;
    loc_t loc = {0};

    priv = healer->this->private;
    loc.inode = inode;
    return syncop_ftw(priv->children[healer->subvol], &loc,
                      GF_CLIENT_PID_SELF_HEALD, healer, afr_shd_full_heal);
}

int
afr_shd_fill_ta_loc(xlator_t *this, loc_t *loc)
{
    afr_private_t *priv = NULL;
    struct iatt stbuf = {
        0,
    };
    int ret = -1;

    priv = this->private;
    loc->parent = inode_ref(this->itable->root);
    gf_uuid_copy(loc->pargfid, loc->parent->gfid);
    loc->name = priv->pending_key[THIN_ARBITER_BRICK_INDEX];
    loc->inode = inode_new(loc->parent->table);
    GF_CHECK_ALLOC(loc->inode, ret, out);

    if (!gf_uuid_is_null(priv->ta_gfid))
        goto assign_gfid;

    ret = syncop_lookup(priv->children[THIN_ARBITER_BRICK_INDEX], loc, &stbuf,
                        0, 0, 0);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "Failed lookup on file %s.", loc->name);
        goto out;
    }

    gf_uuid_copy(priv->ta_gfid, stbuf.ia_gfid);

assign_gfid:
    gf_uuid_copy(loc->gfid, priv->ta_gfid);
    ret = 0;

out:
    if (ret)
        loc_wipe(loc);

    return ret;
}

int
_afr_shd_ta_get_xattrs(xlator_t *this, loc_t *loc, dict_t **xdata)
{
    afr_private_t *priv = NULL;
    dict_t *xattr = NULL;
    int raw[AFR_NUM_CHANGE_LOGS] = {
        0,
    };
    int ret = -1;
    int i = 0;

    priv = this->private;

    xattr = dict_new();
    if (!xattr) {
        gf_msg(this->name, GF_LOG_ERROR, ENOMEM, AFR_MSG_DICT_GET_FAILED,
               "Failed to create dict.");
        goto out;
    }
    for (i = 0; i < priv->child_count; i++) {
        ret = dict_set_static_bin(xattr, priv->pending_key[i], &raw,
                                  AFR_NUM_CHANGE_LOGS * sizeof(int));
        if (ret)
            goto out;
    }

    ret = syncop_xattrop(priv->children[THIN_ARBITER_BRICK_INDEX], loc,
                         GF_XATTROP_ADD_ARRAY, xattr, NULL, xdata, NULL);
    if (ret || !(*xdata)) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "Xattrop failed on %s.", loc->name);
    }

out:
    if (xattr)
        dict_unref(xattr);

    return ret;
}

void
afr_shd_ta_get_xattrs(xlator_t *this, loc_t *loc, struct subvol_healer *healer,
                      dict_t **xdata)
{
    int ret = 0;

    loc_wipe(loc);
    if (afr_shd_fill_ta_loc(this, loc)) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "Failed to populate thin-arbiter loc for: %s.", loc->name);
        ret = -1;
        goto out;
    }

    ret = afr_ta_post_op_lock(this, loc);
    if (ret)
        goto out;

    ret = _afr_shd_ta_get_xattrs(this, loc, xdata);
    if (ret) {
        if (*xdata) {
            dict_unref(*xdata);
            *xdata = NULL;
        }
    }

    afr_ta_post_op_unlock(this, loc);

out:
    if (ret)
        healer->rerun = 1;
}

int
afr_shd_ta_unset_xattrs(xlator_t *this, loc_t *loc, dict_t **xdata, int healer)
{
    afr_private_t *priv = NULL;
    dict_t *xattr = NULL;
    gf_boolean_t need_xattrop = _gf_false;
    void *pending_raw = NULL;
    int *raw = NULL;
    int pending[AFR_NUM_CHANGE_LOGS] = {
        0,
    };
    int i = 0;
    int j = 0;
    int val = 0;
    int ret = -1;

    priv = this->private;

    xattr = dict_new();
    if (!xattr) {
        goto out;
    }

    for (i = 0; i < priv->child_count; i++) {
        raw = GF_CALLOC(AFR_NUM_CHANGE_LOGS, sizeof(int), gf_afr_mt_int32_t);
        if (!raw) {
            goto out;
        }

        ret = dict_get_ptr(*xdata, priv->pending_key[i], &pending_raw);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_GET_FAILED,
                   "Error getting value "
                   "of pending key %s",
                   priv->pending_key[i]);
            GF_FREE(raw);
            goto out;
        }

        memcpy(pending, pending_raw, sizeof(pending));
        for (j = 0; j < AFR_NUM_CHANGE_LOGS; j++) {
            val = ntoh32(pending[j]);
            if (val) {
                if (i == healer) {
                    gf_msg(this->name, GF_LOG_INFO, 0, AFR_MSG_THIN_ARB,
                           "I am "
                           "not the good shd. Skipping. "
                           "SHD = %d.",
                           healer);
                    ret = 0;
                    GF_FREE(raw);
                    goto out;
                }
                need_xattrop = _gf_true;
                raw[j] = hton32(-val);
            }
        }

        ret = dict_set_bin(xattr, priv->pending_key[i], raw,
                           AFR_NUM_CHANGE_LOGS * sizeof(int));
        if (ret) {
            GF_FREE(raw);
            goto out;
        }

        if (need_xattrop)
            break;
    }

    if (!need_xattrop) {
        ret = 0;
        goto out;
    }

    ret = syncop_xattrop(priv->children[THIN_ARBITER_BRICK_INDEX], loc,
                         GF_XATTROP_ADD_ARRAY, xattr, NULL, NULL, NULL);
    if (ret)
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "Xattrop failed.");

out:
    if (xattr)
        dict_unref(xattr);

    return ret;
}

void
afr_shd_ta_check_and_unset_xattrs(xlator_t *this, loc_t *loc,
                                  struct subvol_healer *healer,
                                  dict_t *pre_crawl_xdata)
{
    int ret_lock = 0;
    int ret = 0;
    dict_t *post_crawl_xdata = NULL;

    ret_lock = afr_ta_post_op_lock(this, loc);
    if (ret_lock)
        goto unref;

    ret = _afr_shd_ta_get_xattrs(this, loc, &post_crawl_xdata);
    if (ret)
        goto unref;

    if (!are_dicts_equal(pre_crawl_xdata, post_crawl_xdata, NULL, NULL)) {
        ret = -1;
        goto unref;
    }

    ret = afr_shd_ta_unset_xattrs(this, loc, &post_crawl_xdata, healer->subvol);

unref:
    if (post_crawl_xdata) {
        dict_unref(post_crawl_xdata);
        post_crawl_xdata = NULL;
    }

    if (ret || ret_lock)
        healer->rerun = 1;

    if (!ret_lock)
        afr_ta_post_op_unlock(this, loc);
}

gf_boolean_t
afr_bricks_available_for_heal(afr_private_t *priv)
{
    int up_children = 0;

    up_children = __afr_get_up_children_count(priv);
    if (up_children < 2) {
        return _gf_false;
    }
    return _gf_true;
}

static gf_boolean_t
afr_shd_ta_needs_heal(xlator_t *this, struct subvol_healer *healer)
{
    dict_t *xdata = NULL;
    afr_private_t *priv = NULL;
    loc_t loc = {
        0,
    };
    int ret = -1;
    int i = 0;
    gf_boolean_t need_heal = _gf_false;

    priv = this->private;

    ret = afr_shd_fill_ta_loc(this, &loc);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_THIN_ARB,
               "Failed to populate thin-arbiter loc for: %s.", loc.name);
        healer->rerun = 1;
        goto out;
    }

    if (_afr_shd_ta_get_xattrs(this, &loc, &xdata)) {
        healer->rerun = 1;
        goto out;
    }

    for (i = 0; i < priv->child_count; i++) {
        if (afr_ta_dict_contains_pending_xattr(xdata, priv, i)) {
            need_heal = _gf_true;
            break;
        }
    }

out:
    if (xdata)
        dict_unref(xdata);
    loc_wipe(&loc);

    return need_heal;
}

static int
afr_shd_anon_inode_cleaner(xlator_t *subvol, gf_dirent_t *entry, loc_t *parent,
                           void *data)
{
    struct subvol_healer *healer = data;
    afr_private_t *priv = healer->this->private;
    call_frame_t *frame = NULL;
    afr_local_t *local = NULL;
    int ret = 0;
    loc_t loc = {0};
    int count = 0;
    int i = 0;
    int op_errno = 0;
    struct iatt *iatt = NULL;
    gf_boolean_t multiple_links = _gf_false;
    unsigned char *gfid_present = alloca0(priv->child_count);
    unsigned char *entry_present = alloca0(priv->child_count);
    char *type = "file";

    frame = afr_frame_create(healer->this, &ret);
    if (!frame) {
        ret = -ret;
        goto out;
    }
    local = frame->local;
    if (AFR_COUNT(local->child_up, priv->child_count) != priv->child_count) {
        gf_msg_debug(healer->this->name, 0,
                     "Not all bricks are up. Skipping "
                     "cleanup of %s on %s",
                     entry->d_name, subvol->name);
        ret = 0;
        goto out;
    }

    loc.inode = inode_new(parent->inode->table);
    if (!loc.inode) {
        ret = -ENOMEM;
        goto out;
    }
    ret = gf_uuid_parse(entry->d_name, loc.gfid);
    if (ret) {
        ret = 0;
        goto out;
    }
    AFR_ONLIST(local->child_up, frame, afr_selfheal_discover_cbk, lookup, &loc,
               NULL);
    for (i = 0; i < priv->child_count; i++) {
        if (local->replies[i].op_ret == 0) {
            count++;
            gfid_present[i] = 1;
            iatt = &local->replies[i].poststat;
            if (iatt->ia_type == IA_IFDIR) {
                type = "dir";
            }

            if (i == healer->subvol) {
                if (local->replies[i].poststat.ia_nlink > 1) {
                    multiple_links = _gf_true;
                }
            }
        } else if (local->replies[i].op_errno != ENOENT &&
                   local->replies[i].op_errno != ESTALE) {
            /*We don't have complete view. Skip the entry*/
            gf_msg_debug(healer->this->name, local->replies[i].op_errno,
                         "Skipping cleanup of %s on %s", entry->d_name,
                         subvol->name);
            ret = 0;
            goto out;
        }
    }

    /*Inode is deleted from subvol*/
    if (count == 1 || (iatt->ia_type != IA_IFDIR && multiple_links)) {
        gf_msg(healer->this->name, GF_LOG_WARNING, 0,
               AFR_MSG_EXPUNGING_FILE_OR_DIR, "expunging %s %s/%s on %s", type,
               priv->anon_inode_name, entry->d_name, subvol->name);
        ret = afr_shd_entry_purge(subvol, parent->inode, entry->d_name,
                                  iatt->ia_type);
        if (ret == -ENOENT || ret == -ESTALE)
            ret = 0;
    } else if (count > 1) {
        loc_wipe(&loc);
        loc.parent = inode_ref(parent->inode);
        loc.name = entry->d_name;
        loc.inode = inode_new(parent->inode->table);
        if (!loc.inode) {
            ret = -ENOMEM;
            goto out;
        }
        AFR_ONLIST(local->child_up, frame, afr_selfheal_discover_cbk, lookup,
                   &loc, NULL);
        count = 0;
        for (i = 0; i < priv->child_count; i++) {
            if (local->replies[i].op_ret == 0) {
                count++;
                entry_present[i] = 1;
                iatt = &local->replies[i].poststat;
            } else if (local->replies[i].op_errno != ENOENT &&
                       local->replies[i].op_errno != ESTALE) {
                /*We don't have complete view. Skip the entry*/
                gf_msg_debug(healer->this->name, local->replies[i].op_errno,
                             "Skipping cleanup of %s on %s", entry->d_name,
                             subvol->name);
                ret = 0;
                goto out;
            }
        }
        for (i = 0; i < priv->child_count; i++) {
            if (gfid_present[i] && !entry_present[i]) {
                /*Entry is not anonymous on at least one subvol*/
                gf_msg_debug(healer->this->name, 0,
                             "Valid entry present on %s "
                             "Skipping cleanup of %s on %s",
                             priv->children[i]->name, entry->d_name,
                             subvol->name);
                ret = 0;
                goto out;
            }
        }

        gf_msg(healer->this->name, GF_LOG_WARNING, 0,
               AFR_MSG_EXPUNGING_FILE_OR_DIR,
               "expunging %s %s/%s on all subvols", type, priv->anon_inode_name,
               entry->d_name);
        ret = 0;
        for (i = 0; i < priv->child_count; i++) {
            op_errno = -afr_shd_entry_purge(priv->children[i], loc.parent,
                                            entry->d_name, iatt->ia_type);
            if (op_errno != ENOENT && op_errno != ESTALE) {
                ret |= -op_errno;
            }
        }
    }

out:
    if (frame)
        AFR_STACK_DESTROY(frame);
    loc_wipe(&loc);
    return ret;
}

static void
afr_cleanup_anon_inode_dir(struct subvol_healer *healer)
{
    int ret = 0;
    call_frame_t *frame = NULL;
    afr_private_t *priv = healer->this->private;
    loc_t loc = {0};

    ret = afr_anon_inode_create(healer->this, healer->subvol, &loc.inode);
    if (ret)
        goto out;

    frame = afr_frame_create(healer->this, &ret);
    if (!frame) {
        ret = -ret;
        goto out;
    }

    ret = syncop_mt_dir_scan(frame, priv->children[healer->subvol], &loc,
                             GF_CLIENT_PID_SELF_HEALD, healer,
                             afr_shd_anon_inode_cleaner, NULL,
                             priv->shd.max_threads, priv->shd.wait_qlength);
out:
    if (frame)
        AFR_STACK_DESTROY(frame);
    loc_wipe(&loc);
    return;
}

void *
afr_shd_index_healer(void *data)
{
    struct subvol_healer *healer = NULL;
    xlator_t *this = NULL;
    int ret = 0;
    afr_private_t *priv = NULL;
    dict_t *pre_crawl_xdata = NULL;
    loc_t loc = {
        0,
    };
    gf_lkowner_t lkowner;
    pid_t pid = GF_CLIENT_PID_SELF_HEALD;

    healer = data;
    THIS = this = healer->this;
    priv = this->private;

    syncopctx_setfspid(&pid);
    set_lk_owner_from_ptr(&lkowner, &lkowner);
    syncopctx_setfslkowner(&lkowner);

    for (;;) {
        afr_shd_healer_wait(healer);

        if (!afr_bricks_available_for_heal(priv))
            continue;

        ASSERT_LOCAL(this, healer);
        priv->local[healer->subvol] = healer->local;

        if (priv->thin_arbiter_count) {
            if (afr_shd_ta_needs_heal(this, healer))
                afr_shd_ta_get_xattrs(this, &loc, healer, &pre_crawl_xdata);
        }

        do {
            gf_msg_debug(this->name, 0, "starting index sweep on subvol %s",
                         afr_subvol_name(this, healer->subvol));

            afr_shd_sweep_prepare(healer);

            ret = afr_shd_index_sweep_all(healer);

            afr_shd_sweep_done(healer);
            /*
              As long as at least one gfid was
              healed, keep retrying. We may have
              just healed a directory and thereby
              created entries for other gfids which
              could not be healed thus far.
            */

            gf_msg_debug(this->name, 0, "finished index sweep on subvol %s",
                         afr_subvol_name(this, healer->subvol));
            /*
              Give a pause before retrying to avoid a busy loop
              in case the only entry in index is because of
              an ongoing I/O.
            */
            sleep(1);
        } while (ret > 0);

        if (ret == 0) {
            afr_cleanup_anon_inode_dir(healer);
        }

        if (ret == 0 && pre_crawl_xdata &&
            !healer->crawl_event.heal_failed_count) {
            afr_shd_ta_check_and_unset_xattrs(this, &loc, healer,
                                              pre_crawl_xdata);
        }

        if (pre_crawl_xdata) {
            dict_unref(pre_crawl_xdata);
            pre_crawl_xdata = NULL;
        }
    }

    return NULL;
}

void *
afr_shd_full_healer(void *data)
{
    struct subvol_healer *healer = NULL;
    xlator_t *this = NULL;
    int run = 0;

    healer = data;
    THIS = this = healer->this;

    for (;;) {
        pthread_mutex_lock(&healer->mutex);
        {
            run = __afr_shd_healer_wait(healer);
            if (!run)
                healer->running = _gf_false;
        }
        pthread_mutex_unlock(&healer->mutex);

        if (!run)
            break;

        ASSERT_LOCAL(this, healer);

        gf_msg(this->name, GF_LOG_INFO, 0, AFR_MSG_SELF_HEAL_INFO,
               "starting full sweep on subvol %s",
               afr_subvol_name(this, healer->subvol));

        afr_shd_sweep_prepare(healer);

        afr_shd_full_sweep(healer, this->itable->root);

        afr_shd_sweep_done(healer);

        gf_msg(this->name, GF_LOG_INFO, 0, AFR_MSG_SELF_HEAL_INFO,
               "finished full sweep on subvol %s",
               afr_subvol_name(this, healer->subvol));
    }

    return NULL;
}

int
afr_shd_healer_init(xlator_t *this, struct subvol_healer *healer)
{
    int ret = 0;

    ret = pthread_mutex_init(&healer->mutex, NULL);
    if (ret)
        goto out;

    ret = pthread_cond_init(&healer->cond, NULL);
    if (ret)
        goto out;

    healer->this = this;
    healer->running = _gf_false;
    healer->rerun = _gf_false;
    healer->local = _gf_false;
out:
    return ret;
}

int
afr_shd_healer_spawn(xlator_t *this, struct subvol_healer *healer,
                     void *(threadfn)(void *))
{
    int ret = 0;

    pthread_mutex_lock(&healer->mutex);
    {
        if (healer->running) {
            pthread_cond_signal(&healer->cond);
        } else {
            ret = gf_thread_create(&healer->thread, NULL, threadfn, healer,
                                   "shdheal");
            if (ret)
                goto unlock;
            healer->running = 1;
        }

        healer->rerun = 1;
    }
unlock:
    pthread_mutex_unlock(&healer->mutex);

    return ret;
}

int
afr_shd_full_healer_spawn(xlator_t *this, int subvol)
{
    return afr_shd_healer_spawn(this, NTH_FULL_HEALER(this, subvol),
                                afr_shd_full_healer);
}

int
afr_shd_index_healer_spawn(xlator_t *this, int subvol)
{
    return afr_shd_healer_spawn(this, NTH_INDEX_HEALER(this, subvol),
                                afr_shd_index_healer);
}

int
afr_shd_dict_add_crawl_event(xlator_t *this, dict_t *output,
                             crawl_event_t *crawl_event)
{
    int ret = 0;
    uint64_t count = 0;
    char key[128] = {0};
    int keylen = 0;
    char suffix[64] = {0};
    int xl_id = 0;
    uint64_t healed_count = 0;
    uint64_t split_brain_count = 0;
    uint64_t heal_failed_count = 0;
    char *start_time_str = 0;
    char *end_time_str = NULL;
    char *crawl_type = NULL;
    int progress = -1;
    int child = -1;

    child = crawl_event->child;
    healed_count = crawl_event->healed_count;
    split_brain_count = crawl_event->split_brain_count;
    heal_failed_count = crawl_event->heal_failed_count;
    crawl_type = crawl_event->crawl_type;

    if (!crawl_event->start_time)
        goto out;

    start_time_str = gf_strdup(ctime(&crawl_event->start_time));

    if (crawl_event->end_time)
        end_time_str = gf_strdup(ctime(&crawl_event->end_time));

    ret = dict_get_int32(output, this->name, &xl_id);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_GET_FAILED,
               "xl does not have id");
        goto out;
    }

    snprintf(key, sizeof(key), "statistics-%d-%d-count", xl_id, child);
    ret = dict_get_uint64(output, key, &count);

    snprintf(suffix, sizeof(suffix), "%d-%d-%" PRIu64, xl_id, child, count);
    snprintf(key, sizeof(key), "statistics_healed_cnt-%s", suffix);
    ret = dict_set_uint64(output, key, healed_count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_SET_FAILED,
               "Could not add statistics_healed_count to output");
        goto out;
    }

    snprintf(key, sizeof(key), "statistics_sb_cnt-%s", suffix);
    ret = dict_set_uint64(output, key, split_brain_count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_SET_FAILED,
               "Could not add statistics_split_brain_count to output");
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "statistics_crawl_type-%s", suffix);
    ret = dict_set_strn(output, key, keylen, crawl_type);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_SET_FAILED,
               "Could not add statistics_crawl_type to output");
        goto out;
    }

    snprintf(key, sizeof(key), "statistics_heal_failed_cnt-%s", suffix);
    ret = dict_set_uint64(output, key, heal_failed_count);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_SET_FAILED,
               "Could not add statistics_healed_failed_count to output");
        goto out;
    }

    keylen = snprintf(key, sizeof(key), "statistics_strt_time-%s", suffix);
    ret = dict_set_dynstrn(output, key, keylen, start_time_str);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_SET_FAILED,
               "Could not add statistics_crawl_start_time to output");
        goto out;
    } else {
        start_time_str = NULL;
    }

    if (!end_time_str)
        progress = 1;
    else
        progress = 0;

    keylen = snprintf(key, sizeof(key), "statistics_end_time-%s", suffix);
    if (!end_time_str)
        end_time_str = gf_strdup("Could not determine the end time");
    ret = dict_set_dynstrn(output, key, keylen, end_time_str);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_SET_FAILED,
               "Could not add statistics_crawl_end_time to output");
        goto out;
    } else {
        end_time_str = NULL;
    }

    keylen = snprintf(key, sizeof(key), "statistics_inprogress-%s", suffix);

    ret = dict_set_int32n(output, key, keylen, progress);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_SET_FAILED,
               "Could not add statistics_inprogress to output");
        goto out;
    }

    snprintf(key, sizeof(key), "statistics-%d-%d-count", xl_id, child);
    ret = dict_set_uint64(output, key, count + 1);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_SET_FAILED,
               "Could not increment the counter.");
        goto out;
    }
out:
    GF_FREE(start_time_str);
    GF_FREE(end_time_str);
    return ret;
}

int
afr_shd_dict_add_path(xlator_t *this, dict_t *output, int child, char *path,
                      struct timeval *tv)
{
    int ret = -1;
    uint64_t count = 0;
    char key[64] = {0};
    int keylen = 0;
    char xl_id_child_str[32] = {0};
    int xl_id = 0;

    ret = dict_get_int32(output, this->name, &xl_id);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_GET_FAILED,
               "xl does not have id");
        goto out;
    }

    snprintf(xl_id_child_str, sizeof(xl_id_child_str), "%d-%d", xl_id, child);
    snprintf(key, sizeof(key), "%s-count", xl_id_child_str);
    ret = dict_get_uint64(output, key, &count);

    keylen = snprintf(key, sizeof(key), "%s-%" PRIu64, xl_id_child_str, count);
    ret = dict_set_dynstrn(output, key, keylen, path);

    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_SET_FAILED,
               "%s: Could not add to output", path);
        goto out;
    }

    if (tv) {
        snprintf(key, sizeof(key), "%s-%" PRIu64 "-time", xl_id_child_str,
                 count);
        ret = dict_set_uint32(output, key, tv->tv_sec);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_SET_FAILED,
                   "%s: Could not set time", path);
            goto out;
        }
    }

    snprintf(key, sizeof(key), "%s-count", xl_id_child_str);

    ret = dict_set_uint64(output, key, count + 1);
    if (ret) {
        gf_msg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_SET_FAILED,
               "Could not increment count");
        goto out;
    }

    ret = 0;
out:
    return ret;
}

int
afr_add_shd_event(circular_buffer_t *cb, void *data)
{
    dict_t *output = NULL;
    xlator_t *this = THIS;
    afr_private_t *priv = NULL;
    afr_self_heald_t *shd = NULL;
    shd_event_t *shd_event = NULL;
    char *path = NULL;

    output = data;
    priv = this->private;
    shd = &priv->shd;
    shd_event = cb->data;

    if (!shd->index_healers[shd_event->child].local)
        return 0;

    path = gf_strdup(shd_event->path);
    if (!path)
        return -ENOMEM;

    afr_shd_dict_add_path(this, output, shd_event->child, path, &cb->tv);
    return 0;
}

int
afr_add_crawl_event(circular_buffer_t *cb, void *data)
{
    dict_t *output = NULL;
    xlator_t *this = THIS;
    afr_private_t *priv = NULL;
    afr_self_heald_t *shd = NULL;
    crawl_event_t *crawl_event = NULL;

    output = data;
    priv = this->private;
    shd = &priv->shd;
    crawl_event = cb->data;

    if (!shd->index_healers[crawl_event->child].local)
        return 0;

    afr_shd_dict_add_crawl_event(this, output, crawl_event);

    return 0;
}

int
afr_selfheal_daemon_init(xlator_t *this)
{
    afr_private_t *priv = NULL;
    afr_self_heald_t *shd = NULL;
    int ret = -1;
    int i = 0;

    priv = this->private;
    shd = &priv->shd;

    shd->index_healers = GF_CALLOC(sizeof(*shd->index_healers),
                                   priv->child_count,
                                   gf_afr_mt_subvol_healer_t);
    if (!shd->index_healers)
        goto out;

    for (i = 0; i < priv->child_count; i++) {
        shd->index_healers[i].subvol = i;
        ret = afr_shd_healer_init(this, &shd->index_healers[i]);
        if (ret)
            goto out;
    }

    shd->full_healers = GF_CALLOC(sizeof(*shd->full_healers), priv->child_count,
                                  gf_afr_mt_subvol_healer_t);
    if (!shd->full_healers)
        goto out;
    for (i = 0; i < priv->child_count; i++) {
        shd->full_healers[i].subvol = i;
        ret = afr_shd_healer_init(this, &shd->full_healers[i]);
        if (ret)
            goto out;
    }

    shd->split_brain = eh_new(AFR_EH_SPLIT_BRAIN_LIMIT, _gf_false,
                              afr_destroy_shd_event_data);
    if (!shd->split_brain)
        goto out;

    shd->statistics = GF_CALLOC(sizeof(eh_t *), priv->child_count,
                                gf_common_mt_eh_t);
    if (!shd->statistics)
        goto out;

    for (i = 0; i < priv->child_count; i++) {
        shd->statistics[i] = eh_new(AFR_STATISTICS_HISTORY_SIZE, _gf_false,
                                    afr_destroy_crawl_event_data);
        if (!shd->statistics[i])
            goto out;
        shd->full_healers[i].crawl_event.child = i;
        shd->full_healers[i].crawl_event.crawl_type = "FULL";
        shd->index_healers[i].crawl_event.child = i;
        shd->index_healers[i].crawl_event.crawl_type = "INDEX";
    }

    ret = 0;
out:
    return ret;
}

void
afr_selfheal_childup(xlator_t *this, afr_private_t *priv)
{
    int subvol = 0;

    if (!priv->shd.iamshd)
        return;
    for (subvol = 0; subvol < priv->child_count; subvol++)
        if (priv->child_up[subvol])
            afr_shd_index_healer_spawn(this, subvol);

    return;
}

int
afr_shd_get_index_count(xlator_t *this, int i, uint64_t *count)
{
    afr_private_t *priv = NULL;
    xlator_t *subvol = NULL;
    loc_t rootloc = {
        0,
    };
    dict_t *xattr = NULL;
    int ret = -1;

    priv = this->private;
    subvol = priv->children[i];

    rootloc.inode = inode_ref(this->itable->root);
    gf_uuid_copy(rootloc.gfid, rootloc.inode->gfid);

    ret = syncop_getxattr(subvol, &rootloc, &xattr, GF_XATTROP_INDEX_COUNT,
                          NULL, NULL);
    if (ret < 0)
        goto out;

    ret = dict_get_uint64(xattr, GF_XATTROP_INDEX_COUNT, count);
    if (ret)
        goto out;

    ret = 0;

out:
    if (xattr)
        dict_unref(xattr);
    loc_wipe(&rootloc);

    return ret;
}

int
afr_xl_op(xlator_t *this, dict_t *input, dict_t *output)
{
    gf_xl_afr_op_t op = GF_SHD_OP_INVALID;
    int ret = 0;
    int xl_id = 0;
    afr_private_t *priv = NULL;
    afr_self_heald_t *shd = NULL;
    struct subvol_healer *healer = NULL;
    int i = 0;
    char key[64];
    int keylen = 0;
    int this_name_len = 0;
    int op_ret = 0;
    uint64_t cnt = 0;

#define AFR_SET_DICT_AND_LOG(name, output, key, keylen, dict_str,              \
                             dict_str_len)                                     \
    {                                                                          \
        int ret;                                                               \
                                                                               \
        ret = dict_set_nstrn(output, key, keylen, dict_str, dict_str_len);     \
        if (ret) {                                                             \
            gf_smsg(name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_SET_FAILED,         \
                    "key=%s", key, "value=%s", dict_str, NULL);                \
        }                                                                      \
    }

    priv = this->private;
    shd = &priv->shd;

    ret = dict_get_int32_sizen(input, "xl-op", (int32_t *)&op);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_GET_FAILED,
                "key=xl-op", NULL);
        goto out;
    }
    this_name_len = strlen(this->name);
    ret = dict_get_int32n(input, this->name, this_name_len, &xl_id);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_GET_FAILED,
                "key=%s", this->name, NULL);
        goto out;
    }
    ret = dict_set_int32n(output, this->name, this_name_len, xl_id);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, AFR_MSG_DICT_SET_FAILED,
                "key=%s", this->name, NULL);
        goto out;
    }
    switch (op) {
        case GF_SHD_OP_HEAL_INDEX:
            op_ret = 0;

            for (i = 0; i < priv->child_count; i++) {
                healer = &shd->index_healers[i];
                keylen = snprintf(key, sizeof(key), "%d-%d-status", xl_id, i);

                if (!priv->child_up[i]) {
                    AFR_SET_DICT_AND_LOG(this->name, output, key, keylen,
                                         SBRICK_NOT_CONNECTED,
                                         SLEN(SBRICK_NOT_CONNECTED));
                    op_ret = -1;
                } else if (AFR_COUNT(priv->child_up, priv->child_count) < 2) {
                    AFR_SET_DICT_AND_LOG(this->name, output, key, keylen,
                                         SLESS_THAN2_BRICKS_in_REP,
                                         SLEN(SLESS_THAN2_BRICKS_in_REP));
                    op_ret = -1;
                } else if (!afr_shd_is_subvol_local(this, healer->subvol)) {
                    AFR_SET_DICT_AND_LOG(this->name, output, key, keylen,
                                         SBRICK_IS_REMOTE,
                                         SLEN(SBRICK_IS_REMOTE));
                } else {
                    AFR_SET_DICT_AND_LOG(this->name, output, key, keylen,
                                         SSTARTED_SELF_HEAL,
                                         SLEN(SSTARTED_SELF_HEAL));

                    ret = afr_shd_index_healer_spawn(this, i);

                    if (ret) {
                        gf_smsg(this->name, GF_LOG_ERROR, -ret,
                                AFR_MSG_HEALER_SPAWN_FAILED, NULL);
                    }
                }
            }
            break;
        case GF_SHD_OP_HEAL_FULL:
            op_ret = -1;

            for (i = 0; i < priv->child_count; i++) {
                healer = &shd->full_healers[i];
                keylen = snprintf(key, sizeof(key), "%d-%d-status", xl_id, i);

                if (!priv->child_up[i]) {
                    AFR_SET_DICT_AND_LOG(this->name, output, key, keylen,
                                         SBRICK_NOT_CONNECTED,
                                         SLEN(SBRICK_NOT_CONNECTED));
                } else if (AFR_COUNT(priv->child_up, priv->child_count) < 2) {
                    AFR_SET_DICT_AND_LOG(this->name, output, key, keylen,
                                         SLESS_THAN2_BRICKS_in_REP,
                                         SLEN(SLESS_THAN2_BRICKS_in_REP));
                } else if (!afr_shd_is_subvol_local(this, healer->subvol)) {
                    AFR_SET_DICT_AND_LOG(this->name, output, key, keylen,
                                         SBRICK_IS_REMOTE,
                                         SLEN(SBRICK_IS_REMOTE));
                } else {
                    AFR_SET_DICT_AND_LOG(this->name, output, key, keylen,
                                         SSTARTED_SELF_HEAL,
                                         SLEN(SSTARTED_SELF_HEAL));

                    ret = afr_shd_full_healer_spawn(this, i);

                    if (ret) {
                        gf_smsg(this->name, GF_LOG_ERROR, -ret,
                                AFR_MSG_HEALER_SPAWN_FAILED, NULL);
                    }
                    op_ret = 0;
                }
            }
            break;
        case GF_SHD_OP_INDEX_SUMMARY:
            /* this case has been handled in glfs-heal.c */
            break;
        case GF_SHD_OP_SPLIT_BRAIN_FILES:
            eh_dump(shd->split_brain, output, afr_add_shd_event);
            break;
        case GF_SHD_OP_STATISTICS:
            for (i = 0; i < priv->child_count; i++) {
                eh_dump(shd->statistics[i], output, afr_add_crawl_event);
                ret = afr_shd_dict_add_crawl_event(
                    this, output, &shd->index_healers[i].crawl_event);
                if (ret) {
                    gf_smsg(this->name, GF_LOG_ERROR, -ret,
                            AFR_MSG_ADD_CRAWL_EVENT_FAILED, NULL);
                }

                ret = afr_shd_dict_add_crawl_event(
                    this, output, &shd->full_healers[i].crawl_event);
                if (ret) {
                    gf_smsg(this->name, GF_LOG_ERROR, -ret,
                            AFR_MSG_ADD_CRAWL_EVENT_FAILED, NULL);
                }
            }
            break;
        case GF_SHD_OP_STATISTICS_HEAL_COUNT:
        case GF_SHD_OP_STATISTICS_HEAL_COUNT_PER_REPLICA:
            op_ret = -1;

            for (i = 0; i < priv->child_count; i++) {
                if (!priv->child_up[i]) {
                    keylen = snprintf(key, sizeof(key), "%d-%d-status", xl_id,
                                      i);
                    AFR_SET_DICT_AND_LOG(this->name, output, key, keylen,
                                         SBRICK_NOT_CONNECTED,
                                         SLEN(SBRICK_NOT_CONNECTED));
                } else {
                    snprintf(key, sizeof(key), "%d-%d-hardlinks", xl_id, i);
                    ret = afr_shd_get_index_count(this, i, &cnt);
                    if (ret == 0) {
                        ret = dict_set_uint64(output, key, cnt);
                    }
                    if (ret) {
                        gf_smsg(this->name, GF_LOG_ERROR, -ret,
                                AFR_MSG_DICT_SET_FAILED, NULL);
                    }
                    op_ret = 0;
                }
            }

            break;

        default:
            gf_smsg(this->name, GF_LOG_ERROR, 0, AFR_MSG_INVALID_ARG, "op=%d",
                    op, NULL);
            break;
    }
out:
    dict_deln(output, this->name, this_name_len);
    return op_ret;

#undef AFR_SET_DICT_AND_LOG
}
