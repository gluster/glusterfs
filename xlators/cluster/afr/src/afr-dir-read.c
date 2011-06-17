/*
  Copyright (c) 2007-2010 Gluster, Inc. <http://www.gluster.com>
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
afr_examine_dir_sh_unwind (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local  = NULL;

        local = frame->local;

        afr_set_opendir_done (this, local->fd->inode);

        AFR_STACK_UNWIND (opendir, frame, local->op_ret,
                          local->op_errno, local->fd);

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
                             gf_dirent_t *entries)
{
        afr_private_t *   priv        = NULL;
        afr_local_t *     local       = NULL;
        afr_self_heal_t * sh          = NULL;
        gf_dirent_t *     entry       = NULL;
        gf_dirent_t *     tmp         = NULL;
        int               child_index = 0;
        uint32_t          entry_cksum = 0;
        int               call_count  = 0;
        off_t             last_offset = 0;
        char              sh_type_str[256] = {0,};

        priv  = this->private;
        local = frame->local;
        sh    = &local->self_heal;

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
                entry_cksum = gf_rsync_weak_checksum (entry->d_name,
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
                           local->fd, 131072, last_offset);

        return 0;

out:
        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                if (__checksums_differ (local->cont.opendir.checksum,
                                        priv->child_count,
                                        local->child_up)) {

                        sh->need_entry_self_heal  = _gf_true;
                        sh->forced_merge          = _gf_true;
                        sh->type                  = local->fd->inode->ia_type;
                        sh->background            = _gf_false;
                        sh->unwind                = afr_examine_dir_sh_unwind;

                        afr_self_heal_type_str_get(&local->self_heal,
                                                   sh_type_str,
                                                   sizeof(sh_type_str));
                        gf_log (this->name, GF_LOG_INFO,
                                "%s self-heal triggered. path: %s, "
                                "reason: checksums of directory differ,"
                                " forced merge option set",
                                sh_type_str, local->loc.path);

                        afr_self_heal (frame, this);
                } else {
                        afr_set_opendir_done (this, local->fd->inode);

                        AFR_STACK_UNWIND (opendir, frame, local->op_ret,
                                          local->op_errno, local->fd);
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

        call_count = afr_up_children_count (priv->child_count, local->child_up);

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_examine_dir_readdir_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->readdir,
                                           local->fd, 131072, 0);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int32_t
afr_opendir_cbk (call_frame_t *frame, void *cookie,
                 xlator_t *this, int32_t op_ret, int32_t op_errno,
                 fd_t *fd)
{
        afr_private_t *priv              = NULL;
        afr_local_t   *local             = NULL;
        int32_t        up_children_count = 0;
        int            ret               = -1;
        int            call_count        = -1;

        priv  = this->private;
        local = frame->local;

        up_children_count = afr_up_children_count (priv->child_count,
                                                   local->child_up);

        LOCK (&frame->lock);
        {
                if (op_ret >= 0)
                        local->op_ret = op_ret;

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                if (local->op_ret != 0)
                        goto out;

                ret = afr_fd_ctx_set (this, local->fd);
                if (ret) {
                        local->op_ret = -1;
                        local->op_errno = -1;
                        gf_log (this->name, GF_LOG_ERROR,
                                "failed to set fd ctx for fd %p",
                                local->fd);
                        goto out;
                }
                if (!afr_is_opendir_done (this, local->fd->inode) &&
                    up_children_count > 1) {

                        /*
                         * This is the first opendir on this inode. We need
                         * to check if the directory's entries are the same
                         * on all subvolumes. This is needed in addition
                         * to regular entry self-heal because the readdir
                         * call is sent only to the first subvolume, and
                         * thus files that exist only there will never be healed
                         * otherwise (assuming changelog shows no anamolies).
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
                          local->op_errno, local->fd);

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
        int32_t         op_ret      = -1;
        int32_t         op_errno    = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv = this->private;

        child_count = priv->child_count;

        ALLOC_OR_GOTO (local, afr_local_t, out);
        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        loc_copy (&local->loc, loc);

        frame->local = local;
        local->fd    = fd_ref (fd);

        call_count = local->call_count;

        for (i = 0; i < child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_opendir_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->opendir,
                                    loc, fd);

                        if (!--call_count)
                                break;
                }
        }

        op_ret = 0;
out:
        if (op_ret == -1) {
                AFR_STACK_UNWIND (opendir, frame, op_ret, op_errno, fd);
        }

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


static gf_boolean_t
remembered_name (const char *name, struct list_head *entries)
{
        struct entry_name *e   = NULL;
        gf_boolean_t       ret = _gf_false;

        list_for_each_entry (e, entries, list) {
                if (!strcmp (name, e->name)) {
                        ret = _gf_true;
                        goto out;
                }
        }

out:
        return ret;
}


static void
afr_remember_entries (gf_dirent_t *entries, fd_t *fd)
{
        struct entry_name *n      = NULL;
        gf_dirent_t       *entry  = NULL;
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

        list_for_each_entry (entry, &entries->list, list) {
                n = GF_CALLOC (1, sizeof (*n), gf_afr_mt_entry_name);
                n->name = gf_strdup (entry->d_name);
                INIT_LIST_HEAD (&n->list);

                list_add (&n->list, &fd_ctx->entries);
        }
}


static off_t
afr_filter_entries (gf_dirent_t *entries, fd_t *fd)
{
        gf_dirent_t  *entry  = NULL;
        gf_dirent_t  *tmp    = NULL;
        int           ret    = 0;
        uint64_t      ctx    = 0;
        afr_fd_ctx_t *fd_ctx = NULL;
        off_t         offset = 0;

        ret = fd_ctx_get (fd, THIS, &ctx);
        if (ret < 0) {
                gf_log (THIS->name, GF_LOG_INFO,
                        "could not get fd ctx for fd=%p", fd);
                return -1;
        }

        fd_ctx = (afr_fd_ctx_t *)(long) ctx;

        list_for_each_entry_safe (entry, tmp, &entries->list, list) {
                offset = entry->d_off;

                if (remembered_name (entry->d_name, &fd_ctx->entries)) {
                        list_del (&entry->list);
                        GF_FREE (entry);
                }
        }

        return offset;
}


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


int32_t
afr_readdir_cbk (call_frame_t *frame, void *cookie,
                 xlator_t *this, int32_t op_ret, int32_t op_errno,
                 gf_dirent_t *entries)
{
        afr_private_t * priv        = NULL;
        afr_local_t *   local       = NULL;
        gf_dirent_t *   entry       = NULL;
        gf_dirent_t *   tmp         = NULL;
        int             child_index = -1;

        priv     = this->private;
        local = frame->local;
        child_index = (long) cookie;

        if (op_ret == -1)
                goto out;

        list_for_each_entry_safe (entry, tmp, &entries->list, list) {
                if ((local->fd->inode == local->fd->inode->table->root)
                    && !strcmp (entry->d_name, GF_REPLICATE_TRASH_DIR)) {
                        list_del_init (&entry->list);
                        GF_FREE (entry);
                }
        }

out:
        AFR_STACK_UNWIND (readdir, frame, op_ret, op_errno, entries);

        return 0;
}


int32_t
afr_readdirp_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                  int32_t op_ret, int32_t op_errno, gf_dirent_t *entries)
{
        afr_private_t *  priv        = NULL;
        afr_local_t *    local       = NULL;
        xlator_t **      children    = NULL;
        int              call_child  = 0;
        int              ret         = 0;
        gf_dirent_t *    entry       = NULL;
        gf_dirent_t *    tmp         = NULL;
        int              child_index = -1;
        uint64_t         ctx         = 0;
        afr_fd_ctx_t    *fd_ctx      = NULL;
        off_t            offset      = 0;

        priv     = this->private;
        children = priv->children;

        local = frame->local;

        child_index = (long) cookie;

        if (priv->strict_readdir) {
                ret = fd_ctx_get (local->fd, this, &ctx);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_INFO,
                                "could not get fd ctx for fd=%p", local->fd);
                        op_ret   = -1;
                        op_errno = -ret;
                        goto out;
                }

                fd_ctx = (afr_fd_ctx_t *)(long) ctx;

                if (child_went_down (op_ret, op_errno)) {
                        if (all_tried (child_index, priv->child_count)) {
                                gf_log (this->name, GF_LOG_INFO,
                                        "all options tried going out");
                                goto out;
                        }

                        call_child = ++child_index;

                        gf_log (this->name, GF_LOG_TRACE,
                                "starting readdir afresh on child %d, offset %"PRId64,
                                call_child, (uint64_t) 0);

                        fd_ctx->failed_over = _gf_true;

                        STACK_WIND_COOKIE (frame, afr_readdirp_cbk,
                                           (void *) (long) call_child,
                                           children[call_child],
                                           children[call_child]->fops->readdirp, local->fd,
                                           local->cont.readdir.size, 0);
                        return 0;
                }
        }

        if (op_ret != -1) {
                list_for_each_entry_safe (entry, tmp, &entries->list, list) {
                        if ((local->fd->inode == local->fd->inode->table->root)
                            && !strcmp (entry->d_name, GF_REPLICATE_TRASH_DIR)) {
                                list_del_init (&entry->list);
                                GF_FREE (entry);
                        }
                }
        }

        if (priv->strict_readdir) {
                if (fd_ctx->failed_over) {
                        if (list_empty (&entries->list)) {
                                gf_log (this->name, GF_LOG_DEBUG,
                                        "no entries found");
                                goto out;
                        }

                        offset = afr_filter_entries (entries, local->fd);

                        afr_remember_entries (entries, local->fd);

                        if (list_empty (&entries->list)) {
                                /* All the entries we got were duplicate. We
                                   shouldn't send an empty list now, because
                                   that'll make the application stop reading. So
                                   try to get more entries */

                                gf_log (this->name, GF_LOG_TRACE,
                                        "trying to fetch non-duplicate entries "
                                        "from offset %"PRId64", child %s",
                                        offset, children[child_index]->name);

                                STACK_WIND_COOKIE (frame, afr_readdirp_cbk,
                                                   (void *) (long) child_index,
                                                   children[child_index],
                                                   children[child_index]->fops->readdirp,
                                                   local->fd, local->cont.readdir.size, offset);
                                return 0;
                        }
                } else {
                        afr_remember_entries (entries, local->fd);
                }
        }

