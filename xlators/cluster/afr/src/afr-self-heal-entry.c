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

#define AFR_INIT_SH_FRAME_VALS(_frame, _local, _sh, _sh_frame, _sh_local, _sh_sh)\
        do {\
                _local = _frame->local;\
                _sh = &_local->self_heal;\
                _sh_frame = _sh->sh_frame;\
                _sh_local = _sh_frame->local;\
                _sh_sh    = &_sh_local->self_heal;\
        } while (0);

int
afr_sh_entry_impunge_create_file (call_frame_t *impunge_frame, xlator_t *this,
                                  int child_index);
int
afr_sh_entry_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;

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
                                int32_t op_errno, dict_t *xattr, dict_t *xdata)
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

        local = frame->local;
        sh = &local->self_heal;

        if (sh->entries_skipped) {
                afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
                goto out;
        }
        afr_sh_erase_pending (frame, this, AFR_ENTRY_TRANSACTION,
                              afr_sh_entry_erase_pending_cbk,
                              afr_sh_entry_finish);
        return 0;
out:
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
afr_sh_entry_impunge_all (call_frame_t *frame, xlator_t *this);

int
afr_sh_entry_impunge_subvol (call_frame_t *frame, xlator_t *this);

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
                                         struct iatt *preop, struct iatt *postop,
                                         dict_t *xdata)
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
                                 struct iatt *postparent, dict_t *xdata)
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

        STACK_WIND_COOKIE (expunge_frame, afr_sh_entry_expunge_parent_setattr_cbk,
                           (void *) (long) active_src,
                           priv->children[active_src],
                           priv->children[active_src]->fops->setattr,
                           &expunge_sh->parent_loc,
                           &expunge_sh->parentbuf,
                           valid, NULL);

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
                           &expunge_local->loc, 0, NULL);

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
                           &expunge_local->loc, 1, NULL);

        return 0;
}


int
afr_sh_entry_expunge_remove (call_frame_t *expunge_frame, xlator_t *this,
                             int active_src, struct iatt *buf,
                             struct iatt *parentbuf)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *expunge_local = NULL;
        afr_self_heal_t *expunge_sh = NULL;
        call_frame_t    *frame = NULL;
        int              type = 0;
        afr_self_heal_t *sh            = NULL;
        afr_local_t     *local         = NULL;
        loc_t           *loc           = NULL;

        priv = this->private;
        expunge_local = expunge_frame->local;
        expunge_sh = &expunge_local->self_heal;
        frame = expunge_sh->sh_frame;
        local         = frame->local;
        sh            = &local->self_heal;
        loc           = &expunge_local->loc;

        type = buf->ia_type;
        if (loc->parent && uuid_is_null (loc->parent->gfid))
                uuid_copy (loc->pargfid, parentbuf->ia_gfid);

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

        afr_sh_entry_expunge_remove (expunge_frame, this, active_src, buf,
                                     postparent);

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
                           &expunge_local->loc, NULL);

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
                        "Entry %s is missing on %s and deleting from "
                        "replica's other bricks",
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

static gf_boolean_t
can_skip_entry_self_heal (char *name, loc_t *parent_loc)
{
        if (strcmp (name, ".") == 0) {
                return _gf_true;
        } else if (strcmp (name, "..") == 0) {
                return _gf_true;
        } else if (loc_is_root (parent_loc) &&
                   (strcmp (name, GF_REPLICATE_TRASH_DIR) == 0)) {
                return _gf_true;
        }
        return _gf_false;
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
        if (can_skip_entry_self_heal (name, &local->loc)) {
                op_ret = 0;
                goto out;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "inspecting existence of %s under %s",
                name, local->loc.path);

        expunge_frame = copy_frame (frame);
        if (!expunge_frame) {
                op_errno = ENOMEM;
                goto out;
        }

        AFR_LOCAL_ALLOC_OR_GOTO (expunge_local, out);

        expunge_frame->local = expunge_local;
        expunge_sh = &expunge_local->self_heal;
        expunge_sh->sh_frame = frame;
        expunge_sh->active_source = active_src;
        expunge_sh->entrybuf = entry->d_stat;
        loc_copy (&expunge_sh->parent_loc, &local->loc);

        ret = afr_build_child_loc (this, &expunge_local->loc, &local->loc,
                                   name);
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
                           &expunge_local->loc, NULL);

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
                                  gf_dirent_t *entries, dict_t *xdata)
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
                    sh->healing_fd, sh->block_size, sh->offset, NULL);

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

        if (is_self_heal_failed (sh, AFR_CHECK_SPECIFIC)) {
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
                                 int32_t op_ret, int32_t op_errno)
{
        int              call_count = 0;
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;
        if (op_ret < 0)
                sh->entries_skipped = _gf_true;
        call_count = afr_frame_return (frame);
        if (call_count == 0)
                afr_sh_entry_impunge_subvol (frame, this);

        return 0;
}

