/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"
#include "list.h"
#include "call-stub.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat-errno.h"
#include "compat.h"
#include "checksum.h"

#include "afr.h"
#include "afr-self-heal.h"
#include "afr-self-heal-common.h"

int
afr_examine_dir_sh_unwind (call_frame_t *frame, xlator_t *this, int32_t op_ret,
                           int32_t op_errno, int32_t sh_failed)
{
        afr_local_t *local  = NULL;

        local = frame->local;

        afr_set_opendir_done (this, local->fd->inode);

        AFR_STACK_UNWIND (opendir, frame, local->op_ret,
                          local->op_errno, local->fd, NULL);

        return 0;
}


gf_boolean_t
__checksums_differ (uint32_t *checksum, int child_count,
                    unsigned char *child_up)
{
        int          ret            = _gf_false;
        int          i              = 0;
        uint32_t     cksum          = 0;
        gf_boolean_t activate_check = _gf_false;

        for (i = 0; i < child_count; i++) {
                if (!child_up[i])
                        continue;
                if (_gf_false == activate_check) {
                        cksum          = checksum[i];
                        activate_check = _gf_true;
                        continue;
                }

                if (cksum != checksum[i]) {
                        ret = _gf_true;
                        break;
                }

                cksum = checksum[i];
        }

        return ret;
}


int32_t
afr_examine_dir_readdir_cbk (call_frame_t *frame, void *cookie,
                             xlator_t *this, int32_t op_ret, int32_t op_errno,
                             gf_dirent_t *entries, dict_t *xdata)
{
        afr_private_t *   priv        = NULL;
        afr_local_t *     local       = NULL;
        afr_self_heal_t * sh          = NULL;
        gf_dirent_t *     entry       = NULL;
        gf_dirent_t *     tmp         = NULL;
        char              *reason     = NULL;
        int               child_index = 0;
        uint32_t          entry_cksum = 0;
        int               call_count  = 0;
        off_t             last_offset = 0;
        inode_t           *inode      = NULL;

        priv  = this->private;
        local = frame->local;
        sh    = &local->self_heal;
        inode = local->fd->inode;

        child_index = (long) cookie;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_INFO,
                        "%s: failed to do opendir on %s",
                        local->loc.path, priv->children[child_index]->name);
                local->op_ret = -1;
                local->op_ret = op_errno;
                goto out;
        }

        if (op_ret == 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "%s: no entries found in %s",
                        local->loc.path, priv->children[child_index]->name);
                goto out;
        }

        list_for_each_entry_safe (entry, tmp, &entries->list, list) {
                entry_cksum = gf_rsync_weak_checksum ((unsigned char *)entry->d_name,
                                                      strlen (entry->d_name));
                local->cont.opendir.checksum[child_index] ^= entry_cksum;
        }

        list_for_each_entry (entry, &entries->list, list) {
                last_offset = entry->d_off;
        }

        /* read more entries */

        STACK_WIND_COOKIE (frame, afr_examine_dir_readdir_cbk,
                           (void *) (long) child_index,
                           priv->children[child_index],
                           priv->children[child_index]->fops->readdir,
                           local->fd, 131072, last_offset, NULL);

        return 0;

out:
        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                if (__checksums_differ (local->cont.opendir.checksum,
                                        priv->child_count,
                                        local->child_up)) {

                        sh->do_entry_self_heal  = _gf_true;
                        sh->forced_merge          = _gf_true;

                        reason = "checksums of directory differ";
                        afr_launch_self_heal (frame, this, inode, _gf_false,
                                              inode->ia_type, reason, NULL,
                                              afr_examine_dir_sh_unwind);
                } else {
                        afr_set_opendir_done (this, inode);

                        AFR_STACK_UNWIND (opendir, frame, local->op_ret,
                                          local->op_errno, local->fd, NULL);
                }
        }

        return 0;
}


