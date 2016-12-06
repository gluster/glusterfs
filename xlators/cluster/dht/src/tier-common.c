/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "glusterfs.h"
#include "xlator.h"
#include "libxlator.h"
#include "dht-common.h"
#include "defaults.h"
#include "tier-common.h"
#include "tier.h"

int
dht_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int op_ret, int op_errno,
              inode_t *inode, struct iatt *stbuf, struct iatt *preparent,
              struct iatt *postparent, dict_t *xdata);


int
tier_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
              int op_ret, int op_errno,
              inode_t *inode, struct iatt *stbuf, struct iatt *preparent,
              struct iatt *postparent, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        loc_t        *oldloc = NULL;
        loc_t        *newloc = NULL;

        local = frame->local;

        oldloc = &local->loc;
        newloc = &local->loc2;

        if (op_ret == -1) {
                /* No continuation on DHT inode missing errors, as we should
                 * then have a good stbuf that states P2 happened. We would
                 * get inode missing if, the file completed migrated between
                 * the lookup and the link call */
                goto out;
        }

        if (local->call_cnt != 1) {
                goto out;
        }

        local->call_cnt = 2;

        /* Do this on the hot tier now */

        STACK_WIND (frame, tier_link_cbk, local->cached_subvol,
                    local->cached_subvol->fops->link,
                    oldloc, newloc, xdata);

        return 0;

out:
        DHT_STRIP_PHASE1_FLAGS (stbuf);

        DHT_STACK_UNWIND (link, frame, op_ret, op_errno, inode, stbuf,
                          preparent, postparent, NULL);

        return 0;
}


int
tier_link (call_frame_t *frame, xlator_t *this,
          loc_t *oldloc, loc_t *newloc, dict_t *xdata)
{
        xlator_t    *cached_subvol = NULL;
        xlator_t    *hashed_subvol = NULL;
        int          op_errno = -1;
        int          ret = -1;
        dht_local_t *local = NULL;
        dht_conf_t   *conf           = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (oldloc, err);
        VALIDATE_OR_GOTO (newloc, err);

        conf = this->private;

        local = dht_local_init (frame, oldloc, NULL, GF_FOP_LINK);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }
        local->call_cnt = 1;

        cached_subvol = local->cached_subvol;

        if (!cached_subvol) {
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for path=%s", oldloc->path);
                op_errno = ENOENT;
                goto err;
        }

        hashed_subvol = TIER_HASHED_SUBVOL;

        ret = loc_copy (&local->loc2, newloc);
        if (ret == -1) {
                op_errno = ENOMEM;
                goto err;
        }

        if (hashed_subvol == cached_subvol) {
                STACK_WIND (frame, dht_link_cbk,
                            cached_subvol, cached_subvol->fops->link,
                            oldloc, newloc, xdata);
                return 0;
        }


        /* Create hardlinks to both the data file on the hot tier
           and the linkto file on the cold tier */

        gf_uuid_copy (local->gfid, oldloc->inode->gfid);

        STACK_WIND (frame, tier_link_cbk,
                    hashed_subvol, hashed_subvol->fops->link,
                    oldloc, newloc, xdata);

        return 0;
err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (link, frame, -1, op_errno, NULL, NULL, NULL, NULL,
                          NULL);
        return 0;
}



int
tier_create_unlink_stale_linkto_cbk (call_frame_t *frame, void *cookie,
                                     xlator_t *this, int op_ret, int op_errno,
                                     struct iatt *preparent,
                                     struct iatt *postparent, dict_t *xdata)
{

        dht_local_t     *local = NULL;

        local = frame->local;

        if (local->params) {
                dict_del (local->params, GLUSTERFS_INTERNAL_FOP_KEY);
        }

        DHT_STACK_UNWIND (create, frame, -1, local->op_errno,
                          NULL, NULL, NULL, NULL, NULL, NULL);

        return 0;
}

int
tier_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int op_ret, int op_errno,
                 fd_t *fd, inode_t *inode, struct iatt *stbuf,
                 struct iatt *preparent, struct iatt *postparent, dict_t *xdata)
{
        xlator_t     *prev           = NULL;
        int           ret            = -1;
        dht_local_t  *local          = NULL;
        xlator_t     *hashed_subvol  = NULL;
        dht_conf_t   *conf           = NULL;

        local = frame->local;
        conf  = this->private;

        hashed_subvol = TIER_HASHED_SUBVOL;

