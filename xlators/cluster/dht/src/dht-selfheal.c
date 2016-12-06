/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include "glusterfs.h"
#include "xlator.h"
#include "dht-common.h"
#include "dht-messages.h"
#include "glusterfs-acl.h"

#define DHT_SET_LAYOUT_RANGE(layout,i,srt,chunk,path)    do {           \
                layout->list[i].start = srt;                            \
                layout->list[i].stop  = srt + chunk - 1;                \
                layout->list[i].commit_hash = layout->commit_hash;      \
                                                                        \
                gf_msg_trace (this->name, 0,                            \
                              "gave fix: %u - %u, with commit-hash %u"  \
                              " on %s for %s",                          \
                              layout->list[i].start,                    \
                              layout->list[i].stop,                     \
                              layout->list[i].commit_hash,              \
                              layout->list[i].xlator->name, path);      \
        } while (0)

#define DHT_RESET_LAYOUT_RANGE(layout)    do {                          \
                int cnt = 0;                                            \
                for (cnt = 0; cnt < layout->cnt; cnt++ ) {              \
                        layout->list[cnt].start = 0;                    \
                        layout->list[cnt].stop  = 0;                    \
                }                                                       \
        } while (0)

int
dht_selfheal_layout_lock (call_frame_t *frame, dht_layout_t *layout,
                          gf_boolean_t newdir,
                          dht_selfheal_layout_t healer,
                          dht_need_heal_t should_heal);

static uint32_t
dht_overlap_calc (dht_layout_t *old, int o, dht_layout_t *new, int n)
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

	return min (old->list[o].stop, new->list[n].stop) -
	        max (old->list[o].start, new->list[n].start) + 1;
}

int
dht_selfheal_unlock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        DHT_STACK_DESTROY (frame);
        return 0;
}

int
dht_selfheal_dir_finish (call_frame_t *frame, xlator_t *this, int ret,
                         int invoke_cbk)
{
        dht_local_t  *local      = NULL, *lock_local = NULL;
        call_frame_t *lock_frame = NULL;
        int           lock_count = 0;

        local = frame->local;
        lock_count = dht_lock_count (local->lock.locks, local->lock.lk_count);
        if (lock_count == 0)
                goto done;

        lock_frame = copy_frame (frame);
        if (lock_frame == NULL) {
                goto done;
        }

        lock_local = dht_local_init (lock_frame, &local->loc, NULL,
                                     lock_frame->root->op);
        if (lock_local == NULL) {
                goto done;
        }

        lock_local->lock.locks = local->lock.locks;
        lock_local->lock.lk_count = local->lock.lk_count;

        local->lock.locks = NULL;
        local->lock.lk_count = 0;

        dht_unlock_inodelk (lock_frame, lock_local->lock.locks,
                            lock_local->lock.lk_count,
                            dht_selfheal_unlock_cbk);
        lock_frame = NULL;

done:
        if (invoke_cbk)
                local->selfheal.dir_cbk (frame, NULL, frame->this, ret,
                                         local->op_errno, NULL);
        if (lock_frame != NULL) {
                DHT_STACK_DESTROY (lock_frame);
        }

        return 0;
}

int
dht_refresh_layout_done (call_frame_t *frame)
{
        int                    ret         = -1;
        dht_layout_t          *refreshed   = NULL, *heal = NULL;
        dht_local_t           *local       = NULL;
        dht_need_heal_t        should_heal = NULL;
        dht_selfheal_layout_t  healer      = NULL;

        local = frame->local;

        refreshed = local->selfheal.refreshed_layout;
        heal = local->selfheal.layout;

        healer = local->selfheal.healer;
        should_heal = local->selfheal.should_heal;

        ret = dht_layout_sort (refreshed);
        if (ret == -1) {
                gf_msg (frame->this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_LAYOUT_SORT_FAILED,
                        "sorting the layout failed");
                goto err;
        }

        if (should_heal (frame, &heal, &refreshed)) {
                healer (frame, &local->loc, heal);
        } else {
                local->selfheal.layout = NULL;
                local->selfheal.refreshed_layout = NULL;
                local->selfheal.layout = refreshed;

                dht_layout_unref (frame->this, heal);

                dht_selfheal_dir_finish (frame, frame->this, 0, 1);
        }

        return 0;

err:
        dht_selfheal_dir_finish (frame, frame->this, -1, 1);
        return 0;
}

int
dht_refresh_layout_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, inode_t *inode,
                        struct iatt *stbuf, dict_t *xattr,
                        struct iatt *postparent)
{
        dht_local_t  *local         = NULL;
        int           this_call_cnt = 0;
        xlator_t     *prev          = NULL;
        dht_layout_t *layout        = NULL;

        GF_VALIDATE_OR_GOTO ("dht", frame, err);
        GF_VALIDATE_OR_GOTO ("dht", this, err);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, err);
        GF_VALIDATE_OR_GOTO ("dht", this->private, err);

        local = frame->local;
        prev  = cookie;

        layout = local->selfheal.refreshed_layout;

        LOCK (&frame->lock);
        {
                op_ret = dht_layout_merge (this, layout, prev,
                                           op_ret, op_errno, xattr);

                dht_iatt_merge (this, &local->stbuf, stbuf, prev);

                if (op_ret == -1) {
                        local->op_errno = op_errno;
                        gf_msg_debug (this->name, op_errno,
                                      "lookup of %s on %s returned error",
                                      local->loc.path, prev->name);

                        goto unlock;
                }

                local->op_ret = 0;
        }
unlock:
        UNLOCK (&frame->lock);

        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
                if (local->op_ret == 0) {
                        local->refresh_layout_done (frame);
                } else {
                        goto err;
                }

        }

        return 0;

err:
        local->refresh_layout_unlock (frame, this, -1, 1);
        return 0;
}

int
dht_refresh_layout (call_frame_t *frame)
{
        int          call_cnt = 0;
        int          i        = 0, ret = -1;
        dht_conf_t  *conf     = NULL;
        dht_local_t *local    = NULL;
        xlator_t    *this     = NULL;

        GF_VALIDATE_OR_GOTO ("dht", frame, out);
        GF_VALIDATE_OR_GOTO ("dht", frame->local, out);

        this = frame->this;
        conf = this->private;
        local = frame->local;

        call_cnt = conf->subvolume_cnt;
        local->call_cnt = call_cnt;
        local->op_ret = -1;

        if (local->selfheal.refreshed_layout) {
                dht_layout_unref (this, local->selfheal.refreshed_layout);
                local->selfheal.refreshed_layout = NULL;
        }

        local->selfheal.refreshed_layout = dht_layout_new (this,
                                                           conf->subvolume_cnt);
        if (!local->selfheal.refreshed_layout) {
                goto out;
        }

        if (local->xattr != NULL) {
                dict_del (local->xattr, conf->xattr_name);
        }

        if (local->xattr_req == NULL) {
                local->xattr_req = dict_new ();
                if (local->xattr_req == NULL) {
                        goto out;
                }
        }

        if (dict_get (local->xattr_req, conf->xattr_name) == 0) {
                ret = dict_set_uint32 (local->xattr_req, conf->xattr_name,
                                       4 * 4);
                if (ret)
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_DICT_SET_FAILED,
                                "%s: Failed to set dictionary value:key = %s",
                                local->loc.path, conf->xattr_name);
        }

        for (i = 0; i < call_cnt; i++) {
                STACK_WIND_COOKIE (frame, dht_refresh_layout_cbk,
                                   conf->subvolumes[i], conf->subvolumes[i],
                                   conf->subvolumes[i]->fops->lookup,
                                   &local->loc, local->xattr_req);
        }

        return 0;

out:
        local->refresh_layout_unlock (frame, this, -1, 1);
        return 0;
}


int32_t
dht_selfheal_layout_lock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                              int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        dht_local_t     *local = NULL;

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

        dht_refresh_layout (frame);
        return 0;

err:
        dht_selfheal_dir_finish (frame, this, -1, 1);
        return 0;
}


