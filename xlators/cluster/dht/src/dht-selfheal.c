/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "dht-lock.h"

#define DHT_SET_LAYOUT_RANGE(layout, i, srt, chunk, path)                      \
    do {                                                                       \
        layout->list[i].start = srt;                                           \
        layout->list[i].stop = srt + chunk - 1;                                \
        layout->list[i].commit_hash = layout->commit_hash;                     \
                                                                               \
        gf_msg_trace(this->name, 0,                                            \
                     "gave fix: 0x%x - 0x%x, with commit-hash 0x%x"            \
                     " on %s for %s",                                          \
                     layout->list[i].start, layout->list[i].stop,              \
                     layout->list[i].commit_hash,                              \
                     layout->list[i].xlator->name, path);                      \
    } while (0)

#define DHT_RESET_LAYOUT_RANGE(layout)                                         \
    do {                                                                       \
        int cnt = 0;                                                           \
        for (cnt = 0; cnt < layout->cnt; cnt++) {                              \
            layout->list[cnt].start = 0;                                       \
            layout->list[cnt].stop = 0;                                        \
        }                                                                      \
    } while (0)

static int
dht_selfheal_layout_lock(call_frame_t *frame, dht_layout_t *layout,
                         gf_boolean_t newdir, dht_selfheal_layout_t healer,
                         dht_need_heal_t should_heal);

static uint32_t
dht_overlap_calc(dht_layout_t *old, int o, dht_layout_t *new, int n)
{
    if (o >= old->cnt || n >= new->cnt)
        return 0;

    if (old->list[o].err > 0 || new->list[n].err > 0)
        return 0;

    if (old->list[o].start == old->list[o].stop) {
        return 0;
    }

    if (new->list[n].start == new->list[n].stop) {
        return 0;
    }

    if ((old->list[o].start > new->list[n].stop) ||
        (old->list[o].stop < new->list[n].start))
        return 0;

    return min(old->list[o].stop, new->list[n].stop) -
           max(old->list[o].start, new->list[n].start) + 1;
}

int
dht_selfheal_unlock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    DHT_STACK_DESTROY(frame);
    return 0;
}

int
dht_selfheal_dir_finish(call_frame_t *frame, xlator_t *this, int ret,
                        int invoke_cbk)
{
    dht_local_t *local = NULL, *lock_local = NULL;
    call_frame_t *lock_frame = NULL;
    int lock_count = 0;

    local = frame->local;

    /* Unlock entrylk */
    dht_unlock_entrylk_wrapper(frame, &local->lock[0].ns.directory_ns);

    /* Unlock inodelk */
    lock_count = dht_lock_count(local->lock[0].ns.parent_layout.locks,
                                local->lock[0].ns.parent_layout.lk_count);
    if (lock_count == 0)
        goto done;

    lock_frame = copy_frame(frame);
    if (lock_frame == NULL) {
        goto done;
    }

    lock_local = dht_local_init(lock_frame, &local->loc, NULL,
                                lock_frame->root->op);
    if (lock_local == NULL) {
        goto done;
    }

    lock_local->lock[0].ns.parent_layout.locks = local->lock[0]
                                                     .ns.parent_layout.locks;
    lock_local->lock[0]
        .ns.parent_layout.lk_count = local->lock[0].ns.parent_layout.lk_count;

    local->lock[0].ns.parent_layout.locks = NULL;
    local->lock[0].ns.parent_layout.lk_count = 0;

    dht_unlock_inodelk(lock_frame, lock_local->lock[0].ns.parent_layout.locks,
                       lock_local->lock[0].ns.parent_layout.lk_count,
                       dht_selfheal_unlock_cbk);
    lock_frame = NULL;

done:
    if (invoke_cbk)
        local->selfheal.dir_cbk(frame, NULL, frame->this, ret, local->op_errno,
                                NULL);
    if (lock_frame != NULL) {
        DHT_STACK_DESTROY(lock_frame);
    }

    return 0;
}

static int
dht_refresh_layout_done(call_frame_t *frame)
{
    dht_layout_t *refreshed = NULL, *heal = NULL;
    dht_local_t *local = NULL;
    dht_need_heal_t should_heal = NULL;
    dht_selfheal_layout_t healer = NULL;

    local = frame->local;

    refreshed = local->selfheal.refreshed_layout;
    heal = local->selfheal.layout;

    healer = local->selfheal.healer;
    should_heal = local->selfheal.should_heal;

    dht_layout_sort(refreshed);

    if (should_heal(frame, &heal, &refreshed)) {
        healer(frame, &local->loc, heal);
    } else {
        local->selfheal.layout = NULL;
        local->selfheal.refreshed_layout = NULL;
        local->selfheal.layout = refreshed;

        dht_layout_unref(heal);

        dht_selfheal_dir_finish(frame, frame->this, 0, 1);
    }

    return 0;
}

int
dht_refresh_layout_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno, inode_t *inode,
                       struct iatt *stbuf, dict_t *xattr,
                       struct iatt *postparent)
{
    dht_local_t *local = NULL;
    int this_call_cnt = 0;
    xlator_t *prev = NULL;
    dht_layout_t *layout = NULL;
    char gfid[GF_UUID_BUF_SIZE] = {
        0,
    };

    GF_VALIDATE_OR_GOTO("dht", frame, err);
    GF_VALIDATE_OR_GOTO("dht", this, err);
    GF_VALIDATE_OR_GOTO("dht", frame->local, err);
    GF_VALIDATE_OR_GOTO("dht", this->private, err);

    local = frame->local;
    prev = cookie;

    layout = local->selfheal.refreshed_layout;

    LOCK(&frame->lock);
    {
        op_ret = dht_layout_merge(this, layout, prev, op_ret, op_errno, xattr);

        dht_iatt_merge(this, &local->stbuf, stbuf);

        if (op_ret == -1) {
            gf_uuid_unparse(local->loc.gfid, gfid);
            local->op_errno = op_errno;
            gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                    DHT_MSG_FILE_LOOKUP_FAILED, "path=%s", local->loc.path,
                    "name=%s", prev->name, "gfid=%s", gfid, NULL);

            goto unlock;
        }

        local->op_ret = 0;
    }
unlock:
    UNLOCK(&frame->lock);

    this_call_cnt = dht_frame_return(frame);

    if (is_last_call(this_call_cnt)) {
        if (local->op_ret == 0) {
            local->refresh_layout_done(frame);
        } else {
            goto err;
        }
    }

    return 0;

err:
    if (local) {
        local->refresh_layout_unlock(frame, this, -1, 1);
    }
    return 0;
}

int
dht_refresh_layout(call_frame_t *frame)
{
    int call_cnt = 0;
    int i = 0, ret = -1;
    dht_conf_t *conf = NULL;
    dht_local_t *local = NULL;
    xlator_t *this = NULL;
    char gfid[GF_UUID_BUF_SIZE] = {
        0,
    };

    GF_VALIDATE_OR_GOTO("dht", frame, out);
    GF_VALIDATE_OR_GOTO("dht", frame->local, out);

    this = frame->this;
    conf = this->private;
    local = frame->local;

    call_cnt = conf->subvolume_cnt;
    local->call_cnt = call_cnt;
    local->op_ret = -1;

    if (local->selfheal.refreshed_layout) {
        dht_layout_unref(local->selfheal.refreshed_layout);
        local->selfheal.refreshed_layout = NULL;
    }

    local->selfheal.refreshed_layout = dht_layout_new(this,
                                                      conf->subvolume_cnt);
    if (!local->selfheal.refreshed_layout) {
        gf_uuid_unparse(local->loc.gfid, gfid);
        gf_smsg(this->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_MEM_ALLOC_FAILED,
                "path=%s", local->loc.path, "gfid=%s", gfid, NULL);
        goto out;
    }

    if (local->xattr != NULL) {
        dict_del(local->xattr, conf->xattr_name);
    }

    if (local->xattr_req == NULL) {
        gf_uuid_unparse(local->loc.gfid, gfid);
        local->xattr_req = dict_new();
        if (local->xattr_req == NULL) {
            gf_smsg(this->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_NO_MEMORY,
                    "path=%s", local->loc.path, "gfid=%s", gfid, NULL);
            goto out;
        }
    }

    if (dict_get(local->xattr_req, conf->xattr_name) == 0) {
        ret = dict_set_uint32(local->xattr_req, conf->xattr_name, 4 * 4);
        if (ret)
            gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_DICT_SET_FAILED,
                    "path=%s", local->loc.path, "key=%s", conf->xattr_name,
                    NULL);
    }

    for (i = 0; i < call_cnt; i++) {
        STACK_WIND_COOKIE(frame, dht_refresh_layout_cbk, conf->subvolumes[i],
                          conf->subvolumes[i],
                          conf->subvolumes[i]->fops->lookup, &local->loc,
                          local->xattr_req);
    }

    return 0;

out:
    if (local) {
        local->refresh_layout_unlock(frame, this, -1, 1);
    }
    return 0;
}

int32_t
dht_selfheal_layout_lock_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;

    local = frame->local;

    if (!local) {
        goto err;
    }

    if (op_ret < 0) {
        local->op_errno = op_errno;
        goto err;
    }

    local->refresh_layout_unlock = dht_selfheal_dir_finish;
    local->refresh_layout_done = dht_refresh_layout_done;

    dht_refresh_layout(frame);
    return 0;

err:
    dht_selfheal_dir_finish(frame, this, -1, 1);
    return 0;
}

