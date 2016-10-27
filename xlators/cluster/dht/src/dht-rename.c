/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

/* TODO: link(oldpath, newpath) fails if newpath already exists. DHT should
 *       delete the newpath if it gets EEXISTS from link() call.
 */
#include "glusterfs.h"
#include "xlator.h"
#include "dht-common.h"
#include "defaults.h"

int dht_rename_unlock (call_frame_t *frame, xlator_t *this);

int
dht_rename_dir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *stbuf,
                    struct iatt *preoldparent, struct iatt *postoldparent,
                    struct iatt *prenewparent, struct iatt *postnewparent,
                    dict_t *xdata)
{
        dht_conf_t  *conf                    = NULL;
        dht_local_t *local                   = NULL;
        int          this_call_cnt           = 0;
        xlator_t    *prev                    = NULL;
        int          i                       = 0;
        char         gfid[GF_UUID_BUF_SIZE]  = {0};
        int          subvol_cnt              = -1;

        conf = this->private;
        local = frame->local;
        prev = cookie;
        subvol_cnt = dht_subvol_cnt (this, prev);
        local->ret_cache[subvol_cnt] = op_ret;


        if (op_ret == -1) {
                gf_uuid_unparse(local->loc.inode->gfid, gfid);

                gf_msg (this->name, GF_LOG_INFO, op_errno,
                        DHT_MSG_RENAME_FAILED,
                        "Rename %s -> %s on %s failed, (gfid = %s)",
                        local->loc.path, local->loc2.path,
                        prev->name, gfid);

                local->op_ret   = op_ret;
                local->op_errno = op_errno;
                goto unwind;
        }
        /* TODO: construct proper stbuf for dir */
        /*
         * FIXME: is this the correct way to build stbuf and
         * parent bufs?
         */
        dht_iatt_merge (this, &local->stbuf, stbuf, prev);
        dht_iatt_merge (this, &local->preoldparent, preoldparent, prev);
        dht_iatt_merge (this, &local->postoldparent, postoldparent, prev);
        dht_iatt_merge (this, &local->preparent, prenewparent, prev);
        dht_iatt_merge (this, &local->postparent, postnewparent, prev);


unwind:
        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt)) {
                /* We get here with local->call_cnt == 0. Which means
                 * we are the only one executing this code, there is
                 * no contention. Therefore it's safe to manipulate or
                 * deref local->call_cnt directly (without locking).
                 */
                if (local->ret_cache[conf->subvolume_cnt] == 0) {
                        /* count errant subvols in last field of ret_cache */
                        for (i = 0; i < conf->subvolume_cnt; i++) {
                                if (local->ret_cache[i] != 0)
                                        ++local->ret_cache[conf->subvolume_cnt];
                        }
                        if (local->ret_cache[conf->subvolume_cnt]) {
                                /* undoing the damage:
                                 * for all subvolumes, where rename
                                 * succeeded, we perform the reverse operation
                                 */
                                for (i = 0; i < conf->subvolume_cnt; i++) {
                                        if (local->ret_cache[i] == 0)
                                                ++local->call_cnt;
                                }
                                for (i = 0; i < conf->subvolume_cnt; i++) {
                                        if (local->ret_cache[i])
                                                continue;

                                        STACK_WIND (frame,
                                                    dht_rename_dir_cbk,
                                                    conf->subvolumes[i],
                                                    conf->subvolumes[i]->fops->rename,
                                                    &local->loc2, &local->loc,
                                                    NULL);
                                }

                                return 0;
                        }
                }

                WIPE (&local->preoldparent);
                WIPE (&local->postoldparent);
                WIPE (&local->preparent);
                WIPE (&local->postparent);

                dht_rename_unlock (frame, this);
        }

        return 0;
}


int
dht_rename_hashed_dir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, struct iatt *stbuf,
                           struct iatt *preoldparent,
                           struct iatt *postoldparent,
                           struct iatt *prenewparent,
                           struct iatt *postnewparent, dict_t *xdata)
{
        dht_conf_t   *conf = NULL;
        dht_local_t  *local = NULL;
        int           call_cnt = 0;
        xlator_t     *prev = NULL;
        int           i = 0;
        char          gfid[GF_UUID_BUF_SIZE] = {0};

        conf = this->private;
        local = frame->local;
        prev = cookie;


        if (op_ret == -1) {
                gf_uuid_unparse(local->loc.inode->gfid, gfid);

                gf_msg (this->name, GF_LOG_INFO, op_errno,
                        DHT_MSG_RENAME_FAILED,
                        "rename %s -> %s on %s failed, (gfid = %s) ",
                        local->loc.path, local->loc2.path,
                        prev->name, gfid);

                local->op_ret   = op_ret;
                local->op_errno = op_errno;
                goto unwind;
        }
        /* TODO: construct proper stbuf for dir */
        /*
         * FIXME: is this the correct way to build stbuf and
         * parent bufs?
         */
        dht_iatt_merge (this, &local->stbuf, stbuf, prev);
        dht_iatt_merge (this, &local->preoldparent, preoldparent, prev);
        dht_iatt_merge (this, &local->postoldparent, postoldparent, prev);
        dht_iatt_merge (this, &local->preparent, prenewparent, prev);
        dht_iatt_merge (this, &local->postparent, postnewparent, prev);

        call_cnt = local->call_cnt = conf->subvolume_cnt - 1;

        if (!local->call_cnt)
                goto unwind;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (conf->subvolumes[i] == local->dst_hashed)
                        continue;
                STACK_WIND_COOKIE (frame, dht_rename_dir_cbk,
                                   conf->subvolumes[i],
                                   conf->subvolumes[i],
                                   conf->subvolumes[i]->fops->rename,
                                   &local->loc, &local->loc2, NULL);
                if (!--call_cnt)
                        break;
        }


        return 0;
unwind:
        WIPE (&local->preoldparent);
        WIPE (&local->postoldparent);
        WIPE (&local->preparent);
        WIPE (&local->postparent);

        dht_rename_unlock (frame, this);
        return 0;
}