gf_boolean_t
dht_should_heal_layout (call_frame_t *frame, dht_layout_t **heal,
                        dht_layout_t **ondisk)
{
        gf_boolean_t  fixit = _gf_true;
        dht_local_t  *local = NULL;
        int           ret   = -1, heal_missing_dirs = 0;

        local = frame->local;

        if ((heal == NULL) || (*heal == NULL) || (ondisk == NULL)
            || (*ondisk == NULL))
                goto out;

        ret = dht_layout_anomalies (frame->this, &local->loc, *ondisk,
                                    &local->selfheal.hole_cnt,
                                    &local->selfheal.overlaps_cnt,
                                    NULL, &local->selfheal.down,
                                    &local->selfheal.misc, NULL);

        if (ret < 0)
                goto out;

        /* Directories might've been created as part of this self-heal. We've to
         * sync non-layout xattrs and set range 0-0 on new directories
         */
        heal_missing_dirs = local->selfheal.force_mkdir
                ? local->selfheal.force_mkdir : dht_layout_missing_dirs (*heal);

        if ((local->selfheal.hole_cnt == 0)
            && (local->selfheal.overlaps_cnt == 0) && heal_missing_dirs) {
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

        fixit = (local->selfheal.hole_cnt || local->selfheal.overlaps_cnt
                 || heal_missing_dirs);

out:
        return fixit;
}

int
dht_layout_span (dht_layout_t *layout)
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
dht_decommissioned_bricks_in_layout (xlator_t *this, dht_layout_t *layout)
{
        dht_conf_t *conf  = NULL;
        int         count = 0, i = 0, j = 0;

        if ((this == NULL) || (layout == NULL))
                goto out;

        conf = this->private;

        for (i = 0; i < layout->cnt; i++) {
                for (j = 0; j < conf->subvolume_cnt; j++) {
                        if (conf->decommissioned_bricks[j] &&
                            conf->decommissioned_bricks[j]
                            == layout->list[i].xlator) {
                                count++;
                        }
                }
        }

out:
        return count;
}

dht_distribution_type_t
dht_distribution_type (xlator_t *this, dht_layout_t *layout)
{
        dht_distribution_type_t type        = GF_DHT_EQUAL_DISTRIBUTION;
        int                     i           = 0;
        uint32_t                start_range = 0, range = 0, diff = 0;

        if ((this == NULL) || (layout == NULL) || (layout->cnt < 1)) {
                goto out;
        }

        for (i = 0; i < layout->cnt; i++) {
                if (start_range == 0) {
                        start_range = layout->list[i].stop
                                - layout->list[i].start;
                        continue;
                }

                range = layout->list[i].stop - layout->list[i].start;
                diff = (range >= start_range)
                        ? range - start_range
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
dht_should_fix_layout (call_frame_t *frame, dht_layout_t **inmem,
                       dht_layout_t **ondisk)
{
        gf_boolean_t             fixit                 = _gf_true;

        dht_local_t             *local                 = NULL;
        int                      layout_span           = 0;
        int                      decommissioned_bricks = 0;
        int                      ret                   = 0;
        dht_conf_t              *conf                  = NULL;
        dht_distribution_type_t  inmem_dist_type       = 0;
        dht_distribution_type_t  ondisk_dist_type      = 0;

        conf = frame->this->private;

        local = frame->local;

        if ((inmem == NULL) || (*inmem == NULL) || (ondisk == NULL)
            || (*ondisk == NULL))
                goto out;

        ret = dht_layout_anomalies (frame->this, &local->loc, *ondisk,
                                    &local->selfheal.hole_cnt,
                                    &local->selfheal.overlaps_cnt, NULL,
                                    &local->selfheal.down,
                                    &local->selfheal.misc, NULL);
        if (ret < 0) {
                fixit = _gf_false;
                goto out;
        }

        if (local->selfheal.down || local->selfheal.misc) {
                fixit = _gf_false;
                goto out;
        }

        if (local->selfheal.hole_cnt || local->selfheal.overlaps_cnt)
                goto out;

        /* If commit hashes are being updated, let it through */
        if ((*inmem)->commit_hash != (*ondisk)->commit_hash)
                goto out;

        layout_span = dht_layout_span (*ondisk);

        decommissioned_bricks
                = dht_decommissioned_bricks_in_layout (frame->this,
                                                       *ondisk);
        inmem_dist_type = dht_distribution_type (frame->this, *inmem);
        ondisk_dist_type = dht_distribution_type (frame->this, *ondisk);

        if ((decommissioned_bricks == 0)
            && (layout_span == (conf->subvolume_cnt
                                - conf->decommission_subvols_cnt))
            && (inmem_dist_type == ondisk_dist_type))
                fixit = _gf_false;

out:

        return fixit;
}

int
dht_selfheal_layout_lock (call_frame_t *frame, dht_layout_t *layout,
                          gf_boolean_t newdir,
                          dht_selfheal_layout_t healer,
                          dht_need_heal_t should_heal)
{
        dht_local_t   *local    = NULL;
        int            count    = 1, ret = -1, i = 0;
        dht_lock_t   **lk_array = NULL;
        dht_conf_t    *conf     = NULL;
        dht_layout_t  *tmp      = NULL;

        GF_VALIDATE_OR_GOTO ("dht", frame, err);
        GF_VALIDATE_OR_GOTO (frame->this->name, frame->local, err);

        local = frame->local;

        conf = frame->this->private;

        local->selfheal.healer = healer;
        local->selfheal.should_heal = should_heal;

        tmp = local->selfheal.layout;
        local->selfheal.layout = dht_layout_ref (frame->this, layout);
        dht_layout_unref (frame->this, tmp);

        if (!newdir) {
                count = conf->subvolume_cnt;

                lk_array = GF_CALLOC (count, sizeof (*lk_array),
                                      gf_common_mt_char);
                if (lk_array == NULL)
                        goto err;

                for (i = 0; i < count; i++) {
                        lk_array[i] = dht_lock_new (frame->this,
                                                    conf->subvolumes[i],
                                                    &local->loc, F_WRLCK,
                                                    DHT_LAYOUT_HEAL_DOMAIN);
                        if (lk_array[i] == NULL)
                                goto err;
                }
        } else {
                count = 1;
                lk_array = GF_CALLOC (count, sizeof (*lk_array),
                                      gf_common_mt_char);
                if (lk_array == NULL)
                        goto err;

                lk_array[0] = dht_lock_new (frame->this, local->hashed_subvol,
                                            &local->loc, F_WRLCK,
                                            DHT_LAYOUT_HEAL_DOMAIN);
                if (lk_array[0] == NULL)
                        goto err;
        }

        local->lock.locks = lk_array;
        local->lock.lk_count = count;

        ret = dht_blocking_inodelk (frame, lk_array, count, FAIL_ON_ANY_ERROR,
                                    dht_selfheal_layout_lock_cbk);
        if (ret < 0) {
                local->lock.locks = NULL;
                local->lock.lk_count = 0;
                goto err;
        }

        return 0;
err:
        if (lk_array != NULL) {
                dht_lock_array_free (lk_array, count);
                GF_FREE (lk_array);
        }

        return -1;
}

int
dht_selfheal_dir_xattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int op_ret, int op_errno, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        xlator_t     *subvol = NULL;
        struct iatt  *stbuf = NULL;
        int           i = 0;
        int           ret = 0;
        dht_layout_t *layout = NULL;
        int           err = 0;
        int           this_call_cnt = 0;

        local = frame->local;
        layout = local->selfheal.layout;
        subvol = cookie;

        if (op_ret == 0)
                err = 0;
        else
                err = op_errno;

        ret = dict_get_bin (xdata, DHT_IATT_IN_XDATA_KEY, (void **) &stbuf);
        if (ret < 0) {
                gf_msg_debug (this->name, 0, "key = %s not present in dict",
                              DHT_IATT_IN_XDATA_KEY);
        }

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].xlator == subvol) {
                        layout->list[i].err = err;
                        break;
                }
        }

        LOCK (&frame->lock);
        {
                dht_iatt_merge (this, &local->stbuf, stbuf, subvol);
        }
        UNLOCK (&frame->lock);

        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
                dht_selfheal_dir_finish (frame, this, 0, 1);
        }

        return 0;
}