gf_boolean_t
dht_should_heal_layout(call_frame_t *frame, dht_layout_t **heal,
                       dht_layout_t **ondisk)
{
    gf_boolean_t fixit = _gf_true;
    dht_local_t *local = NULL;
    int heal_missing_dirs = 0;

    local = frame->local;

    if ((heal == NULL) || (*heal == NULL) || (ondisk == NULL) ||
        (*ondisk == NULL))
        goto out;

    dht_layout_anomalies(
        frame->this, &local->loc, *ondisk, &local->selfheal.hole_cnt,
        &local->selfheal.overlaps_cnt, &local->selfheal.missing_cnt,
        &local->selfheal.down, &local->selfheal.misc, NULL);

    /* Directories might've been created as part of this self-heal. We've to
     * sync non-layout xattrs and set range 0-0 on new directories
     */
    heal_missing_dirs = local->selfheal.force_mkdir
                            ? local->selfheal.force_mkdir
                            : dht_layout_missing_dirs(*heal);

    if ((local->selfheal.hole_cnt == 0) &&
        (local->selfheal.overlaps_cnt == 0) && heal_missing_dirs) {
        dht_layout_t *tmp = NULL;

        /* Just added a brick and need to set 0-0 range on this brick.
         * But ondisk layout is well-formed. So, swap layouts "heal" and
         * "ondisk". Now "ondisk" layout will be used for healing
         * xattrs. If there are any non-participating subvols in
         * "ondisk" layout, dht_selfheal_dir_xattr_persubvol will set
         * 0-0 and non-layout xattrs. This way we won't end up in
         * "corrupting" already set and well-formed "ondisk" layout.
         */
        tmp = *heal;
        *heal = *ondisk;
        *ondisk = tmp;

        /* Current selfheal code, heals non-layout xattrs only after
         * an add-brick. In fact non-layout xattrs are considered as
         * secondary citizens which are healed only if layout xattrs
         * need to be healed. This is wrong, since for eg., quota can be
         * set when layout is well-formed, but a node is down. Also,
         * just for healing non-layout xattrs, we don't need locking.
         * This issue is _NOT FIXED_ by this patch.
         */
    }

    fixit = (local->selfheal.hole_cnt || local->selfheal.overlaps_cnt ||
             heal_missing_dirs);

out:
    return fixit;
}

int
dht_layout_span(dht_layout_t *layout)
{
    int i = 0, count = 0;

    for (i = 0; i < layout->cnt; i++) {
        if (layout->list[i].err)
            continue;

        if (layout->list[i].start != layout->list[i].stop)
            count++;
    }

    return count;
}

int
dht_decommissioned_bricks_in_layout(xlator_t *this, dht_layout_t *layout)
{
    dht_conf_t *conf = NULL;
    int count = 0, i = 0, j = 0;

    if ((this == NULL) || (layout == NULL))
        goto out;

    conf = this->private;

    for (i = 0; i < layout->cnt; i++) {
        for (j = 0; j < conf->subvolume_cnt; j++) {
            if (conf->decommissioned_bricks[j] &&
                conf->decommissioned_bricks[j] == layout->list[i].xlator) {
                count++;
            }
        }
    }

out:
    return count;
}

dht_distribution_type_t
dht_distribution_type(xlator_t *this, dht_layout_t *layout)
{
    dht_distribution_type_t type = GF_DHT_EQUAL_DISTRIBUTION;
    int i = 0;
    uint32_t start_range = 0, range = 0, diff = 0;

    if ((this == NULL) || (layout == NULL) || (layout->cnt < 1)) {
        goto out;
    }

    for (i = 0; i < layout->cnt; i++) {
        if (start_range == 0) {
            start_range = layout->list[i].stop - layout->list[i].start;
            continue;
        }

        range = layout->list[i].stop - layout->list[i].start;
        diff = (range >= start_range) ? range - start_range
                                      : start_range - range;

        if ((range != 0) && (diff > layout->cnt)) {
            type = GF_DHT_WEIGHTED_DISTRIBUTION;
            break;
        }
    }

out:
    return type;
}

gf_boolean_t
dht_should_fix_layout(call_frame_t *frame, dht_layout_t **inmem,
                      dht_layout_t **ondisk)
{
    gf_boolean_t fixit = _gf_true;

    dht_local_t *local = NULL;
    int layout_span = 0;
    int decommissioned_bricks = 0;
    dht_conf_t *conf = NULL;
    dht_distribution_type_t inmem_dist_type = 0;
    dht_distribution_type_t ondisk_dist_type = 0;

    conf = frame->this->private;

    local = frame->local;

    if ((inmem == NULL) || (*inmem == NULL) || (ondisk == NULL) ||
        (*ondisk == NULL))
        goto out;

    dht_layout_anomalies(frame->this, &local->loc, *ondisk,
                         &local->selfheal.hole_cnt,
                         &local->selfheal.overlaps_cnt, NULL,
                         &local->selfheal.down, &local->selfheal.misc, NULL);

    if (local->selfheal.down || local->selfheal.misc) {
        fixit = _gf_false;
        goto out;
    }

    if (local->selfheal.hole_cnt || local->selfheal.overlaps_cnt)
        goto out;

    /* If commit hashes are being updated, let it through */
    if ((*inmem)->commit_hash != (*ondisk)->commit_hash)
        goto out;

    layout_span = dht_layout_span(*ondisk);

    decommissioned_bricks = dht_decommissioned_bricks_in_layout(frame->this,
                                                                *ondisk);
    inmem_dist_type = dht_distribution_type(frame->this, *inmem);
    ondisk_dist_type = dht_distribution_type(frame->this, *ondisk);

    if ((decommissioned_bricks == 0) &&
        (layout_span ==
         (conf->subvolume_cnt - conf->decommission_subvols_cnt)) &&
        (inmem_dist_type == ondisk_dist_type))
        fixit = _gf_false;

out:

    return fixit;
}

static int
dht_selfheal_layout_lock(call_frame_t *frame, dht_layout_t *layout,
                         gf_boolean_t newdir, dht_selfheal_layout_t healer,
                         dht_need_heal_t should_heal)
{
    dht_local_t *local = NULL;
    int count = 1, ret = -1, i = 0;
    dht_lock_t **lk_array = NULL;
    dht_conf_t *conf = NULL;
    dht_layout_t *tmp = NULL;
    char gfid[GF_UUID_BUF_SIZE] = {0};

    GF_VALIDATE_OR_GOTO("dht", frame, err);
    GF_VALIDATE_OR_GOTO(frame->this->name, frame->local, err);

    local = frame->local;

    conf = frame->this->private;

    local->selfheal.healer = healer;
    local->selfheal.should_heal = should_heal;

    tmp = local->selfheal.layout;
    local->selfheal.layout = dht_layout_ref(layout);
    dht_layout_unref(tmp);

    if (!newdir) {
        count = conf->subvolume_cnt;

        lk_array = GF_CALLOC(count, sizeof(*lk_array), gf_common_mt_char);
        if (lk_array == NULL) {
            gf_uuid_unparse(local->stbuf.ia_gfid, gfid);
            gf_smsg("dht", GF_LOG_ERROR, ENOMEM, DHT_MSG_MEM_ALLOC_FAILED,
                    "lk_array-gfid=%s", gfid, "path=%s", local->loc.path, NULL);
            goto err;
        }

        for (i = 0; i < count; i++) {
            lk_array[i] = dht_lock_new(
                frame->this, conf->subvolumes[i], &local->loc, F_WRLCK,
                DHT_LAYOUT_HEAL_DOMAIN, NULL, FAIL_ON_ANY_ERROR);
            if (lk_array[i] == NULL) {
                gf_uuid_unparse(local->stbuf.ia_gfid, gfid);
                gf_smsg(THIS->name, GF_LOG_ERROR, ENOMEM,
                        DHT_MSG_MEM_ALLOC_FAILED, "lk_array-gfid=%s", gfid,
                        "path=%s", local->loc.path, NULL);
                goto err;
            }
        }
    } else {
        count = 1;
        lk_array = GF_CALLOC(count, sizeof(*lk_array), gf_common_mt_char);
        if (lk_array == NULL) {
            gf_uuid_unparse(local->stbuf.ia_gfid, gfid);
            gf_smsg(THIS->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_MEM_ALLOC_FAILED,
                    "lk_array-gfid=%s", gfid, "path=%s", local->loc.path, NULL);
            goto err;
        }

        lk_array[0] = dht_lock_new(frame->this, local->hashed_subvol,
                                   &local->loc, F_WRLCK, DHT_LAYOUT_HEAL_DOMAIN,
                                   NULL, FAIL_ON_ANY_ERROR);
        if (lk_array[0] == NULL) {
            gf_uuid_unparse(local->stbuf.ia_gfid, gfid);
            gf_smsg(THIS->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_MEM_ALLOC_FAILED,
                    "lk_array-gfid=%s", gfid, "path=%s", local->loc.path, NULL);
            goto err;
        }
    }

    local->lock[0].layout.my_layout.locks = lk_array;
    local->lock[0].layout.my_layout.lk_count = count;

    ret = dht_blocking_inodelk(frame, lk_array, count,
                               dht_selfheal_layout_lock_cbk);
    if (ret < 0) {
        local->lock[0].layout.my_layout.locks = NULL;
        local->lock[0].layout.my_layout.lk_count = 0;
        goto err;
    }

    return 0;
err:
    if (lk_array != NULL) {
        dht_lock_array_free(lk_array, count);
        GF_FREE(lk_array);
    }

    return -1;
}

static int
dht_selfheal_dir_xattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                           int op_ret, int op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;
    xlator_t *subvol = NULL;
    struct iatt *stbuf = NULL;
    int i = 0;
    int ret = 0;
    dht_layout_t *layout = NULL;
    int err = 0;
    int this_call_cnt = 0;
    char gfid[GF_UUID_BUF_SIZE] = {0};

    local = frame->local;
    layout = local->selfheal.layout;
    subvol = cookie;

    if (op_ret == 0) {
        err = 0;
    } else {
        gf_uuid_unparse(local->loc.gfid, gfid);
        gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                DHT_MSG_DIR_SELFHEAL_XATTR_FAILED, "name=%s", subvol->name,
                "path=%s", local->loc.path, "gfid=%s", gfid, NULL);
        err = op_errno;
    }

    ret = dict_get_bin(xdata, DHT_IATT_IN_XDATA_KEY, (void **)&stbuf);
    if (ret < 0) {
        gf_uuid_unparse(local->loc.gfid, gfid);
        gf_msg_debug(this->name, 0,
                     "key = %s not present in dict"
                     ", path:%s gfid:%s",
                     DHT_IATT_IN_XDATA_KEY, local->loc.path, gfid);
    }

    for (i = 0; i < layout->cnt; i++) {
        if (layout->list[i].xlator == subvol) {
            layout->list[i].err = err;
            break;
        }
    }

    LOCK(&frame->lock);
    {
        dht_iatt_merge(this, &local->stbuf, stbuf);
    }
    UNLOCK(&frame->lock);

    this_call_cnt = dht_frame_return(frame);

    if (is_last_call(this_call_cnt)) {
        dht_selfheal_dir_finish(frame, this, 0, 1);
    }

    return 0;
}

/* Code is required to set user xattr to local->xattr
 */
int
dht_set_user_xattr(dict_t *dict, char *k, data_t *v, void *data)
{
    dict_t *set_xattr = data;
    int ret = -1;

    ret = dict_set(set_xattr, k, v);
    return ret;
}

