/*
  Copyright (c) 2008-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/

#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "inode.h"
#include "afr.h"
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
#include "byte-order.h"

#include "afr-transaction.h"
#include "afr-self-heal.h"
#include "afr-self-heal-common.h"

int
afr_sh_entry_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;

        if (sh->healing_fd)
                fd_unref (sh->healing_fd);
        sh->healing_fd = NULL;

        sh->completion_cbk (frame, this);

        return 0;
}


int
afr_sh_entry_unlock (call_frame_t *frame, xlator_t *this)
{
        afr_local_t         *local    = NULL;
        afr_internal_lock_t *int_lock = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        int_lock->lock_cbk = afr_sh_entry_done;
        afr_unlock (frame, this);

        return 0;
}


int
afr_sh_entry_finish (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local = NULL;

        local = frame->local;

        gf_log (this->name, GF_LOG_TRACE,
                "finishing entry selfheal of %s", local->loc.path);

        afr_sh_entry_unlock (frame, this);

        return 0;
}


int
afr_sh_entry_erase_pending_cbk (call_frame_t *frame, void *cookie,
                                xlator_t *this, int32_t op_ret,
                                int32_t op_errno, dict_t *xattr)
{
        long                 i          = 0;
        int                  call_count = 0;
        afr_local_t         *local      = NULL;
        afr_self_heal_t     *sh         = NULL;
        afr_local_t         *orig_local = NULL;
        call_frame_t        *orig_frame = NULL;
        afr_private_t       *priv       = NULL;
        int32_t             read_child  = -1;

        local = frame->local;
        priv  = this->private;
        sh = &local->self_heal;
        i = (long)cookie;


        afr_children_add_child (sh->fresh_children, i, priv->child_count);
        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_INFO,
                        "%s: failed to erase pending xattrs on %s (%s)",
                        local->loc.path, priv->children[i]->name,
                        strerror (op_errno));
        }

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                if (sh->source == -1) {
                        //this happens if the forced merge option is set
                        read_child = sh->fresh_children[0];
                } else {
                        read_child = sh->source;
                }
                afr_inode_set_read_ctx (this, sh->inode, read_child,
                                        sh->fresh_children);
                orig_frame = sh->orig_frame;
                orig_local = orig_frame->local;

                if (sh->source != -1) {
                        orig_local->cont.lookup.buf.ia_nlink = sh->buf[sh->source].ia_nlink;
                }

                afr_sh_entry_finish (frame, this);
        }

        return 0;
}


int
afr_sh_entry_erase_pending (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int              call_count = 0;
        int              i = 0;
        dict_t          **erase_xattr = NULL;
        int              need_unwind = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        afr_sh_pending_to_delta (priv, sh->xattr, sh->delta_matrix, sh->success,
                                 priv->child_count, AFR_ENTRY_TRANSACTION);

        erase_xattr = GF_CALLOC (sizeof (*erase_xattr), priv->child_count,
                                 gf_afr_mt_dict_t);

        for (i = 0; i < priv->child_count; i++) {
                if (sh->xattr[i]) {
                        call_count++;

                        erase_xattr[i] = get_new_dict();
                        dict_ref (erase_xattr[i]);
                }
        }

        if (call_count == 0)
                need_unwind = 1;

        afr_sh_delta_to_xattr (priv, sh->delta_matrix, erase_xattr,
                               priv->child_count, AFR_ENTRY_TRANSACTION);

        local->call_count = call_count;
        for (i = 0; i < priv->child_count; i++) {
                if (!erase_xattr[i])
                        continue;

                gf_log (this->name, GF_LOG_TRACE,
                        "erasing pending flags from %s on %s",
                        local->loc.path, priv->children[i]->name);

                STACK_WIND_COOKIE (frame, afr_sh_entry_erase_pending_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->xattrop,
                                   &local->loc,
                                   GF_XATTROP_ADD_ARRAY, erase_xattr[i]);
                if (!--call_count)
                        break;
        }

        for (i = 0; i < priv->child_count; i++) {
                if (erase_xattr[i]) {
                        dict_unref (erase_xattr[i]);
                }
        }
        GF_FREE (erase_xattr);

        if (need_unwind)
                afr_sh_entry_finish (frame, this);

        return 0;
}



static int
next_active_source (call_frame_t *frame, xlator_t *this,
                    int current_active_source)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *local  = NULL;
        afr_self_heal_t *sh  = NULL;
        int              source = -1;
        int              next_active_source = -1;
        int              i = 0;

        priv = this->private;
        local = frame->local;
        sh = &local->self_heal;

        source = sh->source;

        if (source != -1) {
                if (current_active_source != source)
                        next_active_source = source;
                goto out;
        }

        /*
          the next active sink becomes the source for the
          'conservative decision' of merging all entries
        */

        for (i = 0; i < priv->child_count; i++) {
                if ((sh->sources[i] == 0)
                    && (local->child_up[i] == 1)
                    && (i > current_active_source)) {

                        next_active_source = i;
                        break;
                }
        }
out:
        return next_active_source;
}



static int
next_active_sink (call_frame_t *frame, xlator_t *this,
                  int current_active_sink)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *local  = NULL;
        afr_self_heal_t *sh  = NULL;
        int              next_active_sink = -1;
        int              i = 0;

        priv = this->private;
        local = frame->local;
        sh = &local->self_heal;

        /*
          the next active sink becomes the source for the
          'conservative decision' of merging all entries
        */

        for (i = 0; i < priv->child_count; i++) {
                if ((sh->sources[i] == 0)
                    && (local->child_up[i] == 1)
                    && (i > current_active_sink)) {

                        next_active_sink = i;
                        break;
                }
        }

        return next_active_sink;
}


int
build_child_loc (xlator_t *this, loc_t *child, loc_t *parent, char *name)
{
        int   ret = -1;

        if (!child) {
                goto out;
        }

        if (strcmp (parent->path, "/") == 0)
                ret = gf_asprintf ((char **)&child->path, "/%s", name);
        else
                ret = gf_asprintf ((char **)&child->path, "%s/%s", parent->path,
                                   name);

        if (-1 == ret) {
                gf_log (this->name, GF_LOG_ERROR,
                        "asprintf failed while setting child path");
        }

        if (!child->path) {
                goto out;
        }

        child->name = strrchr (child->path, '/');
        if (child->name)
                child->name++;

        child->parent = inode_ref (parent->inode);
        child->inode = inode_new (parent->inode->table);

        if (!child->inode) {
                goto out;
        }

        ret = 0;
out:
        if (ret == -1)
                loc_wipe (child);

        return ret;
}


int
afr_sh_entry_impunge_all (call_frame_t *frame, xlator_t *this);

int
afr_sh_entry_impunge_subvol (call_frame_t *frame, xlator_t *this,
                             int active_src);

int
afr_sh_entry_expunge_all (call_frame_t *frame, xlator_t *this);

int
afr_sh_entry_expunge_subvol (call_frame_t *frame, xlator_t *this,
                             int active_src);

int
afr_sh_entry_expunge_entry_done (call_frame_t *frame, xlator_t *this,
                                 int active_src, int32_t op_ret,
                                 int32_t op_errno)
{
        int              call_count = 0;

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                afr_sh_entry_expunge_subvol (frame, this, active_src);

        return 0;
}

int
afr_sh_entry_expunge_parent_setattr_cbk (call_frame_t *expunge_frame,
                                         void *cookie, xlator_t *this,
                                         int32_t op_ret, int32_t op_errno,
                                         struct iatt *preop, struct iatt *postop)
{
        afr_private_t   *priv          = NULL;
        afr_local_t     *expunge_local = NULL;
        afr_self_heal_t *expunge_sh    = NULL;
        call_frame_t    *frame         = NULL;
        int              active_src    = (long) cookie;
        afr_self_heal_t *sh            = NULL;
        afr_local_t     *local         = NULL;