int
dht_selfheal_dir_xattr_persubvol (call_frame_t *frame, loc_t *loc,
                                  dht_layout_t *layout, int i,
                                  xlator_t *req_subvol)
{
        xlator_t          *subvol = NULL;
        dict_t            *xattr = NULL;
        dict_t            *xdata = NULL;
        int                ret = 0;
        xlator_t          *this = NULL;
        int32_t           *disk_layout = NULL;
        dht_local_t       *local = NULL;
        dht_conf_t        *conf = NULL;
        data_t            *data = NULL;
        char              gfid[GF_UUID_BUF_SIZE] = {0};

        local = frame->local;
        if (req_subvol)
                subvol = req_subvol;
        else
                subvol = layout->list[i].xlator;
        this = frame->this;

        GF_VALIDATE_OR_GOTO ("", this, err);
        GF_VALIDATE_OR_GOTO (this->name, layout, err);
        GF_VALIDATE_OR_GOTO (this->name, local, err);
        GF_VALIDATE_OR_GOTO (this->name, subvol, err);
        VALIDATE_OR_GOTO (this->private, err);

        conf = this->private;

        xattr = dict_new ();
        if (!xattr) {
                goto err;
        }

        xdata = dict_new ();
        if (!xdata)
                goto err;

        ret = dict_set_str (xdata, GLUSTERFS_INTERNAL_FOP_KEY, "yes");
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0, DHT_MSG_DICT_SET_FAILED,
                        "%s: Failed to set dictionary value: key = %s,"
                        " gfid = %s", loc->path,
                        GLUSTERFS_INTERNAL_FOP_KEY, gfid);
                goto err;
        }

        ret = dict_set_dynstr_with_alloc (xdata, DHT_IATT_IN_XDATA_KEY, "yes");
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING, 0, DHT_MSG_DICT_SET_FAILED,
                        "%s: Failed to set dictionary value: key = %s,"
                        " gfid = %s", loc->path,
                        DHT_IATT_IN_XDATA_KEY, gfid);
                goto err;
        }

        gf_uuid_unparse(loc->inode->gfid, gfid);

        ret = dht_disk_layout_extract (this, layout, i, &disk_layout);
        if (ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_DIR_SELFHEAL_XATTR_FAILED,
                        "Directory self heal xattr failed:"
                        " %s: (subvol %s) Failed to extract disk layout,"
                        " gfid = %s", loc->path, subvol->name, gfid);
                goto err;
        }

        ret = dict_set_bin (xattr, conf->xattr_name, disk_layout, 4 * 4);
        if (ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_DIR_SELFHEAL_XATTR_FAILED,
                        "Directory self heal xattr failed:"
                        "%s: (subvol %s) Failed to set xattr dictionary,"
                        " gfid = %s", loc->path, subvol->name, gfid);
                goto err;
        }
        disk_layout = NULL;

        gf_msg_trace (this->name, 0,
                      "setting hash range %u - %u (type %d) on subvolume %s"
                      " for %s", layout->list[i].start, layout->list[i].stop,
                      layout->type, subvol->name, loc->path);

        if (local->xattr) {
                data = dict_get (local->xattr, QUOTA_LIMIT_KEY);
                if (data) {
                        ret = dict_add (xattr, QUOTA_LIMIT_KEY, data);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        DHT_MSG_DICT_SET_FAILED,
                                        "%s: Failed to set dictionary value:"
                                        " key = %s",
                                        loc->path, QUOTA_LIMIT_KEY);
                        }
                }
                data = dict_get (local->xattr, QUOTA_LIMIT_OBJECTS_KEY);
                if (data) {
                        ret = dict_add (xattr, QUOTA_LIMIT_OBJECTS_KEY, data);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        DHT_MSG_DICT_SET_FAILED,
                                        "%s: Failed to set dictionary value:"
                                        " key = %s",
                                        loc->path, QUOTA_LIMIT_OBJECTS_KEY);
                        }
                }
        }

        if (!gf_uuid_is_null (local->gfid))
                gf_uuid_copy (loc->gfid, local->gfid);

        STACK_WIND_COOKIE (frame, dht_selfheal_dir_xattr_cbk,
                    (void *) subvol, subvol, subvol->fops->setxattr,
                    loc, xattr, 0, xdata);

        dict_unref (xattr);
        dict_unref (xdata);

        return 0;

err:
        if (xattr)
                dict_unref (xattr);

        if (xdata)
                dict_unref (xdata);

        GF_FREE (disk_layout);

        dht_selfheal_dir_xattr_cbk (frame, (void *) subvol, frame->this,
                                    -1, ENOMEM, NULL);
        return 0;
}

int
dht_fix_dir_xattr (call_frame_t *frame, loc_t *loc, dht_layout_t *layout)
{
        dht_local_t *local = NULL;
        int          i = 0;
        int          count = 0;
        xlator_t    *this = NULL;
        dht_conf_t  *conf = NULL;
        dht_layout_t *dummy = NULL;

        local = frame->local;
        this = frame->this;
        conf = this->private;

        gf_msg_debug (this->name, 0,
                      "%s: Writing the new range for all subvolumes",
                      loc->path);

        local->call_cnt = count = conf->subvolume_cnt;

        if (gf_log_get_loglevel () >= GF_LOG_DEBUG)
                dht_log_new_layout_for_dir_selfheal (this, loc, layout);

        for (i = 0; i < layout->cnt; i++) {
                dht_selfheal_dir_xattr_persubvol (frame, loc, layout, i, NULL);

                if (--count == 0)
                        goto out;
        }
        /* if we are here, subvolcount > layout_count. subvols-per-directory
         * option might be set here. We need to clear out layout from the
         * non-participating subvolumes, else it will result in overlaps */
        dummy = dht_layout_new (this, 1);
        if (!dummy)
                goto out;
        dummy->commit_hash = layout->commit_hash;
        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (_gf_false ==
                    dht_is_subvol_in_layout (layout, conf->subvolumes[i])) {
                        dht_selfheal_dir_xattr_persubvol (frame, loc, dummy, 0,
                                                          conf->subvolumes[i]);
                        if (--count == 0)
                                break;
                }
        }

        dht_layout_unref (this, dummy);
out:
        return 0;
}

int
dht_selfheal_dir_xattr (call_frame_t *frame, loc_t *loc, dht_layout_t *layout)
{
        dht_local_t *local = NULL;
        int          missing_xattr = 0;
        int          i = 0;
        xlator_t    *this = NULL;
        dht_conf_t   *conf = NULL;
        dht_layout_t *dummy = NULL;

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
                if (_gf_false ==
                    dht_is_subvol_in_layout (layout, conf->subvolumes[i])) {
                        missing_xattr++;
                }
        }
        gf_msg_trace (this->name, 0,
                      "%d subvolumes missing xattr for %s",
                      missing_xattr, loc->path);

        if (missing_xattr == 0) {
                dht_selfheal_dir_finish (frame, this, 0, 1);
                return 0;
        }

        local->call_cnt = missing_xattr;

        if (gf_log_get_loglevel () >= GF_LOG_DEBUG)
                dht_log_new_layout_for_dir_selfheal (this, loc, layout);

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err != -1 || !layout->list[i].stop)
                        continue;

                dht_selfheal_dir_xattr_persubvol (frame, loc, layout, i, NULL);

                if (--missing_xattr == 0)
                        break;
        }
        dummy = dht_layout_new (this, 1);
        if (!dummy)
                goto out;
        for (i = 0; i < conf->subvolume_cnt && missing_xattr; i++) {
                if (_gf_false ==
                    dht_is_subvol_in_layout (layout, conf->subvolumes[i])) {
                        dht_selfheal_dir_xattr_persubvol (frame, loc, dummy, 0,
                                                          conf->subvolumes[i]);
                        missing_xattr--;
                }
        }

        dht_layout_unref (this, dummy);
out:
        return 0;
}

gf_boolean_t
dht_is_subvol_part_of_layout (dht_layout_t *layout, xlator_t *xlator)
{
        int               i = 0;
        gf_boolean_t    ret = _gf_false;

        for (i = 0; i < layout->cnt; i++) {
                if (!strcmp (layout->list[i].xlator->name, xlator->name)) {
                        ret = _gf_true;
                        break;

                }
        }

        return ret;
}

int
dht_layout_index_from_conf (dht_layout_t *layout, xlator_t *xlator)
{
        int i = -1;
        int j = 0;

        for (j = 0; j < layout->cnt; j++) {
                if (!strcmp (layout->list[j].xlator->name, xlator->name)) {
                        i = j;
                        break;
                }
        }

        return i;
}