static int
dht_selfheal_dir_xattr_persubvol(call_frame_t *frame, loc_t *loc,
                                 dht_layout_t *layout, int i,
                                 xlator_t *req_subvol)
{
    xlator_t *subvol = NULL;
    dict_t *xattr = NULL;
    dict_t *xdata = NULL;
    int ret = 0;
    xlator_t *this = NULL;
    int32_t *disk_layout = NULL;
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    data_t *data = NULL;
    char gfid[GF_UUID_BUF_SIZE] = {0};

    local = frame->local;
    if (req_subvol)
        subvol = req_subvol;
    else
        subvol = layout->list[i].xlator;
    this = frame->this;

    GF_VALIDATE_OR_GOTO("", this, err);
    GF_VALIDATE_OR_GOTO(this->name, layout, err);
    GF_VALIDATE_OR_GOTO(this->name, local, err);
    GF_VALIDATE_OR_GOTO(this->name, subvol, err);
    VALIDATE_OR_GOTO(this->private, err);

    conf = this->private;

    xattr = dict_new();
    if (!xattr) {
        goto err;
    }

    xdata = dict_new();
    if (!xdata)
        goto err;

    ret = dict_set_str(xdata, GLUSTERFS_INTERNAL_FOP_KEY, "yes");
    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_DICT_SET_FAILED,
                "path=%s", loc->path, "key=%s", GLUSTERFS_INTERNAL_FOP_KEY,
                "gfid=%s", gfid, NULL);
        goto err;
    }

    ret = dict_set_int8(xdata, DHT_IATT_IN_XDATA_KEY, 1);
    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_DICT_SET_FAILED,
                "path=%s", loc->path, "key=%s", DHT_IATT_IN_XDATA_KEY,
                "gfid=%s", gfid, NULL);
        goto err;
    }

    gf_uuid_unparse(loc->inode->gfid, gfid);

    ret = dht_disk_layout_extract(this, layout, i, &disk_layout);
    if (ret == -1) {
        gf_smsg(this->name, GF_LOG_WARNING, 0,
                DHT_MSG_DIR_SELFHEAL_XATTR_FAILED,
                "extract-disk-layout-failed, path=%s", loc->path, "subvol=%s",
                subvol->name, "gfid=%s", gfid, NULL);
        goto err;
    }

    ret = dict_set_bin(xattr, conf->xattr_name, disk_layout, 4 * 4);
    if (ret == -1) {
        gf_smsg(this->name, GF_LOG_WARNING, 0,
                DHT_MSG_DIR_SELFHEAL_XATTR_FAILED, "path=%s", loc->path,
                "subvol=%s", subvol->name,
                "set-xattr-dictionary-failed"
                "gfid=%s",
                gfid, NULL);
        goto err;
    }
    disk_layout = NULL;

    gf_msg_trace(this->name, 0,
                 "setting hash range 0x%x - 0x%x (type %d) on subvolume %s"
                 " for %s",
                 layout->list[i].start, layout->list[i].stop, layout->type,
                 subvol->name, loc->path);

    if (local->xattr) {
        data = dict_get(local->xattr, QUOTA_LIMIT_KEY);
        if (data) {
            ret = dict_add(xattr, QUOTA_LIMIT_KEY, data);
            if (ret) {
                gf_smsg(this->name, GF_LOG_ERROR, 0, DHT_MSG_DICT_SET_FAILED,
                        "path=%s", loc->path, "key=%s", QUOTA_LIMIT_KEY, NULL);
            }
        }
        data = dict_get(local->xattr, QUOTA_LIMIT_OBJECTS_KEY);
        if (data) {
            ret = dict_add(xattr, QUOTA_LIMIT_OBJECTS_KEY, data);
            if (ret) {
                gf_smsg(this->name, GF_LOG_ERROR, 0, DHT_MSG_DICT_SET_FAILED,
                        "path=%s", loc->path, "key=%s", QUOTA_LIMIT_OBJECTS_KEY,
                        NULL);
            }
        }
    }

    if (!gf_uuid_is_null(local->gfid))
        gf_uuid_copy(loc->gfid, local->gfid);

    STACK_WIND_COOKIE(frame, dht_selfheal_dir_xattr_cbk, (void *)subvol, subvol,
                      subvol->fops->setxattr, loc, xattr, 0, xdata);

    dict_unref(xattr);
    dict_unref(xdata);

    return 0;

err:
    if (xattr)
        dict_unref(xattr);
    if (xdata)
        dict_unref(xdata);

    GF_FREE(disk_layout);

    dht_selfheal_dir_xattr_cbk(frame, (void *)subvol, frame->this, -1, ENOMEM,
                               NULL);
    return 0;
}

static int
dht_fix_dir_xattr(call_frame_t *frame, loc_t *loc, dht_layout_t *layout)
{
    dht_local_t *local = NULL;
    int i = 0;
    int count = 0;
    xlator_t *this = NULL;
    dht_conf_t *conf = NULL;
    dht_layout_t *dummy = NULL;

    local = frame->local;
    this = frame->this;
    conf = this->private;

    gf_msg_debug(this->name, 0, "%s: Writing the new range for all subvolumes",
                 loc->path);

    local->call_cnt = count = conf->subvolume_cnt;

    if (gf_log_get_loglevel() >= GF_LOG_DEBUG)
        dht_log_new_layout_for_dir_selfheal(this, loc, layout);

    for (i = 0; i < layout->cnt; i++) {
        dht_selfheal_dir_xattr_persubvol(frame, loc, layout, i, NULL);

        if (--count == 0)
            goto out;
    }
    /* if we are here, subvolcount > layout_count. subvols-per-directory
     * option might be set here. We need to clear out layout from the
     * non-participating subvolumes, else it will result in overlaps */
    dummy = dht_layout_new(this, 1);
    if (!dummy)
        goto out;
    dummy->commit_hash = layout->commit_hash;
    for (i = 0; i < conf->subvolume_cnt; i++) {
        if (_gf_false == dht_is_subvol_in_layout(layout, conf->subvolumes[i])) {
            dht_selfheal_dir_xattr_persubvol(frame, loc, dummy, 0,
                                             conf->subvolumes[i]);
            if (--count == 0)
                break;
        }
    }

    dht_layout_unref(dummy);
out:
    return 0;
}

static int
dht_selfheal_dir_xattr(call_frame_t *frame, loc_t *loc, dht_layout_t *layout)
{
    dht_local_t *local = NULL;
    int missing_xattr = 0;
    int i = 0;
    xlator_t *this = NULL;
    dht_conf_t *conf = NULL;
    dht_layout_t *dummy = NULL;
    char gfid[GF_UUID_BUF_SIZE] = {
        0,
    };

    local = frame->local;
    this = frame->this;
    conf = this->private;

    for (i = 0; i < layout->cnt; i++) {
        if (layout->list[i].err != -1 || !layout->list[i].stop) {
            /* err != -1 would mean xattr present on the directory
             * or the directory is non existent.
             * !layout->list[i].stop would mean layout absent
             */

            continue;
        }
        missing_xattr++;
    }
    /* Also account for subvolumes with no-layout. Used for zero'ing out
     * the layouts and for setting quota key's if present */
    for (i = 0; i < conf->subvolume_cnt; i++) {
        if (_gf_false == dht_is_subvol_in_layout(layout, conf->subvolumes[i])) {
            missing_xattr++;
        }
    }
    gf_msg_trace(this->name, 0, "%d subvolumes missing xattr for %s",
                 missing_xattr, loc->path);

    if (missing_xattr == 0) {
        dht_selfheal_dir_finish(frame, this, 0, 1);
        return 0;
    }

    local->call_cnt = missing_xattr;

    if (gf_log_get_loglevel() >= GF_LOG_DEBUG)
        dht_log_new_layout_for_dir_selfheal(this, loc, layout);

    for (i = 0; i < layout->cnt; i++) {
        if (layout->list[i].err != -1 || !layout->list[i].stop)
            continue;

        dht_selfheal_dir_xattr_persubvol(frame, loc, layout, i, NULL);

        if (--missing_xattr == 0)
            break;
    }
    dummy = dht_layout_new(this, 1);
    if (!dummy) {
        gf_uuid_unparse(loc->gfid, gfid);
        gf_smsg(this->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_DUMMY_ALLOC_FAILED,
                "path=%s", loc->path, "gfid=%s", gfid, NULL);
        goto out;
    }
    for (i = 0; i < conf->subvolume_cnt && missing_xattr; i++) {
        if (_gf_false == dht_is_subvol_in_layout(layout, conf->subvolumes[i])) {
            dht_selfheal_dir_xattr_persubvol(frame, loc, dummy, 0,
                                             conf->subvolumes[i]);
            missing_xattr--;
        }
    }

    dht_layout_unref(dummy);
out:
    return 0;
}

int
dht_selfheal_dir_setattr_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                             int op_ret, int op_errno, struct iatt *statpre,
                             struct iatt *statpost, dict_t *xdata)
{
    dht_local_t *local = NULL;
    dht_layout_t *layout = NULL;
    int this_call_cnt = 0, ret = -1;

    local = frame->local;
    layout = local->selfheal.layout;

    this_call_cnt = dht_frame_return(frame);

    if (is_last_call(this_call_cnt)) {
        if (!local->heal_layout) {
            gf_msg_trace(this->name, 0, "Skip heal layout for %s gfid = %s ",
                         local->loc.path, uuid_utoa(local->gfid));

            dht_selfheal_dir_finish(frame, this, 0, 1);
            return 0;
        }
        ret = dht_selfheal_layout_lock(frame, layout, _gf_false,
                                       dht_selfheal_dir_xattr,
                                       dht_should_heal_layout);

        if (ret < 0) {
            dht_selfheal_dir_finish(frame, this, -1, 1);
        }
    }

    return 0;
}

