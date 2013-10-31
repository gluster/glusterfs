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
                    struct iatt *prenewparent, struct iatt *postnewparent,
                    dict_t *xdata)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = 0;
        call_frame_t *prev = NULL;

        local = frame->local;
        prev = cookie;

        if (op_ret == -1) {
                /* TODO: undo the damage */

                gf_log (this->name, GF_LOG_INFO,
                        "rename %s -> %s on %s failed (%s)",
                        local->loc.path, local->loc2.path,
                        prev->this->name, strerror (op_errno));

                local->op_ret   = op_ret;
                local->op_errno = op_errno;
                goto unwind;
        }
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


unwind:
        this_call_cnt = dht_frame_return (frame);
        if (is_last_call (this_call_cnt)) {
                WIPE (&local->preoldparent);
                WIPE (&local->postoldparent);
                WIPE (&local->preparent);
                WIPE (&local->postparent);

                DHT_STRIP_PHASE1_FLAGS (&local->stbuf);
                DHT_STACK_UNWIND (rename, frame, local->op_ret, local->op_errno,
                                  &local->stbuf, &local->preoldparent,
                                  &local->postoldparent,
                                  &local->preparent, &local->postparent, xdata);
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
        call_frame_t *prev = NULL;
        int           i = 0;

        conf = this->private;
        local = frame->local;
        prev = cookie;

        if (op_ret == -1) {
                /* TODO: undo the damage */

                gf_log (this->name, GF_LOG_INFO,
                        "rename %s -> %s on %s failed (%s)",
                        local->loc.path, local->loc2.path,
                        prev->this->name, strerror (op_errno));

                local->op_ret   = op_ret;
                local->op_errno = op_errno;
                goto unwind;
        }
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

        call_cnt = local->call_cnt = conf->subvolume_cnt - 1;

        if (!local->call_cnt)
                goto unwind;

        for (i = 0; i < conf->subvolume_cnt; i++) {
                if (conf->subvolumes[i] == local->dst_hashed)
                        continue;
                STACK_WIND (frame, dht_rename_dir_cbk,
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

        DHT_STRIP_PHASE1_FLAGS (&local->stbuf);
        DHT_STACK_UNWIND (rename, frame, local->op_ret, local->op_errno,
                          &local->stbuf, &local->preoldparent,
                          &local->postoldparent,
                          &local->preparent, &local->postparent, NULL);

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

        STACK_WIND (frame, dht_rename_hashed_dir_cbk,
                    local->dst_hashed,
                    local->dst_hashed->fops->rename,
                    &local->loc, &local->loc2, NULL);
        return 0;

err:
        DHT_STACK_UNWIND (rename, frame, local->op_ret, local->op_errno, NULL, NULL,
                          NULL, NULL, NULL, NULL);
        return 0;
}


int
dht_rename_readdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int op_ret, int op_errno, gf_dirent_t *entries,
                        dict_t *xdata)
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
                        int op_ret, int op_errno, fd_t *fd, dict_t *xdata)
{
        dht_local_t  *local = NULL;
        int           this_call_cnt = -1;
        call_frame_t *prev = NULL;


        local = frame->local;
        prev  = cookie;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_INFO,
                        "opendir on %s for %s failed (%s)",
                        prev->this->name, local->loc.path,
                        strerror (op_errno));
                goto err;
        }

        STACK_WIND (frame, dht_rename_readdir_cbk,
                    prev->this, prev->this->fops->readdir,
                    local->fd, 4096, 0, NULL);

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
                        gf_log (this->name, GF_LOG_INFO,
                                "one of the subvolumes down (%s)",
                                conf->subvolumes[i]->name);
                        op_errno = ENOTCONN;
                        goto err;
                }
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
                STACK_WIND (frame, dht_rename_opendir_cbk,
                            conf->subvolumes[i],
                            conf->subvolumes[i]->fops->opendir,
                            &local->loc2, local->fd, NULL);
        }

        return 0;

err:
        op_errno = (op_errno == -1) ? errno : op_errno;
        DHT_STACK_UNWIND (rename, frame, -1, op_errno, NULL, NULL, NULL, NULL,
                          NULL, NULL);
        return 0;
}
#define DHT_MARK_FOP_INTERNAL(xattr) do {                                      \
                int tmp = -1;                                                  \
                if (!xattr) {                                                  \
                        xattr = dict_new ();                                   \
                        if (!xattr)                                            \
                                break;                                         \
                }                                                              \
                tmp = dict_set_str (xattr, GLUSTERFS_INTERNAL_FOP_KEY, "yes"); \
                if (tmp) {                                                     \
                        gf_log (this->name, GF_LOG_ERROR, "Failed to set"      \
                                " internal dict key for %s", local->loc.path); \
                }                                                              \
        }while (0)

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
                        gf_log (this->name, GF_LOG_ERROR, "Failed to set"      \
                                " marker dont account key for %s", local->loc.path); \
                }                                                              \
        }while (0)