        priv          = this->private;
        expunge_local = expunge_frame->local;
        expunge_sh    = &expunge_local->self_heal;
        frame         = expunge_sh->sh_frame;
        local         = frame->local;
        sh            = &local->self_heal;

        if (op_ret != 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "setattr on parent directory of %s on subvolume %s failed: %s",
                        expunge_local->loc.path,
                        priv->children[active_src]->name, strerror (op_errno));
        }

        AFR_STACK_DESTROY (expunge_frame);
        sh->expunge_done (frame, this, active_src, op_ret, op_errno);

        return 0;
}


int
afr_sh_entry_expunge_remove_cbk (call_frame_t *expunge_frame, void *cookie,
                                 xlator_t *this,
                                 int32_t op_ret, int32_t op_errno,
                                 struct iatt *preparent,
                                 struct iatt *postparent)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *expunge_local = NULL;
        afr_self_heal_t *expunge_sh = NULL;
        int              active_src = 0;
        int32_t          valid = 0;

        priv = this->private;
        expunge_local = expunge_frame->local;
        expunge_sh = &expunge_local->self_heal;

        active_src = (long) cookie;

        if (op_ret == 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "removed %s on %s",
                        expunge_local->loc.path,
                        priv->children[active_src]->name);
        } else {
                gf_log (this->name, GF_LOG_INFO,
                        "removing %s on %s failed (%s)",
                        expunge_local->loc.path,
                        priv->children[active_src]->name,
                        strerror (op_errno));
        }

        valid = GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME;
        afr_build_parent_loc (&expunge_sh->parent_loc, &expunge_local->loc);

        STACK_WIND_COOKIE (expunge_frame, afr_sh_entry_expunge_parent_setattr_cbk,
                           (void *) (long) active_src,
                           priv->children[active_src],
                           priv->children[active_src]->fops->setattr,
                           &expunge_sh->parent_loc,
                           &expunge_sh->parentbuf,
                           valid);

        return 0;
}


int
afr_sh_entry_expunge_unlink (call_frame_t *expunge_frame, xlator_t *this,
                             int active_src)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *expunge_local = NULL;

        priv          = this->private;
        expunge_local = expunge_frame->local;

        gf_log (this->name, GF_LOG_TRACE,
                "expunging file %s on %s",
                expunge_local->loc.path, priv->children[active_src]->name);

        STACK_WIND_COOKIE (expunge_frame, afr_sh_entry_expunge_remove_cbk,
                           (void *) (long) active_src,
                           priv->children[active_src],
                           priv->children[active_src]->fops->unlink,
                           &expunge_local->loc);

        return 0;
}



int
afr_sh_entry_expunge_rmdir (call_frame_t *expunge_frame, xlator_t *this,
                            int active_src)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *expunge_local = NULL;

        priv          = this->private;
        expunge_local = expunge_frame->local;

        gf_log (this->name, GF_LOG_DEBUG,
                "expunging directory %s on %s",
                expunge_local->loc.path, priv->children[active_src]->name);

        STACK_WIND_COOKIE (expunge_frame, afr_sh_entry_expunge_remove_cbk,
                           (void *) (long) active_src,
                           priv->children[active_src],
                           priv->children[active_src]->fops->rmdir,
                           &expunge_local->loc, 1);

        return 0;
}


int
afr_sh_entry_expunge_remove (call_frame_t *expunge_frame, xlator_t *this,
                             int active_src, struct iatt *buf)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *expunge_local = NULL;
        afr_self_heal_t *expunge_sh = NULL;
        call_frame_t    *frame = NULL;
        int              type = 0;
        afr_self_heal_t *sh            = NULL;
        afr_local_t     *local         = NULL;

        priv = this->private;
        expunge_local = expunge_frame->local;
        expunge_sh = &expunge_local->self_heal;
        frame = expunge_sh->sh_frame;
        local         = frame->local;
        sh            = &local->self_heal;

        type = buf->ia_type;

        switch (type) {
        case IA_IFSOCK:
        case IA_IFREG:
        case IA_IFBLK:
        case IA_IFCHR:
        case IA_IFIFO:
        case IA_IFLNK:
                afr_sh_entry_expunge_unlink (expunge_frame, this, active_src);
                break;
        case IA_IFDIR:
                afr_sh_entry_expunge_rmdir (expunge_frame, this, active_src);
                break;
        default:
                gf_log (this->name, GF_LOG_ERROR,
                        "%s has unknown file type on %s: 0%o",
                        expunge_local->loc.path,
                        priv->children[active_src]->name, type);
                goto out;
                break;
        }

        return 0;
out:
        AFR_STACK_DESTROY (expunge_frame);
        sh->expunge_done (frame, this, active_src, -1, EINVAL);

        return 0;
}


int
afr_sh_entry_expunge_lookup_cbk (call_frame_t *expunge_frame, void *cookie,
                                 xlator_t *this,
                                 int32_t op_ret, int32_t op_errno,
                                 inode_t *inode, struct iatt *buf, dict_t *x,
                                 struct iatt *postparent)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *expunge_local = NULL;
        afr_self_heal_t *expunge_sh = NULL;
        call_frame_t    *frame = NULL;
        int              active_src = 0;
        afr_self_heal_t *sh            = NULL;
        afr_local_t     *local         = NULL;

        priv = this->private;
        expunge_local = expunge_frame->local;
        expunge_sh = &expunge_local->self_heal;
        frame = expunge_sh->sh_frame;
        active_src = (long) cookie;
        local         = frame->local;
        sh            = &local->self_heal;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "lookup of %s on %s failed (%s)",
                        expunge_local->loc.path,
                        priv->children[active_src]->name,
                        strerror (op_errno));
                goto out;
        }

        afr_sh_entry_expunge_remove (expunge_frame, this, active_src, buf);

        return 0;
out:
        AFR_STACK_DESTROY (expunge_frame);
        sh->expunge_done (frame, this, active_src, op_ret, op_errno);

        return 0;
}


int
afr_sh_entry_expunge_purge (call_frame_t *expunge_frame, xlator_t *this,
                            int active_src)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *expunge_local = NULL;

        priv = this->private;
        expunge_local = expunge_frame->local;

        gf_log (this->name, GF_LOG_TRACE,
                "looking up %s on %s",
                expunge_local->loc.path, priv->children[active_src]->name);

        STACK_WIND_COOKIE (expunge_frame, afr_sh_entry_expunge_lookup_cbk,
                           (void *) (long) active_src,
                           priv->children[active_src],
                           priv->children[active_src]->fops->lookup,
                           &expunge_local->loc, 0);

        return 0;
}