int
dht_selfheal_dir_setattr(call_frame_t *frame, loc_t *loc, struct iatt *stbuf,
                         int32_t valid, dht_layout_t *layout)
{
    int missing_attr = 0;
    int i = 0, ret = -1;
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    xlator_t *this = NULL;
    int cnt = 0;

    local = frame->local;
    this = frame->this;
    conf = this->private;

    /* We need to heal the attrs if:
     * 1. Any directories were missing - the newly created dirs will need
     *    to have the correct attrs set
     * 2. An existing dir does not have the correct permissions -they may
     *    have been changed when a brick was down.
     */

    for (i = 0; i < layout->cnt; i++) {
        if (layout->list[i].err == -1)
            missing_attr++;
    }

    if ((missing_attr == 0) && (local->need_attrheal == 0)) {
        if (!local->heal_layout) {
            gf_msg_trace(this->name, 0, "Skip heal layout for %s gfid = %s ",
                         loc->path, uuid_utoa(loc->gfid));
            dht_selfheal_dir_finish(frame, this, 0, 1);
            return 0;
        }
        ret = dht_selfheal_layout_lock(frame, layout, _gf_false,
                                       dht_selfheal_dir_xattr,
                                       dht_should_heal_layout);

        if (ret < 0) {
            dht_selfheal_dir_finish(frame, this, -1, 1);
        }

        return 0;
    }

    cnt = local->call_cnt = conf->subvolume_cnt;

    for (i = 0; i < cnt; i++) {
        STACK_WIND(frame, dht_selfheal_dir_setattr_cbk, layout->list[i].xlator,
                   layout->list[i].xlator->fops->setattr, loc, stbuf, valid,
                   NULL);
    }

    return 0;
}

static int
dht_selfheal_dir_mkdir_cbk(call_frame_t *frame, void *cookie, xlator_t *this,
                           int op_ret, int op_errno, inode_t *inode,
                           struct iatt *stbuf, struct iatt *preparent,
                           struct iatt *postparent, dict_t *xdata)
{
    dht_local_t *local = NULL;
    dht_layout_t *layout = NULL;
    xlator_t *prev = NULL;
    xlator_t *subvol = NULL;
    int i = 0, ret = -1;
    int this_call_cnt = 0;
    char gfid[GF_UUID_BUF_SIZE] = {0};

    local = frame->local;
    layout = local->selfheal.layout;
    prev = cookie;
    subvol = prev;

    if ((op_ret == 0) || ((op_ret == -1) && (op_errno == EEXIST))) {
        for (i = 0; i < layout->cnt; i++) {
            if (layout->list[i].xlator == subvol) {
                layout->list[i].err = -1;
                break;
            }
        }
    }

    if (op_ret) {
        gf_uuid_unparse(local->loc.gfid, gfid);
        gf_smsg(this->name,
                ((op_errno == EEXIST) ? GF_LOG_DEBUG : GF_LOG_WARNING),
                op_errno, DHT_MSG_DIR_SELFHEAL_FAILED, "path=%s",
                local->loc.path, "gfid=%s", gfid, NULL);
        goto out;
    }
    dht_iatt_merge(this, &local->preparent, preparent);
    dht_iatt_merge(this, &local->postparent, postparent);
    ret = 0;

out:
    this_call_cnt = dht_frame_return(frame);

    if (is_last_call(this_call_cnt)) {
        dht_selfheal_dir_finish(frame, this, ret, 0);
        dht_selfheal_dir_setattr(frame, &local->loc, &local->stbuf, 0xffffff,
                                 layout);
    }

    return 0;
}

static int
dht_selfheal_dir_mkdir_lookup_done(call_frame_t *frame, xlator_t *this)
{
    dht_local_t *local = NULL;
    int i = 0;
    dict_t *dict = NULL;
    dht_layout_t *layout = NULL;
    loc_t *loc = NULL;
    int cnt = 0;
    int ret = -1;

    VALIDATE_OR_GOTO(this->private, err);

    local = frame->local;
    layout = local->layout;
    loc = &local->loc;

    if (!gf_uuid_is_null(local->gfid)) {
        dict = dict_new();
        if (!dict)
            return -1;

        ret = dict_set_gfuuid(dict, "gfid-req", local->gfid, true);
        if (ret)
            gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_DICT_SET_FAILED,
                    "path=%s", loc->path, "key=gfid-req", NULL);
    } else if (local->params) {
        /* Send the dictionary from higher layers directly */

        dict = dict_ref(local->params);
    }
    /* Code to update all extended attributed from local->xattr
       to dict
    */
    dht_dir_set_heal_xattr(this, local, dict, local->xattr, NULL, NULL);

    if (!dict) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_DICT_IS_NULL, NULL);
        dict = dict_new();
        if (!dict)
            return -1;
    }
    ret = dict_set_flag(dict, GF_INTERNAL_CTX_KEY, GF_DHT_HEAL_DIR);
    if (ret) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, DHT_MSG_DICT_SET_FAILED, "key=%s",
                GF_INTERNAL_CTX_KEY, "path=%s", loc->path, NULL);
        /* We can still continue. As heal can still happen
         * unless quota limits have reached for the dir.
         */
    }

    cnt = layout->cnt;
    for (i = 0; i < cnt; i++) {
        if (layout->list[i].err == ESTALE || layout->list[i].err == ENOENT ||
            local->selfheal.force_mkdir) {
            gf_msg_debug(this->name, 0, "Creating directory %s on subvol %s",
                         loc->path, layout->list[i].xlator->name);

            STACK_WIND_COOKIE(
                frame, dht_selfheal_dir_mkdir_cbk, layout->list[i].xlator,
                layout->list[i].xlator, layout->list[i].xlator->fops->mkdir,
                loc,
                st_mode_from_ia(local->stbuf.ia_prot, local->stbuf.ia_type), 0,
                dict);
        }
    }

    if (dict)
        dict_unref(dict);

    return 0;

err:
    dht_selfheal_dir_finish(frame, this, -1, 1);
    return 0;
}

static int
dht_selfheal_dir_mkdir_lookup_cbk(call_frame_t *frame, void *cookie,
                                  xlator_t *this, int op_ret, int op_errno,
                                  inode_t *inode, struct iatt *stbuf,
                                  dict_t *xattr, struct iatt *postparent)
{
    dht_local_t *local = NULL;
    int i = 0;
    int this_call_cnt = 0;
    int missing_dirs = 0;
    dht_layout_t *layout = NULL;
    xlator_t *prev = 0;
    loc_t *loc = NULL;
    char gfid_local[GF_UUID_BUF_SIZE] = {0};
    int index = -1;

    VALIDATE_OR_GOTO(this->private, err);

    local = frame->local;
    layout = local->layout;
    loc = &local->loc;
    prev = cookie;

    if (!gf_uuid_is_null(local->gfid))
        gf_uuid_unparse(local->gfid, gfid_local);

    LOCK(&frame->lock);
    {
        index = dht_layout_index_for_subvol(layout, prev);
        if ((op_ret < 0) && (op_errno == ENOENT || op_errno == ESTALE)) {
            local->selfheal.hole_cnt = !local->selfheal.hole_cnt
                                           ? 1
                                           : local->selfheal.hole_cnt + 1;
            /* the status might have changed. Update the layout with the
             * new status
             */
            if (index >= 0) {
                layout->list[index].err = op_errno;
            }
        }

        if (!op_ret) {
            dht_iatt_merge(this, &local->stbuf, stbuf);
            if (prev == local->mds_subvol) {
                dict_unref(local->xattr);
                local->xattr = dict_ref(xattr);
            }
            /* the status might have changed. Update the layout with the
             * new status
             */
            if (index >= 0) {
                layout->list[index].err = -1;
            }
        }
    }
    UNLOCK(&frame->lock);

    this_call_cnt = dht_frame_return(frame);

    if (is_last_call(this_call_cnt)) {
        if (local->selfheal.hole_cnt == layout->cnt) {
            gf_msg_debug(this->name, op_errno,
                         "Lookup failed, an rmdir could have "
                         "deleted this entry %s",
                         loc->name);
            local->op_errno = op_errno;
            goto err;
        } else {
            for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err == ENOENT ||
                    layout->list[i].err == ESTALE ||
                    local->selfheal.force_mkdir)
                    missing_dirs++;
            }

            if (missing_dirs == 0) {
                dht_selfheal_dir_finish(frame, this, 0, 0);
                dht_selfheal_dir_setattr(frame, loc, &local->stbuf, 0xffffffff,
                                         layout);
                return 0;
            }

            local->call_cnt = missing_dirs;
            dht_selfheal_dir_mkdir_lookup_done(frame, this);
        }
    }

    return 0;

err:
    dht_selfheal_dir_finish(frame, this, -1, 1);
    return 0;
}

static int
dht_selfheal_dir_mkdir_lock_cbk(call_frame_t *frame, void *cookie,
                                xlator_t *this, int32_t op_ret,
                                int32_t op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;
    dht_conf_t *conf = NULL;
    int i = 0;
    int ret = -1;
    xlator_t *mds_subvol = NULL;

    VALIDATE_OR_GOTO(this->private, err);

    conf = this->private;
    local = frame->local;
    mds_subvol = local->mds_subvol;

    local->call_cnt = conf->subvolume_cnt;

    if (op_ret < 0) {
        if (op_errno == EINVAL) {
            local->call_cnt = 1;
            dht_selfheal_dir_mkdir_lookup_done(frame, this);
            return 0;
        }

        gf_smsg(this->name, GF_LOG_WARNING, op_errno, DHT_MSG_ENTRYLK_ERROR,
                "path=%s", local->loc.path, NULL);

        local->op_errno = op_errno;
        goto err;
    }

    /* After getting locks, perform lookup again to ensure that the
       directory was not deleted by a racing rmdir
    */
    if (!local->xattr_req)
        local->xattr_req = dict_new();

    ret = dict_set_int32(local->xattr_req, "list-xattr", 1);
    if (ret)
        gf_smsg(this->name, GF_LOG_ERROR, 0, DHT_MSG_DICT_SET_FAILED, "path=%s",
                local->loc.path, NULL);

    for (i = 0; i < conf->subvolume_cnt; i++) {
        if (mds_subvol && conf->subvolumes[i] == mds_subvol) {
            STACK_WIND_COOKIE(frame, dht_selfheal_dir_mkdir_lookup_cbk,
                              conf->subvolumes[i], conf->subvolumes[i],
                              conf->subvolumes[i]->fops->lookup, &local->loc,
                              local->xattr_req);
        } else {
            STACK_WIND_COOKIE(frame, dht_selfheal_dir_mkdir_lookup_cbk,
                              conf->subvolumes[i], conf->subvolumes[i],
                              conf->subvolumes[i]->fops->lookup, &local->loc,
                              NULL);
        }
    }

    return 0;

err:
    dht_selfheal_dir_finish(frame, this, -1, 1);
    return 0;
}

