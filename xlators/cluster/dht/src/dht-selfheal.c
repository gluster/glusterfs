/*
  Copyright (c) 2008-2010 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif


#include "glusterfs.h"
#include "xlator.h"
#include "dht-common.h"


int
dht_selfheal_dir_finish (call_frame_t *frame, xlator_t *this, int ret)
{
        dht_local_t  *local = NULL;

        local = frame->local;
        local->selfheal.dir_cbk (frame, NULL, frame->this, ret,
                                 local->op_errno);

        return 0;
}


int
dht_selfheal_dir_xattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int op_ret, int op_errno)
{
        dht_local_t  *local = NULL;
        call_frame_t *prev = NULL;
        xlator_t     *subvol = NULL;
        int           i = 0;
        dht_layout_t *layout = NULL;
        int           err = 0;
        int           this_call_cnt = 0;

        local = frame->local;
        layout = local->selfheal.layout;
        prev = cookie;
        subvol = prev->this;

        if (op_ret == 0)
                err = 0;
        else
                err = op_errno;

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].xlator == subvol) {
                        layout->list[i].err = err;
                        break;
                }
        }

        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
                dht_selfheal_dir_finish (frame, this, 0);
        }

        return 0;
}


int
dht_selfheal_dir_xattr_persubvol (call_frame_t *frame, loc_t *loc,
                                  dht_layout_t *layout, int i)
{
        xlator_t          *subvol = NULL;
        dict_t            *xattr = NULL;
        int                ret = 0;
        xlator_t          *this = NULL;
        int32_t           *disk_layout = NULL;


        subvol = layout->list[i].xlator;
        this = frame->this;

        xattr = get_new_dict ();
        if (!xattr) {
                goto err;
        }

        ret = dht_disk_layout_extract (this, layout, i, &disk_layout);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: (subvol %s) failed to extract disk layout",
                        loc->path, subvol->name);
                goto err;
        }

        ret = dict_set_bin (xattr, "trusted.glusterfs.dht",
                            disk_layout, 4 * 4);
        if (ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: (subvol %s) failed to set xattr dictionary",
                        loc->path, subvol->name);
                goto err;
        }
        disk_layout = NULL;

        gf_log (this->name, GF_LOG_TRACE,
                "setting hash range %u - %u (type %d) on subvolume %s for %s",
                layout->list[i].start, layout->list[i].stop,
                layout->type, subvol->name, loc->path);

        dict_ref (xattr);

        STACK_WIND (frame, dht_selfheal_dir_xattr_cbk,
                    subvol, subvol->fops->setxattr,
                    loc, xattr, 0);

        dict_unref (xattr);

        return 0;

err:
        if (xattr)
                dict_destroy (xattr);

        if (disk_layout)
                GF_FREE (disk_layout);

        dht_selfheal_dir_xattr_cbk (frame, subvol, frame->this,
                                    -1, ENOMEM);
        return 0;
}


int
dht_selfheal_dir_xattr (call_frame_t *frame, loc_t *loc, dht_layout_t *layout)
{
        dht_local_t *local = NULL;
        int          missing_xattr = 0;
        int          i = 0;
        xlator_t    *this = NULL;

        local = frame->local;
        this = frame->this;

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err != -1 || !layout->list[i].stop) {
                        /* err != -1 would mean xattr present on the directory
                         * or the directory is itself non existant.
                         * !layout->list[i].stop would mean layout absent
                         */

                        continue;
                }
                missing_xattr++;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "%d subvolumes missing xattr for %s",
                missing_xattr, loc->path);

        if (missing_xattr == 0) {
                dht_selfheal_dir_finish (frame, this, 0);
                return 0;
        }

        local->call_cnt = missing_xattr;

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err != -1 || !layout->list[i].stop)
                        continue;

                dht_selfheal_dir_xattr_persubvol (frame, loc, layout, i);

                if (--missing_xattr == 0)
                        break;
        }
        return 0;
}

int
dht_selfheal_dir_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                              int op_ret, int op_errno, struct iatt *statpre,
                              struct iatt *statpost)
{
        dht_local_t   *local = NULL;
        dht_layout_t  *layout = NULL;
        int            this_call_cnt = 0;

        local  = frame->local;
        layout = local->selfheal.layout;

        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
                dht_selfheal_dir_xattr (frame, &local->loc, layout);
        }

        return 0;
}


int
dht_selfheal_dir_setattr (call_frame_t *frame, loc_t *loc, struct iatt *stbuf,
                          int32_t valid, dht_layout_t *layout)
{
        int           missing_attr = 0;
        int           i     = 0;
        dht_local_t  *local = NULL;
        xlator_t     *this = NULL;

        local = frame->local;
        this = frame->this;

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err == -1)
                        missing_attr++;
        }

        if (missing_attr == 0) {
                dht_selfheal_dir_xattr (frame, loc, layout);
                return 0;
        }

        local->call_cnt = missing_attr;
        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err == -1) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "setattr for %s on subvol %s",
                                loc->path, layout->list[i].xlator->name);

                        STACK_WIND (frame, dht_selfheal_dir_setattr_cbk,
                                    layout->list[i].xlator,
                                    layout->list[i].xlator->fops->setattr,
                                    loc, stbuf, valid);
                }
        }

        return 0;
}