int
afr_sh_entry_expunge_entry_cbk (call_frame_t *expunge_frame, void *cookie,
                                xlator_t *this,
                                int32_t op_ret, int32_t op_errno,
                                inode_t *inode, struct iatt *buf, dict_t *x,
                                struct iatt *postparent)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *expunge_local = NULL;
        afr_self_heal_t *expunge_sh = NULL;
        int              source = 0;
        call_frame_t    *frame = NULL;
        int              active_src = 0;
        int              need_expunge = 0;
        afr_self_heal_t *sh            = NULL;
        afr_local_t     *local         = NULL;

        priv = this->private;
        expunge_local = expunge_frame->local;
        expunge_sh = &expunge_local->self_heal;
        frame = expunge_sh->sh_frame;
        active_src = expunge_sh->active_source;
        source = (long) cookie;
        local         = frame->local;
        sh            = &local->self_heal;

        if (op_ret == -1 && op_errno == ENOENT)
                need_expunge = 1;
        else if (op_ret == -1)
                goto out;

        if (!uuid_is_null (expunge_sh->entrybuf.ia_gfid) &&
            !uuid_is_null (buf->ia_gfid) &&
            (uuid_compare (expunge_sh->entrybuf.ia_gfid, buf->ia_gfid) != 0)) {
                char uuidbuf1[64];
                char uuidbuf2[64];
                gf_log (this->name, GF_LOG_DEBUG,
                        "entry %s found on %s with mismatching gfid (%s/%s)",
                        expunge_local->loc.path,
                        priv->children[source]->name,
                        uuid_utoa_r (expunge_sh->entrybuf.ia_gfid, uuidbuf1),
                        uuid_utoa_r (buf->ia_gfid, uuidbuf2));
                need_expunge = 1;
        }

        if (need_expunge) {
                gf_log (this->name, GF_LOG_INFO,
                        "missing entry %s on %s",
                        expunge_local->loc.path,
                        priv->children[source]->name);

                if (postparent)
                        expunge_sh->parentbuf = *postparent;

                afr_sh_entry_expunge_purge (expunge_frame, this, active_src);

                return 0;
        }

out:
        if (op_ret == 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "%s exists under %s",
                        expunge_local->loc.path,
                        priv->children[source]->name);
        } else {
                gf_log (this->name, GF_LOG_INFO,
                        "looking up %s under %s failed (%s)",
                        expunge_local->loc.path,
                        priv->children[source]->name,
                        strerror (op_errno));
        }

        AFR_STACK_DESTROY (expunge_frame);
        sh->expunge_done (frame, this, active_src, op_ret, op_errno);

        return 0;
}


int
afr_sh_entry_expunge_entry (call_frame_t *frame, xlator_t *this,
                            gf_dirent_t *entry)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *local  = NULL;
        afr_self_heal_t *sh  = NULL;
        int              ret = -1;
        call_frame_t    *expunge_frame = NULL;
        afr_local_t     *expunge_local = NULL;
        afr_self_heal_t *expunge_sh = NULL;
        int              active_src = 0;
        int              source = 0;
        int              op_errno = 0;
        char            *name = NULL;
        int             op_ret = -1;

        priv = this->private;
        local = frame->local;
        sh = &local->self_heal;

        active_src = sh->active_source;
        source = sh->source;
        sh->expunge_done = afr_sh_entry_expunge_entry_done;

        name = entry->d_name;

        if ((strcmp (name, ".") == 0)
            || (strcmp (name, "..") == 0)
            || ((strcmp (local->loc.path, "/") == 0)
                && (strcmp (name, GF_REPLICATE_TRASH_DIR) == 0))) {

                gf_log (this->name, GF_LOG_TRACE,
                        "skipping inspection of %s under %s",
                        name, local->loc.path);
                op_ret = 0;
                goto out;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "inspecting existance of %s under %s",
                name, local->loc.path);

        expunge_frame = copy_frame (frame);
        if (!expunge_frame) {
                op_errno = ENOMEM;
                goto out;
        }

        ALLOC_OR_GOTO (expunge_local, afr_local_t, out);

        expunge_frame->local = expunge_local;
        expunge_sh = &expunge_local->self_heal;
        expunge_sh->sh_frame = frame;
        expunge_sh->active_source = active_src;
        expunge_sh->entrybuf = entry->d_stat;

        ret = build_child_loc (this, &expunge_local->loc, &local->loc, name);
        if (ret != 0) {
                op_errno = EINVAL;
                goto out;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "looking up %s on %s", expunge_local->loc.path,
                priv->children[source]->name);

        STACK_WIND_COOKIE (expunge_frame,
                           afr_sh_entry_expunge_entry_cbk,
                           (void *) (long) source,
                           priv->children[source],
                           priv->children[source]->fops->lookup,
                           &expunge_local->loc, 0);

        ret = 0;
out:
        if (ret == -1)
                sh->expunge_done (frame, this, active_src, op_ret, op_errno);

        return 0;
}


int
afr_sh_entry_expunge_readdir_cbk (call_frame_t *frame, void *cookie,
                                  xlator_t *this,
                                  int32_t op_ret, int32_t op_errno,
                                  gf_dirent_t *entries)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *local  = NULL;
        afr_self_heal_t *sh  = NULL;
        gf_dirent_t     *entry = NULL;
        off_t            last_offset = 0;
        int              active_src = 0;
        int              entry_count = 0;

        priv = this->private;
        local = frame->local;
        sh = &local->self_heal;

        active_src = sh->active_source;

        if (op_ret <= 0) {
                if (op_ret < 0) {
                        gf_log (this->name, GF_LOG_INFO,
                                "readdir of %s on subvolume %s failed (%s)",
                                local->loc.path,
                                priv->children[active_src]->name,
                                strerror (op_errno));
                } else {
                        gf_log (this->name, GF_LOG_TRACE,
                                "readdir of %s on subvolume %s complete",
                                local->loc.path,
                                priv->children[active_src]->name);
                }

                afr_sh_entry_expunge_all (frame, this);
                return 0;
        }

        list_for_each_entry (entry, &entries->list, list) {
                last_offset = entry->d_off;
                entry_count++;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "readdir'ed %d entries from %s",
                entry_count, priv->children[active_src]->name);

        sh->offset = last_offset;
        local->call_count = entry_count;

        list_for_each_entry (entry, &entries->list, list) {
                afr_sh_entry_expunge_entry (frame, this, entry);
        }

        return 0;
}

int
afr_sh_entry_expunge_subvol (call_frame_t *frame, xlator_t *this,
                             int active_src)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *local  = NULL;
        afr_self_heal_t *sh  = NULL;

        priv = this->private;
        local = frame->local;
        sh = &local->self_heal;

        STACK_WIND (frame, afr_sh_entry_expunge_readdir_cbk,
                    priv->children[active_src],
                    priv->children[active_src]->fops->readdirp,
                    sh->healing_fd, sh->block_size, sh->offset);

        return 0;
}


int
afr_sh_entry_expunge_all (call_frame_t *frame, xlator_t *this)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *local  = NULL;
        afr_self_heal_t *sh  = NULL;
        int              active_src = -1;

        priv = this->private;
        local = frame->local;
        sh = &local->self_heal;

        sh->offset = 0;

        if (sh->source == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no active sources for %s to expunge entries",
                        local->loc.path);
                goto out;
        }

        active_src = next_active_sink (frame, this, sh->active_source);
        sh->active_source = active_src;

        if (sh->op_failed) {
                goto out;
        }

        if (active_src == -1) {
                /* completed creating missing files on all subvolumes */
                goto out;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "expunging entries of %s on %s to other sinks",
                local->loc.path, priv->children[active_src]->name);

        afr_sh_entry_expunge_subvol (frame, this, active_src);

        return 0;
out:
        afr_sh_entry_impunge_all (frame, this);
        return 0;

}


int
afr_sh_entry_impunge_entry_done (call_frame_t *frame, xlator_t *this,
                                 int active_src, int32_t op_ret,
                                 int32_t op_errno)
{
        int              call_count = 0;

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                afr_sh_entry_impunge_subvol (frame, this, active_src);

        return 0;
}


int
afr_sh_entry_impunge_setattr_cbk (call_frame_t *impunge_frame, void *cookie,
                                  xlator_t *this,
                                  int32_t op_ret, int32_t op_errno,
                                  struct iatt *preop, struct iatt *postop)
{
        int              call_count = 0;
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_self_heal_t *impunge_sh = NULL;
        call_frame_t    *frame = NULL;
        int              child_index = 0;
        int32_t          impunge_ret_child = 0;