int
dht_rename_dir_do (call_frame_t *frame, xlator_t *this)
{
        dht_local_t  *local = NULL;

        local = frame->local;

        if (local->op_ret == -1)
                goto err;

        local->op_ret = 0;

        STACK_WIND_COOKIE (frame, dht_rename_hashed_dir_cbk, local->dst_hashed,
                           local->dst_hashed,
                           local->dst_hashed->fops->rename,
                           &local->loc, &local->loc2, NULL);
        return 0;

err:
        dht_rename_unlock (frame, this);
        return 0;
}


int
dht_rename_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, gf_dirent_t *entries,
                        dict_t *xdata)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = -1;
        xlator_t     *prev = NULL;

        local = frame->local;
        prev  = cookie;

        if (op_ret > 2) {
                gf_msg_trace (this->name, 0,
                              "readdir on %s for %s returned %d entries",
                              prev->name, local->loc.path, op_ret);
                local->op_ret = -1;
                local->op_errno = ENOTEMPTY;
        }

        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
                dht_rename_dir_do (frame, this);
        }

        return 0;
}


int
dht_rename_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, fd_t *fd, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = -1;
        xlator_t     *prev = NULL;
        char          gfid[GF_UUID_BUF_SIZE] = {0};

        local = frame->local;
        prev  = cookie;


        if (op_ret == -1) {

                gf_uuid_unparse(local->loc.inode->gfid, gfid);
                gf_msg (this->name, GF_LOG_INFO, op_errno,
                        DHT_MSG_OPENDIR_FAILED,
                        "opendir on %s for %s failed,(gfid = %s) ",
                        prev->name, local->loc.path, gfid);
                goto err;
        }

        fd_bind (fd);
        STACK_WIND_COOKIE (frame, dht_rename_readdir_cbk, prev, prev,
                           prev->fops->readdir, local->fd, 4096, 0, NULL);

        return 0;

err:
        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
                dht_rename_dir_do (frame, this);
        }

        return 0;
}


int
dht_rename_dir_lock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        dht_local_t *local                      = NULL;
        char         src_gfid[GF_UUID_BUF_SIZE] = {0};
        char         dst_gfid[GF_UUID_BUF_SIZE] = {0};
        dht_conf_t  *conf                       = NULL;
        int          i                          = 0;

        local = frame->local;
        conf = this->private;

        if (op_ret < 0) {
                uuid_utoa_r (local->loc.inode->gfid, src_gfid);

                if (local->loc2.inode)
                        uuid_utoa_r (local->loc2.inode->gfid, dst_gfid);

                gf_msg (this->name, GF_LOG_WARNING, op_errno,
                        DHT_MSG_INODE_LK_ERROR,
                        "acquiring inodelk failed "
                        "rename (%s:%s:%s %s:%s:%s)",
                        local->loc.path, src_gfid, local->src_cached->name,
                        local->loc2.path, dst_gfid,
                        local->dst_cached ? local->dst_cached->name : NULL);

                local->op_ret = -1;
                local->op_errno = op_errno;
                goto err;
        }

        local->fd = fd_create (local->loc.inode, frame->root->pid);
        if (!local->fd) {
                op_errno = ENOMEM;
                goto err;
        }

        local->op_ret = 0;

        if (!local->dst_cached) {
                dht_rename_dir_do (frame, this);
                return 0;
        }

        for (i = 0; i < conf->subvolume_cnt; i++) {
                STACK_WIND_COOKIE (frame, dht_rename_opendir_cbk,
                                   conf->subvolumes[i],
                                   conf->subvolumes[i],
                                   conf->subvolumes[i]->fops->opendir,
                                   &local->loc2, local->fd, NULL);
        }

        return 0;

err:
        /* No harm in calling an extra unlock */
        dht_rename_unlock (frame, this);
        return 0;
}

int
dht_rename_dir (call_frame_t *frame, xlator_t *this)
{
        dht_conf_t    *conf         = NULL;
        dht_local_t   *local        = NULL;
        dht_lock_t   **lk_array     = NULL;
        dht_layout_t  *dst_layout   = NULL;
        xlator_t      *first_subvol = NULL;
        loc_t          parent_loc   = {0, };
        int            count        = 1;
        int            i            = 0;
        int            j            = 0;
        int            ret          = 0;
        int            op_errno     = -1;

        conf = frame->this->private;
        local = frame->local;

        local->ret_cache = GF_CALLOC (conf->subvolume_cnt + 1, sizeof (int),
                                      gf_dht_ret_cache_t);

        if (local->ret_cache == NULL) {
                op_errno = ENOMEM;
                goto err;
        }

        /* We must take a lock on all the subvols with src gfid.
         * Along with this if dst exists we must take lock on
         * any one subvol with dst gfid.
         */
        count = local->call_cnt = conf->subvolume_cnt;
        if (local->loc2.inode) {
                dst_layout = dht_layout_get (this, local->loc2.inode);
                if (dst_layout)
                        ++count;
        } else if (gf_uuid_compare (local->loc.parent->gfid,
                                    local->loc2.parent->gfid)) {
                dst_layout = dht_layout_get (this, local->loc2.parent);
                if (dst_layout)
                        ++count;
        }

        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (!conf->subvolume_status[i]) {
                        gf_msg (this->name, GF_LOG_INFO, 0,
                                DHT_MSG_RENAME_FAILED,
                                "Rename dir failed: subvolume down (%s)",
                                conf->subvolumes[i]->name);
                        op_errno = ENOTCONN;
                        goto err;
                }
        }

        lk_array = GF_CALLOC (count, sizeof (*lk_array), gf_common_mt_char);
        if (lk_array == NULL) {
                op_errno = ENOMEM;
                goto err;
        }

        /* Rename must take locks on src to avoid lookup selfheal from
         * recreating src on those subvols where the rename was successful.
         * Rename must take locks on all subvols with src because selfheal
         * in entry creation phase may not have acquired lock on all subvols.
        */
        for (i = 0; i < local->call_cnt; i++) {
                lk_array[i] = dht_lock_new (frame->this,
                                            conf->subvolumes[i],
                                            &local->loc, F_WRLCK,
                                            DHT_LAYOUT_HEAL_DOMAIN);
                if (lk_array[i] == NULL) {
                        op_errno = ENOMEM;
                        goto err;
                }
        }

        /* If the dst exists, we are going to replace dst layout range with
         * that of src. This will lead to anomalies in dst layout until the
         * rename completes. To avoid a lookup selfheal to change dst layout
         * during this interval we take a lock on one subvol of dst.
         */
        for (j = 0; dst_layout && (j < dst_layout->cnt) &&
                        (dst_layout->list[j].err == 0); j++) {

                first_subvol = dst_layout->list[j].xlator;
                if (local->loc2.inode) {
                        lk_array[i] = dht_lock_new (frame->this, first_subvol,
                                                    &local->loc2, F_WRLCK,
                                                    DHT_LAYOUT_HEAL_DOMAIN);
                } else {
                        ret = dht_build_parent_loc (this, &parent_loc,
                                                    &local->loc2, &op_errno);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                        DHT_MSG_NO_MEMORY,
                                        "parent loc build failed");
                                goto err;
                        }

                        lk_array[i] = dht_lock_new (frame->this, first_subvol,
                                                    &parent_loc, F_WRLCK,
                                                    DHT_LAYOUT_HEAL_DOMAIN);
                }

                if (lk_array[i] == NULL) {
                        op_errno = ENOMEM;
                        goto err;
                }
                break;
        }

        if (!lk_array[i])
                --count;

        local->lock.locks = lk_array;
        local->lock.lk_count = count;

        ret = dht_blocking_inodelk (frame, lk_array, count,
                                    IGNORE_ENOENT_ESTALE,
                                    dht_rename_dir_lock_cbk);
        if (ret < 0) {
                local->lock.locks = NULL;
                local->lock.lk_count = 0;
                op_errno = EINVAL;
                goto err;
        }

        loc_wipe (&parent_loc);
        return 0;