static int
dht_selfheal_dir_mkdir(call_frame_t *frame, loc_t *loc, dht_layout_t *layout,
                       int force)
{
    int missing_dirs = 0;
    int i = 0;
    int op_errno = 0;
    int ret = -1;
    dht_local_t *local = NULL;
    xlator_t *this = NULL;
    dht_conf_t *conf = NULL;

    local = frame->local;
    this = frame->this;
    conf = this->private;

    local->selfheal.force_mkdir = force;
    local->selfheal.hole_cnt = 0;

    for (i = 0; i < layout->cnt; i++) {
        if (layout->list[i].err == ENOENT || force)
            missing_dirs++;
    }

    if (missing_dirs == 0) {
        /* We don't need to create any directories. Proceed to heal the
         * attrs and xattrs
         */
        if (!__is_root_gfid(local->stbuf.ia_gfid)) {
            if (local->need_xattr_heal) {
                local->need_xattr_heal = 0;
                ret = dht_dir_xattr_heal(this, local, &op_errno);
                if (ret) {
                    gf_smsg(this->name, GF_LOG_ERROR, op_errno,
                            DHT_MSG_DIR_XATTR_HEAL_FAILED, "path=%s",
                            local->loc.path, "gfid=%s", uuid_utoa(local->gfid),
                            NULL);
                }
            } else {
                if (!gf_uuid_is_null(local->gfid))
                    gf_uuid_copy(loc->gfid, local->gfid);

                ret = dht_common_mark_mdsxattr(frame, NULL, 0);
                if (!ret)
                    return 0;

                gf_smsg(this->name, GF_LOG_INFO, 0, DHT_MSG_SET_XATTR_FAILED,
                        "path=%s", local->loc.path, "gfid=%s",
                        uuid_utoa(local->gfid), NULL);
            }
        }
        dht_selfheal_dir_setattr(frame, loc, &local->stbuf, 0xffffffff, layout);
        return 0;
    }

    /* MDS xattr is populated only while DHT is having more than one
     subvol.In case of graph switch while adding more dht subvols need to
     consider hash subvol as a MDS to avoid MDS check failure at the time
     of running fop on directory
    */
    if (!dict_get(local->xattr, conf->mds_xattr_key) &&
        (conf->subvolume_cnt > 1)) {
        if (local->hashed_subvol == NULL) {
            local->hashed_subvol = dht_subvol_get_hashed(this, loc);
            if (local->hashed_subvol == NULL) {
                local->op_errno = EINVAL;
                gf_smsg(this->name, GF_LOG_WARNING, local->op_errno,
                        DHT_MSG_HASHED_SUBVOL_GET_FAILED, "gfid=%s",
                        loc->pargfid, "name=%s", loc->name, "path=%s",
                        loc->path, NULL);
                goto err;
            }
        }
        ret = dht_inode_ctx_mdsvol_set(local->inode, this,
                                       local->hashed_subvol);
        if (ret) {
            gf_msg(this->name, GF_LOG_ERROR, 0, DHT_MSG_SET_INODE_CTX_FAILED,
                   "Failed to set hashed subvol for %s on inode vol is %s",
                   local->loc.path,
                   local->hashed_subvol ? local->hashed_subvol->name : "NULL");
            goto err;
        }
    }

    if (local->hashed_subvol == NULL) {
        local->hashed_subvol = dht_subvol_get_hashed(this, loc);
        if (local->hashed_subvol == NULL) {
            local->op_errno = EINVAL;
            gf_smsg(this->name, GF_LOG_WARNING, local->op_errno,
                    DHT_MSG_HASHED_SUBVOL_GET_FAILED, "gfid=%s", loc->pargfid,
                    "name=%s", loc->name, "path=%s", loc->path, NULL);
            goto err;
        }
    }

    local->current = &local->lock[0];
    ret = dht_protect_namespace(frame, loc, local->hashed_subvol,
                                &local->current->ns,
                                dht_selfheal_dir_mkdir_lock_cbk);

    if (ret < 0)
        goto err;

    return 0;
err:
    return -1;
}

static int
dht_selfheal_layout_alloc_start(xlator_t *this, loc_t *loc,
                                dht_layout_t *layout)
{
    int start = 0;
    uint32_t hashval = 0;
    int ret = 0;
    const char *str = NULL;
    dht_conf_t *conf = NULL;
    char buf[UUID_CANONICAL_FORM_LEN + 1] = {
        0,
    };

    conf = this->private;

    if (conf->randomize_by_gfid) {
        str = uuid_utoa_r(loc->gfid, buf);
    } else {
        str = loc->path;
    }

    ret = dht_hash_compute(this, layout->type, str, &hashval);
    if (ret == 0) {
        start = (hashval % layout->cnt);
    }

    return start;
}

static int
dht_get_layout_count(xlator_t *this, dht_layout_t *layout, int new_layout)
{
    int i = 0;
    int j = 0;
    int err = 0;
    int count = 0;
    dht_conf_t *conf = NULL;

    /* Gets in use only for replace-brick, remove-brick */
    conf = this->private;
    for (i = 0; i < layout->cnt; i++) {
        for (j = 0; j < conf->subvolume_cnt; j++) {
            if (conf->decommissioned_bricks[j] &&
                conf->decommissioned_bricks[j] == layout->list[i].xlator) {
                layout->list[i].err = EINVAL;
                break;
            }
        }
    }

    for (i = 0; i < layout->cnt; i++) {
        err = layout->list[i].err;
        if (err == -1 || err == 0 || err == ENOENT) {
            /* Take this with a pinch of salt. The behaviour seems
             * to be slightly different when this function is
             * invoked from mkdir codepath. For eg., err == 0 in
             * mkdir codepath means directory created but xattr
             * is not set yet.
             */

            /* Setting list[i].err = -1 is an indication for
               dht_selfheal_layout_new_directory() to assign
               a range. We set it to -1 based on any one of
               the three criteria:

               - err == -1 already, which means directory
                 existed but layout was not set on it.

               - err == 0, which means directory exists and
                 has an old layout piece which will be
                 overwritten now.

               - err == ENOENT, which means directory does
                 not exist (possibly racing with mkdir or
                 finishing half done mkdir). The missing
                 directory will be attempted to be recreated.
            */
            count++;
            if (!err)
                layout->list[i].err = -1;
        }
    }

    /* no subvolume has enough space, but can't stop directory creation */
    if (!count || !new_layout) {
        for (i = 0; i < layout->cnt; i++) {
            err = layout->list[i].err;
            if (err == ENOSPC) {
                layout->list[i].err = -1;
                count++;
            }
        }
    }

    /* if layout->spread_cnt is set, check if it is <= available
     * subvolumes (down brick and decommissioned bricks are considered
     * un-available). Else return count (available up bricks) */
    count = ((layout->spread_cnt && (layout->spread_cnt <= count))
                 ? layout->spread_cnt
                 : ((count) ? count : 1));

    return count;
}

void
dht_selfheal_layout_new_directory(call_frame_t *frame, loc_t *loc,
                                  dht_layout_t *new_layout);

void
dht_layout_range_swap(dht_layout_t *layout, int i, int j);

/*
 * It's a bit icky using local variables in a macro, but it makes the rest
 * of the code a lot clearer.
 */
#define OV_ENTRY(x, y) table[x * new->cnt + y]

static void
dht_selfheal_layout_maximize_overlap(call_frame_t *frame, loc_t *loc,
                                     dht_layout_t *new, dht_layout_t *old)
{
    int i = 0;
    int j = 0;
    uint32_t curr_overlap = 0;
    uint32_t max_overlap = 0;
    int max_overlap_idx = -1;
    uint32_t overlap = 0;
    uint32_t *table = NULL;

    dht_layout_sort_volname(old);
    /* Now both old_layout->list[] and new_layout->list[]
       are match the same xlators/subvolumes. i.e,
       old_layout->[i] and new_layout->[i] are referring
       to the same subvolumes
    */

    /* Build a table of overlaps between new[i] and old[j]. */
    table = alloca(sizeof(overlap) * old->cnt * new->cnt);
    if (!table) {
        return;
    }
    memset(table, 0, sizeof(overlap) * old->cnt * new->cnt);
    for (i = 0; i < new->cnt; ++i) {
        for (j = 0; j < old->cnt; ++j) {
            OV_ENTRY(i, j) = dht_overlap_calc(old, j, new, i);
        }
    }

    for (i = 0; i < new->cnt; i++) {
        if (new->list[i].err > 0) {
            /* Subvol might be marked for decommission
               with EINVAL, or some other serious error
               marked with positive errno.
            */
            continue;
        }

        max_overlap = 0;
        max_overlap_idx = i;
        for (j = (i + 1); j < new->cnt; ++j) {
            if (new->list[j].err > 0) {
                /* Subvol might be marked for decommission
                with EINVAL, or some other serious error
                marked with positive errno.
                */
                continue;
            }
            /* Calculate the overlap now. */
            curr_overlap = OV_ENTRY(i, i) + OV_ENTRY(j, j);
            /* Calculate the overlap after the proposed swap. */
            overlap = OV_ENTRY(i, j) + OV_ENTRY(j, i);
            /* Are we better than status quo? */
            if (overlap > curr_overlap) {
                overlap -= curr_overlap;
                /* Are we better than the previous choice? */
                if (overlap > max_overlap) {
                    max_overlap = overlap;
                    max_overlap_idx = j;
                }
            }
        }

        if (max_overlap_idx != i) {
            dht_layout_range_swap(new, i, max_overlap_idx);
            /* Need to swap the table values too. */
            for (j = 0; j < old->cnt; ++j) {
                overlap = OV_ENTRY(i, j);
                OV_ENTRY(i, j) = OV_ENTRY(max_overlap_idx, j);
                OV_ENTRY(max_overlap_idx, j) = overlap;
            }
        }
    }
}