void
afr_sh_entry_call_impunge_done (call_frame_t *impunge_frame, xlator_t *this,
                                int32_t op_ret, int32_t op_errno)
{
        afr_local_t     *impunge_local = NULL;
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_self_heal_t *impunge_sh = NULL;
        call_frame_t    *frame = NULL;

        AFR_INIT_SH_FRAME_VALS (impunge_frame, impunge_local, impunge_sh,
                                frame, local, sh);

        AFR_STACK_DESTROY (impunge_frame);
        sh->impunge_done (frame, this, op_ret, op_errno);
}

int
afr_sh_entry_impunge_setattr_cbk (call_frame_t *impunge_frame, void *cookie,
                                  xlator_t *this,
                                  int32_t op_ret, int32_t op_errno,
                                  struct iatt *preop, struct iatt *postop,
                                  dict_t *xdata)
{
        int              call_count = 0;
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;
        int              child_index = 0;

        priv = this->private;
        impunge_local = impunge_frame->local;
        child_index = (long) cookie;

        if (op_ret == 0) {
                gf_log (this->name, GF_LOG_DEBUG,
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

        call_count = afr_frame_return (impunge_frame);
        if (call_count == 0) {
                afr_sh_entry_call_impunge_done (impunge_frame, this,
                                                0, op_errno);
        }

        return 0;
}

int
afr_sh_entry_impunge_parent_setattr_cbk (call_frame_t *setattr_frame,
                                         void *cookie, xlator_t *this,
                                         int32_t op_ret, int32_t op_errno,
                                         struct iatt *preop, struct iatt *postop,
                                         dict_t *xdata)
{
        int             call_count = 0;
        afr_local_t     *setattr_local = NULL;

        setattr_local = setattr_frame->local;
        if (op_ret != 0) {
                gf_log (this->name, GF_LOG_INFO,
                        "setattr on parent directory (%s) failed: %s",
                        setattr_local->loc.path, strerror (op_errno));
        }

        call_count = afr_frame_return (setattr_frame);
        if (call_count == 0)
                AFR_STACK_DESTROY (setattr_frame);
        return 0;
}

int
afr_sh_entry_impunge_setattr (call_frame_t *impunge_frame, xlator_t *this)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;
        afr_local_t     *setattr_local = NULL;
        afr_self_heal_t *impunge_sh = NULL;
        call_frame_t    *setattr_frame = NULL;
        int32_t          valid = 0;
        int32_t          op_errno = 0;
        int              child_index = 0;
        int              call_count = 0;
        int              i = 0;

        priv          = this->private;
        impunge_local = impunge_frame->local;
        impunge_sh    = &impunge_local->self_heal;

        gf_log (this->name, GF_LOG_DEBUG,
                "setting ownership of %s on %s to %d/%d",
                impunge_local->loc.path,
                priv->children[child_index]->name,
                impunge_sh->entrybuf.ia_uid,
                impunge_sh->entrybuf.ia_gid);

        setattr_frame = copy_frame (impunge_frame);
        if (!setattr_frame) {
                op_errno = ENOMEM;
                goto out;
        }
        AFR_LOCAL_ALLOC_OR_GOTO (setattr_frame->local, out);
        setattr_local = setattr_frame->local;
        call_count = afr_errno_count (NULL, impunge_sh->child_errno,
                                      priv->child_count, 0);
        loc_copy (&setattr_local->loc, &impunge_sh->parent_loc);
        impunge_local->call_count = call_count;
        setattr_local->call_count = call_count;
        for (i = 0; i < priv->child_count; i++) {
                if (impunge_sh->child_errno[i])
                        continue;
                valid         = GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME;
                STACK_WIND_COOKIE (setattr_frame,
                                   afr_sh_entry_impunge_parent_setattr_cbk,
                                   (void *) (long) i, priv->children[i],
                                   priv->children[i]->fops->setattr,
                                   &setattr_local->loc,
                                   &impunge_sh->parentbuf, valid, NULL);

                valid = GF_SET_ATTR_UID   | GF_SET_ATTR_GID |
                        GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME;
                STACK_WIND_COOKIE (impunge_frame,
                                   afr_sh_entry_impunge_setattr_cbk,
                                   (void *) (long) i, priv->children[i],
                                   priv->children[i]->fops->setattr,
                                   &impunge_local->loc,
                                   &impunge_sh->entrybuf, valid, NULL);
                call_count--;
        }
        GF_ASSERT (!call_count);
        return 0;
out:
        if (setattr_frame)
                AFR_STACK_DESTROY (setattr_frame);
        afr_sh_entry_call_impunge_done (impunge_frame, this, 0, op_errno);
        return 0;
}

int
afr_sh_entry_impunge_xattrop_cbk (call_frame_t *impunge_frame, void *cookie,
                                  xlator_t *this,
                                  int32_t op_ret, int32_t op_errno,
                                  dict_t *xattr, dict_t *xdata)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;
        int              child_index = 0;
        int              call_count = -1;

        priv          = this->private;
        impunge_local = impunge_frame->local;

        child_index = (long) cookie;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_INFO,
                        "%s: failed to perform xattrop on %s (%s)",
                        impunge_local->loc.path,
                        priv->children[child_index]->name, strerror (op_errno));

                        LOCK (&impunge_frame->lock);
                        {
                                impunge_local->op_ret = -1;
                                impunge_local->op_errno = op_errno;
                        }
                        UNLOCK (&impunge_frame->lock);
        }

        call_count = afr_frame_return (impunge_frame);

        if (call_count == 0) {
                if (impunge_local->op_ret == 0) {
                        afr_sh_entry_impunge_setattr (impunge_frame, this);
                } else {
                        afr_sh_entry_call_impunge_done (impunge_frame, this,
                                                -1, impunge_local->op_errno);
                }
        }
        return 0;
}