err:
        if (lk_array != NULL) {
                dht_lock_array_free (lk_array, count);
                GF_FREE (lk_array);
        }

        loc_wipe (&parent_loc);
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (rename, frame, -1, op_errno, NULL, NULL, NULL, NULL,
                          NULL, NULL);
        return 0;
}

static int
dht_rename_track_for_changelog (xlator_t *this, dict_t *xattr,
                                loc_t *oldloc, loc_t *newloc)
{
        int ret        = -1;
        dht_changelog_rename_info_t *info = NULL;
        char *name     = NULL;
        int len1       = 0;
        int len2       = 0;
        int size       = 0;

        if (!xattr || !oldloc || !newloc || !this)
                return ret;

        len1 = strlen (oldloc->name) + 1;
        len2 = strlen (newloc->name) + 1;
        size = sizeof (dht_changelog_rename_info_t) + len1 + len2;

        info = GF_CALLOC (size, sizeof(char), gf_common_mt_char);
        if (!info) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_DICT_SET_FAILED,
                        "Failed to calloc memory");
                return ret;
        }

        gf_uuid_copy (info->old_pargfid, oldloc->pargfid);
        gf_uuid_copy (info->new_pargfid, newloc->pargfid);

        info->oldname_len = len1;
        info->newname_len = len2;
        strncpy (info->buffer, oldloc->name, len1);
        name = info->buffer + len1;
        strncpy (name, newloc->name, len2);

        ret = dict_set_bin (xattr, DHT_CHANGELOG_RENAME_OP_KEY,
                            info, size);
        if (ret) {
                gf_msg (this->name, GF_LOG_ERROR, 0, DHT_MSG_DICT_SET_FAILED,
                        "Failed to set dictionary value: key = %s,"
                        " path = %s", DHT_CHANGELOG_RENAME_OP_KEY,
                        oldloc->name);
                GF_FREE (info);
        }

        return ret;
}



#define DHT_MARKER_DONT_ACCOUNT(xattr) do {                             \
                int tmp = -1;                                                  \
                if (!xattr) {                                                  \
                        xattr = dict_new ();                                   \
                        if (!xattr)                                            \
                                break;                                         \
                }                                                              \
                tmp = dict_set_str (xattr, GLUSTERFS_MARKER_DONT_ACCOUNT_KEY,  \
                                    "yes");                                    \
                if (tmp) {                                                     \
                        gf_msg (this->name, GF_LOG_ERROR, 0,                    \
                                DHT_MSG_DICT_SET_FAILED,                       \
                                "Failed to set dictionary value: key = %s,"    \
                                " path = %s",GLUSTERFS_MARKER_DONT_ACCOUNT_KEY, \
                                local->loc.path);                             \
                }                                                              \
        }while (0)


#define DHT_CHANGELOG_TRACK_AS_RENAME(xattr, oldloc, newloc) do {            \
                int tmp = -1;                                                \
                if (!xattr) {                                                \
                        xattr = dict_new ();                                 \
                        if (!xattr) {                                        \
                                gf_msg (this->name, GF_LOG_ERROR, 0,         \
                                        DHT_MSG_DICT_SET_FAILED,             \
                                        "Failed to create dictionary to "    \
                                        "track rename");                     \
                                break;                                       \
                        }                                                    \
                }                                                            \
                                                                             \
                tmp = dht_rename_track_for_changelog (this, xattr,           \
                                oldloc, newloc);                             \
                                                                             \
                if (tmp) {                                                   \
                        gf_msg (this->name, GF_LOG_ERROR, 0,                 \
                                DHT_MSG_DICT_SET_FAILED,                     \
                                "Failed to set dictionary value: key = %s,"  \
                                " path = %s", DHT_CHANGELOG_RENAME_OP_KEY,   \
                                (oldloc)->path);                             \
                }                                                            \
        } while (0)

int
dht_rename_unlock_cbk (call_frame_t *frame, void *cookie,
                       xlator_t *this, int32_t op_ret, int32_t op_errno,
                       dict_t *xdata)
{
        dht_local_t *local = NULL;

        local = frame->local;

        dht_set_fixed_dir_stat (&local->preoldparent);
        dht_set_fixed_dir_stat (&local->postoldparent);
        dht_set_fixed_dir_stat (&local->preparent);
        dht_set_fixed_dir_stat (&local->postparent);

        if (IA_ISREG (local->stbuf.ia_type))
                DHT_STRIP_PHASE1_FLAGS (&local->stbuf);

        DHT_STACK_UNWIND (rename, frame, local->op_ret, local->op_errno,
                          &local->stbuf, &local->preoldparent,
                          &local->postoldparent, &local->preparent,
                          &local->postparent, local->xattr);
        return 0;
}