        priv = this->private;
        impunge_local = impunge_frame->local;
        impunge_sh = &impunge_local->self_heal;
        frame = impunge_sh->sh_frame;
        local = frame->local;
        sh    = &local->self_heal;
        child_index = (long) cookie;

        if (op_ret == 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "setattr done for %s on %s",
                        impunge_local->loc.path,
                        priv->children[child_index]->name);
        } else {
                gf_log (this->name, GF_LOG_INFO,
                        "setattr (%s) on %s failed (%s)",
                        impunge_local->loc.path,
                        priv->children[child_index]->name,
                        strerror (op_errno));
        }

        LOCK (&impunge_frame->lock);
        {
                call_count = --impunge_local->call_count;
        }
        UNLOCK (&impunge_frame->lock);

        if (call_count == 0) {
                impunge_ret_child = impunge_sh->impunge_ret_child;
                AFR_STACK_DESTROY (impunge_frame);
                sh->impunge_done (frame, this, impunge_ret_child, op_ret,
                                  op_errno);
        }

        return 0;
}


int
afr_sh_entry_impunge_xattrop_cbk (call_frame_t *impunge_frame, void *cookie,
                                  xlator_t *this,
                                  int32_t op_ret, int32_t op_errno,
                                  dict_t *xattr)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;
        int              child_index = 0;
        struct iatt      stbuf = {0};
        int32_t          valid = 0;

        priv          = this->private;
        impunge_local = impunge_frame->local;

        child_index = (long) cookie;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_INFO,
                        "%s: failed to perform xattrop on %s (%s)",
                        impunge_local->loc.path,
                        priv->children[child_index]->name,
                        strerror (op_errno));
        }

        gf_log (this->name, GF_LOG_TRACE,
                "setting ownership of %s on %s to %d/%d",
                impunge_local->loc.path,
                priv->children[child_index]->name,
                impunge_local->cont.lookup.buf.ia_uid,
                impunge_local->cont.lookup.buf.ia_gid);

        stbuf.ia_atime = impunge_local->cont.lookup.buf.ia_atime;
        stbuf.ia_atime_nsec = impunge_local->cont.lookup.buf.ia_atime_nsec;
        stbuf.ia_mtime = impunge_local->cont.lookup.buf.ia_mtime;
        stbuf.ia_mtime_nsec = impunge_local->cont.lookup.buf.ia_mtime_nsec;

        stbuf.ia_uid = impunge_local->cont.lookup.buf.ia_uid;
        stbuf.ia_gid = impunge_local->cont.lookup.buf.ia_gid;

        valid = GF_SET_ATTR_UID   | GF_SET_ATTR_GID |
                GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME;

        STACK_WIND_COOKIE (impunge_frame, afr_sh_entry_impunge_setattr_cbk,
                           (void *) (long) child_index,
                           priv->children[child_index],
                           priv->children[child_index]->fops->setattr,
                           &impunge_local->loc,
                           &stbuf, valid);
        return 0;
}


int
afr_sh_entry_impunge_parent_setattr_cbk (call_frame_t *setattr_frame,
                                         void *cookie, xlator_t *this,
                                         int32_t op_ret, int32_t op_errno,
                                         struct iatt *preop, struct iatt *postop)
{
        loc_t *parent_loc = cookie;

        if (op_ret != 0) {
                gf_log (this->name, GF_LOG_INFO,
                        "setattr on parent directory (%s) failed: %s",
                        parent_loc->path, strerror (op_errno));
        }

        loc_wipe (parent_loc);

        GF_FREE (parent_loc);

        AFR_STACK_DESTROY (setattr_frame);
        return 0;
}


int
afr_sh_entry_impunge_newfile_cbk (call_frame_t *impunge_frame, void *cookie,
                                  xlator_t *this,
                                  int32_t op_ret, int32_t op_errno,
                                  inode_t *inode, struct iatt *stbuf,
                                  struct iatt *preparent,
                                  struct iatt *postparent)
{
        int              call_count       = 0;
        afr_private_t   *priv             = NULL;
        afr_local_t     *impunge_local    = NULL;
        afr_self_heal_t *impunge_sh       = NULL;
        call_frame_t    *frame            = NULL;
        int              active_src       = 0;
        int              child_index      = 0;
        int32_t         *pending_array    = NULL;
        dict_t          *xattr            = NULL;
        int              ret              = 0;
        int              idx              = 0;
        afr_local_t     *local            = NULL;
        afr_self_heal_t *sh               = NULL;
        call_frame_t    *setattr_frame    = NULL;
        int32_t          valid            = 0;
        loc_t           *parent_loc       = NULL;
        struct iatt      parentbuf        = {0,};
        int32_t          impunge_ret_child = 0;

        priv = this->private;
        impunge_local = impunge_frame->local;
        impunge_sh = &impunge_local->self_heal;
        frame = impunge_sh->sh_frame;
        local = frame->local;
        sh    = &local->self_heal;
        active_src = impunge_sh->active_source;

        child_index = (long) cookie;

        if (op_ret == -1) {
                ret = -1;
                gf_log (this->name, GF_LOG_ERROR,
                        "creation of %s on %s failed (%s)",
                        impunge_local->loc.path,
                        priv->children[child_index]->name,
                        strerror (op_errno));
                goto out;
        }

        inode->ia_type = stbuf->ia_type;

        xattr = dict_new ();
        if (!xattr) {
                ret = -1;
                goto out;
        }

        pending_array = (int32_t*) GF_CALLOC (3, sizeof (*pending_array),
                                              gf_afr_mt_int32_t);

        if (!pending_array) {
                ret = -1;
                goto out;
        }
        idx = afr_index_for_transaction_type (AFR_METADATA_TRANSACTION);
        pending_array[idx] = hton32 (1);
        if (IA_ISDIR (stbuf->ia_type))
                idx = afr_index_for_transaction_type (AFR_ENTRY_TRANSACTION);
        else
                idx = afr_index_for_transaction_type (AFR_DATA_TRANSACTION);
        pending_array[idx] = hton32 (1);

        ret = dict_set_dynptr (xattr, priv->pending_key[child_index],
                               pending_array,
                               3 * sizeof (*pending_array));
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "Unable to set dict value.");
        } else {
                pending_array = NULL;
        }

        valid         = GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME;
        parentbuf     = impunge_sh->parentbuf;
        setattr_frame = copy_frame (impunge_frame);

        parent_loc = GF_CALLOC (1, sizeof (*parent_loc),
                                gf_afr_mt_loc_t);
        if (!parent_loc) {
                ret = -1;
                goto out;
        }
        afr_build_parent_loc (parent_loc, &impunge_local->loc);

        STACK_WIND_COOKIE (impunge_frame, afr_sh_entry_impunge_xattrop_cbk,
                           (void *) (long) child_index,
                           priv->children[active_src],
                           priv->children[active_src]->fops->xattrop,
                           &impunge_local->loc, GF_XATTROP_ADD_ARRAY, xattr);

        STACK_WIND_COOKIE (setattr_frame, afr_sh_entry_impunge_parent_setattr_cbk,
                           (void *) (long) parent_loc,
                           priv->children[child_index],
                           priv->children[child_index]->fops->setattr,
                           parent_loc, &parentbuf, valid);