static dht_layout_t *
dht_fix_layout_of_directory(call_frame_t *frame, loc_t *loc,
                            dht_layout_t *layout)
{
    int i = 0;
    xlator_t *this = NULL;
    dht_layout_t *new_layout = NULL;
    dht_conf_t *priv = NULL;
    dht_local_t *local = NULL;
    uint32_t subvol_down = 0;
    gf_boolean_t maximize_overlap = _gf_true;
    char gfid[GF_UUID_BUF_SIZE] = {0};

    this = frame->this;
    priv = this->private;
    local = frame->local;

    if (layout->type == DHT_HASH_TYPE_DM_USER) {
        gf_msg_debug(THIS->name, 0, "leaving %s alone", loc->path);
        goto done;
    }

    new_layout = dht_layout_new(this, priv->subvolume_cnt);
    if (!new_layout) {
        gf_uuid_unparse(loc->gfid, gfid);
        gf_smsg(this->name, GF_LOG_ERROR, ENOMEM, DHT_MSG_MEM_ALLOC_FAILED,
                "new_layout, path=%s", loc->path, "gfid=%s", gfid, NULL);
        goto done;
    }

    /* If a subvolume is down, do not re-write the layout. */
    dht_layout_anomalies(this, loc, layout, NULL, NULL, NULL, &subvol_down,
                         NULL, NULL);

    if (subvol_down) {
        gf_uuid_unparse(loc->gfid, gfid);
        gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_LAYOUT_FIX_FAILED,
                "subvol-down=%u", subvol_down, "Skipping-fix-layout", "path=%s",
                loc->path, "gfid=%s", gfid, NULL);
        GF_FREE(new_layout);
        return NULL;
    }

    for (i = 0; i < new_layout->cnt; i++) {
        if (layout->list[i].err != ENOSPC)
            new_layout->list[i].err = layout->list[i].err;
        else
            new_layout->list[i].err = -1;

        new_layout->list[i].xlator = layout->list[i].xlator;
    }

    new_layout->commit_hash = layout->commit_hash;

    if (priv->du_stats) {
        for (i = 0; i < priv->subvolume_cnt; ++i) {
            gf_smsg(this->name, GF_LOG_DEBUG, 0, DHT_MSG_SUBVOL_INFO,
                    "index=%d", i, "name=%s", priv->subvolumes[i]->name,
                    "chunks=%u", priv->du_stats[i].chunks, "path=%s", loc->path,
                    NULL);

            /* Maximize overlap if the bricks are all the same
             *  size.
             * This is probably not going to be very common on
             * live setups but will benefit our regression tests
             */
            if (i && (priv->du_stats[i].chunks != priv->du_stats[0].chunks)) {
                maximize_overlap = _gf_false;
            }
        }
    } else {
        gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_NO_DISK_USAGE_STATUS,
                NULL);
    }

    /* First give it a layout as though it is a new directory. This
       ensures rotation to kick in */
    dht_layout_sort_volname(new_layout);
    dht_selfheal_layout_new_directory(frame, loc, new_layout);

    /* Maximize overlap if weighted-rebalance is disabled */
    if (!priv->do_weighting)
        maximize_overlap = _gf_true;

    /* Now selectively re-assign ranges only when it helps */
    if (maximize_overlap) {
        dht_selfheal_layout_maximize_overlap(frame, loc, new_layout, layout);
    }
done:
    if (new_layout) {
        /* Make sure the extra 'ref' for existing layout is removed */
        dht_layout_unref(local->layout);

        local->layout = new_layout;
    }

    return local->layout;
}

/*
 * Having to call this 2x for each entry in the layout is pretty horrible, but
 * that's what all of this layout-sorting nonsense gets us.
 */
static uint32_t
dht_get_chunks_from_xl(xlator_t *parent, xlator_t *child)
{
    dht_conf_t *priv = parent->private;
    xlator_list_t *trav;
    uint32_t index = 0;

    if (!priv->du_stats) {
        return 0;
    }

    for (trav = parent->children; trav; trav = trav->next) {
        if (trav->xlator == child) {
            return priv->du_stats[index].chunks;
        }
        ++index;
    }

    return 0;
}

void
dht_selfheal_layout_new_directory(call_frame_t *frame, loc_t *loc,
                                  dht_layout_t *layout)
{
    xlator_t *this = NULL;
    double chunk = 0;
    int i = 0;
    uint32_t start = 0;
    int bricks_to_use = 0;
    int err = 0;
    int start_subvol = 0;
    uint32_t curr_size;
    uint32_t range_size;
    uint64_t total_size = 0;
    int real_i;
    dht_conf_t *priv;
    gf_boolean_t weight_by_size;
    int bricks_used = 0;

    this = frame->this;
    priv = this->private;
    weight_by_size = priv->do_weighting;

    bricks_to_use = dht_get_layout_count(this, layout, 1);
    GF_ASSERT(bricks_to_use > 0);

    bricks_used = 0;
    for (i = 0; i < layout->cnt; ++i) {
        err = layout->list[i].err;
        if ((err != -1) && (err != ENOENT)) {
            continue;
        }
        curr_size = dht_get_chunks_from_xl(this, layout->list[i].xlator);
        if (!curr_size) {
            weight_by_size = _gf_false;
            break;
        }
        total_size += curr_size;
        if (++bricks_used >= bricks_to_use) {
            break;
        }
    }

    if (weight_by_size && total_size) {
        /* We know total_size is not zero. */
        chunk = ((double)0xffffffff) / ((double)total_size);
        gf_msg_debug(this->name, 0,
                     "chunk size = 0xffffffff / %" PRIu64 " = %f", total_size,
                     chunk);
    } else {
        weight_by_size = _gf_false;
        chunk = ((double)0xffffffff) / ((double)bricks_to_use);
    }

    start_subvol = dht_selfheal_layout_alloc_start(this, loc, layout);

    /* clear out the range, as we are re-computing here */
    DHT_RESET_LAYOUT_RANGE(layout);

    /*
     * OK, what's this "real_i" stuff about?  This used to be two loops -
     * from start_subvol to layout->cnt-1, then from 0 to start_subvol-1.
     * That way is practically an open invitation to bugs when only one
     * of the loops is updated.  Using real_i and modulo operators to make
     * it one loop avoids this problem.  Remember, folks: it's everyone's
     * responsibility to help stamp out copy/paste abuse.
     */
    bricks_used = 0;
    for (real_i = 0; real_i < layout->cnt; real_i++) {
        i = (real_i + start_subvol) % layout->cnt;
        err = layout->list[i].err;
        if ((err != -1) && (err != ENOENT)) {
            continue;
        }
        if (weight_by_size) {
            curr_size = dht_get_chunks_from_xl(this, layout->list[i].xlator);
            if (!curr_size) {
                continue;
            }
        } else {
            curr_size = 1;
        }
        range_size = chunk * curr_size;
        gf_msg_debug(this->name, 0, "assigning range size 0x%x to %s",
                     range_size, layout->list[i].xlator->name);
        DHT_SET_LAYOUT_RANGE(layout, i, start, range_size, loc->path);
        if (++bricks_used >= bricks_to_use) {
            layout->list[i].stop = 0xffffffff;
            goto done;
        }
        start += range_size;
    }

done:
    return;
}

static int
dht_selfheal_dir_getafix(call_frame_t *frame, loc_t *loc, dht_layout_t *layout)
{
    dht_local_t *local = NULL;
    uint32_t holes = 0;
    int ret = -1;
    int i = -1;
    uint32_t overlaps = 0;

    local = frame->local;

    holes = local->selfheal.hole_cnt;
    overlaps = local->selfheal.overlaps_cnt;

    if (holes || overlaps) {
        /* If the layout has anomalies which would change the hash
         * ranges, then we need to reset the commit_hash for this
         * directory, as the layout would change and things may not
         * be in place as expected */
        layout->commit_hash = DHT_LAYOUT_HASH_INVALID;
        dht_selfheal_layout_new_directory(frame, loc, layout);
        ret = 0;
    }

    for (i = 0; i < layout->cnt; i++) {
        /* directory not present */
        if (layout->list[i].err == ENOENT) {
            ret = 0;
            break;
        }
    }

    /* TODO: give a fix to these non-virgins */

    return ret;
}

int
dht_selfheal_new_directory(call_frame_t *frame, dht_selfheal_dir_cbk_t dir_cbk,
                           dht_layout_t *layout)
{
    dht_local_t *local = NULL;
    int ret = 0;
    inode_t *linked_inode = NULL, *inode = NULL;
    loc_t *loc = NULL;
    char pgfid[GF_UUID_BUF_SIZE] = {0};
    char gfid[GF_UUID_BUF_SIZE] = {0};
    int32_t op_errno = EIO;

    local = frame->local;

    loc = &local->loc;

    gf_uuid_unparse(local->stbuf.ia_gfid, gfid);
    gf_uuid_unparse(loc->parent->gfid, pgfid);

    linked_inode = inode_link(loc->inode, loc->parent, loc->name,
                              &local->stbuf);
    if (!linked_inode) {
        gf_smsg(frame->this->name, GF_LOG_WARNING, 0, DHT_MSG_LINK_INODE_FAILED,
                "pgfid=%s", pgfid, "name=%s", loc->name, "gfid=%s", gfid, NULL);
        ret = -1;
        goto out;
    }

    inode = loc->inode;
    loc->inode = linked_inode;
    inode_unref(inode);

    local->selfheal.dir_cbk = dir_cbk;
    local->selfheal.layout = dht_layout_ref(layout);

    dht_layout_sort_volname(layout);
    dht_selfheal_layout_new_directory(frame, &local->loc, layout);

    op_errno = ENOMEM;
    ret = dht_selfheal_layout_lock(frame, layout, _gf_true,
                                   dht_selfheal_dir_xattr,
                                   dht_should_heal_layout);

out:
    if (ret < 0) {
        dir_cbk(frame, NULL, frame->this, -1, op_errno, NULL);
    }

    return 0;
}

int
dht_fix_directory_layout(call_frame_t *frame, dht_selfheal_dir_cbk_t dir_cbk,
                         dht_layout_t *layout)
{
    dht_local_t *local = NULL;
    dht_layout_t *tmp_layout = NULL;
    int ret = 0;

    local = frame->local;

    local->selfheal.dir_cbk = dir_cbk;
    local->selfheal.layout = dht_layout_ref(layout);

    /* No layout sorting required here */
    tmp_layout = dht_fix_layout_of_directory(frame, &local->loc, layout);
    if (!tmp_layout) {
        return -1;
    }

    ret = dht_selfheal_layout_lock(frame, tmp_layout, _gf_false,
                                   dht_fix_dir_xattr, dht_should_fix_layout);

    return ret;
}