int
dht_rename_done (call_frame_t *frame, xlator_t *this)
{
        dht_local_t     *local = NULL;

        local = frame->local;

        if (local->linked == _gf_true) {
                local->linked = _gf_false;
                dht_linkfile_attr_heal (frame, this);
        }
        DHT_STRIP_PHASE1_FLAGS (&local->stbuf);
        DHT_STACK_UNWIND (rename, frame, local->op_ret, local->op_errno,
                          &local->stbuf, &local->preoldparent,
                          &local->postoldparent, &local->preparent,
                          &local->postparent, NULL);

        return 0;
}

int
dht_rename_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                       struct iatt *postparent, dict_t *xdata)
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
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: unlink on %s failed (%s)",
                        local->loc.path, prev->this->name, strerror (op_errno));
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
        dht_local_t *local = NULL;
        xlator_t    *this = NULL;
        xlator_t    *src_hashed = NULL;
        xlator_t    *src_cached = NULL;
        xlator_t    *dst_hashed = NULL;
        xlator_t    *dst_cached = NULL;
        int          call_cnt = 0;
        dict_t      *xattr      = NULL;

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

        DHT_MARK_FOP_INTERNAL (xattr);

        if (dst_hashed != src_hashed && dst_hashed != src_cached) {
                dict_t *xattr_new = NULL;

                gf_log (this->name, GF_LOG_TRACE,
                        "unlinking linkfile %s @ %s => %s",
                        local->loc.path, dst_hashed->name, src_cached->name);

                xattr_new = dict_copy_with_ref (xattr, NULL);


                DHT_MARKER_DONT_ACCOUNT(xattr_new);

                STACK_WIND (frame, dht_rename_unlink_cbk,
                            dst_hashed, dst_hashed->fops->unlink,
                            &local->loc, 0, xattr_new);

                dict_unref (xattr_new);
                xattr_new = NULL;
        }

        if (src_cached != dst_hashed) {
                dict_t *xattr_new = NULL;

                gf_log (this->name, GF_LOG_TRACE,
                        "unlinking link %s => %s (%s)", local->loc.path,
                        local->loc2.path, src_cached->name);

                xattr_new = dict_copy_with_ref (xattr, NULL);

                if (uuid_compare (local->loc.pargfid,
                                  local->loc2.pargfid) == 0) {
                        DHT_MARKER_DONT_ACCOUNT(xattr_new);
                }

                STACK_WIND (frame, dht_rename_unlink_cbk,
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

        DHT_STRIP_PHASE1_FLAGS (&local->stbuf);
        DHT_STACK_UNWIND (rename, frame, local->op_ret, local->op_errno,
                          &local->stbuf, &local->preoldparent,
                          &local->postoldparent, &local->preparent,
                          &local->postparent, NULL);

        return 0;
}


int
dht_rename_links_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno,
                             inode_t *inode, struct iatt *stbuf,
                             struct iatt *preparent, struct iatt *postparent,
                             dict_t *xdata)
{
        call_frame_t *prev = NULL;
        dht_local_t  *local = NULL;

        prev = cookie;
        local = frame->local;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "link/file %s on %s failed (%s)",
                        local->loc.path, prev->this->name, strerror (op_errno));
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
        call_frame_t *prev = NULL;
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
        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: rename on %s failed (%s)", local->loc.path,
                        prev->this->name, strerror (op_errno));
                local->op_ret   = op_ret;
                local->op_errno = op_errno;
                goto cleanup;
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
                uuid_copy (link_local->gfid, local->loc.inode->gfid);

                dht_linkfile_create (link_frame, dht_rename_links_create_cbk,
                                     this, src_cached, dst_hashed,
                                     &link_local->loc);
        }