int
afr_examine_dir (call_frame_t *frame, xlator_t *this)
{
        afr_private_t * priv       = NULL;
        afr_local_t *   local      = NULL;
        int             i          = 0;
        int             call_count = 0;

        local = frame->local;
        priv  = this->private;

        local->cont.opendir.checksum = GF_CALLOC (priv->child_count,
                                                  sizeof (*local->cont.opendir.checksum),
                                                  gf_afr_mt_int32_t);

        call_count = afr_up_children_count (local->child_up, priv->child_count);

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_examine_dir_readdir_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->readdir,
                                           local->fd, 131072, 0, NULL);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int32_t
afr_opendir_cbk (call_frame_t *frame, void *cookie,
                 xlator_t *this, int32_t op_ret, int32_t op_errno,
                 fd_t *fd, dict_t *xdata)
{
        afr_private_t *priv              = NULL;
        afr_local_t   *local             = NULL;
        int32_t        up_children_count = 0;
        int            ret               = -1;
        int            call_count        = -1;
        int32_t        child_index       = 0;

        priv  = this->private;
        local = frame->local;
        child_index = (long) cookie;

        up_children_count = afr_up_children_count (local->child_up,
                                                   priv->child_count);

        LOCK (&frame->lock);
        {
                if (op_ret >= 0) {
                        local->op_ret = op_ret;
                        ret = afr_child_fd_ctx_set (this, fd, child_index, 0);
                        if (ret) {
                                local->op_ret = -1;
                                local->op_errno = -ret;
                                goto unlock;
                        }
                }

                local->op_errno = op_errno;
        }
unlock:
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                if (local->op_ret != 0)
                        goto out;

                if (!afr_is_opendir_done (this, local->fd->inode) &&
                    up_children_count > 1 && priv->entry_self_heal) {

                        /*
                         * This is the first opendir on this inode. We need
                         * to check if the directory's entries are the same
                         * on all subvolumes. This is needed in addition
                         * to regular entry self-heal because the readdir
                         * call is sent only to the first subvolume, and
                         * thus files that exist only there will never be healed
                         * otherwise (assuming changelog shows no anomalies).
                         */

                        gf_log (this->name, GF_LOG_TRACE,
                                "reading contents of directory %s looking for mismatch",
                                local->loc.path);

                        afr_examine_dir (frame, this);

                } else {
                        /* do the unwind */
                        goto out;
                }
        }

        return 0;

out:
        AFR_STACK_UNWIND (opendir, frame, local->op_ret,
                          local->op_errno, local->fd, NULL);

        return 0;
}


int32_t
afr_opendir (call_frame_t *frame, xlator_t *this,
             loc_t *loc, fd_t *fd)
{
        afr_private_t * priv        = NULL;
        afr_local_t   * local       = NULL;
        int             child_count = 0;
        int             i           = 0;
        int             ret         = -1;
        int             call_count  = -1;
        int32_t         op_errno    = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        child_count = priv->child_count;

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        loc_copy (&local->loc, loc);

        local->fd    = fd_ref (fd);

        call_count = local->call_count;

        for (i = 0; i < child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_opendir_cbk,
                                           (void*) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->opendir,
                                           loc, fd, NULL);

                        if (!--call_count)
                                break;
                }
        }

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (opendir, frame, -1, op_errno, fd, NULL);

        return 0;
}


/**
 * Common algorithm for directory read calls:
 *
 * - Try the fop on the first child that is up
 * - if we have failed due to ENOTCONN:
 *     try the next child
 *
 * Applicable to: readdir
 */


struct entry_name {
        char *name;
        struct list_head list;
};

static void
afr_forget_entries (fd_t *fd)
{
        struct entry_name *entry  = NULL;
        struct entry_name *tmp    = NULL;
        int                ret    = 0;
        uint64_t           ctx    = 0;
        afr_fd_ctx_t      *fd_ctx = NULL;

        ret = fd_ctx_get (fd, THIS, &ctx);
        if (ret < 0) {
                gf_log (THIS->name, GF_LOG_INFO,
                        "could not get fd ctx for fd=%p", fd);
                return;
        }

        fd_ctx = (afr_fd_ctx_t *)(long) ctx;

        list_for_each_entry_safe (entry, tmp, &fd_ctx->entries, list) {
                GF_FREE (entry->name);
                list_del (&entry->list);
                GF_FREE (entry);
        }
}