int
dht_rename_unlock (call_frame_t *frame, xlator_t *this)
{
        dht_local_t *local                      = NULL;
        int          op_ret                     = -1;
        char         src_gfid[GF_UUID_BUF_SIZE] = {0};
        char         dst_gfid[GF_UUID_BUF_SIZE] = {0};

        local = frame->local;
        op_ret = dht_unlock_inodelk (frame, local->lock.locks,
                                     local->lock.lk_count,
                                     dht_rename_unlock_cbk);
        if (op_ret < 0) {
                uuid_utoa_r (local->loc.inode->gfid, src_gfid);

                if (local->loc2.inode)
                        uuid_utoa_r (local->loc2.inode->gfid, dst_gfid);

                if (IA_ISREG (local->stbuf.ia_type))
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_UNLOCKING_FAILED,
                                "winding unlock inodelk failed "
                                "rename (%s:%s:%s %s:%s:%s), "
                                "stale locks left on bricks",
                                local->loc.path, src_gfid,
                                local->src_cached->name,
                                local->loc2.path, dst_gfid,
                                local->dst_cached ?
                                local->dst_cached->name : NULL);
                else
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                DHT_MSG_UNLOCKING_FAILED,
                                "winding unlock inodelk failed "
                                "rename (%s:%s %s:%s), "
                                "stale locks left on bricks",
                                local->loc.path, src_gfid,
                                local->loc2.path, dst_gfid);

                dht_rename_unlock_cbk (frame, NULL, this, 0, 0, NULL);
        }

        return 0;
}

int
dht_rename_done (call_frame_t *frame, xlator_t *this)
{
        dht_local_t *local                      = NULL;

        local = frame->local;

        if (local->linked == _gf_true) {
                local->linked = _gf_false;
                dht_linkfile_attr_heal (frame, this);
        }

        dht_rename_unlock (frame, this);
        return 0;
}

int
dht_rename_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                       struct iatt *postparent, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        xlator_t     *prev = NULL;
        int           this_call_cnt = 0;

        local = frame->local;
        prev  = cookie;

        FRAME_SU_UNDO (frame, dht_local_t);
        if (!local) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        DHT_MSG_INVALID_VALUE,
                        "!local, should not happen");
                goto out;
        }

        this_call_cnt = dht_frame_return (frame);

        if (op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING, op_errno,
                        DHT_MSG_UNLINK_FAILED,
                        "%s: Rename: unlink on %s failed ",
                        local->loc.path, prev->name);
        }

        WIPE (&local->preoldparent);
        WIPE (&local->postoldparent);
        WIPE (&local->preparent);
        WIPE (&local->postparent);

        if (is_last_call (this_call_cnt)) {
                dht_rename_done (frame, this);
        }

out:
        return 0;
}


int
dht_rename_cleanup (call_frame_t *frame)
{
        dht_local_t *local                  = NULL;
        xlator_t    *this                   = NULL;
        xlator_t    *src_hashed             = NULL;
        xlator_t    *src_cached             = NULL;
        xlator_t    *dst_hashed             = NULL;
        xlator_t    *dst_cached             = NULL;
        int          call_cnt               = 0;
        dict_t      *xattr                  = NULL;
        char         gfid[GF_UUID_BUF_SIZE] = {0};

        local = frame->local;
        this  = frame->this;

        src_hashed = local->src_hashed;
        src_cached = local->src_cached;
        dst_hashed = local->dst_hashed;
        dst_cached = local->dst_cached;

        if (src_cached == dst_cached)
                goto nolinks;

        if (local->linked && (dst_hashed != src_hashed )&&
                (dst_hashed != src_cached)) {
                call_cnt++;
        }

        if (local->added_link && (src_cached != dst_hashed)) {
                call_cnt++;
        }

        local->call_cnt = call_cnt;

        if (!call_cnt)
                goto nolinks;

        DHT_MARK_FOP_INTERNAL (xattr);

        gf_uuid_unparse(local->loc.inode->gfid, gfid);

        if (local->linked && (dst_hashed != src_hashed) &&
                        (dst_hashed != src_cached)) {
                dict_t *xattr_new = NULL;

                gf_msg_trace (this->name, 0,
                              "unlinking linkfile %s @ %s => %s, (gfid = %s)",
                              local->loc.path, dst_hashed->name,
                              src_cached->name, gfid);

                xattr_new = dict_copy_with_ref (xattr, NULL);


                DHT_MARKER_DONT_ACCOUNT(xattr_new);

                FRAME_SU_DO (frame, dht_local_t);
                STACK_WIND_COOKIE (frame, dht_rename_unlink_cbk, dst_hashed,
                                   dst_hashed, dst_hashed->fops->unlink,
                                   &local->loc, 0, xattr_new);

                dict_unref (xattr_new);
                xattr_new = NULL;
        }

        if (local->added_link && (src_cached != dst_hashed)) {
                dict_t *xattr_new = NULL;

                gf_msg_trace (this->name, 0,
                              "unlinking link %s => %s (%s), (gfid = %s)",
                              local->loc.path, local->loc2.path,
                              src_cached->name, gfid);

                xattr_new = dict_copy_with_ref (xattr, NULL);

                if (gf_uuid_compare (local->loc.pargfid,
                                  local->loc2.pargfid) == 0) {
                        DHT_MARKER_DONT_ACCOUNT(xattr_new);
                }
                /* *
                 * The link to file is created using root permission.
                 * Hence deletion should happen using root. Otherwise
                 * it will fail.
                 */
                FRAME_SU_DO (frame, dht_local_t);
                STACK_WIND_COOKIE (frame, dht_rename_unlink_cbk, src_cached,
                                   src_cached, src_cached->fops->unlink,
                                   &local->loc2, 0, xattr_new);

                dict_unref (xattr_new);
                xattr_new = NULL;
        }

        if (xattr)
                dict_unref (xattr);

        return 0;

nolinks:
        WIPE (&local->preoldparent);
        WIPE (&local->postoldparent);
        WIPE (&local->preparent);
        WIPE (&local->postparent);

        dht_rename_unlock (frame, this);
        return 0;
}


