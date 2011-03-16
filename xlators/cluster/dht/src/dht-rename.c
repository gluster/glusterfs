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

/* TODO: link(oldpath, newpath) fails if newpath already exists. DHT should
 *       delete the newpath if it gets EEXISTS from link() call.
 */
#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "xlator.h"
#include "dht-common.h"
#include "defaults.h"


int
dht_rename_dir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, struct iatt *stbuf,
                    struct iatt *preoldparent, struct iatt *postoldparent,
                    struct iatt *prenewparent, struct iatt *postnewparent)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = 0;
        call_frame_t *prev = NULL;


        local = frame->local;
        prev = cookie;

        if (op_ret == -1) {
                /* TODO: undo the damage */

                gf_log (this->name, GF_LOG_DEBUG,
                        "rename %s -> %s on %s failed (%s)",
                        local->loc.path, local->loc2.path,
                        prev->this->name, strerror (op_errno));

                local->op_ret   = op_ret;
                local->op_errno = op_errno;
        } else {
                /* TODO: construct proper stbuf for dir */
                /*
                 * FIXME: is this the correct way to build stbuf and
                 * parent bufs?
                 */
                dht_iatt_merge (this, &local->stbuf, stbuf, prev->this);
                dht_iatt_merge (this, &local->preoldparent, preoldparent,
                                prev->this);
                dht_iatt_merge (this, &local->postoldparent, postoldparent,
                                prev->this);
                dht_iatt_merge (this, &local->preparent, prenewparent,
                                prev->this);
                dht_iatt_merge (this, &local->postparent, postnewparent,
                                prev->this);
        }

        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt)) {
                local->stbuf.ia_ino = local->loc.inode->ino;

                local->preoldparent.ia_ino = local->loc.parent->ino;
                local->postoldparent.ia_ino = local->loc.parent->ino;

                local->preparent.ia_ino = local->loc2.parent->ino;
                local->postparent.ia_ino = local->loc2.parent->ino;

                WIPE (&local->preoldparent);
                WIPE (&local->postoldparent);
                WIPE (&local->preparent);
                WIPE (&local->postparent);

                DHT_STACK_UNWIND (rename, frame, local->op_ret, local->op_errno,
                                  &local->stbuf, &local->preoldparent,
                                  &local->postoldparent,
                                  &local->preparent, &local->postparent);
        }

        return 0;
}



int
dht_rename_dir_do (call_frame_t *frame, xlator_t *this)
{
        dht_local_t  *local = NULL;
        dht_conf_t   *conf = NULL;
        int           i = 0;

        conf = this->private;
        local = frame->local;

        if (local->op_ret == -1)
                goto err;

        local->call_cnt = conf->subvolume_cnt;
        local->op_ret = 0;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                STACK_WIND (frame, dht_rename_dir_cbk,
                            conf->subvolumes[i],
                            conf->subvolumes[i]->fops->rename,
                            &local->loc, &local->loc2);
        }

        return 0;

err:
        DHT_STACK_UNWIND (rename, frame, local->op_ret, local->op_errno, NULL, NULL,
                          NULL, NULL, NULL);
        return 0;
}


int
dht_rename_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, gf_dirent_t *entries)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = -1;
        call_frame_t *prev = NULL;

        local = frame->local;
        prev  = cookie;

        if (op_ret > 2) {
                gf_log (this->name, GF_LOG_TRACE,
                        "readdir on %s for %s returned %d entries",
                        prev->this->name, local->loc.path, op_ret);
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
                        int op_ret, int op_errno, fd_t *fd)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = -1;
        call_frame_t *prev = NULL;


        local = frame->local;
        prev  = cookie;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "opendir on %s for %s failed (%s)",
                        prev->this->name, local->loc.path,
                        strerror (op_errno));
                goto err;
        }

        STACK_WIND (frame, dht_rename_readdir_cbk,
                    prev->this, prev->this->fops->readdir,
                    local->fd, 4096, 0);

        return 0;

err:
        this_call_cnt = dht_frame_return (frame);

        if (is_last_call (this_call_cnt)) {
                dht_rename_dir_do (frame, this);
        }

        return 0;
}