int
dht_selfheal_dir_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int op_ret, int op_errno,
                            inode_t *inode, struct iatt *stbuf,
                            struct iatt *preparent, struct iatt *postparent)
{
        dht_local_t   *local = NULL;
        dht_layout_t  *layout = NULL;
        call_frame_t  *prev = NULL;
        xlator_t      *subvol = NULL;
        int            i = 0;
        int            this_call_cnt = 0;


        local  = frame->local;
        layout = local->selfheal.layout;
        prev   = cookie;
        subvol = prev->this;

        if ((op_ret == 0) || ((op_ret == -1) && (op_errno == EEXIST))) {
                for (i = 0; i < layout->cnt; i++) {
                        if (layout->list[i].xlator == subvol) {
                                layout->list[i].err = -1;
                                break;
                        }
                }
        }

        if (op_ret) {
                gf_log (this->name, ((op_errno == EEXIST) ? GF_LOG_DEBUG :
                                     GF_LOG_WARNING),
                        "selfhealing directory %s failed: %s",
                        local->loc.path, strerror (op_errno));
                goto out;
        }

        dht_iatt_merge (this, &local->stbuf, stbuf, prev->this);
        if (prev->this == local->hashed_subvol)
                local->ia_ino = local->stbuf.ia_ino;

        dht_iatt_merge (this, &local->preparent, preparent, prev->this);
        dht_iatt_merge (this, &local->postparent, postparent, prev->this);

out:
        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
                dht_selfheal_dir_setattr (frame, &local->loc, &local->stbuf, 0xffffff, layout);
        }

        return 0;
}


int
dht_selfheal_dir_mkdir (call_frame_t *frame, loc_t *loc,
                        dht_layout_t *layout, int force)
{
        int           missing_dirs = 0;
        int           i     = 0;
        int           ret   = -1;
        dht_local_t  *local = NULL;
        xlator_t     *this = NULL;
        dict_t       *dict = NULL;

        local = frame->local;
        this = frame->this;

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err == ENOENT || force)
                        missing_dirs++;
        }

        if (missing_dirs == 0) {
                dht_selfheal_dir_setattr (frame, loc, &local->stbuf, 0xffffffff, layout);
                return 0;
        }

        local->call_cnt = missing_dirs;
        if (!uuid_is_null (local->gfid)) {
                dict = dict_new ();
                if (!dict)
                        return -1;

                ret = dict_set_static_bin (dict, "gfid-req", local->gfid, 16);
                if (ret)
                        gf_log (this->name, GF_LOG_INFO,
                                "%s: failed to set gfid in dict", loc->path);
        } else if (local->params) {
                /* Send the dictionary from higher layers directly */
                dict = dict_ref (local->params);
        }

        if (!dict)
                gf_log (this->name, GF_LOG_DEBUG,
                        "dict is NULL, need to make sure gfid's are same");

        for (i = 0; i < layout->cnt; i++) {
                if (layout->list[i].err == ENOENT || force) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "creating directory %s on subvol %s",
                                loc->path, layout->list[i].xlator->name);

                        STACK_WIND (frame, dht_selfheal_dir_mkdir_cbk,
                                    layout->list[i].xlator,
                                    layout->list[i].xlator->fops->mkdir,
                                    loc,
                                    st_mode_from_ia (local->stbuf.ia_prot,
                                                     local->stbuf.ia_type),
                                    dict);
                }
        }

        if (dict)
                dict_unref (dict);

        return 0;
}


int
dht_selfheal_layout_alloc_start (xlator_t *this, loc_t *loc,
                                 dht_layout_t *layout)
{
        int           start = 0;
        uint32_t      hashval = 0;
        int           ret = 0;

        ret = dht_hash_compute (layout->type, loc->path, &hashval);
        if (ret == 0) {
                start = (hashval % layout->cnt);
        }

        return start;
}