int
afr_sh_entry_impunge_perform_xattrop (call_frame_t *impunge_frame,
                                      xlator_t *this)
{
        int              active_src       = 0;
        dict_t          *xattr            = NULL;
        afr_private_t   *priv             = NULL;
        afr_local_t     *impunge_local    = NULL;
        afr_self_heal_t *impunge_sh       = NULL;
        int32_t         op_errno          = 0;
        int32_t         call_count        = 0;
        int32_t         i                 = 0;


        priv = this->private;
        impunge_local = impunge_frame->local;
        impunge_sh = &impunge_local->self_heal;
        active_src = impunge_sh->active_source;
        impunge_local->op_ret = 0;

        afr_prepare_new_entry_pending_matrix (impunge_local->pending,
                                              afr_is_errno_unset,
                                              impunge_sh->child_errno,
                                              &impunge_sh->entrybuf,
                                              priv->child_count);
        xattr = dict_new ();
        if (!xattr) {
                op_errno = ENOMEM;
                goto out;
        }

        afr_set_pending_dict (priv, xattr, impunge_local->pending, active_src,
                              LOCAL_LAST);

        for (i = 0; i < priv->child_count; i++) {
                if ((impunge_sh->child_errno[i] == EEXIST) &&
                    (impunge_local->child_up[i] == 1))

                        call_count++;
        }

        impunge_local->call_count  = call_count;

        for (i = 0; i < priv->child_count; i++) {

                if ((impunge_sh->child_errno[i] == EEXIST)
                    && (impunge_local->child_up[i] == 1)) {


                        STACK_WIND_COOKIE (impunge_frame,
                                           afr_sh_entry_impunge_xattrop_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->xattrop,
                                           &impunge_local->loc,
                                           GF_XATTROP_ADD_ARRAY, xattr, NULL);
                        if (!--call_count)
                                break;
                }
        }

        if (xattr)
                dict_unref (xattr);
        return 0;
out:
        afr_sh_entry_call_impunge_done (impunge_frame, this,
                                        -1, op_errno);
        return 0;
}

int
afr_sh_entry_impunge_newfile_cbk (call_frame_t *impunge_frame, void *cookie,
                                  xlator_t *this,
                                  int32_t op_ret, int32_t op_errno,
                                  inode_t *inode, struct iatt *stbuf,
                                  struct iatt *preparent,
                                  struct iatt *postparent, dict_t *xdata)
{
        int              call_count       = 0;
        afr_private_t   *priv             = NULL;
        afr_local_t     *impunge_local    = NULL;
        afr_self_heal_t *impunge_sh       = NULL;
        int              child_index      = 0;

        priv = this->private;
        impunge_local = impunge_frame->local;
        impunge_sh = &impunge_local->self_heal;

        child_index = (long) cookie;

        if (op_ret == -1) {
                impunge_sh->child_errno[child_index] = op_errno;
                gf_log (this->name, GF_LOG_ERROR,
                        "creation of %s on %s failed (%s)",
                        impunge_local->loc.path,
                        priv->children[child_index]->name,
                        strerror (op_errno));
        } else {
                impunge_sh->child_errno[child_index] = 0;
        }

        call_count = afr_frame_return (impunge_frame);
        if (call_count == 0) {
                if (!afr_errno_count (NULL, impunge_sh->child_errno,
                                      priv->child_count, 0)) {
                        // new_file creation failed every where
                        afr_sh_entry_call_impunge_done (impunge_frame, this,
                                                        -1, op_errno);
                        goto out;
                }
                afr_sh_entry_impunge_perform_xattrop (impunge_frame, this);
        }
out:
        return 0;
}