err:
        /* Merge attrs only from src_cached. In case there of src_cached !=
         * dst_hashed, this ignores linkfile attrs. */
        if (prev->this == src_cached) {
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

                gf_log (this->name, GF_LOG_TRACE,
                        "deleting old src datafile %s @ %s",
                        local->loc.path, src_cached->name);

                if (uuid_compare (local->loc.pargfid,
                                  local->loc2.pargfid) == 0) {
                        DHT_MARKER_DONT_ACCOUNT(xattr_new);
                }

                STACK_WIND (frame, dht_rename_unlink_cbk,
                            src_cached, src_cached->fops->unlink,
                            &local->loc, 0, xattr_new);

                dict_unref (xattr_new);
                xattr_new = NULL;
        }

        if (src_hashed != rename_subvol && src_hashed != src_cached) {
                dict_t *xattr_new = NULL;

                xattr_new = dict_copy_with_ref (xattr, NULL);

                gf_log (this->name, GF_LOG_TRACE,
                        "deleting old src linkfile %s @ %s",
                        local->loc.path, src_hashed->name);

                DHT_MARKER_DONT_ACCOUNT(xattr_new);

                STACK_WIND (frame, dht_rename_unlink_cbk,
                            src_hashed, src_hashed->fops->unlink,
                            &local->loc, 0, xattr_new);

                dict_unref (xattr_new);
                xattr_new = NULL;
        }

        if (dst_cached
            && (dst_cached != dst_hashed)
            && (dst_cached != src_cached)) {
                gf_log (this->name, GF_LOG_TRACE,
                        "deleting old dst datafile %s @ %s",
                        local->loc2.path, dst_cached->name);

                STACK_WIND (frame, dht_rename_unlink_cbk,
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
        if (xattr)
                dict_unref (xattr);

        dht_rename_done (frame, this);

        return 0;

cleanup:
        if (xattr)
                dict_unref (xattr);
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
        dict_t      *dict          = NULL;


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
                DHT_MARKER_DONT_ACCOUNT(dict);
        }

        gf_log (this->name, GF_LOG_TRACE,
                "renaming %s => %s (%s)",
                local->loc.path, local->loc2.path, rename_subvol->name);

        if (local->linked == _gf_true)
                FRAME_SU_DO (frame, dht_local_t);
        STACK_WIND (frame, dht_rename_cbk,
                    rename_subvol, rename_subvol->fops->rename,
                    &local->loc, &local->loc2, dict);

        return 0;
}


int
dht_rename_links_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      inode_t *inode, struct iatt *stbuf,
                      struct iatt *preparent, struct iatt *postparent,
                      dict_t *xdata)
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
                if (op_errno != ENOENT)
                        local->op_errno = op_errno;
        } else if (local->src_cached == prev->this) {
                /* merge of attr returned only from linkfile creation */
                dht_iatt_merge (this, &local->stbuf, stbuf, prev->this);
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
dht_rename_unlink_links_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno,
                             struct iatt *preparent, struct iatt *postparent,
                             dict_t *xdata)
{
	dht_local_t  *local = NULL;
	call_frame_t *prev = NULL;


	local = frame->local;
	prev = cookie;

	if ((op_ret == -1) && (op_errno != ENOENT)) {
		gf_log (this->name, GF_LOG_DEBUG,
			"unlink of %s on %s failed (%s)",
			local->loc2.path, prev->this->name,
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

		gf_log (this->name, GF_LOG_TRACE,
			"unlinking dst linkfile %s @ %s",
			local->loc2.path, dst_hashed->name);

                DHT_MARKER_DONT_ACCOUNT(xattr_new);

		STACK_WIND (frame, dht_rename_unlink_links_cbk,
			    dst_hashed, dst_hashed->fops->unlink,
			    &local->loc2, 0, xattr_new);

                dict_unref (xattr_new);
                return 0;
        }

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
		dht_linkfile_create (frame, dht_rename_links_cbk, this,
				     src_cached, dst_hashed, &local->loc);
	}

	if (src_cached != dst_hashed) {
                dict_t *xattr_new = NULL;

                xattr_new = dict_copy_with_ref (xattr, NULL);

		gf_log (this->name, GF_LOG_TRACE,
			"link %s => %s (%s)", local->loc.path,
			local->loc2.path, src_cached->name);
                if (uuid_compare (local->loc.pargfid,
                                  local->loc2.pargfid) == 0) {
                        DHT_MARKER_DONT_ACCOUNT(xattr_new);
                }

		STACK_WIND (frame, dht_rename_links_cbk,
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


        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);
        VALIDATE_OR_GOTO (oldloc, err);
        VALIDATE_OR_GOTO (newloc, err);

        src_hashed = dht_subvol_get_hashed (this, oldloc);
        if (!src_hashed) {
                gf_log (this->name, GF_LOG_INFO,
                        "no subvolume in layout for path=%s",
                        oldloc->path);
                op_errno = EINVAL;
                goto err;
        }

        src_cached = dht_subvol_get_cached (this, oldloc->inode);
        if (!src_cached) {
                gf_log (this->name, GF_LOG_INFO,
                        "no cached subvolume for path=%s", oldloc->path);
                op_errno = EINVAL;
                goto err;
        }

        dst_hashed = dht_subvol_get_hashed (this, newloc);
        if (!dst_hashed) {
                gf_log (this->name, GF_LOG_INFO,
                        "no subvolume in layout for path=%s",
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
        DHT_STACK_UNWIND (rename, frame, -1, op_errno, NULL, NULL, NULL, NULL,
                          NULL, NULL);

        return 0;
}