int
dht_selfheal_directory(call_frame_t *frame, dht_selfheal_dir_cbk_t dir_cbk,
                       loc_t *loc, dht_layout_t *layout)
{
    dht_local_t *local = NULL;
    xlator_t *this = NULL;
    uint32_t down = 0;
    uint32_t misc = 0;
    int ret = 0;
    char pgfid[GF_UUID_BUF_SIZE] = {0};
    char gfid[GF_UUID_BUF_SIZE] = {0};
    inode_t *linked_inode = NULL, *inode = NULL;

    FRAME_SU_DO(frame, dht_local_t);

    local = frame->local;
    this = frame->this;

    local->selfheal.dir_cbk = dir_cbk;
    local->selfheal.layout = dht_layout_ref(layout);

    if (local->need_attrheal) {
        if (__is_root_gfid(local->stbuf.ia_gfid)) {
            local->stbuf.ia_gid = local->prebuf.ia_gid;
            local->stbuf.ia_uid = local->prebuf.ia_uid;

            local->stbuf.ia_ctime = local->prebuf.ia_ctime;
            local->stbuf.ia_ctime_nsec = local->prebuf.ia_ctime_nsec;
            local->stbuf.ia_prot = local->prebuf.ia_prot;

        } else if (!IA_ISINVAL(local->mds_stbuf.ia_type)) {
            local->stbuf = local->mds_stbuf;
        }
    }

    if (!__is_root_gfid(local->stbuf.ia_gfid)) {
        gf_uuid_unparse(local->stbuf.ia_gfid, gfid);
        gf_uuid_unparse(loc->parent->gfid, pgfid);

        linked_inode = inode_link(loc->inode, loc->parent, loc->name,
                                  &local->stbuf);
        if (!linked_inode) {
            gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_LINK_INODE_FAILED,
                    "pgfid=%s", pgfid, "name=%s", loc->name, "gfid=%s", gfid,
                    NULL);
            ret = 0;
            goto sorry_no_fix;
        }

        inode = loc->inode;
        loc->inode = linked_inode;
        inode_unref(inode);
    }

    if (local->need_xattr_heal && (local->mds_xattr)) {
        dht_dir_set_heal_xattr(this, local, local->xattr, local->mds_xattr,
                               NULL, NULL);
        dict_unref(local->mds_xattr);
        local->mds_xattr = NULL;
    }

    dht_layout_anomalies(this, loc, layout, &local->selfheal.hole_cnt,
                         &local->selfheal.overlaps_cnt,
                         &local->selfheal.missing_cnt, &local->selfheal.down,
                         &local->selfheal.misc, NULL);

    down = local->selfheal.down;
    misc = local->selfheal.misc;

    if (down) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_SELFHEAL_FAILED,
                "path=%s", loc->path, "subvol-down=%d", down, "Not-fixing",
                "gfid=%s", gfid, NULL);
        ret = 0;
        goto sorry_no_fix;
    }

    if (misc) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_SELFHEAL_FAILED,
                "path=%s", loc->path, "misc=%d", misc, "unrecoverable-errors",
                "gfid=%s", gfid, NULL);

        ret = 0;
        goto sorry_no_fix;
    }

    dht_layout_sort_volname(layout);
    local->heal_layout = _gf_true;

    /* Ignore return value as it can be inferred from result of
     * dht_layout_anomalies
     */
    dht_selfheal_dir_getafix(frame, loc, layout);

    if (!(local->selfheal.hole_cnt || local->selfheal.overlaps_cnt ||
          local->selfheal.missing_cnt)) {
        local->heal_layout = _gf_false;
    }

    ret = dht_selfheal_dir_mkdir(frame, loc, layout, 0);
    if (ret < 0) {
        ret = 0;
        goto sorry_no_fix;
    }

    return 0;

sorry_no_fix:
    /* TODO: need to put appropriate local->op_errno */
    dht_selfheal_dir_finish(frame, this, ret, 1);

    return 0;
}

int
dht_selfheal_restore(call_frame_t *frame, dht_selfheal_dir_cbk_t dir_cbk,
                     loc_t *loc, dht_layout_t *layout)
{
    int ret = 0;
    dht_local_t *local = NULL;

    local = frame->local;

    local->selfheal.dir_cbk = dir_cbk;
    local->selfheal.layout = dht_layout_ref(layout);

    ret = dht_selfheal_dir_mkdir(frame, loc, layout, 1);

    return ret;
}

int
dht_dir_heal_xattrs(void *data)
{
    call_frame_t *frame = NULL;
    dht_local_t *local = NULL;
    xlator_t *subvol = NULL;
    xlator_t *mds_subvol = NULL;
    xlator_t *this = NULL;
    dht_conf_t *conf = NULL;
    dict_t *user_xattr = NULL;
    dict_t *internal_xattr = NULL;
    dict_t *mds_xattr = NULL;
    dict_t *xdata = NULL;
    int call_cnt = 0;
    int ret = -1;
    int uret = 0;
    int uflag = 0;
    int i = 0;
    int xattr_hashed = 0;
    char gfid[GF_UUID_BUF_SIZE] = {0};
    int32_t allzero[1] = {0};

    GF_VALIDATE_OR_GOTO("dht", data, out);

    frame = data;
    local = frame->local;
    this = frame->this;
    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO(this->name, local, out);
    mds_subvol = local->mds_subvol;
    conf = this->private;
    GF_VALIDATE_OR_GOTO(this->name, conf, out);
    gf_uuid_unparse(local->loc.gfid, gfid);

    if (!mds_subvol) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_NO_MDS_SUBVOL, "path=%s",
                local->loc.path, "gfid=%s", gfid, NULL);
        goto out;
    }

    if ((local->loc.inode && gf_uuid_is_null(local->loc.inode->gfid)) ||
        gf_uuid_is_null(local->loc.gfid)) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_GFID_NOT_PRESENT,
                "skip-heal path=%s", local->loc.path, "gfid=%s", gfid, NULL);
        goto out;
    }

    internal_xattr = dict_new();
    if (!internal_xattr) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, DHT_MSG_CREATE_FAILED,
                "dictionary", NULL);
        goto out;
    }
    xdata = dict_new();
    if (!xdata) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, DHT_MSG_CREATE_FAILED,
                "dictionary", NULL);
        goto out;
    }

    call_cnt = conf->subvolume_cnt;

    user_xattr = dict_new();
    if (!user_xattr) {
        gf_smsg(this->name, GF_LOG_ERROR, 0, DHT_MSG_CREATE_FAILED,
                "dictionary", NULL);
        goto out;
    }

    ret = syncop_listxattr(local->mds_subvol, &local->loc, &mds_xattr, NULL,
                           NULL);
    if (ret < 0) {
        gf_smsg(this->name, GF_LOG_ERROR, -ret, DHT_MSG_LIST_XATTRS_FAILED,
                "path=%s", local->loc.path, "name=%s", local->mds_subvol->name,
                NULL);
    }

    if (!mds_xattr)
        goto out;

    dht_dir_set_heal_xattr(this, local, user_xattr, mds_xattr, &uret, &uflag);

    /* To set quota related xattr need to set GLUSTERFS_INTERNAL_FOP_KEY
     * key value to 1
     */
    if (dict_get(user_xattr, QUOTA_LIMIT_KEY) ||
        dict_get(user_xattr, QUOTA_LIMIT_OBJECTS_KEY)) {
        ret = dict_set_int32(xdata, GLUSTERFS_INTERNAL_FOP_KEY, 1);
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, 0, DHT_MSG_DICT_SET_FAILED,
                    "key=%s", GLUSTERFS_INTERNAL_FOP_KEY, "path=%s",
                    local->loc.path, NULL);
            goto out;
        }
    }
    if (uret <= 0 && !uflag)
        goto out;

    for (i = 0; i < call_cnt; i++) {
        subvol = conf->subvolumes[i];
        if (subvol == mds_subvol)
            continue;
        if (uret || uflag) {
            /* Custom xattr heal is required - let posix handle it */
            ret = dict_set_int8(xdata, "sync_backend_xattrs", _gf_true);
            if (ret) {
                gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_DICT_SET_FAILED,
                        "path=%s", local->loc.path, "key=%s",
                        "sync_backend_xattrs", NULL);
                goto out;
            }

            ret = syncop_setxattr(subvol, &local->loc, user_xattr, 0, xdata,
                                  NULL);
            if (ret) {
                xattr_hashed = 1;
                gf_smsg(this->name, GF_LOG_ERROR, -ret,
                        DHT_MSG_DIR_XATTR_HEAL_FAILED,
                        "set-user-xattr-failed path=%s", local->loc.path,
                        "subvol=%s", subvol->name, "gfid=%s", gfid, NULL);
            } else {
                dict_del(xdata, "sync_backend_xattrs");
            }
        }
    }
    /* After heal all custom xattr reset internal MDS xattr to 0 */
    if (!xattr_hashed) {
        ret = dht_dict_set_array(internal_xattr, conf->mds_xattr_key, allzero,
                                 1);
        if (ret) {
            gf_smsg(this->name, GF_LOG_WARNING, ENOMEM, DHT_MSG_DICT_SET_FAILED,
                    "key=%s", conf->mds_xattr_key, "path=%s", local->loc.path,
                    NULL);
            goto out;
        }
        ret = syncop_setxattr(mds_subvol, &local->loc, internal_xattr, 0, NULL,
                              NULL);
        if (ret) {
            gf_smsg(this->name, GF_LOG_ERROR, -ret,
                    DHT_MSG_DIR_XATTR_HEAL_FAILED, "path=%s", local->loc.path,
                    "subvol=%s", mds_subvol->name, "gfid=%s", gfid, NULL);
        }
    }

out:
    if (user_xattr)
        dict_unref(user_xattr);
    if (mds_xattr)
        dict_unref(mds_xattr);
    if (internal_xattr)
        dict_unref(internal_xattr);
    if (xdata)
        dict_unref(xdata);
    return 0;
}

int
dht_dir_heal_xattrs_done(int ret, call_frame_t *sync_frame, void *data)
{
    DHT_STACK_DESTROY(sync_frame);
    return 0;
}