static int
dht_selfheal_dir_xattr_for_nameless_lookup (call_frame_t *frame, loc_t *loc,
                                            dht_layout_t  *layout)
{
        dht_local_t     *local = NULL;
        int             missing_xattr = 0;
        int             i = 0;
        xlator_t        *this = NULL;
        dht_conf_t      *conf = NULL;
        dht_layout_t    *dummy = NULL;
        int             j = 0;

        local = frame->local;
        this = frame->this;
        conf = this->private;

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err != -1 || !layout->list[i].stop) {
                        /* err != -1 would mean xattr present on the directory
                           or the directory is non existent.
                           !layout->list[i].stop would mean layout absent
                        */

                        continue;
                }
                missing_xattr++;
        }

        /* Also account for subvolumes with no-layout. Used for zero'ing out
           the layouts and for setting quota key's if present */

        /* Send  where either the subvol is not part of layout,
         * or it is part of the layout but error is non-zero but error
         * is not equal to -1 or ENOENT.
         */

        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (dht_is_subvol_part_of_layout (layout, conf->subvolumes[i])
                    == _gf_false) {
                        missing_xattr++;
                        continue;
                }

                j = dht_layout_index_from_conf (layout, conf->subvolumes[i]);

                if ((j != -1) && (layout->list[j].err != -1) &&
                   (layout->list[j].err != 0) &&
                   (layout->list[j].err != ENOENT)) {
                        missing_xattr++;
                }

        }


        gf_msg_trace (this->name, 0,
                      "%d subvolumes missing xattr for %s",
                      missing_xattr, loc->path);

        if (missing_xattr == 0) {
                dht_selfheal_dir_finish (frame, this, 0, 1);
                return 0;
        }

        local->call_cnt = missing_xattr;

        if (gf_log_get_loglevel () >= GF_LOG_DEBUG)
                dht_log_new_layout_for_dir_selfheal (this, loc, layout);

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err != -1 || !layout->list[i].stop)
                        continue;

                dht_selfheal_dir_xattr_persubvol (frame, loc, layout, i, NULL);

                if (--missing_xattr == 0)
                        break;
        }

        dummy = dht_layout_new (this, 1);
        if (!dummy)
                goto out;

        for (i = 0; i < conf->subvolume_cnt && missing_xattr; i++) {
              if (dht_is_subvol_part_of_layout (layout, conf->subvolumes[i])
                  == _gf_false) {
                        dht_selfheal_dir_xattr_persubvol (frame, loc, dummy, 0,
                                                          conf->subvolumes[i]);
                        missing_xattr--;
                        continue;
              }

                j = dht_layout_index_from_conf (layout, conf->subvolumes[i]);

                if ((j != -1) && (layout->list[j].err != -1) &&
                    (layout->list[j].err != ENOENT) &&
                    (layout->list[j].err != 0)) {
                        dht_selfheal_dir_xattr_persubvol (frame, loc, dummy, 0,
                                                          conf->subvolumes[i]);
                        missing_xattr--;
                }
        }

        dht_layout_unref (this, dummy);
out:
        return 0;

}

int
dht_selfheal_dir_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                              int op_ret, int op_errno, struct iatt *statpre,
                              struct iatt *statpost, dict_t *xdata)
{
        dht_local_t   *local = NULL;
        dht_layout_t  *layout = NULL;
        int            this_call_cnt = 0, ret = -1;

        local  = frame->local;
        layout = local->selfheal.layout;

        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
                ret = dht_selfheal_layout_lock (frame, layout, _gf_false,
                                                dht_selfheal_dir_xattr,
                                                dht_should_heal_layout);

                if (ret < 0) {
                        dht_selfheal_dir_finish (frame, this, -1, 1);
                }
        }

        return 0;
}


int
dht_selfheal_dir_setattr (call_frame_t *frame, loc_t *loc, struct iatt *stbuf,
                          int32_t valid, dht_layout_t *layout)
{
        int           missing_attr = 0;
        int           i     = 0, ret = -1;
        dht_local_t  *local = NULL;
        xlator_t     *this = NULL;

        local = frame->local;
        this = frame->this;

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err == -1)
                        missing_attr++;
        }

        if (missing_attr == 0) {
                ret = dht_selfheal_layout_lock (frame, layout, _gf_false,
                                                dht_selfheal_dir_xattr,
                                                dht_should_heal_layout);

                if (ret < 0) {
                        dht_selfheal_dir_finish (frame, this, -1, 1);
                }

                return 0;
        }

        if (!gf_uuid_is_null (local->gfid))
                gf_uuid_copy (loc->gfid, local->gfid);

        local->call_cnt = missing_attr;
        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err == -1) {
                        gf_msg_trace (this->name, 0,
                                      "%s: setattr on subvol %s, gfid = %s",
                                      loc->path, layout->list[i].xlator->name,
                                      uuid_utoa(loc->gfid));

                        STACK_WIND (frame, dht_selfheal_dir_setattr_cbk,
                                    layout->list[i].xlator,
                                    layout->list[i].xlator->fops->setattr,
                                    loc, stbuf, valid, NULL);
                }
        }

        return 0;
}

int
dht_selfheal_dir_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int op_ret, int op_errno,
                            inode_t *inode, struct iatt *stbuf,
                            struct iatt *preparent, struct iatt *postparent,
                            dict_t *xdata)
{
        dht_local_t   *local = NULL;
        dht_layout_t  *layout = NULL;
        xlator_t      *prev = NULL;
        xlator_t      *subvol = NULL;
        int            i = 0, ret = -1;
        int            this_call_cnt = 0;
        char           gfid[GF_UUID_BUF_SIZE] = {0};

        local  = frame->local;
        layout = local->selfheal.layout;
        prev   = cookie;
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
                gf_msg (this->name, ((op_errno == EEXIST) ? GF_LOG_DEBUG :
                                     GF_LOG_WARNING),
                        op_errno, DHT_MSG_DIR_SELFHEAL_FAILED,
                        "Directory selfheal failed: path = %s, gfid = %s",
                        local->loc.path, gfid );
                goto out;
        }
        dht_iatt_merge (this, &local->preparent, preparent, prev);
        dht_iatt_merge (this, &local->postparent, postparent, prev);
        ret = 0;

out:
        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
                dht_selfheal_dir_finish (frame, this, ret, 0);
                dht_selfheal_dir_setattr (frame, &local->loc, &local->stbuf, 0xffffff, layout);
        }

        return 0;
}

void
dht_selfheal_dir_mkdir_setacl (dict_t *xattr, dict_t *dict)
{
        data_t          *acl_default = NULL;
        data_t          *acl_access = NULL;
        xlator_t        *this = NULL;
        int     ret = -1;

        GF_ASSERT (xattr);
        GF_ASSERT (dict);

        this = THIS;
        GF_ASSERT (this);

        acl_default = dict_get (xattr, POSIX_ACL_DEFAULT_XATTR);

        if (!acl_default) {
                gf_msg_debug (this->name, 0,
                              "ACL_DEFAULT xattr not present");
                goto cont;
        }
        ret = dict_set (dict, POSIX_ACL_DEFAULT_XATTR, acl_default);
        if (ret)
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_DICT_SET_FAILED,
                        "Failed to set dictionary value.key = %s",
                        POSIX_ACL_DEFAULT_XATTR);
cont:
        acl_access = dict_get (xattr, POSIX_ACL_ACCESS_XATTR);
        if (!acl_access) {
                gf_msg_debug (this->name, 0,
                              "ACL_ACCESS xattr not present");
                goto out;
        }
        ret = dict_set (dict, POSIX_ACL_ACCESS_XATTR, acl_access);
        if (ret)
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_DICT_SET_FAILED,
                        "Failed to set dictionary value.key = %s",
                        POSIX_ACL_ACCESS_XATTR);

out:
        return;
}