int
dht_rename_links_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno,
                             inode_t *inode, struct iatt *stbuf,
                             struct iatt *preparent, struct iatt *postparent,
                             dict_t *xdata)
{
        xlator_t *prev = NULL;
        dht_local_t  *local = NULL;

        prev = cookie;
        local = frame->local;

        if (op_ret == -1) {
                gf_msg (this->name, GF_LOG_WARNING, op_errno,
                        DHT_MSG_CREATE_LINK_FAILED,
                        "link/file %s on %s failed",
                        local->loc.path, prev->name);
        }

        if (local->linked == _gf_true) {
                local->linked = _gf_false;
                dht_linkfile_attr_heal (frame, this);
        }
        DHT_STACK_DESTROY (frame);

        return 0;
}


int
dht_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *stbuf,
                struct iatt *preoldparent, struct iatt *postoldparent,
                struct iatt *prenewparent, struct iatt *postnewparent,
                dict_t *xdata)
{
        dht_local_t  *local = NULL;
        xlator_t     *prev = NULL;
        xlator_t     *src_hashed = NULL;
        xlator_t     *src_cached = NULL;
        xlator_t     *dst_hashed = NULL;
        xlator_t     *dst_cached = NULL;
        xlator_t     *rename_subvol = NULL;
        call_frame_t *link_frame = NULL;
        dht_local_t *link_local = NULL;
        dict_t       *xattr     = NULL;

        local = frame->local;
        prev = cookie;

        src_hashed = local->src_hashed;
        src_cached = local->src_cached;
        dst_hashed = local->dst_hashed;
        dst_cached = local->dst_cached;

        if (local->linked == _gf_true)
                FRAME_SU_UNDO (frame, dht_local_t);

        /* It is a critical failure iff we fail to rename the cached file
         * if the rename of the linkto failed, it is not a critical failure,
         * and we do not want to lose the created hard link for the new
         * name as that could have been read by other clients.
         *
         * NOTE: If another client is attempting the same oldname -> newname
         * rename, and finds both file names as existing, and are hard links
         * to each other, then FUSE would send in an unlink for oldname. In
         * this time duration if we treat the linkto as a critical error and
         * unlink the newname we created, we would have effectively lost the
         * file to rename operations.
         *
         * Repercussions of treating this as a non-critical error is that
         * we could leave behind a stale linkto file and/or not create the new
         * linkto file, the second case would be rectified by a subsequent
         * lookup, the first case by a rebalance, like for all stale linkto
         * files */

        if (op_ret == -1) {
                /* Critical failure: unable to rename the cached file */
                if (prev == src_cached) {
                        gf_msg (this->name, GF_LOG_WARNING, op_errno,
                                DHT_MSG_RENAME_FAILED,
                                "%s: Rename on %s failed, (gfid = %s) ",
                                local->loc.path, prev->name,
                                local->loc.inode ?
                                uuid_utoa(local->loc.inode->gfid):"");
                        local->op_ret   = op_ret;
                        local->op_errno = op_errno;
                        goto cleanup;
                } else {
                        /* Non-critical failure, unable to rename the linkto
                         * file
                         */
                        gf_msg (this->name, GF_LOG_INFO, op_errno,
                                DHT_MSG_RENAME_FAILED,
                                "%s: Rename (linkto file) on %s failed, "
                                "(gfid = %s) ",
                                local->loc.path, prev->name,
                                local->loc.inode ?
                                uuid_utoa(local->loc.inode->gfid):"");
                }
        }
        if (xdata) {
                if (!local->xattr)
                        local->xattr = dict_ref (xdata);
                else
                        local->xattr = dict_copy_with_ref (xdata, local->xattr);
        }

        if ((src_cached == dst_cached) && (dst_hashed != dst_cached)) {
                link_frame = copy_frame (frame);
                if (!link_frame) {
                        goto err;
                }

                /* fop value sent as maxvalue because it is not used
                   anywhere in this case */
                link_local = dht_local_init (link_frame, &local->loc2, NULL,
                                             GF_FOP_MAXVALUE);
                if (!link_local) {
                        goto err;
                }

                if (link_local->loc.inode)
                        inode_unref (link_local->loc.inode);
                link_local->loc.inode = inode_ref (local->loc.inode);
                gf_uuid_copy (link_local->gfid, local->loc.inode->gfid);

                dht_linkfile_create (link_frame, dht_rename_links_create_cbk,
                                     this, src_cached, dst_hashed,
                                     &link_local->loc);
        }

err:
        /* Merge attrs only from src_cached. In case there of src_cached !=
         * dst_hashed, this ignores linkfile attrs. */
        if (prev == src_cached) {
                dht_iatt_merge (this, &local->stbuf, stbuf, prev);
                dht_iatt_merge (this, &local->preoldparent, preoldparent,
                                prev);
                dht_iatt_merge (this, &local->postoldparent, postoldparent,
                                prev);
                dht_iatt_merge (this, &local->preparent, prenewparent, prev);
                dht_iatt_merge (this, &local->postparent, postnewparent, prev);
        }


        /* NOTE: rename_subvol is the same subvolume from which dht_rename_cbk
         *       is called. since rename has already happened on rename_subvol,
         *       unlink should not be sent for oldpath (either linkfile or cached-file)
         *       on rename_subvol. */
        if (src_cached == dst_cached)
                rename_subvol = src_cached;
        else
                rename_subvol = dst_hashed;

        /* TODO: delete files in background */

        if (src_cached != dst_hashed && src_cached != dst_cached)
                local->call_cnt++;

        if (src_hashed != rename_subvol && src_hashed != src_cached)
                local->call_cnt++;

        if (dst_cached && dst_cached != dst_hashed && dst_cached != src_cached)
                local->call_cnt++;

        if (local->call_cnt == 0)
                goto unwind;

        DHT_MARK_FOP_INTERNAL (xattr);

        if (src_cached != dst_hashed && src_cached != dst_cached) {
                dict_t *xattr_new = NULL;

                xattr_new = dict_copy_with_ref (xattr, NULL);

                gf_msg_trace (this->name, 0,
                              "deleting old src datafile %s @ %s",
                              local->loc.path, src_cached->name);

                if (gf_uuid_compare (local->loc.pargfid,
                                  local->loc2.pargfid) == 0) {
                        DHT_MARKER_DONT_ACCOUNT(xattr_new);
                }

                DHT_CHANGELOG_TRACK_AS_RENAME(xattr_new, &local->loc,
                                              &local->loc2);
                STACK_WIND_COOKIE (frame, dht_rename_unlink_cbk, src_cached,
                                   src_cached, src_cached->fops->unlink,
                                   &local->loc, 0, xattr_new);

                dict_unref (xattr_new);
                xattr_new = NULL;
        }

        if (src_hashed != rename_subvol && src_hashed != src_cached) {
                dict_t *xattr_new = NULL;

                xattr_new = dict_copy_with_ref (xattr, NULL);

                gf_msg_trace (this->name, 0,
                              "deleting old src linkfile %s @ %s",
                              local->loc.path, src_hashed->name);

                DHT_MARKER_DONT_ACCOUNT(xattr_new);

                STACK_WIND_COOKIE (frame, dht_rename_unlink_cbk, src_hashed,
                                   src_hashed, src_hashed->fops->unlink,
                                   &local->loc, 0, xattr_new);

                dict_unref (xattr_new);
                xattr_new = NULL;
        }

        if (dst_cached
            && (dst_cached != dst_hashed)
            && (dst_cached != src_cached)) {
                gf_msg_trace (this->name, 0,
                              "deleting old dst datafile %s @ %s",
                              local->loc2.path, dst_cached->name);

                STACK_WIND_COOKIE (frame, dht_rename_unlink_cbk, dst_cached,
                                   dst_cached, dst_cached->fops->unlink,
                                   &local->loc2, 0, xattr);
        }
        if (xattr)
                dict_unref (xattr);
        return 0;

unwind:
        WIPE (&local->preoldparent);
        WIPE (&local->postoldparent);
        WIPE (&local->preparent);
        WIPE (&local->postparent);

        dht_rename_done (frame, this);

        return 0;

cleanup:
        dht_rename_cleanup (frame);

        return 0;
}