out:
        if (xattr)
                dict_unref (xattr);

        if (ret) {
                if (pending_array)
                        GF_FREE (pending_array);

                LOCK (&impunge_frame->lock);
                {
                        call_count = --impunge_local->call_count;
                }
                UNLOCK (&impunge_frame->lock);

                if (call_count == 0) {
                        impunge_ret_child = impunge_sh->impunge_ret_child;
                        AFR_STACK_DESTROY (impunge_frame);
                        sh->impunge_done (frame, this, impunge_ret_child, -1,
                                          op_errno);
                }
        }

        return 0;
}


int
afr_sh_entry_impunge_mknod (call_frame_t *impunge_frame, xlator_t *this,
                            int child_index, struct iatt *stbuf)
{
        afr_private_t *priv          = NULL;
        afr_local_t   *impunge_local = NULL;
        dict_t        *dict          = NULL;
        int            ret           = 0;

        priv = this->private;
        impunge_local = impunge_frame->local;

        gf_log (this->name, GF_LOG_DEBUG,
                "creating missing file %s on %s",
                impunge_local->loc.path,
                priv->children[child_index]->name);

        dict = dict_new ();
        if (!dict)
                gf_log (this->name, GF_LOG_ERROR, "Out of memory");

        ret = afr_set_dict_gfid (dict, stbuf->ia_gfid);
        if (ret)
                gf_log (this->name, GF_LOG_INFO, "%s: gfid set failed",
                        impunge_local->loc.path);

        STACK_WIND_COOKIE (impunge_frame, afr_sh_entry_impunge_newfile_cbk,
                           (void *) (long) child_index,
                           priv->children[child_index],
                           priv->children[child_index]->fops->mknod,
                           &impunge_local->loc,
                           st_mode_from_ia (stbuf->ia_prot, stbuf->ia_type),
                           makedev (ia_major (stbuf->ia_rdev),
                                    ia_minor (stbuf->ia_rdev)), dict);

        if (dict)
                dict_unref (dict);

        return 0;
}



int
afr_sh_entry_impunge_mkdir (call_frame_t *impunge_frame, xlator_t *this,
                            int child_index, struct iatt *stbuf)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;
        dict_t          *dict = NULL;

        int ret = 0;

        priv = this->private;
        impunge_local = impunge_frame->local;

        dict = dict_new ();
        if (!dict) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                return 0;
        }

        ret = afr_set_dict_gfid (dict, stbuf->ia_gfid);
        if (ret)
                gf_log (this->name, GF_LOG_INFO, "%s: gfid set failed",
                        impunge_local->loc.path);

        gf_log (this->name, GF_LOG_DEBUG,
                "creating missing directory %s on %s",
                impunge_local->loc.path,
                priv->children[child_index]->name);

        STACK_WIND_COOKIE (impunge_frame, afr_sh_entry_impunge_newfile_cbk,
                           (void *) (long) child_index,
                           priv->children[child_index],
                           priv->children[child_index]->fops->mkdir,
                           &impunge_local->loc,
                           st_mode_from_ia (stbuf->ia_prot, stbuf->ia_type),
                           dict);

        if (dict)
                dict_unref (dict);

        return 0;
}


int
afr_sh_entry_impunge_symlink (call_frame_t *impunge_frame, xlator_t *this,
                              int child_index, const char *linkname)
{
        afr_private_t   *priv          = NULL;
        afr_local_t     *impunge_local = NULL;
        dict_t          *dict          = NULL;
        struct iatt     *buf           = NULL;
        int              ret           = 0;
        call_frame_t    *frame         = NULL;
        afr_local_t     *local         = NULL;
        afr_self_heal_t *sh            = NULL;
        afr_self_heal_t *impunge_sh    = NULL;
        int32_t          impunge_ret_child = 0;

        priv = this->private;
        impunge_local = impunge_frame->local;
        impunge_sh    = &impunge_local->self_heal;
        frame         = impunge_sh->sh_frame;
        local         = frame->local;
        sh            = &local->self_heal;

        buf = &impunge_local->cont.symlink.buf;

        dict = dict_new ();
        if (!dict) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory");
                impunge_ret_child = impunge_sh->impunge_ret_child;
                AFR_STACK_DESTROY (impunge_frame);
                sh->impunge_done (impunge_frame, this, impunge_ret_child, -1,
                                  ENOMEM);
                goto out;
        }

        ret = afr_set_dict_gfid (dict, buf->ia_gfid);
        if (ret)
                gf_log (this->name, GF_LOG_INFO,
                        "%s: dict set gfid failed",
                        impunge_local->loc.path);

        gf_log (this->name, GF_LOG_DEBUG,
                "creating missing symlink %s -> %s on %s",
                impunge_local->loc.path, linkname,
                priv->children[child_index]->name);

        STACK_WIND_COOKIE (impunge_frame, afr_sh_entry_impunge_newfile_cbk,
                           (void *) (long) child_index,
                           priv->children[child_index],
                           priv->children[child_index]->fops->symlink,
                           linkname, &impunge_local->loc, dict);

        if (dict)
                dict_unref (dict);
out:
        return 0;
}


int
afr_sh_entry_impunge_symlink_unlink_cbk (call_frame_t *impunge_frame,
                                         void *cookie, xlator_t *this,
                                         int32_t op_ret, int32_t op_errno,
                                         struct iatt *preparent,
                                         struct iatt *postparent)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;
        afr_self_heal_t *impunge_sh = NULL;
        int              child_index = -1;
        call_frame_t    *frame = NULL;
        int              call_count = -1;
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        int32_t          impunge_ret_child = 0;

        priv          = this->private;
        impunge_local = impunge_frame->local;
        impunge_sh    = &impunge_local->self_heal;
        frame         = impunge_sh->sh_frame;
        local         = frame->local;
        sh            = &local->self_heal;

        child_index = (long) cookie;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_INFO,
                        "unlink of %s on %s failed (%s)",
                        impunge_local->loc.path,
                        priv->children[child_index]->name,
                        strerror (op_errno));
                goto out;
        }

        afr_sh_entry_impunge_symlink (impunge_frame, this, child_index,
                                      impunge_sh->linkname);

        return 0;
out:
        LOCK (&impunge_frame->lock);
        {
                call_count = --impunge_local->call_count;
        }
        UNLOCK (&impunge_frame->lock);

        if (call_count == 0) {
                impunge_ret_child = impunge_sh->impunge_ret_child;
                AFR_STACK_DESTROY (impunge_frame);
                sh->impunge_done (frame, this, impunge_ret_child, op_ret,
                                  op_errno);
        }

        return 0;
}


int
afr_sh_entry_impunge_symlink_unlink (call_frame_t *impunge_frame, xlator_t *this,
                                     int child_index)
{
        afr_private_t   *priv          = NULL;
        afr_local_t     *impunge_local = NULL;

        priv          = this->private;
        impunge_local = impunge_frame->local;

        gf_log (this->name, GF_LOG_DEBUG,
                "unlinking symlink %s with wrong target on %s",
                impunge_local->loc.path,
                priv->children[child_index]->name);

        STACK_WIND_COOKIE (impunge_frame, afr_sh_entry_impunge_symlink_unlink_cbk,
                           (void *) (long) child_index,
                           priv->children[child_index],
                           priv->children[child_index]->fops->unlink,
                           &impunge_local->loc);

        return 0;
}