int
dht_selfheal_dir_mkdir_lookup_done (call_frame_t *frame, xlator_t *this)
{
        dht_local_t  *local = NULL;
        int           i     = 0;
        int           ret   = -1;
        dict_t       *dict = NULL;
        dht_layout_t  *layout = NULL;
        loc_t        *loc   = NULL;

        VALIDATE_OR_GOTO (this->private, err);

        local = frame->local;
        layout = local->layout;
        loc    = &local->loc;

        if (!gf_uuid_is_null (local->gfid)) {
                dict = dict_new ();
                if (!dict)
                        return -1;

                ret = dict_set_static_bin (dict, "gfid-req", local->gfid, 16);
                if (ret)
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_DICT_SET_FAILED,
                                "%s: Failed to set dictionary value:"
                                " key = gfid-req", loc->path);
        } else if (local->params) {
                /* Send the dictionary from higher layers directly */

                dict = dict_ref (local->params);
        }
        /* Set acls */
        if (local->xattr && dict)
                dht_selfheal_dir_mkdir_setacl (local->xattr, dict);

        if (!dict)
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_DICT_SET_FAILED,
                        "dict is NULL, need to make sure gfids are same");

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err == ESTALE ||
                    layout->list[i].err == ENOENT ||
                    local->selfheal.force_mkdir) {
                        gf_msg_debug (this->name, 0,
                                      "Creating directory %s on subvol %s",
                                      loc->path, layout->list[i].xlator->name);

                        STACK_WIND_COOKIE (frame, dht_selfheal_dir_mkdir_cbk,
                                           layout->list[i].xlator,
                                           layout->list[i].xlator,
                                           layout->list[i].xlator->fops->mkdir,
                                           loc,
                                           st_mode_from_ia (local->stbuf.ia_prot,
                                                            local->stbuf.ia_type),
                                           0, dict);
                }
        }

        if (dict)
                dict_unref (dict);

        return 0;

err:
        dht_selfheal_dir_finish (frame, this, -1, 1);
        return 0;
}

int
dht_selfheal_dir_mkdir_lookup_cbk (call_frame_t *frame, void *cookie,
                                   xlator_t *this, int op_ret, int op_errno,
                                   inode_t *inode, struct iatt *stbuf,
                                   dict_t *xattr, struct iatt *postparent)
{
        dht_local_t  *local = NULL;
        int           i     = 0;
        int           this_call_cnt = 0;
        int           missing_dirs = 0;
        dht_layout_t  *layout = NULL;
        loc_t         *loc    = NULL;
        xlator_t      *prev    = NULL;

        VALIDATE_OR_GOTO (this->private, err);

        local = frame->local;
        layout = local->layout;
        loc = &local->loc;
        prev  = cookie;

        this_call_cnt = dht_frame_return (frame);

        LOCK (&frame->lock);
        {
                if ((op_ret < 0) &&
                    (op_errno == ENOENT || op_errno == ESTALE)) {
                        local->selfheal.hole_cnt = !local->selfheal.hole_cnt ? 1
                                                : local->selfheal.hole_cnt + 1;
                }

                if (!op_ret) {
                        dht_iatt_merge (this, &local->stbuf, stbuf, prev);
                }

        }
        UNLOCK (&frame->lock);

        if (is_last_call (this_call_cnt)) {
                if (local->selfheal.hole_cnt == layout->cnt) {
                        gf_msg_debug (this->name, op_errno,
                                      "Lookup failed, an rmdir could have "
                                      "deleted this entry %s", loc->name);
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
                                dht_selfheal_dir_finish (frame, this, 0, 0);
                                dht_selfheal_dir_setattr (frame, loc,
                                                          &local->stbuf,
                                                          0xffffffff, layout);
                                return 0;
                        }

                        local->call_cnt = missing_dirs;
                        dht_selfheal_dir_mkdir_lookup_done (frame, this);
                }
        }

        return 0;

err:
        dht_selfheal_dir_finish (frame, this, -1, 1);
        return 0;
}


int
dht_selfheal_dir_mkdir_lock_cbk (call_frame_t *frame, void *cookie,
                                 xlator_t *this, int32_t op_ret,
                                 int32_t op_errno, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        dht_conf_t   *conf  = NULL;
        int           i     = 0;

        VALIDATE_OR_GOTO (this->private, err);

        conf = this->private;
        local = frame->local;

	    local->call_cnt = conf->subvolume_cnt;

        if (op_ret < 0) {

                /* We get this error when the directory entry was not created
                 * on a newky attatched tier subvol. Hence proceed and do mkdir
                 * on the tier subvol.
                 */
                if (op_errno == EINVAL) {
                        local->call_cnt = 1;
                        dht_selfheal_dir_mkdir_lookup_done (frame, this);
                        return 0;
                }

                gf_msg (this->name, GF_LOG_WARNING, op_errno,
                        DHT_MSG_INODE_LK_ERROR,
                        "acquiring inodelk failed for %s",
                        local->loc.path);

                local->op_errno = op_errno;
                goto err;
        }

        /* After getting locks, perform lookup again to ensure that the
           directory was not deleted by a racing rmdir
        */

        for (i = 0; i < conf->subvolume_cnt; i++) {
                STACK_WIND_COOKIE (frame, dht_selfheal_dir_mkdir_lookup_cbk,
                                   conf->subvolumes[i], conf->subvolumes[i],
                                   conf->subvolumes[i]->fops->lookup,
                                   &local->loc, NULL);
        }

        return 0;

err:
        dht_selfheal_dir_finish (frame, this, -1, 1);
        return 0;
}

int
dht_selfheal_dir_mkdir (call_frame_t *frame, loc_t *loc,
                        dht_layout_t *layout, int force)
{
        int           missing_dirs = 0;
        int           i     = 0;
        int           ret   = -1;
        int           count = 1;
        dht_local_t  *local = NULL;
        dht_conf_t   *conf  = NULL;
        xlator_t     *this = NULL;
        dht_lock_t   **lk_array = NULL;

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
                dht_selfheal_dir_setattr (frame, loc, &local->stbuf,
                                          0xffffffff, layout);
                return 0;
        }

        count = conf->subvolume_cnt;

        /* Locking on all subvols in the mkdir phase of lookup selfheal is
           is done to synchronize with rmdir/rename.
        */
        lk_array = GF_CALLOC (count, sizeof (*lk_array), gf_common_mt_char);
        if (lk_array == NULL)
                goto err;

        for (i = 0; i < count; i++) {
                lk_array[i] = dht_lock_new (frame->this,
                                            conf->subvolumes[i],
                                            &local->loc, F_WRLCK,
                                            DHT_LAYOUT_HEAL_DOMAIN);
                if (lk_array[i] == NULL)
                        goto err;
        }

        local->lock.locks = lk_array;
        local->lock.lk_count = count;

        ret = dht_blocking_inodelk (frame, lk_array, count,
                                    IGNORE_ENOENT_ESTALE,
                                    dht_selfheal_dir_mkdir_lock_cbk);

        if (ret < 0) {
                local->lock.locks = NULL;
                local->lock.lk_count = 0;
                goto err;
        }

        return 0;
err:
        if (lk_array != NULL) {
                dht_lock_array_free (lk_array, count);
                GF_FREE (lk_array);
        }

        return -1;
}

int
dht_selfheal_layout_alloc_start (xlator_t *this, loc_t *loc,
                                 dht_layout_t *layout)
{
        int         start                               = 0;
        uint32_t    hashval                             = 0;
        int         ret                                 = 0;
        const char *str                                 = NULL;
        dht_conf_t *conf                                = NULL;
        char           buf[UUID_CANONICAL_FORM_LEN + 1] = {0, };

        conf = this->private;

        if (conf->randomize_by_gfid) {
                str = uuid_utoa_r (loc->gfid, buf);
        } else {
                str = loc->path;
        }

        ret = dht_hash_compute (this, layout->type, str, &hashval);
        if (ret == 0) {
                start = (hashval % layout->cnt);
        }

        return start;
}

static int
dht_get_layout_count (xlator_t *this, dht_layout_t *layout, int new_layout)
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
         * un-availbale). Else return count (available up bricks) */
        count = ((layout->spread_cnt &&
                 (layout->spread_cnt <= count)) ?
                 layout->spread_cnt : ((count) ? count : 1));

        return count;
}


void dht_selfheal_layout_new_directory (call_frame_t *frame, loc_t *loc,
					dht_layout_t *new_layout);

void dht_layout_entry_swap (dht_layout_t *layout, int i, int j);
void dht_layout_range_swap (dht_layout_t *layout, int i, int j);

/*
 * It's a bit icky using local variables in a macro, but it makes the rest
 * of the code a lot clearer.
 */
#define OV_ENTRY(x,y)      table[x*new->cnt+y]