int
afr_sh_entry_impunge_hardlink_cbk (call_frame_t *impunge_frame, void *cookie,
                                   xlator_t *this, int32_t op_ret,
                                   int32_t op_errno, inode_t *inode,
                                   struct iatt *buf, struct iatt *preparent,
                                   struct iatt *postparent, dict_t *xdata)
{
        int              call_count        = 0;
        afr_local_t     *impunge_local = NULL;
        afr_self_heal_t *impunge_sh  = NULL;

        impunge_local = impunge_frame->local;
        impunge_sh = &impunge_local->self_heal;

        if (IA_IFLNK == impunge_sh->entrybuf.ia_type) {
                //For symlinks impunge is attempted un-conditionally
                //So the file can already exist.
                if ((op_ret < 0) && (op_errno == EEXIST))
                        op_ret = 0;
        }

        call_count = afr_frame_return (impunge_frame);
        if (call_count == 0)
                afr_sh_entry_call_impunge_done (impunge_frame, this,
                                                op_ret, op_errno);

        return 0;
}

int
afr_sh_entry_impunge_hardlink (call_frame_t *impunge_frame, xlator_t *this,
                               int child_index)
{
        afr_private_t   *priv          = NULL;
        afr_local_t     *impunge_local = NULL;
        afr_self_heal_t *impunge_sh  = NULL;
        loc_t           *loc           = NULL;
        struct iatt     *buf           = NULL;
        loc_t            oldloc        = {0};

        priv = this->private;
        impunge_local = impunge_frame->local;
        impunge_sh = &impunge_local->self_heal;
        loc = &impunge_local->loc;
        buf = &impunge_sh->entrybuf;

        oldloc.inode = inode_ref (loc->inode);
        uuid_copy (oldloc.gfid, buf->ia_gfid);
        gf_log (this->name, GF_LOG_DEBUG, "linking missing file %s on %s",
                loc->path, priv->children[child_index]->name);

        STACK_WIND_COOKIE (impunge_frame, afr_sh_entry_impunge_hardlink_cbk,
                           (void *) (long) child_index,
                           priv->children[child_index],
                           priv->children[child_index]->fops->link,
                           &oldloc, loc, NULL);
        loc_wipe (&oldloc);

        return 0;
}

int
afr_sh_nameless_lookup_cbk (call_frame_t *impunge_frame, void *cookie,
                            xlator_t *this,
                            int32_t op_ret, int32_t op_errno, inode_t *inode,
                            struct iatt *buf, dict_t *xattr,
                            struct iatt *postparent)
{
        if (op_ret < 0) {
                 afr_sh_entry_impunge_create_file (impunge_frame, this,
                                                   (long)cookie);
        } else {
                afr_sh_entry_impunge_hardlink (impunge_frame, this,
                                               (long)cookie);
        }
        return 0;
}