        if (!local) {
                op_ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (op_ret == -1) {
                if (local->linked == _gf_true && local->xattr_req) {
                        local->op_errno = op_errno;
                        local->op_ret = op_ret;
                        ret = dht_fill_dict_to_avoid_unlink_of_migrating_file
                              (local->xattr_req);
                        if (ret) {
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        DHT_MSG_DICT_SET_FAILED,
                                        "Failed to set dictionary value to "
                                        "unlink of migrating file");
                                goto out;
                        }

                        STACK_WIND (frame,
                                    tier_create_unlink_stale_linkto_cbk,
                                    hashed_subvol,
                                    hashed_subvol->fops->unlink,
                                    &local->loc, 0, local->xattr_req);
                        return 0;
                }
                goto out;
        }

        prev = cookie;

        if (local->loc.parent) {
                dht_inode_ctx_time_update (local->loc.parent, this,
                                           preparent, 0);

                dht_inode_ctx_time_update (local->loc.parent, this,
                                           postparent, 1);
        }

        ret = dht_layout_preset (this, prev, inode);
        if (ret != 0) {
                gf_msg_debug (this->name, 0,
                              "could not set preset layout for subvol %s",
                              prev->name);
                op_ret   = -1;
                op_errno = EINVAL;
                goto out;
        }

        local->op_errno = op_errno;

        if (local->linked == _gf_true) {
                local->stbuf = *stbuf;
                dht_linkfile_attr_heal (frame, this);
        }
out:
        if (local->xattr_req) {
                dict_del (local->xattr_req, TIER_LINKFILE_GFID);
        }

        DHT_STRIP_PHASE1_FLAGS (stbuf);

        DHT_STACK_UNWIND (create, frame, op_ret, op_errno, fd, inode,
                          stbuf, preparent, postparent, xdata);

        return 0;
}

int
tier_create_linkfile_create_cbk (call_frame_t *frame, void *cookie,
                                xlator_t *this,
                                int32_t op_ret, int32_t op_errno,
                                inode_t *inode, struct iatt *stbuf,
                                struct iatt *preparent, struct iatt *postparent,
                                dict_t *xdata)
{
        dht_local_t     *local             = NULL;
        xlator_t        *cached_subvol     = NULL;
        dht_conf_t      *conf              = NULL;
        int              ret               = -1;
        unsigned char   *gfid              = NULL;

        local = frame->local;
        if (!local) {
                op_errno = EINVAL;
                goto err;
        }

        if (op_ret == -1) {
                local->op_errno = op_errno;
                goto err;
        }

        conf = this->private;
        if (!conf) {
                local->op_errno = EINVAL;
                op_errno = EINVAL;
                goto err;
        }

        cached_subvol = TIER_UNHASHED_SUBVOL;

        if (local->params) {
                dict_del (local->params, conf->link_xattr_name);
                dict_del (local->params, GLUSTERFS_INTERNAL_FOP_KEY);
        }

        /*
         * We will delete the linkfile if data file creation fails.
         * When deleting this stale linkfile, there is a possibility
         * for a race between this linkfile deletion and a stale
         * linkfile deletion triggered by another lookup from different
         * client.
         *
         * For eg:
         *
         *     Client 1                        Client 2
         *
         * 1   linkfile created for foo
         *
         * 2   data file creation failed
         *
         * 3                                   creating a file with same name
         *
         * 4                                   lookup before creation deleted
         *                                     the linkfile created by client1
         *                                     considering as a stale linkfile.
         *
         * 5                                   New linkfile created for foo
         *                                     with different gfid.
         *
         * 6 Trigger linkfile deletion as
         *   data file creation failed.
         *
         * 7 Linkfile deleted which is
         *   created by client2.
         *
         * 8                                   Data file created.
         *
         * With this race, we will end up having a file in a non-hashed subvol
         * without a linkfile in hashed subvol.
         *
         * To avoid this, we store the gfid of linkfile created by client, So
         * If we delete the linkfile , we validate gfid of existing file with
         * stored value from posix layer.
         *
         * Storing this value in local->xattr_req as local->params was also used
         * to create the data file. During the linkfile deletion we will use
         * local->xattr_req dictionary.
         */
        if (!local->xattr_req) {
                local->xattr_req = dict_new ();
                if (!local->xattr_req) {
                        local->op_errno = ENOMEM;
                        op_errno = ENOMEM;
                        goto err;
                }
        }

        gfid = GF_CALLOC (1, sizeof (uuid_t), gf_common_mt_char);
        if (!gfid) {
                local->op_errno = ENOMEM;
                op_errno = ENOMEM;
                goto err;
        }

        gf_uuid_copy (gfid, stbuf->ia_gfid);
        ret = dict_set_dynptr (local->xattr_req, TIER_LINKFILE_GFID,
                                   gfid, sizeof (uuid_t));
        if (ret) {
                GF_FREE (gfid);
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        DHT_MSG_DICT_SET_FAILED,
                        "Failed to set dictionary value"
                        " : key = %s", TIER_LINKFILE_GFID);
        }

        STACK_WIND_COOKIE (frame, tier_create_cbk, cached_subvol,
                           cached_subvol, cached_subvol->fops->create,
                           &local->loc, local->flags, local->mode,
                           local->umask, local->fd, local->params);

        return 0;