int
dht_rename_dir (call_frame_t *frame, xlator_t *this)
{
        dht_conf_t  *conf = NULL;
        dht_local_t *local = NULL;
        int          i = 0;
        int          op_errno = -1;


        conf = frame->this->private;
        local = frame->local;

        local->call_cnt = conf->subvolume_cnt;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (!conf->subvolume_status[i]) {
                        op_errno = ENOTCONN;
                        goto err;
                }
        }

        local->fd = fd_create (local->loc.inode, frame->root->pid);
        if (!local->fd) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                op_errno = ENOMEM;
                goto err;
        }

        local->op_ret = 0;

        if (!local->dst_cached) {
                dht_rename_dir_do (frame, this);
                return 0;
        }

        for (i = 0; i < conf->subvolume_cnt; i++) {
                STACK_WIND (frame, dht_rename_opendir_cbk,
                            conf->subvolumes[i],
                            conf->subvolumes[i]->fops->opendir,
                            &local->loc2, local->fd);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (rename, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);
        return 0;
}


int
dht_rename_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                       struct iatt *postparent)
{
        dht_local_t  *local = NULL;
        call_frame_t *prev = NULL;
        int           this_call_cnt = 0;

        local = frame->local;
        prev  = cookie;

        if (!local) {
                gf_log (this->name, GF_LOG_ERROR,
                        "!local, should not happen");
                goto out;
        }

        this_call_cnt = dht_frame_return (frame);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "unlink on %s failed (%s)",
                        prev->this->name, strerror (op_errno));
        }

        WIPE (&local->preoldparent);
        WIPE (&local->postoldparent);
        WIPE (&local->preparent);
        WIPE (&local->postparent);

        if (is_last_call (this_call_cnt)) {
                DHT_STACK_UNWIND (rename, frame, local->op_ret, local->op_errno,
                                  &local->stbuf, &local->preoldparent,
                                  &local->postoldparent, &local->preparent,
                                  &local->postparent);
        }

out:
        return 0;
}


int
dht_rename_cleanup (call_frame_t *frame)
{
        dht_local_t *local = NULL;
        xlator_t    *this = NULL;
        xlator_t    *src_hashed = NULL;
        xlator_t    *src_cached = NULL;
        xlator_t    *dst_hashed = NULL;
        xlator_t    *dst_cached = NULL;
        int          call_cnt = 0;


        local = frame->local;
        this  = frame->this;

        src_hashed = local->src_hashed;
        src_cached = local->src_cached;
        dst_hashed = local->dst_hashed;
        dst_cached = local->dst_cached;

        if (src_cached == dst_cached)
                goto nolinks;

        if (dst_hashed != src_hashed && dst_hashed != src_cached)
                call_cnt++;

        if (src_cached != dst_hashed)
                call_cnt++;

        local->call_cnt = call_cnt;

        if (!call_cnt)
                goto nolinks;

        if (dst_hashed != src_hashed && dst_hashed != src_cached) {
                gf_log (this->name, GF_LOG_TRACE,
                        "unlinking linkfile %s @ %s => %s",
                        local->loc.path, dst_hashed->name, src_cached->name);
                STACK_WIND (frame, dht_rename_unlink_cbk,
                            dst_hashed, dst_hashed->fops->unlink,
                            &local->loc);
        }

        if (src_cached != dst_hashed) {
                gf_log (this->name, GF_LOG_TRACE,
                        "unlinking link %s => %s (%s)", local->loc.path,
                        local->loc2.path, src_cached->name);
                STACK_WIND (frame, dht_rename_unlink_cbk,
                            src_cached, src_cached->fops->unlink,
                            &local->loc2);
        }

        return 0;

nolinks:
        WIPE (&local->preoldparent);
        WIPE (&local->postoldparent);
        WIPE (&local->preparent);
        WIPE (&local->postparent);

        DHT_STACK_UNWIND (rename, frame, local->op_ret, local->op_errno,
                          &local->stbuf, &local->preoldparent,
                          &local->postoldparent, &local->preparent,
                          &local->postparent);

        return 0;
}