int
afr_sh_entry_impunge_check_hardlink (call_frame_t *impunge_frame,
                                     xlator_t *this,
                                     int child_index, struct iatt *stbuf)
{
        afr_private_t   *priv          = NULL;
        call_frame_t    *frame             = NULL;
        afr_local_t     *impunge_local     = NULL;
        afr_local_t     *local             = NULL;
        afr_self_heal_t *impunge_sh        = NULL;
        afr_self_heal_t *sh                = NULL;
        loc_t           *loc           = NULL;
        dict_t          *xattr_req     = NULL;
        loc_t            oldloc        = {0};
        int              ret           = -1;

        priv = this->private;
        AFR_INIT_SH_FRAME_VALS (impunge_frame, impunge_local, impunge_sh,
                                frame, local, sh);
        loc = &impunge_local->loc;

        xattr_req = dict_new ();
        if (!xattr_req)
                goto out;
        oldloc.inode = inode_ref (loc->inode);
        uuid_copy (oldloc.gfid, stbuf->ia_gfid);

        STACK_WIND_COOKIE (impunge_frame, afr_sh_nameless_lookup_cbk,
                           (void *) (long) child_index,
                           priv->children[child_index],
                           priv->children[child_index]->fops->lookup,
                           &oldloc, xattr_req);
        ret = 0;
out:
        if (xattr_req)
                dict_unref (xattr_req);
        loc_wipe (&oldloc);
        if (ret)
                sh->impunge_done (frame, this, -1, ENOMEM);
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

        GF_ASSERT (!uuid_is_null (stbuf->ia_gfid));
        ret = afr_set_dict_gfid (dict, stbuf->ia_gfid);
        if (ret)
                gf_log (this->name, GF_LOG_INFO, "%s: gfid set failed",
                        impunge_local->loc.path);

        /*
         * Reason for adding GLUSTERFS_INTERNAL_FOP_KEY :
         *
         * Problem:
         * While a brick is down in a replica pair, lets say the user creates
         * one file(file-A) and a hard link to that file(h-file-A). After the
         * brick comes back up, entry self-heal is attempted on parent dir of
         * these two files. As part of readdir in self-heal it reads both the
         * entries file-A and h-file-A for both of them it does name less lookup
         * to check if there are any hardlinks already present in the
         * destination brick. It finds that there are no hard links already
         * present for files file-A, h-file-A. Self-heal does mknods for both
         * file-A and h-file-A. This leads to file-A and h-file-A not being
         * hardlinks anymore.
         *
         * Fix: (More like shrinking of race-window, the race itself is still
         * present in posix-mknod).
         * If mknod comes with the presence of GLUSTERFS_INTERNAL_FOP_KEY then
         * posix_mknod checks if there are already any gfid-links and does
         * link() instead of mknod. There still can be a race where two
         * posix_mknods same gfid see that
         * gfid-link file is not present and proceeds with mknods and result in
         * two different files with same gfid.
         */
        ret = dict_set_str (dict, GLUSTERFS_INTERNAL_FOP_KEY, "yes");
        if (ret)
                gf_log (this->name, GF_LOG_INFO, "%s: %s set failed",
                        impunge_local->loc.path, GLUSTERFS_INTERNAL_FOP_KEY);

        STACK_WIND_COOKIE (impunge_frame, afr_sh_entry_impunge_newfile_cbk,
                           (void *) (long) child_index,
                           priv->children[child_index],
                           priv->children[child_index]->fops->mknod,
                           &impunge_local->loc,
                           st_mode_from_ia (stbuf->ia_prot, stbuf->ia_type),
                           makedev (ia_major (stbuf->ia_rdev),
                                    ia_minor (stbuf->ia_rdev)), 0, dict);

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

        GF_ASSERT (!uuid_is_null (stbuf->ia_gfid));
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
                           0, dict);

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

        priv = this->private;
        impunge_local = impunge_frame->local;

        buf = &impunge_local->cont.dir_fop.buf;

        dict = dict_new ();
        if (!dict) {
                afr_sh_entry_call_impunge_done (impunge_frame, this,
                                                -1, ENOMEM);
                goto out;
        }

        GF_ASSERT (!uuid_is_null (buf->ia_gfid));
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
                           linkname, &impunge_local->loc, 0, dict);

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
                                         struct iatt *postparent, dict_t *xdata)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;
        afr_self_heal_t *impunge_sh = NULL;
        int              child_index = -1;
        int              call_count = -1;

        priv          = this->private;
        impunge_local = impunge_frame->local;
        impunge_sh    = &impunge_local->self_heal;

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

        if (call_count == 0)
                afr_sh_entry_call_impunge_done (impunge_frame, this,
                                                op_ret, op_errno);

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
                           &impunge_local->loc, 0, NULL);

        return 0;
}


int
afr_sh_entry_impunge_readlink_sink_cbk (call_frame_t *impunge_frame, void *cookie,
                                        xlator_t *this,
                                        int32_t op_ret, int32_t op_errno,
                                        const char *linkname, struct iatt *sbuf, dict_t *xdata)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;
        afr_self_heal_t *impunge_sh = NULL;
        int              child_index = -1;
        int              call_count = -1;
        int              active_src = -1;

        priv          = this->private;
        impunge_local = impunge_frame->local;
        impunge_sh    = &impunge_local->self_heal;
        active_src    = impunge_sh->active_source;

        child_index = (long) cookie;

        if ((op_ret == -1) && (!afr_inode_missing(op_errno))) {
                gf_log (this->name, GF_LOG_INFO,
                        "readlink of %s on %s failed (%s)",
                        impunge_local->loc.path,
                        priv->children[active_src]->name,
                        strerror (op_errno));
                goto out;
        }

        /* symlink doesn't exist on the sink */

        if ((op_ret == -1) && (afr_inode_missing(op_errno))) {
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

        if (call_count == 0)
                afr_sh_entry_call_impunge_done (impunge_frame, this,
                                                op_ret, op_errno);

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
                           &impunge_local->loc, 4096, NULL);

        return 0;
}


int
afr_sh_entry_impunge_readlink_cbk (call_frame_t *impunge_frame, void *cookie,
                                   xlator_t *this,
                                   int32_t op_ret, int32_t op_errno,
                                   const char *linkname, struct iatt *sbuf, dict_t *xdata)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;
        afr_self_heal_t *impunge_sh = NULL;
        int              child_index = -1;
        int              call_count = -1;
        int              active_src = -1;

        priv = this->private;
        impunge_local = impunge_frame->local;
        impunge_sh = &impunge_local->self_heal;
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

        if (call_count == 0)
                afr_sh_entry_call_impunge_done (impunge_frame, this,
                                                op_ret, op_errno);

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
        impunge_local->cont.dir_fop.buf = *stbuf;

        STACK_WIND_COOKIE (impunge_frame, afr_sh_entry_impunge_readlink_cbk,
                           (void *) (long) child_index,
                           priv->children[active_src],
                           priv->children[active_src]->fops->readlink,
                           &impunge_local->loc, 4096, NULL);

        return 0;
}