static void
afr_readdir_filter_trash_dir (gf_dirent_t *entries, fd_t *fd)
{
        gf_dirent_t *   entry       = NULL;
        gf_dirent_t *   tmp         = NULL;

        list_for_each_entry_safe (entry, tmp, &entries->list, list) {
                if (__is_root_gfid (fd->inode->gfid) &&
                    !strcmp (entry->d_name, GF_REPLICATE_TRASH_DIR)) {
                        list_del_init (&entry->list);
                        GF_FREE (entry);
                }
        }
}

int32_t
afr_readdir_cbk (call_frame_t *frame, void *cookie,
                 xlator_t *this, int32_t op_ret, int32_t op_errno,
                 gf_dirent_t *entries, dict_t *xdata)
{
        afr_local_t     *local = NULL;

        if (op_ret == -1)
                goto out;

        local = frame->local;
        afr_readdir_filter_trash_dir (entries, local->fd);

out:
        AFR_STACK_UNWIND (readdir, frame, op_ret, op_errno, entries, NULL);
        return 0;
}


int32_t
afr_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, gf_dirent_t *entries,
                  dict_t *xdata)
{
        afr_local_t     *local = NULL;

        if (op_ret == -1)
                goto out;

        local = frame->local;
        afr_readdir_filter_trash_dir (entries, local->fd);

out:
        AFR_STACK_UNWIND (readdirp, frame, op_ret, op_errno, entries, NULL);
        return 0;
}

int32_t
afr_do_readdir (call_frame_t *frame, xlator_t *this,
                fd_t *fd, size_t size, off_t offset, int whichop, dict_t *dict)
{
        afr_private_t *priv      = NULL;
        xlator_t      **children = NULL;
        int           call_child = 0;
        afr_local_t   *local     = NULL;
        afr_fd_ctx_t  *fd_ctx    = NULL;
        int           ret        = -1;
        int32_t       op_errno   = 0;
        uint64_t      read_child = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv     = this->private;
        children = priv->children;

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        local->fresh_children = afr_children_create (priv->child_count);
        if (!local->fresh_children) {
                op_errno = ENOMEM;
                goto out;
        }

        read_child = afr_inode_get_read_ctx (this, fd->inode,
                                             local->fresh_children);
        ret = afr_get_call_child (this, local->child_up, read_child,
                                  local->fresh_children,
                                  &call_child,
                                  &local->cont.readdir.last_index);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        fd_ctx  = afr_fd_ctx_get (fd, this);
        if (!fd_ctx) {
                op_errno = EBADF;
                goto out;
        }

        if ((offset == 0) || (fd_ctx->call_child == -1)) {
                fd_ctx->call_child = call_child;
        } else if ((priv->readdir_failover == _gf_false) &&
                   (call_child != fd_ctx->call_child)) {
                op_errno = EBADF;
                goto out;
        }

        local->fd                  = fd_ref (fd);
        local->cont.readdir.size   = size;
        local->cont.readdir.dict   = (dict)? dict_ref (dict) : NULL;

        if (whichop == GF_FOP_READDIR)
                STACK_WIND_COOKIE (frame, afr_readdir_cbk,
                                   (void *) (long) call_child,
                                   children[call_child],
                                   children[call_child]->fops->readdir, fd,
                                   size, offset, dict);
        else
                STACK_WIND_COOKIE (frame, afr_readdirp_cbk,
                                   (void *) (long) call_child,
                                   children[call_child],
                                   children[call_child]->fops->readdirp, fd,
                                   size, offset, dict);

        return 0;
out:
        AFR_STACK_UNWIND (readdir, frame, -1, op_errno, NULL, NULL);
        return 0;
}


int32_t
afr_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset, dict_t *xdata)
{
        afr_do_readdir (frame, this, fd, size, offset, GF_FOP_READDIR, xdata);
        return 0;
}


int32_t
afr_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t offset, dict_t *dict)
{
        afr_do_readdir (frame, this, fd, size, offset, GF_FOP_READDIRP, dict);
        return 0;
}


int32_t
afr_releasedir (xlator_t *this, fd_t *fd)
{
        afr_forget_entries (fd);
        afr_cleanup_fd_ctx (this, fd);

        return 0;
}