int
dht_do_rename (call_frame_t *frame)
{
        dht_local_t *local         = NULL;
        xlator_t    *dst_hashed    = NULL;
        xlator_t    *src_cached    = NULL;
        xlator_t    *dst_cached    = NULL;
        xlator_t    *this          = NULL;
        xlator_t    *rename_subvol = NULL;

        local = frame->local;
        this  = frame->this;

        dst_hashed = local->dst_hashed;
        dst_cached = local->dst_cached;
        src_cached = local->src_cached;

        if (src_cached == dst_cached)
                rename_subvol = src_cached;
        else
                rename_subvol = dst_hashed;

        if ((src_cached != dst_hashed) && (rename_subvol == dst_hashed)) {
                DHT_MARKER_DONT_ACCOUNT(local->xattr_req);
        }

        if (rename_subvol == src_cached) {
                DHT_CHANGELOG_TRACK_AS_RENAME(local->xattr_req, &local->loc,
                                              &local->loc2);
        }

        gf_msg_trace (this->name, 0,
                      "renaming %s => %s (%s)",
                      local->loc.path, local->loc2.path, rename_subvol->name);

        if (local->linked == _gf_true)
                FRAME_SU_DO (frame, dht_local_t);
        STACK_WIND_COOKIE (frame, dht_rename_cbk, rename_subvol, rename_subvol,
                           rename_subvol->fops->rename, &local->loc,
                           &local->loc2, local->xattr_req);
        return 0;
}

int
dht_rename_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      inode_t *inode, struct iatt *stbuf,
                      struct iatt *preparent, struct iatt *postparent,
                      dict_t *xdata)
{
        dht_local_t  *local = NULL;
        xlator_t     *prev = NULL;

        local = frame->local;
        prev = cookie;

        if (op_ret == -1) {
                gf_msg_debug (this->name, 0,
                              "link/file on %s failed (%s)",
                              prev->name, strerror (op_errno));
                local->op_ret   = -1;
                local->op_errno = op_errno;
                local->added_link = _gf_false;
        } else
                dht_iatt_merge (this, &local->stbuf, stbuf, prev);

        if (local->op_ret == -1)
                goto cleanup;

        dht_do_rename (frame);

        return 0;

cleanup:
        dht_rename_cleanup (frame);

        return 0;
}

int
dht_rename_linkto_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      inode_t *inode, struct iatt *stbuf,
                      struct iatt *preparent, struct iatt *postparent,
                      dict_t *xdata)
{
        dht_local_t     *local = NULL;
        xlator_t        *prev = NULL;
        xlator_t        *src_cached = NULL;
        dict_t          *xattr = NULL;

        local = frame->local;
        DHT_MARK_FOP_INTERNAL (xattr);
        prev = cookie;
        src_cached = local->src_cached;

        if (op_ret == -1) {
                gf_msg_debug (this->name, 0,
                              "link/file on %s failed (%s)",
                              prev->name, strerror (op_errno));
                local->op_ret = -1;
                local->op_errno = op_errno;
        }

        /* If linkto creation failed move to failure cleanup code,
        * instead of continuing with creating the link file */
        if (local->op_ret != 0) {
                goto cleanup;
        }

        gf_msg_trace (this->name, 0,
                      "link %s => %s (%s)", local->loc.path,
                      local->loc2.path, src_cached->name);
        if (gf_uuid_compare (local->loc.pargfid,
                                local->loc2.pargfid) == 0) {
                DHT_MARKER_DONT_ACCOUNT(xattr);
        }

        local->added_link = _gf_true;

        STACK_WIND_COOKIE (frame, dht_rename_link_cbk, src_cached, src_cached,
                           src_cached->fops->link, &local->loc, &local->loc2,
                           xattr);

        if (xattr)
                dict_unref (xattr);

        return 0;

cleanup:
        dht_rename_cleanup (frame);

        if (xattr)
                dict_unref (xattr);

        return 0;
}