int
afr_sh_entry_impunge_create (call_frame_t *impunge_frame, xlator_t *this,
                             int child_index)
{
        call_frame_t    *frame             = NULL;
        afr_local_t     *impunge_local     = NULL;
        afr_local_t     *local             = NULL;
        afr_self_heal_t *impunge_sh        = NULL;
        afr_self_heal_t *sh                = NULL;
        afr_private_t   *priv = NULL;
        ia_type_t       type = IA_INVAL;
        int             active_src = 0;
        struct iatt     *buf = NULL;

        AFR_INIT_SH_FRAME_VALS (impunge_frame, impunge_local, impunge_sh,
                                frame, local, sh);
        active_src = impunge_sh->active_source;
        afr_update_loc_gfids (&impunge_local->loc, &impunge_sh->entrybuf,
                              &impunge_sh->parentbuf);

        buf = &impunge_sh->entrybuf;
        type = buf->ia_type;

        switch (type) {
        case IA_IFSOCK:
        case IA_IFREG:
        case IA_IFBLK:
        case IA_IFCHR:
        case IA_IFIFO:
        case IA_IFLNK:
                afr_sh_entry_impunge_check_hardlink (impunge_frame, this,
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
                sh->impunge_done (frame, this, -1, EINVAL);
                break;
        }

        return 0;
}

int
afr_sh_entry_impunge_create_file (call_frame_t *impunge_frame, xlator_t *this,
                                  int child_index)
{
        call_frame_t    *frame             = NULL;
        afr_local_t     *impunge_local     = NULL;
        afr_local_t     *local             = NULL;
        afr_self_heal_t *impunge_sh        = NULL;
        afr_self_heal_t *sh                = NULL;
        afr_private_t   *priv = NULL;
        ia_type_t       type = IA_INVAL;
        int             active_src = 0;
        struct iatt     *buf = NULL;

        AFR_INIT_SH_FRAME_VALS (impunge_frame, impunge_local, impunge_sh,
                                frame, local, sh);
        active_src = impunge_sh->active_source;
        buf = &impunge_sh->entrybuf;
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
        default:
                gf_log (this->name, GF_LOG_ERROR,
                        "%s has unknown file type on %s: 0%o",
                        impunge_local->loc.path,
                        priv->children[active_src]->name, type);
                sh->impunge_done (frame, this, -1, EINVAL);
                break;
        }

        return 0;
}

gf_boolean_t
afr_sh_need_recreate (afr_self_heal_t *impunge_sh, unsigned int child,
                      unsigned int child_count)
{
        gf_boolean_t    recreate = _gf_false;

        GF_ASSERT (impunge_sh->child_errno);

        if (child == impunge_sh->active_source)
                goto out;

        if (IA_IFLNK == impunge_sh->entrybuf.ia_type) {
                recreate = _gf_true;
                goto out;
        }

        if (impunge_sh->child_errno[child] == ENOENT)
                recreate = _gf_true;
out:
        return recreate;
}

unsigned int
afr_sh_recreate_count (afr_self_heal_t *impunge_sh, int *sources,
                       unsigned int child_count)
{
        int             count = 0;
        int             i = 0;

        for (i = 0; i < child_count; i++) {
                if (afr_sh_need_recreate (impunge_sh, i, child_count))
                        count++;
        }

        return count;
}

int
afr_sh_entry_call_impunge_recreate (call_frame_t *impunge_frame,
                                    xlator_t *this)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;
        afr_self_heal_t *impunge_sh = NULL;
        call_frame_t    *frame = NULL;
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        unsigned int     recreate_count = 0;
        int              i = 0;
        int              active_src = 0;

        priv          = this->private;
        AFR_INIT_SH_FRAME_VALS (impunge_frame, impunge_local, impunge_sh,
                                frame, local, sh);
        active_src = impunge_sh->active_source;
        impunge_sh->entrybuf = impunge_sh->buf[active_src];
        impunge_sh->parentbuf = impunge_sh->parentbufs[active_src];
        recreate_count = afr_sh_recreate_count (impunge_sh, sh->sources,
                                                priv->child_count);
        if (!recreate_count) {
                afr_sh_entry_call_impunge_done (impunge_frame, this, 0, 0);
                goto out;
        }
        impunge_local->call_count = recreate_count;
        for (i = 0; i < priv->child_count; i++) {
                if (!impunge_local->child_up[i]) {
                        impunge_sh->child_errno[i] = ENOTCONN;
                        continue;
                }
                if (!afr_sh_need_recreate (impunge_sh, i, priv->child_count)) {
                        impunge_sh->child_errno[i] = EEXIST;
                        continue;
                }
        }
        for (i = 0; i < priv->child_count; i++) {
                if (!afr_sh_need_recreate (impunge_sh, i, priv->child_count))
                        continue;
                (void)afr_sh_entry_impunge_create (impunge_frame, this, i);
                recreate_count--;
        }
        GF_ASSERT (!recreate_count);