err:
        DHT_STACK_UNWIND (create, frame, -1, op_errno, NULL, NULL, NULL,
                          NULL, NULL, NULL);
        return 0;
}

gf_boolean_t
tier_is_hot_tier_decommissioned (xlator_t *this)
{
        dht_conf_t              *conf        = NULL;
        xlator_t                *hot_tier    = NULL;
        int                      i           = 0;

        conf = this->private;
        hot_tier = conf->subvolumes[1];

        if (conf->decommission_subvols_cnt) {
                for (i = 0; i < conf->subvolume_cnt; i++) {
                        if (conf->decommissioned_bricks[i] &&
                                conf->decommissioned_bricks[i] == hot_tier)
                                return _gf_true;
                }
        }

        return _gf_false;
}

int
tier_create (call_frame_t *frame, xlator_t *this,
            loc_t *loc, int32_t flags, mode_t mode,
            mode_t umask, fd_t *fd, dict_t *params)
{
        int                     op_errno            = -1;
        dht_local_t             *local              = NULL;
        dht_conf_t              *conf               = NULL;
        xlator_t                *hot_subvol         = NULL;
        xlator_t                *cold_subvol        = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);

        conf = this->private;

        dht_get_du_info (frame, this, loc);

        local = dht_local_init (frame, loc, fd, GF_FOP_CREATE);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }


        cold_subvol = TIER_HASHED_SUBVOL;
        hot_subvol = TIER_UNHASHED_SUBVOL;

        if (conf->subvolumes[0] != cold_subvol) {
                hot_subvol = conf->subvolumes[0];
        }
        /*
         * if hot tier full, write to cold.
         * Also if hot tier is full, create in cold
         */
        if (dht_is_subvol_filled (this, hot_subvol) ||
            tier_is_hot_tier_decommissioned (this)) {
                gf_msg_debug (this->name, 0,
                              "creating %s on %s", loc->path,
                              cold_subvol->name);

                STACK_WIND_COOKIE (frame, tier_create_cbk, cold_subvol,
                                   cold_subvol, cold_subvol->fops->create,
                                   loc, flags, mode, umask, fd, params);
        } else {
                local->params = dict_ref (params);
                local->flags = flags;
                local->mode = mode;
                local->umask = umask;
                local->cached_subvol = hot_subvol;
                local->hashed_subvol = cold_subvol;

                gf_msg_debug (this->name, 0,
                              "creating %s on %s (link at %s)", loc->path,
                              hot_subvol->name, cold_subvol->name);

                dht_linkfile_create (frame, tier_create_linkfile_create_cbk,
                                     this, hot_subvol, cold_subvol, loc);

                goto out;
        }
out:
        return 0;

err:

        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (create, frame, -1, op_errno, NULL, NULL, NULL,
                          NULL, NULL, NULL);

        return 0;
}

int
tier_unlink_nonhashed_linkfile_cbk (call_frame_t *frame, void *cookie,
                                    xlator_t *this, int op_ret, int op_errno,
                                    struct iatt *preparent,
                                    struct iatt *postparent, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        xlator_t     *prev = NULL;

        local = frame->local;
        prev  = cookie;

        LOCK (&frame->lock);
        {
                if ((op_ret == -1) && (op_errno != ENOENT)) {
                        local->op_errno = op_errno;
                        local->op_ret = op_ret;
                        gf_msg_debug (this->name, op_errno,
                                      "Unlink link: subvolume %s"
                                      " returned -1",
                                      prev->name);
                        goto unlock;
                }

                local->op_ret = 0;
        }
unlock:
        UNLOCK (&frame->lock);

        if (local->op_ret == -1)
                goto err;
        DHT_STACK_UNWIND (unlink, frame, local->op_ret, local->op_errno,
                          &local->preparent, &local->postparent, NULL);


        return 0;

err:
        DHT_STACK_UNWIND (unlink, frame, -1, local->op_errno,
                          NULL, NULL, NULL);
        return 0;
}