void
dht_selfheal_layout_new_directory (call_frame_t *frame, loc_t *loc,
                                   dht_layout_t *layout)
{
        xlator_t    *this = NULL;
        uint32_t     chunk = 0;
        int          i = 0;
        uint32_t     start = 0;
        int          cnt = 0;
        int          err = 0;
        int          start_subvol = 0;

        this = frame->this;

        for (i = 0; i < layout->cnt; i++) {
                err = layout->list[i].err;
                if (err == -1 || err == 0) {
                        layout->list[i].err = -1;
                        cnt++;
                }
        }

        /* no subvolume has enough space, but can't stop directory creation */
        if (!cnt) {
                for (i = 0; i < layout->cnt; i++) {
                        err = layout->list[i].err;
                        if (err == ENOSPC) {
                                layout->list[i].err = -1;
                                cnt++;
                        }
                }
        }

        chunk = ((unsigned long) 0xffffffff) / ((cnt) ? cnt : 1);

        start_subvol = dht_selfheal_layout_alloc_start (this, loc, layout);

        for (i = start_subvol; i < layout->cnt; i++) {
                err = layout->list[i].err;
                if (err == -1) {
                        layout->list[i].start = start;
                        layout->list[i].stop  = start + chunk - 1;

                        start = start + chunk;

                        gf_log (this->name, GF_LOG_TRACE,
                                "gave fix: %u - %u on %s for %s",
                                layout->list[i].start, layout->list[i].stop,
                                layout->list[i].xlator->name, loc->path);
                        if (--cnt == 0) {
                                layout->list[i].stop = 0xffffffff;
                                break;
                        }
                }
        }

        for (i = 0; i < start_subvol; i++) {
                err = layout->list[i].err;
                if (err == -1) {
                        layout->list[i].start = start;
                        layout->list[i].stop  = start + chunk - 1;

                        start = start + chunk;

                        gf_log (this->name, GF_LOG_TRACE,
                                "gave fix: %u - %u on %s for %s",
                                layout->list[i].start, layout->list[i].stop,
                                layout->list[i].xlator->name, loc->path);
                        if (--cnt == 0) {
                                layout->list[i].stop = 0xffffffff;
                                break;
                        }
                }
        }
}


int
dht_selfheal_dir_getafix (call_frame_t *frame, loc_t *loc,
                          dht_layout_t *layout)
{
        dht_conf_t  *conf = NULL;
        xlator_t    *this = NULL;
        dht_local_t *local = NULL;
        int          missing = -1;
        int          down = -1;
        int          holes = -1;
        int          ret = -1;
        int          i = -1;
        int          overlaps = -1;

        this = frame->this;
        conf = this->private;
        local = frame->local;

        missing = local->selfheal.missing;
        down = local->selfheal.down;
        holes = local->selfheal.hole_cnt;
        overlaps = local->selfheal.overlaps_cnt;

        if ((missing + down) == conf->subvolume_cnt) {
                dht_selfheal_layout_new_directory (frame, loc, layout);
                ret = 0;
        }

        if (holes <= down) {
                /* the down subvol might fill up the holes */
                ret = 0;
        }

        if (holes || overlaps) {
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
        dht_local_t *local = NULL;

        local = frame->local;

        local->selfheal.dir_cbk = dir_cbk;
        local->selfheal.layout = dht_layout_ref (frame->this, layout);

        dht_layout_sort_volname (layout);
        dht_selfheal_layout_new_directory (frame, &local->loc, layout);
        dht_selfheal_dir_xattr (frame, &local->loc, layout);
        return 0;
}


int
dht_selfheal_directory (call_frame_t *frame, dht_selfheal_dir_cbk_t dir_cbk,
                        loc_t *loc, dht_layout_t *layout)
{
        dht_local_t *local    = NULL;
        uint32_t     holes    = 0;
        uint32_t     down     = 0;
        uint32_t     misc     = 0;
        int          ret      = 0;
        xlator_t    *this     = NULL;

        local = frame->local;
        this = frame->this;

        dht_layout_anomalies (this, loc, layout,
                              &local->selfheal.hole_cnt,
                              &local->selfheal.overlaps_cnt,
                              &local->selfheal.missing,
                              &local->selfheal.down,
                              &local->selfheal.misc);

        holes    = local->selfheal.hole_cnt;
        down     = local->selfheal.down;
        misc     = local->selfheal.misc;

        local->selfheal.dir_cbk = dir_cbk;
        local->selfheal.layout = dht_layout_ref (this, layout);

        if (down) {
                gf_log (this->name, GF_LOG_INFO,
                        "%d subvolumes down -- not fixing", down);
                ret = 0;
                goto sorry_no_fix;
        }

        if (misc) {
                gf_log (this->name, GF_LOG_INFO,
                        "%d subvolumes have unrecoverable errors", misc);
                ret = 0;
                goto sorry_no_fix;
        }

        dht_layout_sort_volname (layout);
        ret = dht_selfheal_dir_getafix (frame, loc, layout);

        if (ret == -1) {
                gf_log (this->name, GF_LOG_INFO,
                        "not able to form layout for the directory");
                goto sorry_no_fix;
        }

        dht_selfheal_dir_mkdir (frame, loc, layout, 0);

        return 0;

sorry_no_fix:
        /* TODO: need to put appropriate local->op_errno */
        dht_selfheal_dir_finish (frame, this, ret);

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