out:
        return 0;
}

void
afr_sh_entry_common_lookup_done (call_frame_t *impunge_frame, xlator_t *this,
                                 int32_t op_ret, int32_t op_errno)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *impunge_local = NULL;
        afr_self_heal_t *impunge_sh = NULL;
        call_frame_t    *frame = NULL;
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        unsigned int     gfid_miss_count = 0;
        unsigned int     children_up_count = 0;
        uuid_t           gfid = {0};
        int              active_src = 0;

        priv          = this->private;
        AFR_INIT_SH_FRAME_VALS (impunge_frame, impunge_local, impunge_sh,
                                frame, local, sh);
        active_src    = impunge_sh->active_source;

        if (op_ret < 0)
                goto done;
        if (impunge_sh->child_errno[active_src]) {
                op_ret = -1;
                op_errno = impunge_sh->child_errno[active_src];
                goto done;
        }

        gfid_miss_count = afr_gfid_missing_count (this->name,
                                                  impunge_sh->success_children,
                                                  impunge_sh->buf, priv->child_count,
                                                  impunge_local->loc.path);
        children_up_count = afr_up_children_count (impunge_local->child_up,
                                                   priv->child_count);
        if ((gfid_miss_count == children_up_count) &&
            (children_up_count < priv->child_count)) {
                op_ret = -1;
                op_errno = ENODATA;
                gf_log (this->name, GF_LOG_ERROR, "Not all children are up, "
                        "gfid should not be assigned in this state for %s",
                        impunge_local->loc.path);
                goto done;
        }

        if (gfid_miss_count) {
                afr_update_gfid_from_iatts (gfid, impunge_sh->buf,
                                            impunge_sh->success_children,
                                            priv->child_count);
                if (uuid_is_null (gfid)) {
                        sh->entries_skipped = _gf_true;
                        gf_log (this->name, GF_LOG_INFO, "%s: Skipping entry "
                                "self-heal because of gfid absence",
                                impunge_local->loc.path);
                        goto done;
                }
                afr_sh_common_lookup (impunge_frame, this, &impunge_local->loc,
                                      afr_sh_entry_common_lookup_done, gfid,
                                      AFR_LOOKUP_FAIL_CONFLICTS |
                                      AFR_LOOKUP_FAIL_MISSING_GFIDS,
                                      NULL);
        } else {
                afr_sh_entry_call_impunge_recreate (impunge_frame, this);
        }
        return;
done:
        afr_sh_entry_call_impunge_done (impunge_frame, this,
                                        op_ret, op_errno);
        return;
}

int
afr_sh_entry_impunge_entry (call_frame_t *frame, xlator_t *this,
                            gf_dirent_t *entry)
{
        afr_local_t     *local  = NULL;
        afr_self_heal_t *sh  = NULL;
        afr_self_heal_t *impunge_sh  = NULL;
        int              ret = -1;
        call_frame_t    *impunge_frame = NULL;
        afr_local_t     *impunge_local = NULL;
        int              active_src = 0;
        int              op_errno = 0;
        int              op_ret = -1;

        local = frame->local;
        sh = &local->self_heal;

        active_src = sh->active_source;
        sh->impunge_done = afr_sh_entry_impunge_entry_done;

        if (can_skip_entry_self_heal (entry->d_name, &local->loc)) {
                op_ret = 0;
                goto out;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "inspecting existence of %s under %s",
                entry->d_name, local->loc.path);

        ret = afr_impunge_frame_create (frame, this, active_src,
                                        &impunge_frame);
        if (ret) {
                op_errno = -ret;
                goto out;
        }

        impunge_local = impunge_frame->local;
        impunge_sh = &impunge_local->self_heal;
        ret = afr_build_child_loc (this, &impunge_local->loc, &local->loc,
                                   entry->d_name);
        loc_copy (&impunge_sh->parent_loc, &local->loc);
        if (ret != 0) {
                op_errno = ENOMEM;
                goto out;
        }

        afr_sh_common_lookup (impunge_frame, this, &impunge_local->loc,
                              afr_sh_entry_common_lookup_done, NULL,
                              AFR_LOOKUP_FAIL_CONFLICTS, NULL);

        op_ret = 0;
out:
        if (ret) {
                if (impunge_frame)
                        AFR_STACK_DESTROY (impunge_frame);
                sh->impunge_done (frame, this, op_ret, op_errno);
        }

        return 0;
}