int
tier_unlink_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, inode_t *inode,
                        struct iatt *preparent, dict_t *xdata,
                        struct iatt *postparent)
{
        dht_local_t             *local             = NULL;
        xlator_t                *prev              = NULL;
        dht_conf_t              *conf              = NULL;
        xlator_t                *hot_subvol        = NULL;

        local = frame->local;
        prev  = cookie;
        conf = this->private;
        hot_subvol = TIER_UNHASHED_SUBVOL;

        if (!op_ret) {
                /*
                 * linkfile present on hot tier. unlinking the linkfile
                 */
                STACK_WIND_COOKIE (frame, tier_unlink_nonhashed_linkfile_cbk,
                                   hot_subvol, hot_subvol, hot_subvol->fops->unlink,
                                   &local->loc, local->flags, NULL);
                return 0;
        }

        LOCK (&frame->lock);
        {
                if (op_errno == ENOENT) {
                        local->op_ret   = 0;
                        local->op_errno = op_errno;
                } else {
                        local->op_ret = op_ret;
                        local->op_errno = op_errno;
                }
                gf_msg_debug (this->name, op_errno,
                              "Lookup : subvolume %s returned -1",
                               prev->name);
        }

        UNLOCK (&frame->lock);

        DHT_STACK_UNWIND (unlink, frame, local->op_ret, local->op_errno,
                          &local->preparent, &local->postparent, xdata);

        return 0;
}

int
tier_unlink_linkfile_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int op_ret, int op_errno, struct iatt *preparent,
                         struct iatt *postparent, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        xlator_t     *prev = NULL;

        local = frame->local;
        prev  = cookie;

        LOCK (&frame->lock);
        {
                /* Ignore EINVAL for tier to ignore error when the file
                        does not exist on the other tier  */
                if ((op_ret == -1) && !((op_errno == ENOENT) ||
                                        (op_errno == EINVAL))) {
                        local->op_errno = op_errno;
                        local->op_ret   = op_ret;
                        gf_msg_debug (this->name, op_errno,
                                      "Unlink link: subvolume %s"
                                      " returned -1",
                                      prev->name);
                        goto unlock;
                }

                local->op_ret = 0;
        }
unlock:
        UNLOCK (&frame->lock);

        if (local->op_ret == -1)
                goto err;

        DHT_STACK_UNWIND (unlink, frame, local->op_ret, local->op_errno,
                          &local->preparent, &local->postparent, xdata);

        return 0;

err:
        DHT_STACK_UNWIND (unlink, frame, -1, local->op_errno,
                          NULL, NULL, NULL);
        return 0;
}

int32_t
tier_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, struct iatt *preparent,
                struct iatt *postparent, dict_t *xdata)
{
        dht_local_t     *local          = NULL;
        xlator_t        *prev           = NULL;
        struct iatt     *stbuf          = NULL;
        dht_conf_t      *conf           = NULL;
        int              ret            = -1;
        xlator_t        *hot_tier       = NULL;
        xlator_t        *cold_tier      = NULL;

        local = frame->local;
        prev  = cookie;
        conf  = this->private;

        cold_tier = TIER_HASHED_SUBVOL;
        hot_tier = TIER_UNHASHED_SUBVOL;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        if (op_errno == ENOENT) {
                                local->op_ret = 0;
                        } else {
                                local->op_ret   = -1;
                                local->op_errno = op_errno;
                        }
                        gf_msg_debug (this->name, op_errno,
                                      "Unlink: subvolume %s returned -1"
                                      " with errno = %d",
                                       prev->name, op_errno);
                        goto unlock;
                }

                local->op_ret = 0;

                local->postparent = *postparent;
                local->preparent = *preparent;

                if (local->loc.parent) {
                        dht_inode_ctx_time_update (local->loc.parent, this,
                                                   &local->preparent, 0);
                        dht_inode_ctx_time_update (local->loc.parent, this,
                                                   &local->postparent, 1);
                }
        }
unlock:
        UNLOCK (&frame->lock);

        if (local->op_ret)
                goto out;

        if (cold_tier != local->cached_subvol) {
                /*
                 * File is present in hot tier, so there will be
                 * a link file on cold tier, deleting the linkfile
                 * from cold tier
                 */
                STACK_WIND_COOKIE (frame, tier_unlink_linkfile_cbk, cold_tier,
                                   cold_tier, cold_tier->fops->unlink,
                                   &local->loc,
                                   local->flags, xdata);
                return 0;
        }

        ret = dict_get_bin (xdata, DHT_IATT_IN_XDATA_KEY, (void **) &stbuf);
        if (!ret && stbuf && ((IS_DHT_MIGRATION_PHASE2 (stbuf)) ||
                      IS_DHT_MIGRATION_PHASE1 (stbuf))) {
                /*
                 * File is migrating from cold to hot tier.
                 * Delete the destination linkfile.
                 */
                STACK_WIND_COOKIE (frame, tier_unlink_lookup_cbk, hot_tier,
                                   hot_tier, hot_tier->fops->lookup,
                                   &local->loc, NULL);
                return 0;

        }