void
dht_selfheal_layout_maximize_overlap (call_frame_t *frame, loc_t *loc,
				      dht_layout_t *new, dht_layout_t *old)
{
	int           i            = 0;
	int           j            = 0;
	uint32_t      curr_overlap = 0;
	uint32_t      max_overlap  = 0;
	int           max_overlap_idx = -1;
	uint32_t      overlap      = 0;
        uint32_t     *table = NULL;

	dht_layout_sort_volname (old);
	/* Now both old_layout->list[] and new_layout->list[]
	   are match the same xlators/subvolumes. i.e,
	   old_layout->[i] and new_layout->[i] are referring
	   to the same subvolumes
	*/

        /* Build a table of overlaps between new[i] and old[j]. */
        table = alloca(sizeof(overlap)*old->cnt*new->cnt);
        if (!table) {
                return;
        }
        memset(table,0,sizeof(overlap)*old->cnt*new->cnt);
        for (i = 0; i < new->cnt; ++i) {
                for (j = 0; j < old->cnt; ++j) {
                        OV_ENTRY(i,j) = dht_overlap_calc(old,j,new,i);
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
                        curr_overlap = OV_ENTRY(i,i) + OV_ENTRY(j,j);
                        /* Calculate the overlap after the proposed swap. */
                        overlap = OV_ENTRY(i,j) + OV_ENTRY(j,i);
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
 			dht_layout_range_swap (new, i, max_overlap_idx);
                        /* Need to swap the table values too. */
                        for (j = 0; j < old->cnt; ++j) {
                                overlap = OV_ENTRY(i,j);
                                OV_ENTRY(i,j) = OV_ENTRY(max_overlap_idx,j);
                                OV_ENTRY(max_overlap_idx,j) = overlap;
                        }
                }
	}
}


dht_layout_t *
dht_fix_layout_of_directory (call_frame_t *frame, loc_t *loc,
                             dht_layout_t *layout)
{
        int           i            = 0;
        xlator_t     *this         = NULL;
        dht_layout_t *new_layout   = NULL;
        dht_conf_t   *priv         = NULL;
        dht_local_t  *local        = NULL;
        uint32_t      subvol_down  = 0;
        int           ret          = 0;
        gf_boolean_t  maximize_overlap = _gf_true;

        this  = frame->this;
        priv  = this->private;
        local = frame->local;

        if (layout->type == DHT_HASH_TYPE_DM_USER) {
                gf_msg_debug (THIS->name, 0, "leaving %s alone",
                              loc->path);
                goto done;
        }

        new_layout = dht_layout_new (this, priv->subvolume_cnt);
        if (!new_layout)
                goto done;

        /* If a subvolume is down, do not re-write the layout. */
        ret = dht_layout_anomalies (this, loc, layout, NULL, NULL, NULL,
                                    &subvol_down, NULL, NULL);

        if (subvol_down || (ret == -1)) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_LAYOUT_FIX_FAILED,
                        "Layout fix failed: %u subvolume(s) are down"
                        ". Skipping fix layout.", subvol_down);
                GF_FREE (new_layout);
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
                        gf_msg (this->name, GF_LOG_DEBUG, 0,
                                DHT_MSG_SUBVOL_INFO,
                                "subvolume %d (%s): %u chunks", i,
                                priv->subvolumes[i]->name,
                                priv->du_stats[i].chunks);

                        /* Maximize overlap if the bricks are all the same
                         *  size.
                         * This is probably not going to be very common on
                         * live setups but will benefit our regression tests
                         */
                        if (i && (priv->du_stats[i].chunks
                                  != priv->du_stats[0].chunks)) {
                                maximize_overlap = _gf_false;
                        }
                }
        } else {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_NO_DISK_USAGE_STATUS, "no du stats ?!?");
        }

	/* First give it a layout as though it is a new directory. This
	   ensures rotation to kick in */
        dht_layout_sort_volname (new_layout);
	dht_selfheal_layout_new_directory (frame, loc, new_layout);


        /* Maximize overlap if weighted-rebalance is disabled */
        if (!priv->do_weighting)
                maximize_overlap = _gf_true;

	/* Now selectively re-assign ranges only when it helps */
        if (maximize_overlap) {
                dht_selfheal_layout_maximize_overlap (frame, loc, new_layout,
                                                      layout);
        }
done:
        if (new_layout) {
                /* Now that the new layout has all the proper layout, change the
                   inode context */
                dht_layout_set (this, loc->inode, new_layout);

                /* Make sure the extra 'ref' for existing layout is removed */
                dht_layout_unref (this, local->layout);

                local->layout = new_layout;
        }

        return local->layout;
}


/*
 * Having to call this 2x for each entry in the layout is pretty horrible, but
 * that's what all of this layout-sorting nonsense gets us.
 */
uint32_t
dht_get_chunks_from_xl (xlator_t *parent, xlator_t *child)
{
        dht_conf_t      *priv   = parent->private;
        xlator_list_t   *trav;
        uint32_t        index   = 0;

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
dht_selfheal_layout_new_directory (call_frame_t *frame, loc_t *loc,
                                   dht_layout_t *layout)
{
        xlator_t    *this = NULL;
        double       chunk = 0;
        int          i = 0;
        uint32_t     start = 0;
        int          bricks_to_use = 0;
        int          err = 0;
        int          start_subvol = 0;
        uint32_t     curr_size;
        uint32_t     range_size;
        uint64_t     total_size = 0;
        int          real_i;
        dht_conf_t   *priv;
        gf_boolean_t weight_by_size;
        int          bricks_used = 0;

        this = frame->this;
        priv = this->private;
        weight_by_size = priv->do_weighting;

        bricks_to_use = dht_get_layout_count (this, layout, 1);
        GF_ASSERT (bricks_to_use > 0);

        bricks_used = 0;
        for (i = 0; i < layout->cnt; ++i) {
                err = layout->list[i].err;
                if ((err != -1) && (err != ENOENT)) {
                        continue;
                }
                curr_size = dht_get_chunks_from_xl (this,
                                                    layout->list[i].xlator);
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
                chunk = ((double) 0xffffffff) / ((double) total_size);
                gf_msg_debug (this->name, 0,
                              "chunk size = 0xffffffff / %lu = %f",
                              total_size, chunk);
        }
        else {
                weight_by_size = _gf_false;
                chunk = ((unsigned long) 0xffffffff) / bricks_to_use;
        }

        start_subvol = dht_selfheal_layout_alloc_start (this, loc, layout);

        /* clear out the range, as we are re-computing here */
        DHT_RESET_LAYOUT_RANGE (layout);

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
                        curr_size = dht_get_chunks_from_xl (this,
                                layout->list[i].xlator);
                        if (!curr_size) {
                                continue;
                        }
                }
                else {
                        curr_size = 1;
                }
                range_size = chunk * curr_size;
                gf_msg_debug (this->name, 0,
                              "assigning range size 0x%x to %s",
                              range_size,
                              layout->list[i].xlator->name);
                DHT_SET_LAYOUT_RANGE(layout, i, start, range_size,
                                     loc->path);
                if (++bricks_used >= bricks_to_use) {
                        layout->list[i].stop = 0xffffffff;
                        goto done;
                }
                start += range_size;
        }

done:
        return;
}