int
afr_sh_entry_impunge_readlink_sink_cbk (call_frame_t *impunge_frame, void *cookie,
                                        xlator_t *this,
                                        int32_t op_ret, int32_t op_errno,
                                        const char *linkname, struct iatt *sbuf)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;
        afr_self_heal_t *impunge_sh = NULL;
        int              child_index = -1;
        call_frame_t    *frame = NULL;
        int              call_count = -1;
        int              active_src = -1;
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        int32_t          impunge_ret_child = 0;

        priv          = this->private;
        impunge_local = impunge_frame->local;
        impunge_sh    = &impunge_local->self_heal;
        frame         = impunge_sh->sh_frame;
        local         = frame->local;
        sh            = &local->self_heal;
        active_src    = impunge_sh->active_source;

        child_index = (long) cookie;

        if ((op_ret == -1) && (op_errno != ENOENT)) {
                gf_log (this->name, GF_LOG_INFO,
                        "readlink of %s on %s failed (%s)",
                        impunge_local->loc.path,
                        priv->children[active_src]->name,
                        strerror (op_errno));
                goto out;
        }

        /* symlink doesn't exist on the sink */

        if ((op_ret == -1) && (op_errno == ENOENT)) {
                afr_sh_entry_impunge_symlink (impunge_frame, this,
                                              child_index, impunge_sh->linkname);
                return 0;
        }


        /* symlink exists on the sink, so check if targets match */

        if (strcmp (linkname, impunge_sh->linkname) == 0) {
                /* targets match, nothing to do */

                goto out;
        } else {
                /*
                 * Hah! Sneaky wolf in sheep's clothing!
                 */
                afr_sh_entry_impunge_symlink_unlink (impunge_frame, this,
                                                     child_index);
                return 0;
        }

out:
        LOCK (&impunge_frame->lock);
        {
                call_count = --impunge_local->call_count;
        }
        UNLOCK (&impunge_frame->lock);

        if (call_count == 0) {
                impunge_ret_child = impunge_sh->impunge_ret_child;
                AFR_STACK_DESTROY (impunge_frame);
                sh->impunge_done (frame, this, impunge_ret_child, op_ret,
                                  op_errno);
        }

        return 0;
}


int
afr_sh_entry_impunge_readlink_sink (call_frame_t *impunge_frame, xlator_t *this,
                                    int child_index)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;

        priv = this->private;
        impunge_local = impunge_frame->local;

        gf_log (this->name, GF_LOG_DEBUG,
                "checking symlink target of %s on %s",
                impunge_local->loc.path, priv->children[child_index]->name);

        STACK_WIND_COOKIE (impunge_frame, afr_sh_entry_impunge_readlink_sink_cbk,
                           (void *) (long) child_index,
                           priv->children[child_index],
                           priv->children[child_index]->fops->readlink,
                           &impunge_local->loc, 4096);

        return 0;
}


int
afr_sh_entry_impunge_readlink_cbk (call_frame_t *impunge_frame, void *cookie,
                                   xlator_t *this,
                                   int32_t op_ret, int32_t op_errno,
                                   const char *linkname, struct iatt *sbuf)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;
        afr_self_heal_t *impunge_sh = NULL;
        int              child_index = -1;
        call_frame_t    *frame = NULL;
        int              call_count = -1;
        int              active_src = -1;
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        int32_t          impunge_ret_child = 0;

        priv = this->private;
        impunge_local = impunge_frame->local;
        impunge_sh = &impunge_local->self_heal;
        frame = impunge_sh->sh_frame;
        local         = frame->local;
        sh            = &local->self_heal;
        active_src = impunge_sh->active_source;

        child_index = (long) cookie;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_INFO,
                        "readlink of %s on %s failed (%s)",
                        impunge_local->loc.path,
                        priv->children[active_src]->name,
                        strerror (op_errno));
                goto out;
        }

        impunge_sh->linkname = gf_strdup (linkname);
        afr_sh_entry_impunge_readlink_sink (impunge_frame, this, child_index);

        return 0;

out:
        LOCK (&impunge_frame->lock);
        {
                call_count = --impunge_local->call_count;
        }
        UNLOCK (&impunge_frame->lock);

        if (call_count == 0) {
                impunge_ret_child = impunge_sh->impunge_ret_child;
                AFR_STACK_DESTROY (impunge_frame);
                sh->impunge_done (frame, this, impunge_ret_child, op_ret,
                                  op_errno);
        }

        return 0;
}


int
afr_sh_entry_impunge_readlink (call_frame_t *impunge_frame, xlator_t *this,
                               int child_index, struct iatt *stbuf)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;
        afr_self_heal_t *impunge_sh = NULL;
        int              active_src = -1;

        priv = this->private;
        impunge_local = impunge_frame->local;
        impunge_sh = &impunge_local->self_heal;
        active_src = impunge_sh->active_source;
        impunge_local->cont.symlink.buf = *stbuf;

        STACK_WIND_COOKIE (impunge_frame, afr_sh_entry_impunge_readlink_cbk,
                           (void *) (long) child_index,
                           priv->children[active_src],
                           priv->children[active_src]->fops->readlink,
                           &impunge_local->loc, 4096);

        return 0;
}

int
afr_sh_entry_impunge_create (call_frame_t *impunge_frame, xlator_t *this,
                             int child_index, struct iatt *buf,
                             struct iatt *postparent)
{
        afr_local_t     *impunge_local = NULL;
        afr_self_heal_t *impunge_sh = NULL;
        afr_private_t   *priv = NULL;
        ia_type_t       type = IA_INVAL;
        int             ret = 0;
        int             active_src = 0;

        impunge_local = impunge_frame->local;
        impunge_sh = &impunge_local->self_heal;
        impunge_sh->parentbuf = *postparent;
        active_src = impunge_sh->active_source;
        impunge_local->cont.lookup.buf = *buf;
        afr_update_loc_gfids (&impunge_local->loc, buf, postparent);

        type = buf->ia_type;

        switch (type) {
        case IA_IFSOCK:
        case IA_IFREG:
        case IA_IFBLK:
        case IA_IFCHR:
        case IA_IFIFO:
                afr_sh_entry_impunge_mknod (impunge_frame, this,
                                            child_index, buf);
                break;
        case IA_IFLNK:
                afr_sh_entry_impunge_readlink (impunge_frame, this,
                                               child_index, buf);
                break;
        case IA_IFDIR:
                afr_sh_entry_impunge_mkdir (impunge_frame, this,
                                            child_index, buf);
                break;
        default:
                gf_log (this->name, GF_LOG_ERROR,
                        "%s has unknown file type on %s: 0%o",
                        impunge_local->loc.path,
                        priv->children[active_src]->name, type);
                ret = -1;
                break;
        }

        return ret;
}

int
afr_sh_entry_impunge_recreate_lookup_cbk (call_frame_t *impunge_frame,
                                          void *cookie, xlator_t *this,
                                          int32_t op_ret, int32_t op_errno,
                                          inode_t *inode, struct iatt *buf,
                                          dict_t *xattr,struct iatt *postparent)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;
        afr_local_t     *local = NULL;
        afr_self_heal_t *impunge_sh = NULL;
        afr_self_heal_t *sh = NULL;
        int              active_src = 0;
        int              child_index = 0;
        call_frame_t    *frame = NULL;
        int              call_count = 0;
        int              ret = 0;
        int32_t          impunge_ret_child = 0;

        priv = this->private;
        impunge_local = impunge_frame->local;
        impunge_sh = &impunge_local->self_heal;
        frame = impunge_sh->sh_frame;
        local = frame->local;
        sh    = &local->self_heal;

        child_index = (long) cookie;

        active_src = impunge_sh->active_source;

        if (op_ret != 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "looking up %s on %s (for %s) failed (%s)",
                        impunge_local->loc.path,
                        priv->children[active_src]->name,
                        priv->children[child_index]->name,
                        strerror (op_errno));
                goto out;
        }

        ret = afr_sh_entry_impunge_create (impunge_frame, this, child_index, buf,
                                           postparent);
        if (ret)
                goto out;

        return 0;