out:
        DHT_STACK_UNWIND (unlink, frame, local->op_ret, local->op_errno,
                          &local->preparent, &local->postparent, xdata);

        return 0;
}

int
tier_unlink (call_frame_t *frame, xlator_t *this, loc_t *loc, int xflag,
            dict_t *xdata)
{
        xlator_t                *cached_subvol = NULL;
        xlator_t                *hashed_subvol = NULL;
        dht_conf_t              *conf          = NULL;
        int                      op_errno      = -1;
        dht_local_t             *local         = NULL;
        int                      ret           = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);

        conf = this->private;

        local = dht_local_init (frame, loc, NULL, GF_FOP_UNLINK);
        if (!local) {
                op_errno = ENOMEM;

                goto err;
        }

        hashed_subvol = TIER_HASHED_SUBVOL;

        cached_subvol = local->cached_subvol;
        if (!cached_subvol) {
                gf_msg_debug (this->name, 0,
                              "no cached subvolume for path=%s", loc->path);
                op_errno = EINVAL;
                goto err;
        }

        local->flags = xflag;
        if (IA_ISREG (loc->inode->ia_type) &&
            (hashed_subvol == cached_subvol)) {
                /*
                 * File resides in cold tier. We need to stat
                 * the file to see if it is being promoted.
                 * If yes we need to delete the destination
                 * file as well.
                 *
                 * Currently we are doing this check only for
                 * regular files.
                 */
                xdata = xdata ? dict_ref (xdata) : dict_new ();
                if (xdata) {
                        ret = dict_set_dynstr_with_alloc (xdata,
                                DHT_IATT_IN_XDATA_KEY, "yes");
                        if (ret) {
                                gf_msg_debug (this->name, 0,
                                        "Failed to set dictionary key %s",
                                         DHT_IATT_IN_XDATA_KEY);
                        }
                }
        }

        /*
         * File is on hot tier, delete the data file first, then
         * linkfile from cold.
         */
        STACK_WIND_COOKIE (frame, tier_unlink_cbk, cached_subvol,
                           cached_subvol, cached_subvol->fops->unlink, loc,
                           xflag, xdata);
        if (xdata)
                dict_unref (xdata);
        return 0;
err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (unlink, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

int
tier_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int op_ret, int op_errno, gf_dirent_t *orig_entries,
                 dict_t *xdata)
{
        dht_local_t  *local = NULL;
        gf_dirent_t   entries;
        gf_dirent_t  *orig_entry = NULL;
        gf_dirent_t  *entry = NULL;
        xlator_t     *prev = NULL;
        xlator_t     *next_subvol = NULL;
        off_t         next_offset = 0;
        int           count = 0;

        INIT_LIST_HEAD (&entries.list);
        prev = cookie;
        local = frame->local;

        if (op_ret < 0)
                goto done;

        list_for_each_entry (orig_entry, (&orig_entries->list), list) {
                next_offset = orig_entry->d_off;

                entry = gf_dirent_for_name (orig_entry->d_name);
                if (!entry) {
                        gf_msg (this->name, GF_LOG_ERROR, ENOMEM,
                                DHT_MSG_NO_MEMORY,
                                "Memory allocation failed ");
                        goto unwind;
                }

                entry->d_off  = orig_entry->d_off;
                entry->d_ino  = orig_entry->d_ino;
                entry->d_type = orig_entry->d_type;
                entry->d_len  = orig_entry->d_len;

                list_add_tail (&entry->list, &entries.list);
                count++;
        }
        op_ret = count;

done:
        if (count == 0) {
                /* non-zero next_offset means that
                   EOF is not yet hit on the current subvol
                */
                if (next_offset != 0) {
                        next_subvol = prev;
                } else {
                        goto unwind;
                }

                STACK_WIND_COOKIE (frame, tier_readdir_cbk, next_subvol,
                                   next_subvol, next_subvol->fops->readdir,
                                   local->fd, local->size, next_offset, NULL);
                return 0;
        }

unwind:
        if (op_ret < 0)
                op_ret = 0;

        DHT_STACK_UNWIND (readdir, frame, op_ret, op_errno, &entries, NULL);

        gf_dirent_free (&entries);

        return 0;
}