int
dht_selfheal_dir_getafix (call_frame_t *frame, loc_t *loc,
                          dht_layout_t *layout)
{
        dht_local_t *local = NULL;
        uint32_t     holes = 0;
        int          ret = -1;
        int          i = -1;
        uint32_t     overlaps = 0;

        local = frame->local;

        holes = local->selfheal.hole_cnt;
        overlaps = local->selfheal.overlaps_cnt;

        if (holes || overlaps) {
                /* If the layout has anomolies which would change the hash
                 * ranges, then we need to reset the commit_hash for this
                 * directory, as the layout would change and things may not
                 * be in place as expected */
                layout->commit_hash = DHT_LAYOUT_HASH_INVALID;
                dht_selfheal_layout_new_directory (frame, loc, layout);
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
dht_selfheal_new_directory (call_frame_t *frame,
                            dht_selfheal_dir_cbk_t dir_cbk,
                            dht_layout_t *layout)
{
        dht_local_t *local                   = NULL;
        int          ret                     = 0;
        inode_t     *linked_inode            = NULL, *inode = NULL;
        loc_t       *loc                     = NULL;
        char         pgfid[GF_UUID_BUF_SIZE] = {0};
        char         gfid[GF_UUID_BUF_SIZE]  = {0};
        int32_t      op_errno                = EIO;

        local = frame->local;

        loc = &local->loc;

        gf_uuid_unparse(local->stbuf.ia_gfid, gfid);
        gf_uuid_unparse(loc->parent->gfid, pgfid);

        linked_inode = inode_link (loc->inode, loc->parent, loc->name,
                                   &local->stbuf);
        if (!linked_inode) {
                gf_msg (frame->this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_DIR_SELFHEAL_FAILED,
                        "linking inode failed (%s/%s) => %s",
                        pgfid, loc->name, gfid);
                ret = -1;
                goto out;
        }

        inode = loc->inode;
        loc->inode = linked_inode;
        inode_unref (inode);

        local->selfheal.dir_cbk = dir_cbk;
        local->selfheal.layout = dht_layout_ref (frame->this, layout);

        dht_layout_sort_volname (layout);
        dht_selfheal_layout_new_directory (frame, &local->loc, layout);

        op_errno = ENOMEM;
        ret = dht_selfheal_layout_lock (frame, layout, _gf_true,
                                        dht_selfheal_dir_xattr,
                                        dht_should_heal_layout);

out:
        if (ret < 0) {
                dir_cbk (frame, NULL, frame->this, -1, op_errno, NULL);
        }

        return 0;
}

int
dht_fix_directory_layout (call_frame_t *frame,
                          dht_selfheal_dir_cbk_t dir_cbk,
                          dht_layout_t *layout)
{
        dht_local_t  *local      = NULL;
        dht_layout_t *tmp_layout = NULL;
        int           ret        = 0;

        local = frame->local;

        local->selfheal.dir_cbk = dir_cbk;
        local->selfheal.layout = dht_layout_ref (frame->this, layout);

        /* No layout sorting required here */
        tmp_layout = dht_fix_layout_of_directory (frame, &local->loc, layout);
        if (!tmp_layout) {
                return -1;
        }

        ret = dht_selfheal_layout_lock (frame, tmp_layout, _gf_false,
                                        dht_fix_dir_xattr,
                                        dht_should_fix_layout);

        return ret;
}


int
dht_selfheal_directory (call_frame_t *frame, dht_selfheal_dir_cbk_t dir_cbk,
                        loc_t *loc, dht_layout_t *layout)
{
        dht_local_t *local                   = NULL;
        uint32_t     down                    = 0;
        uint32_t     misc                    = 0;
        int          ret                     = 0;
        xlator_t    *this                    = NULL;
        char         pgfid[GF_UUID_BUF_SIZE] = {0};
        char         gfid[GF_UUID_BUF_SIZE]  = {0};
        inode_t     *linked_inode            = NULL, *inode = NULL;

        local = frame->local;
        this = frame->this;

        local->selfheal.dir_cbk = dir_cbk;
        local->selfheal.layout = dht_layout_ref (this, layout);

        if (!__is_root_gfid (local->stbuf.ia_gfid)) {
                gf_uuid_unparse(local->stbuf.ia_gfid, gfid);
                gf_uuid_unparse(loc->parent->gfid, pgfid);

                linked_inode = inode_link (loc->inode, loc->parent, loc->name,
                                           &local->stbuf);
                if (!linked_inode) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_DIR_SELFHEAL_FAILED,
                                "linking inode failed (%s/%s) => %s",
                                pgfid, loc->name, gfid);
                        ret = 0;
                        goto sorry_no_fix;
                }

                inode = loc->inode;
                loc->inode = linked_inode;
                inode_unref (inode);
        }

        dht_layout_anomalies (this, loc, layout,
                              &local->selfheal.hole_cnt,
                              &local->selfheal.overlaps_cnt,
                              NULL, &local->selfheal.down,
                              &local->selfheal.misc, NULL);

        down     = local->selfheal.down;
        misc     = local->selfheal.misc;

        if (down) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_DIR_SELFHEAL_FAILED,
                        "Directory selfheal failed: %d subvolumes down."
                        "Not fixing. path = %s, gfid = %s",
                        down, loc->path, gfid);
                ret = 0;
                goto sorry_no_fix;
        }

        if (misc) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_DIR_SELFHEAL_FAILED,
                        "Directory selfheal failed : %d subvolumes "
                        "have unrecoverable errors. path = %s, gfid = %s",
                        misc, loc->path, gfid);

                ret = 0;
                goto sorry_no_fix;
        }

        dht_layout_sort_volname (layout);
        ret = dht_selfheal_dir_getafix (frame, loc, layout);

        if (ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_DIR_SELFHEAL_FAILED,
                        "Directory selfheal failed: "
                        "Unable to form layout for directory %s",
                        loc->path);
                goto sorry_no_fix;
        }

        dht_selfheal_dir_mkdir (frame, loc, layout, 0);

        return 0;

sorry_no_fix:
        /* TODO: need to put appropriate local->op_errno */
        dht_selfheal_dir_finish (frame, this, ret, 1);

        return 0;
}

int
dht_selfheal_directory_for_nameless_lookup (call_frame_t *frame,
                                            dht_selfheal_dir_cbk_t dir_cbk,
                                            loc_t *loc, dht_layout_t *layout)
{
        dht_local_t     *local  = NULL;
        uint32_t        down    = 0;
        uint32_t        misc    = 0;
        int             ret     = 0;
        xlator_t        *this   = NULL;

        local = frame->local;
        this = frame->this;
        dht_layout_anomalies (this, loc, layout,
                              &local->selfheal.hole_cnt,
                              &local->selfheal.overlaps_cnt,
                              NULL, &local->selfheal.down,
                              &local->selfheal.misc, NULL);

        down     = local->selfheal.down;
        misc     = local->selfheal.misc;

        local->selfheal.dir_cbk = dir_cbk;
        local->selfheal.layout = dht_layout_ref (this, layout);

        if (down) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_SUBVOL_DOWN_ERROR,
                        "%d subvolumes down -- not fixing", down);
                ret = 0;
                goto sorry_no_fix;
        }

        if (misc) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_SUBVOL_ERROR,
                        "%d subvolumes have unrecoverable errors", misc);
                ret = 0;
                goto sorry_no_fix;
        }

        dht_layout_sort_volname (layout);
        ret = dht_selfheal_dir_getafix (frame, loc, layout);

        if (ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_LAYOUT_FORM_FAILED,
                        "not able to form layout for the directory");
                goto sorry_no_fix;
        }

        ret = dht_selfheal_layout_lock (frame, layout, _gf_false,
                                     dht_selfheal_dir_xattr_for_nameless_lookup,
                                        dht_should_heal_layout);

        if (ret < 0) {
                goto sorry_no_fix;
        }

        return 0;

sorry_no_fix:
        /* TODO: need to put appropriate local->op_errno */
        dht_selfheal_dir_finish (frame, this, ret, 1);

        return 0;


}

int
dht_selfheal_restore (call_frame_t *frame, dht_selfheal_dir_cbk_t dir_cbk,
                      loc_t *loc, dht_layout_t *layout)
{
        int          ret = 0;
        dht_local_t *local    = NULL;

        local = frame->local;

        local->selfheal.dir_cbk = dir_cbk;
        local->selfheal.layout = dht_layout_ref (frame->this, layout);

        ret = dht_selfheal_dir_mkdir (frame, loc, layout, 1);

        return ret;
}

int
dht_dir_attr_heal (void *data)
{
        call_frame_t    *frame = NULL;
        dht_local_t     *local = NULL;
        xlator_t        *subvol = NULL;
        xlator_t        *this  = NULL;
        dht_conf_t      *conf  = NULL;
        int              call_cnt = 0;
        int              ret   = -1;
        int              i     = 0;
        char         gfid[GF_UUID_BUF_SIZE] = {0};


        GF_VALIDATE_OR_GOTO ("dht", data, out);

        frame = data;
        local = frame->local;
        this = frame->this;
        GF_VALIDATE_OR_GOTO ("dht", this, out);
        GF_VALIDATE_OR_GOTO ("dht", local, out);
        conf = this->private;
        GF_VALIDATE_OR_GOTO ("dht", conf, out);

        call_cnt = conf->subvolume_cnt;

        for (i = 0; i < call_cnt; i++) {
                subvol = conf->subvolumes[i];
                if (!subvol)
                        continue;

                if (__is_root_gfid (local->stbuf.ia_gfid)) {
                        ret = syncop_setattr (subvol, &local->loc, &local->stbuf,
                                              (GF_SET_ATTR_UID | GF_SET_ATTR_GID | GF_SET_ATTR_MODE),
                                              NULL, NULL, NULL, NULL);
                } else {
                        ret = syncop_setattr (subvol, &local->loc, &local->stbuf,
                                              (GF_SET_ATTR_UID | GF_SET_ATTR_GID),
                                              NULL, NULL, NULL, NULL);
                }

                if (ret) {
                        gf_uuid_unparse(local->loc.gfid, gfid);

                        gf_msg ("dht", GF_LOG_ERROR, -ret,
                                DHT_MSG_DIR_ATTR_HEAL_FAILED,
                                "Directory attr heal failed. Failed to set"
                                " uid/gid on path %s on subvol %s, gfid = %s ",
                                local->loc.path, subvol->name, gfid);
                }
        }
out:
        return 0;
}