int
dht_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                int32_t op_ret, int32_t op_errno, struct iatt *stbuf,
                struct iatt *preoldparent, struct iatt *postoldparent,
                struct iatt *prenewparent, struct iatt *postnewparent)
{
        dht_local_t  *local = NULL;
        call_frame_t *prev = NULL;
        xlator_t     *src_hashed = NULL;
        xlator_t     *src_cached = NULL;
        xlator_t     *dst_hashed = NULL;
        xlator_t     *dst_cached = NULL;
        xlator_t     *rename_subvol = NULL;

        local = frame->local;
        prev = cookie;

        src_hashed = local->src_hashed;
        src_cached = local->src_cached;
        dst_hashed = local->dst_hashed;
        dst_cached = local->dst_cached;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "rename on %s failed (%s)", prev->this->name,
                        strerror (op_errno));
                local->op_ret   = op_ret;
                local->op_errno = op_errno;
                goto cleanup;
        }

        dht_iatt_merge (this, &local->stbuf, stbuf, prev->this);
        dht_iatt_merge (this, &local->preoldparent, preoldparent, prev->this);
        dht_iatt_merge (this, &local->postoldparent, postoldparent, prev->this);
        dht_iatt_merge (this, &local->preparent, prenewparent, prev->this);
        dht_iatt_merge (this, &local->postparent, postnewparent, prev->this);

        local->stbuf.ia_ino = local->loc.inode->ino;

        local->preoldparent.ia_ino = local->loc.parent->ino;
        local->postoldparent.ia_ino = local->loc.parent->ino;

        local->preparent.ia_ino = local->loc2.parent->ino;
        local->postparent.ia_ino = local->loc2.parent->ino;

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

        if (src_cached != dst_hashed && src_cached != dst_cached) {
                gf_log (this->name, GF_LOG_TRACE,
                        "deleting old src datafile %s @ %s",
                        local->loc.path, src_cached->name);

                STACK_WIND (frame, dht_rename_unlink_cbk,
                            src_cached, src_cached->fops->unlink,
                            &local->loc);
        }

        if (src_hashed != rename_subvol && src_hashed != src_cached) {
                gf_log (this->name, GF_LOG_TRACE,
                        "deleting old src linkfile %s @ %s",
                        local->loc.path, src_hashed->name);

                STACK_WIND (frame, dht_rename_unlink_cbk,
                            src_hashed, src_hashed->fops->unlink,
                            &local->loc);
        }

        if (dst_cached
            && (dst_cached != dst_hashed)
            && (dst_cached != src_cached)) {
                gf_log (this->name, GF_LOG_TRACE,
                        "deleting old dst datafile %s @ %s",
                        local->loc2.path, dst_cached->name);

                STACK_WIND (frame, dht_rename_unlink_cbk,
                            dst_cached, dst_cached->fops->unlink,
                            &local->loc2);
        }
        return 0;

unwind:
        WIPE (&local->preoldparent);
        WIPE (&local->postoldparent);
        WIPE (&local->preparent);
        WIPE (&local->postparent);

        DHT_STACK_UNWIND (rename, frame, local->op_ret, local->op_errno,
                          &local->stbuf, &local->preoldparent,
                          &local->postoldparent, &local->preparent,
                          &local->postparent);

        return 0;

cleanup:
        dht_rename_cleanup (frame);

        return 0;
}


int
dht_do_rename (call_frame_t *frame)
{
        dht_local_t *local = NULL;
        xlator_t    *dst_hashed = NULL;
        xlator_t    *src_cached = NULL;
        xlator_t    *dst_cached = NULL;
        xlator_t    *this = NULL;
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

        gf_log (this->name, GF_LOG_TRACE,
                "renaming %s => %s (%s)",
                local->loc.path, local->loc2.path, rename_subvol->name);

        STACK_WIND (frame, dht_rename_cbk,
                    rename_subvol, rename_subvol->fops->rename,
                    &local->loc, &local->loc2);

        return 0;
}


int
dht_rename_links_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      inode_t *inode, struct iatt *stbuf,
                      struct iatt *preparent, struct iatt *postparent)
{
        dht_local_t  *local = NULL;
        call_frame_t *prev = NULL;
        int           this_call_cnt  = 0;