int
tier_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
                  int op_errno, gf_dirent_t *orig_entries, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        gf_dirent_t   entries;
        gf_dirent_t  *orig_entry = NULL;
        gf_dirent_t  *entry = NULL;
        xlator_t     *prev = NULL;
        xlator_t     *next_subvol = NULL;
        off_t         next_offset = 0;
        int           count = 0;
        dht_conf_t   *conf   = NULL;
        int           ret    = 0;
        inode_table_t           *itable = NULL;
        inode_t                 *inode = NULL;

        INIT_LIST_HEAD (&entries.list);
        prev = cookie;
        local = frame->local;
        itable = local->fd ? local->fd->inode->table : NULL;

        conf  = this->private;
        GF_VALIDATE_OR_GOTO(this->name, conf, unwind);

        if (op_ret < 0)
                goto done;

        list_for_each_entry (orig_entry, (&orig_entries->list), list) {
                next_offset = orig_entry->d_off;

                if (IA_ISINVAL(orig_entry->d_stat.ia_type)) {
                        /*stat failed somewhere- ignore this entry*/
                        continue;
                }

                entry = gf_dirent_for_name (orig_entry->d_name);
                if (!entry) {

                        goto unwind;
                }

                entry->d_off  = orig_entry->d_off;
                entry->d_stat = orig_entry->d_stat;
                entry->d_ino  = orig_entry->d_ino;
                entry->d_type = orig_entry->d_type;
                entry->d_len  = orig_entry->d_len;

                if (orig_entry->dict)
                        entry->dict = dict_ref (orig_entry->dict);

                if (check_is_linkfile (NULL, (&orig_entry->d_stat),
                                       orig_entry->dict,
                                       conf->link_xattr_name)) {
                        inode = inode_find (itable,
                                            orig_entry->d_stat.ia_gfid);
                        if (inode) {
                                ret = dht_layout_preset
                                        (this, TIER_UNHASHED_SUBVOL,
                                         inode);
                                if (ret)
                                        gf_msg (this->name,
                                                GF_LOG_WARNING, 0,
                                                DHT_MSG_LAYOUT_SET_FAILED,
                                                "failed to link the layout"
                                                " in inode");
                                inode_unref (inode);
                                inode = NULL;
                        }

                } else if (IA_ISDIR(entry->d_stat.ia_type)) {
                        if (orig_entry->inode) {
                                dht_inode_ctx_time_update (orig_entry->inode,
                                                           this, &entry->d_stat,
                                                           1);
                        }
                } else {
                        if (orig_entry->inode) {
                                ret = dht_layout_preset (this, prev,
                                                         orig_entry->inode);
                                if (ret)
                                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                                DHT_MSG_LAYOUT_SET_FAILED,
                                                "failed to link the layout "
                                                "in inode");

                                entry->inode = inode_ref (orig_entry->inode);
                        } else if (itable) {
                                /*
                                 * orig_entry->inode might be null if any upper
                                 * layer xlators below client set to null, to
                                 * force a lookup on the inode even if the inode
                                 * is present in the inode table. In that case
                                 * we just update the ctx to make sure we didn't
                                 * missed anything.
                                 */
                                inode = inode_find (itable,
                                                    orig_entry->d_stat.ia_gfid);
                                if (inode) {
                                        ret = dht_layout_preset
                                                (this, TIER_HASHED_SUBVOL,
                                                 inode);
                                        if (ret)
                                                gf_msg (this->name,
                                                     GF_LOG_WARNING, 0,
                                                     DHT_MSG_LAYOUT_SET_FAILED,
                                                     "failed to link the layout"
                                                     " in inode");
                                        inode_unref (inode);
                                        inode = NULL;
                                }
                        }
                }
                list_add_tail (&entry->list, &entries.list);
                count++;
        }
        op_ret = count;

done:
        if (count == 0) {
                /* non-zero next_offset means that
                   EOF is not yet hit on the current subvol
                */
                if (next_offset != 0) {
                        next_subvol = prev;
                } else {
                        goto unwind;
                }

                STACK_WIND_COOKIE (frame, tier_readdirp_cbk, next_subvol,
                                   next_subvol, next_subvol->fops->readdirp,
                                   local->fd, local->size, next_offset,
                                   local->xattr);
                return 0;
        }

unwind:
        if (op_ret < 0)
                op_ret = 0;

        DHT_STACK_UNWIND (readdirp, frame, op_ret, op_errno, &entries, NULL);

        gf_dirent_free (&entries);

        return 0;
}