int
afr_sh_entry_impunge_readdir_cbk (call_frame_t *frame, void *cookie,
                                  xlator_t *this,
                                  int32_t op_ret, int32_t op_errno,
                                  gf_dirent_t *entries, dict_t *xdata)
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
                        afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
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

        gf_log (this->name, GF_LOG_DEBUG,
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
afr_sh_entry_impunge_subvol (call_frame_t *frame, xlator_t *this)
{
        afr_private_t   *priv = NULL;
        afr_local_t     *local  = NULL;
        afr_self_heal_t *sh  = NULL;
        int32_t         active_src = 0;

        priv = this->private;
        local = frame->local;
        sh = &local->self_heal;
        active_src = sh->active_source;
        gf_log (this->name, GF_LOG_DEBUG, "%s: readdir from offset %zd",
                local->loc.path, sh->offset);

        STACK_WIND (frame, afr_sh_entry_impunge_readdir_cbk,
                    priv->children[active_src],
                    priv->children[active_src]->fops->readdirp,
                    sh->healing_fd, sh->block_size, sh->offset, NULL);

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

        if (is_self_heal_failed (sh, AFR_CHECK_SPECIFIC)) {
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

        afr_sh_entry_impunge_subvol (frame, this);

        return 0;
}


int
afr_sh_entry_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, fd_t *fd, dict_t *xdata)
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
                        afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
                }
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                if (is_self_heal_failed (sh, AFR_CHECK_SPECIFIC)) {
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

        sh->block_size = priv->sh_readdir_size;
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
                                   &local->loc, fd, NULL);
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
                                   &local->loc, fd, NULL);

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

        sh->actual_sh_started = _gf_true;
        afr_set_self_heal_status (sh, AFR_SELF_HEAL_SYNC_BEGIN);
        afr_sh_entry_open (frame, this);

        return 0;
}


void
afr_sh_entry_fix (call_frame_t *frame, xlator_t *this,
                  int32_t op_ret, int32_t op_errno)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int              source = 0;
        int              nsources = 0;
        int32_t          subvol_status = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        if (op_ret < 0) {
                afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
                afr_sh_set_error (sh, op_errno);
                afr_sh_entry_finish (frame, this);
                goto out;
        }

        if (sh->forced_merge) {
                sh->source = -1;
                goto heal;
        }

        nsources = afr_build_sources (this, sh->xattr, sh->buf,
                                      sh->pending_matrix, sh->sources,
                                      sh->success_children,
                                      AFR_ENTRY_TRANSACTION, &subvol_status,
                                      _gf_true);
        if ((subvol_status & ALL_FOOLS) ||
            (subvol_status & SPLIT_BRAIN)) {
                gf_log (this->name, GF_LOG_INFO, "%s: Performing conservative "
                        "merge", local->loc.path);
                source = -1;
                memset (sh->sources, 0,
                        sizeof (*sh->sources) * priv->child_count);
        } else if (nsources == 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "No self-heal needed for %s",
                        local->loc.path);

                afr_sh_entry_finish (frame, this);
                return;
        } else {
                source = afr_sh_select_source (sh->sources, priv->child_count);
        }

        sh->source = source;

        afr_reset_children (sh->fresh_children, priv->child_count);
        afr_get_fresh_children (sh->success_children, sh->sources,
                                sh->fresh_children, priv->child_count);
        if (sh->source >= 0)
                afr_inode_set_read_ctx (this, sh->inode, sh->source,
                                        sh->fresh_children);

heal:
        afr_sh_entry_sync_prepare (frame, this);
out:
        return;
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
                afr_set_self_heal_status (sh, AFR_SELF_HEAL_FAILED);
                afr_sh_entry_done (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG, "Non Blocking entrylks done "
                        "for %s. Proceeding to FOP", local->loc.path);
                afr_sh_common_lookup (frame, this, &local->loc,
                                      afr_sh_entry_fix, NULL,
                                      AFR_LOOKUP_FAIL_CONFLICTS |
                                      AFR_LOOKUP_FAIL_MISSING_GFIDS,
                                      NULL);
        }

        return 0;
}

int
afr_self_heal_entry (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_private_t   *priv = NULL;
        afr_self_heal_t *sh = NULL;

        priv = this->private;
        local = frame->local;
        sh = &local->self_heal;

        sh->sh_type_in_action = AFR_SELF_HEAL_ENTRY;

        if (local->self_heal.do_entry_self_heal && priv->entry_self_heal) {
                afr_set_self_heal_status (sh, AFR_SELF_HEAL_STARTED);
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