int
dht_rename_unlink_links_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno,
                             struct iatt *preparent, struct iatt *postparent,
                             dict_t *xdata)
{
	dht_local_t  *local = NULL;
	xlator_t     *prev = NULL;


	local = frame->local;
	prev = cookie;

	if ((op_ret == -1) && (op_errno != ENOENT)) {
		gf_msg_debug (this->name, 0,
		              "unlink of %s on %s failed (%s)",
			      local->loc2.path, prev->name,
                              strerror (op_errno));
		local->op_ret   = -1;
		local->op_errno = op_errno;
	}

        if (local->op_ret == -1)
                goto cleanup;

        dht_do_rename (frame);

	return 0;

cleanup:
        dht_rename_cleanup (frame);

	return 0;
}


int
dht_rename_create_links (call_frame_t *frame)
{
        dht_local_t *local = NULL;
        xlator_t    *this = NULL;
        xlator_t    *src_hashed = NULL;
        xlator_t    *src_cached = NULL;
        xlator_t    *dst_hashed = NULL;
        xlator_t    *dst_cached = NULL;
        int          call_cnt = 0;
        dict_t      *xattr = NULL;


        local = frame->local;
        this  = frame->this;

        src_hashed = local->src_hashed;
        src_cached = local->src_cached;
        dst_hashed = local->dst_hashed;
        dst_cached = local->dst_cached;

        DHT_MARK_FOP_INTERNAL (xattr);

        if (src_cached == dst_cached) {
                dict_t *xattr_new = NULL;

                if (dst_hashed == dst_cached)
                        goto nolinks;

                xattr_new = dict_copy_with_ref (xattr, NULL);

		gf_msg_trace (this->name, 0,
		              "unlinking dst linkfile %s @ %s",
			      local->loc2.path, dst_hashed->name);

                DHT_MARKER_DONT_ACCOUNT(xattr_new);

		STACK_WIND_COOKIE (frame, dht_rename_unlink_links_cbk,
                                   dst_hashed, dst_hashed,
			           dst_hashed->fops->unlink, &local->loc2, 0,
			           xattr_new);

                dict_unref (xattr_new);
                if (xattr)
                        dict_unref (xattr);

                return 0;
        }

        if (src_cached != dst_hashed) {
                /* needed to create the link file */
                call_cnt++;
                if (dst_hashed != src_hashed)
                        /* needed to create the linkto file */
                        call_cnt ++;
        }

        /* We should not have any failures post the link creation, as this
         * introduces the newname into the namespace. Clients could have cached
         * the existence of the newname and may start taking actions based on
         * the same. Hence create the linkto first, and then attempt the link.
         *
         * NOTE: If another client is attempting the same oldname -> newname
         * rename, and finds both file names as existing, and are hard links
         * to each other, then FUSE would send in an unlink for oldname. In
         * this time duration if we treat the linkto as a critical error and
         * unlink the newname we created, we would have effectively lost the
         * file to rename operations. */
        if (dst_hashed != src_hashed && src_cached != dst_hashed) {
                gf_msg_trace (this->name, 0,
                              "linkfile %s @ %s => %s",
                              local->loc.path, dst_hashed->name,
                              src_cached->name);

                memcpy (local->gfid, local->loc.inode->gfid, 16);
                dht_linkfile_create (frame, dht_rename_linkto_cbk, this,
                                     src_cached, dst_hashed, &local->loc);
        } else if (src_cached != dst_hashed) {
                dict_t *xattr_new = NULL;

                xattr_new = dict_copy_with_ref (xattr, NULL);

                gf_msg_trace (this->name, 0,
                              "link %s => %s (%s)", local->loc.path,
                              local->loc2.path, src_cached->name);
                if (gf_uuid_compare (local->loc.pargfid,
                                  local->loc2.pargfid) == 0) {
                        DHT_MARKER_DONT_ACCOUNT(xattr_new);
                }

                local->added_link = _gf_true;

                STACK_WIND_COOKIE (frame, dht_rename_link_cbk, src_cached,
                                   src_cached, src_cached->fops->link,
                                   &local->loc, &local->loc2, xattr_new);

                dict_unref (xattr_new);
        }

nolinks:
        if (!call_cnt) {
                /* skip to next step */
                dht_do_rename (frame);
        }
        if (xattr)
                dict_unref (xattr);

        return 0;
}

int
dht_rename_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int op_ret, int op_errno,
                       inode_t *inode, struct iatt *stbuf, dict_t *xattr,
                       struct iatt *postparent)
{
        dht_local_t *local                      = NULL;
        int          call_cnt                   = 0;
        dht_conf_t  *conf                       = NULL;

        local = frame->local;
        conf = this->private;

        if (op_ret < 0) {
                /* The meaning of is_linkfile is overloaded here. For locking
                 * to work properly both rebalance and rename should acquire
                 * lock on datafile. The reason for sending this lookup is to
                 * find out whether we've acquired a lock on data file.
                 * Between the lookup before rename and this rename, the
                 * file could be migrated by a rebalance process and now this
                 * file this might be a linkto file. We verify that by sending
                 * this lookup. However, if this lookup fails we cannot really
                 * say whether we've acquired lock on a datafile or linkto file.
                 * So, we act conservatively and _assume_
                 * that this is a linkfile and fail the rename operation.
                 */
                local->is_linkfile = _gf_true;
        } else if (xattr && check_is_linkfile (inode, stbuf, xattr,
                                               conf->link_xattr_name)) {
                local->is_linkfile = _gf_true;
        }

        call_cnt = dht_frame_return (frame);
        if (is_last_call (call_cnt)) {
                if (local->is_linkfile) {
                        local->op_ret = -1;
                        local->op_errno = op_errno;
                        goto fail;
                }

                dht_rename_create_links (frame);
        }

        return 0;
fail:
        dht_rename_unlock (frame, this);
        return 0;
}