int
tier_do_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
                off_t yoff, int whichop, dict_t *dict)
{
        dht_local_t  *local         = NULL;
        int           op_errno      = -1;
        xlator_t     *hashed_subvol = NULL;
        int           ret           = 0;
        dht_conf_t   *conf          = NULL;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (fd, err);
        VALIDATE_OR_GOTO (this->private, err);

        conf = this->private;

        local = dht_local_init (frame, NULL, NULL, whichop);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        local->fd = fd_ref (fd);
        local->size = size;
        local->xattr_req = (dict) ? dict_ref (dict) : NULL;

        hashed_subvol = TIER_HASHED_SUBVOL;


        /* TODO: do proper readdir */
        if (whichop == GF_FOP_READDIRP) {
                if (dict)
                        local->xattr = dict_ref (dict);
                else
                        local->xattr = dict_new ();

                if (local->xattr) {
                        ret = dict_set_uint32 (local->xattr,
                                               conf->link_xattr_name, 256);
                        if (ret)
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        DHT_MSG_DICT_SET_FAILED,
                                        "Failed to set dictionary value"
                                        " : key = %s",
                                        conf->link_xattr_name);

                }

                STACK_WIND_COOKIE (frame, tier_readdirp_cbk, hashed_subvol,
                                   hashed_subvol, hashed_subvol->fops->readdirp,
                                   fd, size, yoff, local->xattr);

        } else {
                STACK_WIND_COOKIE (frame, tier_readdir_cbk, hashed_subvol,
                                   hashed_subvol, hashed_subvol->fops->readdir,
                                   fd, size, yoff, local->xattr);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (readdir, frame, -1, op_errno, NULL, NULL);

        return 0;
}

int
tier_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t yoff, dict_t *xdata)
{
        int          op = GF_FOP_READDIR;
        dht_conf_t  *conf = NULL;
        int          i = 0;

        conf = this->private;
        if (!conf)
                goto out;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (!conf->subvolume_status[i]) {
                        op = GF_FOP_READDIRP;
                        break;
                }
        }

        if (conf->use_readdirp)
                op = GF_FOP_READDIRP;

out:
        tier_do_readdir (frame, this, fd, size, yoff, op, 0);
        return 0;
}

int
tier_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
               off_t yoff, dict_t *dict)
{
        tier_do_readdir (frame, this, fd, size, yoff, GF_FOP_READDIRP, dict);
        return 0;
}

int
tier_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int op_ret, int op_errno, struct statvfs *statvfs,
                dict_t *xdata)
{
        gf_boolean_t            event              = _gf_false;
        qdstatfs_action_t       action             = qdstatfs_action_OFF;
        dht_local_t             *local             = NULL;
        int                     this_call_cnt      = 0;
        int                     bsize              = 0;
        int                     frsize             = 0;
        GF_UNUSED int           ret                = 0;
        unsigned long           new_usage          = 0;
        unsigned long           cur_usage          = 0;
        xlator_t                *prev              = NULL;
        dht_conf_t              *conf              = NULL;
        tier_statvfs_t          *tier_stat         = NULL;

        prev = cookie;
        local = frame->local;
        GF_ASSERT (local);

        conf = this->private;

        if (xdata)
                ret = dict_get_int8 (xdata, "quota-deem-statfs",
                                     (int8_t *)&event);

        tier_stat = &local->tier_statvfs;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        local->op_errno = op_errno;
                        goto unlock;
                }
                if (!statvfs) {
                        op_errno = EINVAL;
                        local->op_ret = -1;
                        goto unlock;
                }
                local->op_ret = 0;

                switch (local->quota_deem_statfs) {
                case _gf_true:
                        if (event == _gf_true)
                                action = qdstatfs_action_COMPARE;
                        else
                                action = qdstatfs_action_NEGLECT;
                        break;

                case _gf_false:
                        if (event == _gf_true) {
                                action = qdstatfs_action_REPLACE;
                                local->quota_deem_statfs = _gf_true;
                        }
                        break;

                default:
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                DHT_MSG_INVALID_VALUE,
                                "Encountered third "
                                "value for boolean variable %d",
                                local->quota_deem_statfs);
                        break;
                }

                if (local->quota_deem_statfs) {
                        switch (action) {
                        case qdstatfs_action_NEGLECT:
                                goto unlock;

                        case qdstatfs_action_REPLACE:
                                local->statvfs = *statvfs;
                                goto unlock;

                        case qdstatfs_action_COMPARE:
                                new_usage = statvfs->f_blocks -
                                             statvfs->f_bfree;
                                cur_usage = local->statvfs.f_blocks -
                                             local->statvfs.f_bfree;

                                /* Take the max of the usage from subvols */
                                if (new_usage >= cur_usage)
                                        local->statvfs = *statvfs;
                                goto unlock;

                        default:
                                break;
                        }
                }

                if (local->statvfs.f_bsize != 0) {
                        bsize = max(local->statvfs.f_bsize, statvfs->f_bsize);
                        frsize = max(local->statvfs.f_frsize, statvfs->f_frsize);
                        dht_normalize_stats(&local->statvfs, bsize, frsize);
                        dht_normalize_stats(statvfs, bsize, frsize);
                } else {
                        local->statvfs.f_bsize    = statvfs->f_bsize;
                        local->statvfs.f_frsize   = statvfs->f_frsize;
                }

                if (prev == TIER_HASHED_SUBVOL) {
                        local->statvfs.f_blocks   = statvfs->f_blocks;
                        local->statvfs.f_files    = statvfs->f_files;
                        local->statvfs.f_fsid     = statvfs->f_fsid;
                        local->statvfs.f_flag     = statvfs->f_flag;
                        local->statvfs.f_namemax  = statvfs->f_namemax;
                        tier_stat->blocks_used    = (statvfs->f_blocks - statvfs->f_bfree);
                        tier_stat->pblocks_used   = (statvfs->f_blocks - statvfs->f_bavail);
                        tier_stat->files_used     = (statvfs->f_files - statvfs->f_ffree);
                        tier_stat->pfiles_used    = (statvfs->f_files - statvfs->f_favail);
                        tier_stat->hashed_fsid    = statvfs->f_fsid;
                } else {
                        tier_stat->unhashed_fsid      = statvfs->f_fsid;
                        tier_stat->unhashed_blocks_used    = (statvfs->f_blocks - statvfs->f_bfree);
                        tier_stat->unhashed_pblocks_used   = (statvfs->f_blocks - statvfs->f_bavail);
                        tier_stat->unhashed_files_used     = (statvfs->f_files - statvfs->f_ffree);
                        tier_stat->unhashed_pfiles_used    = (statvfs->f_files - statvfs->f_favail);
                }

        }