out:
        LOCK (&impunge_frame->lock);
        {
                call_count = --impunge_local->call_count;
        }
        UNLOCK (&impunge_frame->lock);

        if (call_count == 0) {
                impunge_ret_child = impunge_sh->impunge_ret_child;
                AFR_STACK_DESTROY (impunge_frame);
                sh->impunge_done (frame, this, impunge_ret_child, op_ret,
                                  op_errno);
        }

        return 0;
}


int
afr_sh_entry_impunge_recreate (call_frame_t *impunge_frame, xlator_t *this,
                               int child_index)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;
        afr_self_heal_t *impunge_sh = NULL;
        int              active_src = 0;


        priv = this->private;
        impunge_local = impunge_frame->local;
        impunge_sh = &impunge_local->self_heal;

        active_src = impunge_sh->active_source;

        STACK_WIND_COOKIE (impunge_frame,
                           afr_sh_entry_impunge_recreate_lookup_cbk,
                           (void *) (long) child_index,
                           priv->children[active_src],
                           priv->children[active_src]->fops->lookup,
                           &impunge_local->loc, 0);

        return 0;
}


int
afr_sh_entry_impunge_entry_cbk (call_frame_t *impunge_frame, void *cookie,
                                xlator_t *this,
                                int32_t op_ret, int32_t op_errno,
                                inode_t *inode, struct iatt *buf, dict_t *x,
                                struct iatt *postparent)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;
        afr_self_heal_t *impunge_sh = NULL;
        int              call_count = 0;
        int              child_index = 0;
        call_frame_t    *frame = NULL;
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        int32_t          impunge_ret_child = 0;

        priv = this->private;
        impunge_local = impunge_frame->local;
        impunge_sh = &impunge_local->self_heal;
        frame = impunge_sh->sh_frame;
        local         = frame->local;
        sh            = &local->self_heal;
        child_index = (long) cookie;

        if ((op_ret == -1 && op_errno == ENOENT)
            || (IA_ISLNK (impunge_sh->impunging_entry_mode))) {

                /*
                 * A symlink's target might have changed, so
                 * always go down the recreate path for them.
                 */

                /* decrease call_count in recreate-callback */

                gf_log (this->name, GF_LOG_TRACE,
                        "missing entry %s on %s",
                        impunge_local->loc.path,
                        priv->children[child_index]->name);

                afr_sh_entry_impunge_recreate (impunge_frame, this,
                                               child_index);
                return 0;
        }

        if (op_ret == 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "%s exists under %s",
                        impunge_local->loc.path,
                        priv->children[child_index]->name);

                impunge_sh->parentbuf = *postparent;
        } else {
                gf_log (this->name, GF_LOG_WARNING,
                        "looking up %s under %s failed (%s)",
                        impunge_local->loc.path,
                        priv->children[child_index]->name,
                        strerror (op_errno));
        }

        LOCK (&impunge_frame->lock);
        {
                call_count = --impunge_local->call_count;
        }
        UNLOCK (&impunge_frame->lock);

        if (call_count == 0) {
                impunge_ret_child = impunge_sh->impunge_ret_child;
                AFR_STACK_DESTROY (impunge_frame);
                sh->impunge_done (frame, this, impunge_ret_child, op_ret,
                                  op_errno);
        }

        return 0;
}


int
afr_sh_entry_impunge_entry (call_frame_t *frame, xlator_t *this,
                            gf_dirent_t *entry)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *local  = NULL;
        afr_self_heal_t *sh  = NULL;
        int              ret = -1;
        call_frame_t    *impunge_frame = NULL;
        afr_local_t     *impunge_local = NULL;
        afr_self_heal_t *impunge_sh = NULL;
        int              active_src = 0;
        int              i = 0;
        int              call_count = 0;
        int              op_errno = 0;
        int              op_ret = -1;

        priv = this->private;
        local = frame->local;
        sh = &local->self_heal;

        active_src = sh->active_source;
        sh->impunge_done = afr_sh_entry_impunge_entry_done;

        if ((strcmp (entry->d_name, ".") == 0)
            || (strcmp (entry->d_name, "..") == 0)
            || ((strcmp (local->loc.path, "/") == 0)
                && (strcmp (entry->d_name, GF_REPLICATE_TRASH_DIR) == 0))) {

                gf_log (this->name, GF_LOG_TRACE,
                        "skipping inspection of %s under %s",
                        entry->d_name, local->loc.path);
                op_ret = 0;
                goto out;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "inspecting existance of %s under %s",
                entry->d_name, local->loc.path);

        impunge_frame = copy_frame (frame);
        if (!impunge_frame) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Out of memory.");
                op_errno = ENOMEM;
                goto out;
        }

        ALLOC_OR_GOTO (impunge_local, afr_local_t, out);

        impunge_frame->local = impunge_local;
        impunge_sh = &impunge_local->self_heal;
        impunge_sh->sh_frame = frame;
        impunge_sh->active_source = active_src;
        impunge_sh->impunge_ret_child = active_src;

        impunge_sh->impunging_entry_mode =
                st_mode_from_ia (entry->d_stat.ia_prot, entry->d_stat.ia_type);

        ret = build_child_loc (this, &impunge_local->loc, &local->loc, entry->d_name);
        if (ret != 0) {
                op_errno = ENOMEM;
                goto out;
        }

        for (i = 0; i < priv->child_count; i++) {
                if (i == active_src)
                        continue;
                if (local->child_up[i] == 0)
                        continue;
                if (sh->sources[i] == 1)
                        continue;
                call_count++;
        }

        impunge_local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (i == active_src)
                        continue;
                if (local->child_up[i] == 0)
                        continue;
                if (sh->sources[i] == 1)
                        continue;

                gf_log (this->name, GF_LOG_TRACE,
                        "looking up %s on %s", impunge_local->loc.path,
                        priv->children[i]->name);

                STACK_WIND_COOKIE (impunge_frame,
                                   afr_sh_entry_impunge_entry_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->lookup,
                                   &impunge_local->loc, 0);

                if (!--call_count)
                        break;
        }

        ret = 0;
out:
        if (ret == -1)
                sh->impunge_done (frame, this, active_src, op_ret, op_errno);

        return 0;
}


int
afr_sh_entry_impunge_readdir_cbk (call_frame_t *frame, void *cookie,
                                  xlator_t *this,
                                  int32_t op_ret, int32_t op_errno,
                                  gf_dirent_t *entries)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *local  = NULL;
        afr_self_heal_t *sh  = NULL;
        gf_dirent_t     *entry = NULL;
        off_t            last_offset = 0;
        int              active_src = 0;
        int              entry_count = 0;

        priv = this->private;
        local = frame->local;
        sh = &local->self_heal;

        active_src = sh->active_source;

        if (op_ret <= 0) {
                if (op_ret < 0) {
                        gf_log (this->name, GF_LOG_INFO,
                                "readdir of %s on subvolume %s failed (%s)",
                                local->loc.path,
                                priv->children[active_src]->name,
                                strerror (op_errno));
                } else {
                        gf_log (this->name, GF_LOG_TRACE,
                                "readdir of %s on subvolume %s complete",
                                local->loc.path,
                                priv->children[active_src]->name);
                }

                afr_sh_entry_impunge_all (frame, this);
                return 0;
        }

        list_for_each_entry (entry, &entries->list, list) {
                last_offset = entry->d_off;
                entry_count++;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "readdir'ed %d entries from %s",
                entry_count, priv->children[active_src]->name);

        sh->offset = last_offset;
        local->call_count = entry_count;

        list_for_each_entry (entry, &entries->list, list) {
                afr_sh_entry_impunge_entry (frame, this, entry);
        }

        return 0;
}