int32_t
dht_rename_lock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        dht_local_t *local                      = NULL;
        char         src_gfid[GF_UUID_BUF_SIZE] = {0};
        char         dst_gfid[GF_UUID_BUF_SIZE] = {0};
        dict_t      *xattr_req                  = NULL;
        dht_conf_t  *conf                       = NULL;
        int          i                          = 0;

        local = frame->local;
        conf = this->private;

        if (op_ret < 0) {
                uuid_utoa_r (local->loc.inode->gfid, src_gfid);

                if (local->loc2.inode)
                        uuid_utoa_r (local->loc2.inode->gfid, dst_gfid);

                gf_msg (this->name, GF_LOG_WARNING, op_errno,
                        DHT_MSG_INODE_LK_ERROR,
                        "acquiring inodelk failed "
                        "rename (%s:%s:%s %s:%s:%s)",
                        local->loc.path, src_gfid, local->src_cached->name,
                        local->loc2.path, dst_gfid,
                        local->dst_cached ? local->dst_cached->name : NULL);

                local->op_ret = -1;
                local->op_errno = op_errno;

                goto done;
        }

        xattr_req = dict_new ();
        if (xattr_req == NULL) {
                local->op_ret = -1;
                local->op_errno = ENOMEM;
                goto done;
        }

        op_ret = dict_set_uint32 (xattr_req,
                                  conf->link_xattr_name, 256);
        if (op_ret < 0) {
                local->op_ret = -1;
                local->op_errno = -op_ret;
                goto done;
        }

        local->call_cnt = local->lock.lk_count;

        for (i = 0; i < local->lock.lk_count; i++) {
                STACK_WIND (frame, dht_rename_lookup_cbk,
                            local->lock.locks[i]->xl,
                            local->lock.locks[i]->xl->fops->lookup,
                            &local->lock.locks[i]->loc, xattr_req);
        }

        dict_unref (xattr_req);
        return 0;

done:
        /* Its fine to call unlock even when no locks are acquired, as we check
         * for lock->locked before winding a unlock call.
         */
        dht_rename_unlock (frame, this);

        if (xattr_req)
                dict_unref (xattr_req);

        return 0;
}

int
dht_rename_lock (call_frame_t *frame)
{
        dht_local_t  *local    = NULL;
        int           count    = 1, ret = -1;
        dht_lock_t  **lk_array = NULL;

        local = frame->local;

        if (local->dst_cached)
                count++;

        lk_array = GF_CALLOC (count, sizeof (*lk_array), gf_common_mt_char);
        if (lk_array == NULL)
                goto err;

        lk_array[0] = dht_lock_new (frame->this, local->src_cached, &local->loc,
                                    F_WRLCK, DHT_FILE_MIGRATE_DOMAIN);
        if (lk_array[0] == NULL)
                goto err;

        if (local->dst_cached) {
                lk_array[1] = dht_lock_new (frame->this, local->dst_cached,
                                            &local->loc2, F_WRLCK,
                                            DHT_FILE_MIGRATE_DOMAIN);
                if (lk_array[1] == NULL)
                        goto err;
        }

        local->lock.locks = lk_array;
        local->lock.lk_count = count;

        ret = dht_blocking_inodelk (frame, lk_array, count,
                                    FAIL_ON_ANY_ERROR, dht_rename_lock_cbk);
        if (ret < 0) {
                local->lock.locks = NULL;
                local->lock.lk_count = 0;
                goto err;
        }

        return 0;
err:
        if (lk_array != NULL) {
                int tmp_count = 0, i = 0;

                for (i = 0; (i < count) && (lk_array[i]); i++, tmp_count++);

                dht_lock_array_free (lk_array, tmp_count);
                GF_FREE (lk_array);
        }

        return -1;
}

int
dht_rename (call_frame_t *frame, xlator_t *this,
            loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        xlator_t    *src_cached = NULL;
        xlator_t    *src_hashed = NULL;
        xlator_t    *dst_cached = NULL;
        xlator_t    *dst_hashed = NULL;
        int          op_errno = -1;
        int          ret = -1;
        dht_local_t *local = NULL;
        char         gfid[GF_UUID_BUF_SIZE] = {0};

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (oldloc, err);
        VALIDATE_OR_GOTO (newloc, err);

        gf_uuid_unparse(oldloc->inode->gfid, gfid);

        src_hashed = dht_subvol_get_hashed (this, oldloc);
        if (!src_hashed) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        DHT_MSG_RENAME_FAILED,
                        "No hashed subvolume in layout for path=%s,"
                        "(gfid = %s)", oldloc->path, gfid);
                op_errno = EINVAL;
                goto err;
        }

        src_cached = dht_subvol_get_cached (this, oldloc->inode);
        if (!src_cached) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        DHT_MSG_RENAME_FAILED,
                        "No cached subvolume for path = %s,"
                        "(gfid = %s)", oldloc->path, gfid);

                op_errno = EINVAL;
                goto err;
        }

        dst_hashed = dht_subvol_get_hashed (this, newloc);
        if (!dst_hashed) {
                gf_msg (this->name, GF_LOG_INFO, 0,
                        DHT_MSG_RENAME_FAILED,
                        "No hashed subvolume in layout for path=%s",
                        newloc->path);
                op_errno = EINVAL;
                goto err;
        }

        if (newloc->inode)
                dst_cached = dht_subvol_get_cached (this, newloc->inode);

        local = dht_local_init (frame, oldloc, NULL, GF_FOP_RENAME);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        /* cached_subvol will be set from dht_local_init, reset it to NULL,
           as the logic of handling rename is different  */
        local->cached_subvol = NULL;

        ret = loc_copy (&local->loc2, newloc);
        if (ret == -1) {
                op_errno = ENOMEM;
                goto err;
        }

        local->src_hashed = src_hashed;
        local->src_cached = src_cached;
        local->dst_hashed = dst_hashed;
        local->dst_cached = dst_cached;
        if (xdata)
                local->xattr_req = dict_ref (xdata);

        gf_msg (this->name, GF_LOG_INFO, 0,
                DHT_MSG_RENAME_INFO,
                "renaming %s (hash=%s/cache=%s) => %s (hash=%s/cache=%s)",
                oldloc->path, src_hashed->name, src_cached->name,
                newloc->path, dst_hashed->name,
                dst_cached ? dst_cached->name : "<nul>");

        if (IA_ISDIR (oldloc->inode->ia_type)) {
                dht_rename_dir (frame, this);
        } else {
                local->op_ret = 0;
                ret = dht_rename_lock (frame);
                if (ret < 0)
                        goto err;
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (rename, frame, -1, op_errno, NULL, NULL, NULL, NULL,
                          NULL, NULL);

        return 0;
}