        local = frame->local;
        prev = cookie;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "link/file on %s failed (%s)",
                        prev->this->name, strerror (op_errno));
                local->op_ret   = -1;
                local->op_errno = op_errno;
        }

        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt)) {
                if (local->op_ret == -1)
                        goto cleanup;

                dht_do_rename (frame);
        }

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


        local = frame->local;
        this  = frame->this;

        src_hashed = local->src_hashed;
        src_cached = local->src_cached;
        dst_hashed = local->dst_hashed;
        dst_cached = local->dst_cached;

        if (src_cached == dst_cached)
                goto nolinks;

        if (dst_hashed != src_hashed && dst_hashed != src_cached)
                call_cnt++;

        if (src_cached != dst_hashed)
                call_cnt++;

        local->call_cnt = call_cnt;

        if (dst_hashed != src_hashed && dst_hashed != src_cached) {
                gf_log (this->name, GF_LOG_TRACE,
                        "linkfile %s @ %s => %s",
                        local->loc.path, dst_hashed->name, src_cached->name);
                memcpy (local->gfid, local->loc.inode->gfid, 16);
                dht_linkfile_create (frame, dht_rename_links_cbk,
                                     src_cached, dst_hashed, &local->loc);
        }

        if (src_cached != dst_hashed) {
                gf_log (this->name, GF_LOG_TRACE,
                        "link %s => %s (%s)", local->loc.path,
                        local->loc2.path, src_cached->name);
                STACK_WIND (frame, dht_rename_links_cbk,
                            src_cached, src_cached->fops->link,
                            &local->loc, &local->loc2);
        }

nolinks:
        if (!call_cnt) {
                /* skip to next step */
                dht_do_rename (frame);
        }

        return 0;
}


int
dht_rename (call_frame_t *frame, xlator_t *this,
            loc_t *oldloc, loc_t *newloc)
{
        xlator_t    *src_cached = NULL;
        xlator_t    *src_hashed = NULL;
        xlator_t    *dst_cached = NULL;
        xlator_t    *dst_hashed = NULL;
        int          op_errno = -1;
        int          ret = -1;
        dht_local_t *local = NULL;


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (oldloc, err);
        VALIDATE_OR_GOTO (newloc, err);

        src_hashed = dht_subvol_get_hashed (this, oldloc);
        if (!src_hashed) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no subvolume in layout for path=%s",
                        oldloc->path);
                op_errno = EINVAL;
                goto err;
        }

        src_cached = dht_subvol_get_cached (this, oldloc->inode);
        if (!src_cached) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no cached subvolume for path=%s", oldloc->path);
                op_errno = EINVAL;
                goto err;
        }

        dst_hashed = dht_subvol_get_hashed (this, newloc);
        if (!dst_hashed) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no subvolume in layout for path=%s",
                        newloc->path);
                op_errno = EINVAL;
                goto err;
        }

        if (newloc->inode)
                dst_cached = dht_subvol_get_cached (this, newloc->inode);

        local = dht_local_init (frame);
        if (!local) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                goto err;
        }

        ret = loc_copy (&local->loc, oldloc);
        if (ret == -1) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                goto err;
        }

        ret = loc_copy (&local->loc2, newloc);
        if (ret == -1) {
                op_errno = ENOMEM;
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                goto err;
        }

        local->src_hashed = src_hashed;
        local->src_cached = src_cached;
        local->dst_hashed = dst_hashed;
        local->dst_cached = dst_cached;

        gf_log (this->name, GF_LOG_TRACE,
                "renaming %s (hash=%s/cache=%s) => %s (hash=%s/cache=%s)",
                oldloc->path, src_hashed->name, src_cached->name,
                newloc->path, dst_hashed->name,
                dst_cached ? dst_cached->name : "<nul>");

        if (IA_ISDIR (oldloc->inode->ia_type)) {
                dht_rename_dir (frame, this);
        } else {
                local->op_ret = 0;
                dht_rename_create_links (frame);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (rename, frame, -1, op_errno, NULL, NULL, NULL, NULL, NULL);

        return 0;
}