out:
        AFR_STACK_UNWIND (readdirp, frame, op_ret, op_errno, entries);

        return 0;
}


int32_t
afr_do_readdir (call_frame_t *frame, xlator_t *this,
                fd_t *fd, size_t size, off_t offset, int whichop)
{
        afr_private_t *  priv       = NULL;
        xlator_t **      children   = NULL;
        int              call_child = 0;
        afr_local_t     *local      = NULL;
        uint64_t         ctx        = 0;
        afr_fd_ctx_t    *fd_ctx     = NULL;
        int              ret        = -1;
        int32_t          op_ret     = -1;
        int32_t          op_errno   = 0;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv     = this->private;
        children = priv->children;

        ALLOC_OR_GOTO (local, afr_local_t, out);
        ret = AFR_LOCAL_INIT (local, priv);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        frame->local = local;

        call_child = afr_first_up_child (priv);
        if (call_child == -1) {
                op_errno = ENOTCONN;
                gf_log (this->name, GF_LOG_INFO,
                        "no child is up");
                goto out;
        }

        local->fd                  = fd_ref (fd);
        local->cont.readdir.size   = size;

        if (priv->strict_readdir) {
                ret = fd_ctx_get (fd, this, &ctx);
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_INFO,
                                "could not get fd ctx for fd=%p", fd);
                        op_errno = -ret;
                        goto out;
                }

                fd_ctx = (afr_fd_ctx_t *)(long) ctx;

                if (fd_ctx->last_tried != call_child) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "first up child has changed from %d to %d, "
                                "restarting readdir from offset 0",
                                fd_ctx->last_tried, call_child);

                        fd_ctx->failed_over = _gf_true;
                        offset = 0;
                }

                fd_ctx->last_tried = call_child;
        }

        if (whichop == GF_FOP_READDIR)
                STACK_WIND_COOKIE (frame, afr_readdir_cbk,
                                   (void *) (long) call_child,
                                   children[call_child],
                                   children[call_child]->fops->readdir, fd,
                                   size, offset);
        else
                STACK_WIND_COOKIE (frame, afr_readdirp_cbk,
                                   (void *) (long) call_child,
                                   children[call_child],
                                   children[call_child]->fops->readdirp, fd,
                                   size, offset);

        op_ret = 0;
out:
        if (op_ret == -1) {
                AFR_STACK_UNWIND (readdir, frame, op_ret, op_errno, NULL);
        }
        return 0;
}


int32_t
afr_readdir (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
             off_t offset)
{
        afr_do_readdir (frame, this, fd, size, offset, GF_FOP_READDIR);
        return 0;
}


int32_t
afr_readdirp (call_frame_t *frame, xlator_t *this, fd_t *fd, size_t size,
              off_t offset)
{
        afr_do_readdir (frame, this, fd, size, offset, GF_FOP_READDIRP);
        return 0;
}


int32_t
afr_releasedir (xlator_t *this, fd_t *fd)
{
        afr_forget_entries (fd);
        afr_cleanup_fd_ctx (this, fd);

        return 0;
}