int
dht_dir_attr_heal_done (int ret, call_frame_t *sync_frame, void *data)
{
        DHT_STACK_DESTROY (sync_frame);
        return 0;
}

/* EXIT: dht_update_commit_hash_for_layout */
int
dht_update_commit_hash_for_layout_done (call_frame_t *frame, void *cookie,
                       xlator_t *this, int32_t op_ret, int32_t op_errno,
                       dict_t *xdata)
{
        dht_local_t  *local = NULL;

        local = frame->local;

        /* preserve oldest error */
        if (op_ret && !local->op_ret) {
                local->op_ret = op_ret;
                local->op_errno = op_errno;
        }

        DHT_STACK_UNWIND (setxattr, frame, local->op_ret,
                          local->op_errno, NULL);

        return 0;
}

int
dht_update_commit_hash_for_layout_unlock (call_frame_t *frame, xlator_t *this)
{
        dht_local_t  *local = NULL;
        int ret = 0;

        local = frame->local;

        ret = dht_unlock_inodelk (frame, local->lock.locks,
                                  local->lock.lk_count,
                                  dht_update_commit_hash_for_layout_done);
        if (ret < 0) {
                /* preserve oldest error, just ... */
                if (!local->op_ret) {
                        local->op_errno = errno;
                        local->op_ret = -1;
                }

                gf_msg (this->name, GF_LOG_WARNING, errno,
                        DHT_MSG_DIR_SELFHEAL_XATTR_FAILED,
                        "Winding unlock failed: stale locks left on brick"
                        " %s", local->loc.path);

                dht_update_commit_hash_for_layout_done (frame, NULL, this,
                                                        0, 0, NULL);
        }

        return 0;
}

int
dht_update_commit_hash_for_layout_cbk (call_frame_t *frame, void *cookie,
                                       xlator_t *this, int op_ret,
                                       int op_errno, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = 0;

        local = frame->local;

        LOCK (&frame->lock);
        /* store first failure, just because */
        if (op_ret && !local->op_ret) {
                local->op_ret = op_ret;
                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
                dht_update_commit_hash_for_layout_unlock (frame, this);
        }

        return 0;
}

int
dht_update_commit_hash_for_layout_resume (call_frame_t *frame, void *cookie,
                                          xlator_t *this, int32_t op_ret,
                                          int32_t op_errno, dict_t *xdata)
{
        dht_local_t   *local = NULL;
        int            count = 1, ret = -1, i = 0, j = 0;
        dht_conf_t    *conf = NULL;
        dht_layout_t  *layout = NULL;
        int32_t       *disk_layout = NULL;
        dict_t        **xattr = NULL;

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
        xattr = GF_CALLOC (count, sizeof (*xattr), gf_common_mt_char);
        if (!xattr) {
                local->op_errno = errno;

                gf_msg (this->name, GF_LOG_WARNING, errno,
                        DHT_MSG_DIR_SELFHEAL_XATTR_FAILED,
                        "Directory commit hash update failed:"
                        " %s: Allocation failed", local->loc.path);

                goto err;
        }

        for (i = 0; i < count; i++) {
                /* find the layout index for the subvolume */
                ret = dht_layout_index_for_subvol (layout,
                                                   conf->local_subvols[i]);
                if (ret < 0) {
                        local->op_errno = ENOENT;

                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_DIR_SELFHEAL_XATTR_FAILED,
                                "Directory commit hash update failed:"
                                " %s: (subvol %s) Failed to find disk layout",
                                local->loc.path, conf->local_subvols[i]->name);

                        goto err;
                }
                j = ret;

                /* update the commit hash for the layout */
                layout->list[j].commit_hash = layout->commit_hash;

                /* extract the current layout */
                ret = dht_disk_layout_extract (this, layout, j, &disk_layout);
                if (ret == -1) {
                        local->op_errno = errno;

                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                DHT_MSG_DIR_SELFHEAL_XATTR_FAILED,
                                "Directory commit hash update failed:"
                                " %s: (subvol %s) Failed to extract disk"
                                " layout", local->loc.path,
                                conf->local_subvols[i]->name);

                        goto err;
                }

                xattr[i] = dict_new ();
                if (!xattr[i]) {
                        local->op_errno = errno;

                        gf_msg (this->name, GF_LOG_WARNING, errno,
                                DHT_MSG_DIR_SELFHEAL_XATTR_FAILED,
                                "Directory commit hash update failed:"
                                " %s: Allocation failed", local->loc.path);

                        goto err;
                }

                ret = dict_set_bin (xattr[i], conf->xattr_name,
                                    disk_layout, 4 * 4);
                if (ret != 0) {
                        local->op_errno = ENOMEM;

                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_DIR_SELFHEAL_XATTR_FAILED,
                                "Directory self heal xattr failed:"
                                "%s: (subvol %s) Failed to set xattr"
                                " dictionary,", local->loc.path,
                                conf->local_subvols[i]->name);

                        GF_FREE (disk_layout);

                        goto err;
                }
                disk_layout = NULL;

                gf_msg_trace (this->name, 0,
                              "setting commit hash %u on subvolume %s"
                              " for %s", layout->list[j].commit_hash,
                              conf->local_subvols[i]->name, local->loc.path);
        }

        /* wind the setting of the commit hash across the local subvols */
        local->call_cnt = count;
        local->op_ret = 0;
        local->op_errno = 0;
        for (i = 0; i < count; i++) {
                STACK_WIND (frame, dht_update_commit_hash_for_layout_cbk,
                            conf->local_subvols[i],
                            conf->local_subvols[i]->fops->setxattr,
                            &local->loc, xattr[i], 0, NULL);

        }
        for (i = 0; i < count; i++)
                dict_unref (xattr[i]);
        GF_FREE (xattr);

        return 0;
err:
        if (xattr) {
                for (i = 0; i < count; i++) {
                        if (xattr[i])
                                dict_unref (xattr[i]);
                }

                GF_FREE (xattr);
        }

        GF_FREE (disk_layout);

        local->op_ret = -1;

        dht_update_commit_hash_for_layout_unlock (frame, this);

        return 0;
err_done:
        local->op_ret = -1;

        dht_update_commit_hash_for_layout_done (frame, NULL, this, 0, 0, NULL);

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
dht_update_commit_hash_for_layout (call_frame_t *frame)
{
        dht_local_t   *local = NULL;
        int            count = 1, ret = -1, i = 0;
        dht_lock_t   **lk_array = NULL;
        dht_conf_t    *conf = NULL;

        GF_VALIDATE_OR_GOTO ("dht", frame, err);
        GF_VALIDATE_OR_GOTO (frame->this->name, frame->local, err);

        local = frame->local;
        conf = frame->this->private;

        if (!conf->defrag)
                goto err;

        count = conf->local_subvols_cnt;
        lk_array = GF_CALLOC (count, sizeof (*lk_array),
                              gf_common_mt_char);
        if (lk_array == NULL)
                goto err;

        for (i = 0; i < count; i++) {
                lk_array[i] = dht_lock_new (frame->this,
                                            conf->local_subvols[i],
                                            &local->loc, F_WRLCK,
                                            DHT_LAYOUT_HEAL_DOMAIN);
                if (lk_array[i] == NULL)
                        goto err;
        }

        local->lock.locks = lk_array;
        local->lock.lk_count = count;

        ret = dht_blocking_inodelk (frame, lk_array, count, FAIL_ON_ANY_ERROR,
                                    dht_update_commit_hash_for_layout_resume);
        if (ret < 0) {
                local->lock.locks = NULL;
                local->lock.lk_count = 0;
                goto err;
        }

        return 0;
err:
        if (lk_array != NULL) {
                dht_lock_array_free (lk_array, count);
                GF_FREE (lk_array);
        }

        return -1;
}