int
afr_sh_entry_impunge_subvol (call_frame_t *frame, xlator_t *this,
                             int active_src)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *local  = NULL;
        afr_self_heal_t *sh  = NULL;

        priv = this->private;
        local = frame->local;
        sh = &local->self_heal;

        STACK_WIND (frame, afr_sh_entry_impunge_readdir_cbk,
                    priv->children[active_src],
                    priv->children[active_src]->fops->readdirp,
                    sh->healing_fd, sh->block_size, sh->offset);

        return 0;
}


int
afr_sh_entry_impunge_all (call_frame_t *frame, xlator_t *this)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *local  = NULL;
        afr_self_heal_t *sh  = NULL;
        int              active_src = -1;

        priv = this->private;
        local = frame->local;
        sh = &local->self_heal;

        sh->offset = 0;

        active_src = next_active_source (frame, this, sh->active_source);
        sh->active_source = active_src;

        if (sh->op_failed) {
                afr_sh_entry_finish (frame, this);
                return 0;
        }

        if (active_src == -1) {
                /* completed creating missing files on all subvolumes */
                afr_sh_entry_erase_pending (frame, this);
                return 0;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "impunging entries of %s on %s to other sinks",
                local->loc.path, priv->children[active_src]->name);

        afr_sh_entry_impunge_subvol (frame, this, active_src);

        return 0;
}


int
afr_sh_entry_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, fd_t *fd)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int              call_count = 0;
        int              child_index = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        child_index = (long) cookie;

        /* TODO: some of the open's might fail.
           In that case, modify cleanup fn to send flush on those
           fd's which are already open */

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_ERROR,
                                "opendir of %s failed on child %s (%s)",
                                local->loc.path,
                                priv->children[child_index]->name,
                                strerror (op_errno));
                        sh->op_failed = 1;
                }
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                if (sh->op_failed) {
                        afr_sh_entry_finish (frame, this);
                        return 0;
                }
                gf_log (this->name, GF_LOG_TRACE,
                        "fd for %s opened, commencing sync",
                        local->loc.path);

                sh->active_source = -1;
                afr_sh_entry_expunge_all (frame, this);
        }

        return 0;
}


int
afr_sh_entry_open (call_frame_t *frame, xlator_t *this)
{
        int i = 0;
        int call_count = 0;

        int source = -1;
        int *sources = NULL;

        fd_t *fd = NULL;

        afr_local_t *   local = NULL;
        afr_private_t * priv  = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        source  = local->self_heal.source;
        sources = local->self_heal.sources;

        sh->block_size = 65536; //131072
        sh->offset = 0;

        call_count = sh->active_sinks;
        if (source != -1)
                call_count++;

        local->call_count = call_count;

        fd = fd_create (local->loc.inode, frame->root->pid);
        sh->healing_fd = fd;

        if (source != -1) {
                gf_log (this->name, GF_LOG_TRACE,
                        "opening directory %s on subvolume %s (source)",
                        local->loc.path, priv->children[source]->name);

                /* open source */
                STACK_WIND_COOKIE (frame, afr_sh_entry_opendir_cbk,
                                   (void *) (long) source,
                                   priv->children[source],
                                   priv->children[source]->fops->opendir,
                                   &local->loc, fd);
                call_count--;
        }

        /* open sinks */
        for (i = 0; i < priv->child_count; i++) {
                if (sources[i] || !local->child_up[i])
                        continue;

                gf_log (this->name, GF_LOG_TRACE,
                        "opening directory %s on subvolume %s (sink)",
                        local->loc.path, priv->children[i]->name);

                STACK_WIND_COOKIE (frame, afr_sh_entry_opendir_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->opendir,
                                   &local->loc, fd);

                if (!--call_count)
                        break;
        }

        return 0;
}


int
afr_sh_entry_sync_prepare (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int              source = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        source = sh->source;

        afr_sh_mark_source_sinks (frame, this);
        if (source != -1)
                sh->success[source] = 1;

        if (sh->active_sinks == 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "no active sinks for self-heal on dir %s",
                        local->loc.path);
                afr_sh_entry_finish (frame, this);
                return 0;
        }
        if (source == -1 && sh->active_sinks < 2) {
                gf_log (this->name, GF_LOG_TRACE,
                        "cannot sync with 0 sources and 1 sink on dir %s",
                        local->loc.path);
                afr_sh_entry_finish (frame, this);
                return 0;
        }

        if (source != -1)
                gf_log (this->name, GF_LOG_DEBUG,
                        "self-healing directory %s from subvolume %s to "
                        "%d other",
                        local->loc.path, priv->children[source]->name,
                        sh->active_sinks);
        else
                gf_log (this->name, GF_LOG_DEBUG,
                        "no active sources for %s found. "
                        "merging all entries as a conservative decision",
                        local->loc.path);

        afr_sh_entry_open (frame, this);

        return 0;
}


int
afr_sh_entry_fix (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int              source = 0;

        int nsources = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        if (sh->forced_merge) {
                sh->source = -1;
                goto heal;
        }

        nsources = afr_build_sources (this, sh->xattr, sh->buf,
                                      sh->pending_matrix, sh->sources,
                                      sh->success_children,
                                      AFR_ENTRY_TRANSACTION);
        if (nsources == 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "No self-heal needed for %s",
                        local->loc.path);

                afr_sh_entry_finish (frame, this);
                return 0;
        }

        source = afr_sh_select_source (sh->sources, priv->child_count);

        sh->source = source;

        afr_reset_children (sh->fresh_children, priv->child_count);
        afr_get_fresh_children (sh->success_children, sh->sources,
                                sh->fresh_children, priv->child_count);
        afr_inode_set_read_ctx (this, sh->inode, sh->source,
                                sh->fresh_children);


heal:
        afr_sh_entry_sync_prepare (frame, this);

        return 0;
}



int
afr_sh_entry_lookup_cbk (call_frame_t *frame, void *cookie,
                         xlator_t *this, int32_t op_ret, int32_t op_errno,
                         inode_t *inode, struct iatt *buf, dict_t *xattr,
                         struct iatt *postparent)
{
        int             call_count  = 0;
        afr_local_t     *local = NULL;

        local = frame->local;
        afr_sh_common_lookup_resp_handler (frame, cookie, this, op_ret,
                                           op_errno, inode, buf, xattr,
                                           postparent, &local->loc);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                afr_sh_entry_fix (frame, this);
        }

        return 0;
}

int
afr_sh_post_nonblocking_entry_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_self_heal_t     *sh       = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;
        sh       = &local->self_heal;

        if (int_lock->lock_op_ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "Non Blocking entrylks "
                        "failed for %s.", local->loc.path);
                sh->op_failed = 1;
                afr_sh_entry_done (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG, "Non Blocking entrylks done "
                        "for %s. Proceeding to FOP", local->loc.path);
                afr_sh_common_lookup (frame, this, &local->loc,
                                      afr_sh_entry_lookup_cbk, _gf_false);
        }

        return 0;
}

int
afr_self_heal_entry (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local = NULL;
        afr_private_t   *priv = NULL;


        priv = this->private;
        local = frame->local;

        if (local->self_heal.need_entry_self_heal && priv->entry_self_heal) {
                afr_sh_entrylk (frame, this, &local->loc, NULL,
                                afr_sh_post_nonblocking_entry_cbk);
        } else {
                gf_log (this->name, GF_LOG_TRACE,
                        "proceeding to completion on %s",
                        local->loc.path);
                afr_sh_entry_done (frame, this);
        }

        return 0;
}