int
dht_dir_attr_heal(void *data)
{
    call_frame_t *frame = NULL;
    dht_local_t *local = NULL;
    xlator_t *subvol = NULL;
    xlator_t *mds_subvol = NULL;
    xlator_t *this = NULL;
    dht_conf_t *conf = NULL;
    int call_cnt = 0;
    int ret = -1;
    int i = 0;
    char gfid[GF_UUID_BUF_SIZE] = {0};

    GF_VALIDATE_OR_GOTO("dht", data, out);

    frame = data;
    local = frame->local;
    this = frame->this;
    GF_VALIDATE_OR_GOTO("dht", this, out);
    GF_VALIDATE_OR_GOTO("dht", local, out);
    conf = this->private;
    GF_VALIDATE_OR_GOTO("dht", conf, out);

    mds_subvol = local->mds_subvol;
    call_cnt = conf->subvolume_cnt;

    if (!__is_root_gfid(local->stbuf.ia_gfid) && (!mds_subvol)) {
        gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_NO_MDS_SUBVOL, "path=%s",
                local->loc.path, "gfid=%s", gfid, NULL);
        goto out;
    }

    if (!__is_root_gfid(local->stbuf.ia_gfid)) {
        for (i = 0; i < conf->subvolume_cnt; i++) {
            if (conf->subvolumes[i] == mds_subvol) {
                if (!conf->subvolume_status[i]) {
                    gf_smsg(this->name, GF_LOG_ERROR, 0,
                            DHT_MSG_MDS_DOWN_UNABLE_TO_SET, "path=%s",
                            local->loc.path, "gfid=%s", gfid, NULL);
                    goto out;
                }
            }
        }
    }

    for (i = 0; i < call_cnt; i++) {
        subvol = conf->subvolumes[i];
        if (!subvol || subvol == mds_subvol)
            continue;
        if (__is_root_gfid(local->stbuf.ia_gfid)) {
            ret = syncop_setattr(
                subvol, &local->loc, &local->stbuf,
                (GF_SET_ATTR_UID | GF_SET_ATTR_GID | GF_SET_ATTR_MODE), NULL,
                NULL, NULL, NULL);
        } else {
            ret = syncop_setattr(
                subvol, &local->loc, &local->mds_stbuf,
                (GF_SET_ATTR_UID | GF_SET_ATTR_GID | GF_SET_ATTR_MODE), NULL,
                NULL, NULL, NULL);
        }

        if (ret) {
            gf_uuid_unparse(local->loc.gfid, gfid);

            gf_smsg(this->name, GF_LOG_ERROR, -ret,
                    DHT_MSG_DIR_ATTR_HEAL_FAILED, "path=%s", local->loc.path,
                    "subvol=%s", subvol->name, "gfid=%s", gfid, NULL);
        }
    }
out:
    return 0;
}

int
dht_dir_attr_heal_done(int ret, call_frame_t *sync_frame, void *data)
{
    DHT_STACK_DESTROY(sync_frame);
    return 0;
}

/* EXIT: dht_update_commit_hash_for_layout */
static int
dht_update_commit_hash_for_layout_done(call_frame_t *frame, void *cookie,
                                       xlator_t *this, int32_t op_ret,
                                       int32_t op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;

    local = frame->local;

    /* preserve oldest error */
    if (op_ret && !local->op_ret) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;
    }

    DHT_STACK_UNWIND(setxattr, frame, local->op_ret, local->op_errno, NULL);

    return 0;
}

static int
dht_update_commit_hash_for_layout_unlock(call_frame_t *frame, xlator_t *this)
{
    dht_local_t *local = NULL;
    int ret = 0;

    local = frame->local;

    ret = dht_unlock_inodelk(frame, local->lock[0].layout.my_layout.locks,
                             local->lock[0].layout.my_layout.lk_count,
                             dht_update_commit_hash_for_layout_done);
    if (ret < 0) {
        /* preserve oldest error, just ... */
        if (!local->op_ret) {
            local->op_errno = errno;
            local->op_ret = -1;
        }

        gf_smsg(this->name, GF_LOG_WARNING, errno, DHT_MSG_WIND_UNLOCK_FAILED,
                "path=%s", local->loc.path, NULL);

        dht_update_commit_hash_for_layout_done(frame, NULL, this, 0, 0, NULL);
    }

    return 0;
}

static int
dht_update_commit_hash_for_layout_cbk(call_frame_t *frame, void *cookie,
                                      xlator_t *this, int op_ret, int op_errno,
                                      dict_t *xdata)
{
    dht_local_t *local = NULL;
    int this_call_cnt = 0;

    local = frame->local;

    LOCK(&frame->lock);
    /* store first failure, just because */
    if (op_ret && !local->op_ret) {
        local->op_ret = op_ret;
        local->op_errno = op_errno;
    }
    UNLOCK(&frame->lock);

    this_call_cnt = dht_frame_return(frame);

    if (is_last_call(this_call_cnt)) {
        dht_update_commit_hash_for_layout_unlock(frame, this);
    }

    return 0;
}

static int
dht_update_commit_hash_for_layout_resume(call_frame_t *frame, void *cookie,
                                         xlator_t *this, int32_t op_ret,
                                         int32_t op_errno, dict_t *xdata)
{
    dht_local_t *local = NULL;
    int count = 1, ret = -1, i = 0, j = 0;
    dht_conf_t *conf = NULL;
    dht_layout_t *layout = NULL;
    int32_t *disk_layout = NULL;
    dict_t **xattr = NULL;

    local = frame->local;
    conf = frame->this->private;
    count = conf->local_subvols_cnt;
    layout = local->layout;

    if (op_ret < 0) {
        goto err_done;
    }

    /* We precreate the xattr list as we cannot change call count post the
     * first wind as we may never continue from there. So we finish prep
     * work before winding the setxattrs */
    xattr = GF_CALLOC(count, sizeof(*xattr), gf_common_mt_char);
    if (!xattr) {
        local->op_errno = errno;

        gf_smsg(this->name, GF_LOG_WARNING, errno, DHT_MSG_COMMIT_HASH_FAILED,
                "allocation-failed path=%s", local->loc.path, NULL);

        goto err;
    }

    for (i = 0; i < count; i++) {
        /* find the layout index for the subvolume */
        ret = dht_layout_index_for_subvol(layout, conf->local_subvols[i]);
        if (ret < 0) {
            local->op_errno = ENOENT;

            gf_smsg(this->name, GF_LOG_WARNING, 0, DHT_MSG_COMMIT_HASH_FAILED,
                    "path=%s", local->loc.path, "subvol=%s",
                    conf->local_subvols[i]->name, "find-disk-layout-failed",
                    NULL);

            goto err;
        }
        j = ret;

        /* update the commit hash for the layout */
        layout->list[j].commit_hash = layout->commit_hash;

        /* extract the current layout */
        ret = dht_disk_layout_extract(this, layout, j, &disk_layout);
        if (ret == -1) {
            local->op_errno = errno;

            gf_smsg(this->name, GF_LOG_WARNING, errno,
                    DHT_MSG_COMMIT_HASH_FAILED, "path=%s", local->loc.path,
                    "subvol=%s", conf->local_subvols[i]->name,
                    "extract-disk-layout-failed", NULL);

            goto err;
        }

        xattr[i] = dict_new();
        if (!xattr[i]) {
            local->op_errno = errno;

            gf_smsg(this->name, GF_LOG_WARNING, errno,
                    DHT_MSG_COMMIT_HASH_FAILED, "path=%s Allocation-failed",
                    local->loc.path, NULL);

            goto err;
        }

        ret = dict_set_bin(xattr[i], conf->xattr_name, disk_layout, 4 * 4);
        if (ret != 0) {
            local->op_errno = ENOMEM;

            gf_smsg(this->name, GF_LOG_WARNING, 0,
                    DHT_MSG_DIR_SELFHEAL_XATTR_FAILED, "path=%s",
                    local->loc.path, "subvol=%s", conf->local_subvols[i]->name,
                    "set-xattr-failed", NULL);

            goto err;
        }
        disk_layout = NULL;

        gf_msg_trace(this->name, 0,
                     "setting commit hash %u on subvolume %s"
                     " for %s",
                     layout->list[j].commit_hash, conf->local_subvols[i]->name,
                     local->loc.path);
    }

    /* wind the setting of the commit hash across the local subvols */
    local->call_cnt = count;
    local->op_ret = 0;
    local->op_errno = 0;
    for (i = 0; i < count; i++) {
        STACK_WIND(frame, dht_update_commit_hash_for_layout_cbk,
                   conf->local_subvols[i],
                   conf->local_subvols[i]->fops->setxattr, &local->loc,
                   xattr[i], 0, NULL);
    }
    for (i = 0; i < count; i++)
        dict_unref(xattr[i]);
    GF_FREE(xattr);

    return 0;
err:
    if (xattr) {
        for (i = 0; i < count; i++) {
            if (xattr[i])
                dict_unref(xattr[i]);
        }

        GF_FREE(xattr);
    }

    GF_FREE(disk_layout);

    local->op_ret = -1;

    dht_update_commit_hash_for_layout_unlock(frame, this);

    return 0;
err_done:
    local->op_ret = -1;

    dht_update_commit_hash_for_layout_done(frame, NULL, this, 0, 0, NULL);

    return 0;
}

/* ENTER: dht_update_commit_hash_for_layout (see EXIT above)
 * This function is invoked from rebalance only.
 * As a result, the check here is simple enough to see if defrag is present
 * in the conf, as other data would be populated appropriately if so.
 * If ever this was to be used in other code paths, checks would need to
 * change.
 *
 * Functional details:
 *  - Lock the inodes on the subvols that we want the commit hash updated
 *  - Update each layout with the inode layout, modified to take in the new
 *    commit hash.
 *  - Unlock and return.
 */
int
dht_update_commit_hash_for_layout(call_frame_t *frame)
{
    dht_local_t *local = NULL;
    int count = 1, ret = -1, i = 0;
    dht_lock_t **lk_array = NULL;
    dht_conf_t *conf = NULL;

    GF_VALIDATE_OR_GOTO("dht", frame, err);
    GF_VALIDATE_OR_GOTO(frame->this->name, frame->local, err);

    local = frame->local;
    conf = frame->this->private;

    if (!conf->defrag)
        goto err;

    count = conf->local_subvols_cnt;
    lk_array = GF_CALLOC(count, sizeof(*lk_array), gf_common_mt_char);
    if (lk_array == NULL)
        goto err;

    for (i = 0; i < count; i++) {
        lk_array[i] = dht_lock_new(frame->this, conf->local_subvols[i],
                                   &local->loc, F_WRLCK, DHT_LAYOUT_HEAL_DOMAIN,
                                   NULL, FAIL_ON_ANY_ERROR);
        if (lk_array[i] == NULL)
            goto err;
    }

    local->lock[0].layout.my_layout.locks = lk_array;
    local->lock[0].layout.my_layout.lk_count = count;

    ret = dht_blocking_inodelk(frame, lk_array, count,
                               dht_update_commit_hash_for_layout_resume);
    if (ret < 0) {
        local->lock[0].layout.my_layout.locks = NULL;
        local->lock[0].layout.my_layout.lk_count = 0;
        goto err;
    }

    return 0;
err:
    if (lk_array != NULL) {
        dht_lock_array_free(lk_array, count);
        GF_FREE(lk_array);
    }

    return -1;
}