unlock:
        UNLOCK (&frame->lock);

        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt)) {
                if (tier_stat->unhashed_fsid != tier_stat->hashed_fsid) {
                        tier_stat->blocks_used    += tier_stat->unhashed_blocks_used;
                        tier_stat->pblocks_used   += tier_stat->unhashed_pblocks_used;
                        tier_stat->files_used     += tier_stat->unhashed_files_used;
                        tier_stat->pfiles_used    += tier_stat->unhashed_pfiles_used;
                }
                local->statvfs.f_bfree = local->statvfs.f_blocks -
                        tier_stat->blocks_used;
                local->statvfs.f_bavail = local->statvfs.f_blocks -
                        tier_stat->pblocks_used;
                local->statvfs.f_ffree = local->statvfs.f_files -
                        tier_stat->files_used;
                local->statvfs.f_favail = local->statvfs.f_files -
                        tier_stat->pfiles_used;
                DHT_STACK_UNWIND (statfs, frame, local->op_ret, local->op_errno,
                                  &local->statvfs, xdata);
        }

        return 0;
}


int
tier_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        dht_local_t      *local  = NULL;
        dht_conf_t       *conf = NULL;
        int               op_errno = -1;
        int               i = -1;
        inode_t          *inode         = NULL;
        inode_table_t    *itable        = NULL;
        uuid_t            root_gfid     = {0, };
        loc_t             newloc        = {0, };

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (loc, err);
        VALIDATE_OR_GOTO (this->private, err);

        conf = this->private;

        local = dht_local_init (frame, NULL, NULL, GF_FOP_STATFS);
        if (!local) {
                op_errno = ENOMEM;
                goto err;
        }

        if (loc->inode && !IA_ISDIR (loc->inode->ia_type)) {
                itable = loc->inode->table;
                if (!itable) {
                        op_errno = EINVAL;
                        goto err;
                }

                loc = &local->loc2;
                root_gfid[15] = 1;

                inode = inode_find (itable, root_gfid);
                if (!inode) {
                        op_errno = EINVAL;
                        goto err;
                }

                dht_build_root_loc (inode, &newloc);
                loc = &newloc;
        }

        local->call_cnt = conf->subvolume_cnt;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                STACK_WIND_COOKIE (frame, tier_statfs_cbk, conf->subvolumes[i],
                                   conf->subvolumes[i],
                                   conf->subvolumes[i]->fops->statfs, loc,
                                   xdata);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (statfs, frame, -1, op_errno, NULL, NULL);

        return 0;
}
