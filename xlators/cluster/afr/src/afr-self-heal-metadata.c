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
afr_sh_metadata_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int              i = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

//      memset (sh->child_errno, 0, sizeof (int) * priv->child_count);
        memset (sh->buf, 0, sizeof (struct iatt) * priv->child_count);
        memset (sh->success, 0, sizeof (int) * priv->child_count);

/*         for (i = 0; i < priv->child_count; i++) { */
/*                 sh->locked_nodes[i] = 1; */
/*         } */

        for (i = 0; i < priv->child_count; i++) {
                if (sh->xattr[i])
                        dict_unref (sh->xattr[i]);
                sh->xattr[i] = NULL;
        }

        if (local->govinda_gOvinda) {
                gf_log (this->name, GF_LOG_INFO,
                        "split-brain detected, aborting selfheal of %s",
                        local->loc.path);
                sh->op_failed = 1;
                sh->completion_cbk (frame, this);
        } else {
                if (IA_ISREG (sh->type)) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "proceeding to data check on %s",
                                local->loc.path);
                        afr_self_heal_data (frame, this);
                        return 0;
                }

                if (IA_ISDIR (sh->type)) {
                        gf_log (this->name, GF_LOG_DEBUG,
                                "proceeding to entry check on %s",
                                local->loc.path);
                        afr_self_heal_entry (frame, this);
                        return 0;
                }
                sh->completion_cbk (frame, this);
        }

        return 0;
}


int
afr_sh_metadata_unlck_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno)
{
        int               call_count = 0;

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                afr_sh_metadata_done (frame, this);

        return 0;
}

int
afr_sh_inode_unlock (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        int_lock->lock_cbk = afr_sh_metadata_done;
        afr_unlock (frame, this);

        return 0;
}

int
afr_sh_metadata_finish (call_frame_t *frame, xlator_t *this)
{
        afr_sh_inode_unlock (frame, this);

        return 0;
}


int
afr_sh_metadata_erase_pending_cbk (call_frame_t *frame, void *cookie,
                                   xlator_t *this, int32_t op_ret,
                                   int32_t op_errno, dict_t *xattr)
{
        afr_local_t     *local     = NULL;
        int             call_count = 0;
        long            i          = 0;
        afr_self_heal_t *sh        = NULL;
        afr_private_t   *priv      = NULL;

        local = frame->local;
        priv  = this->private;
        sh = &local->self_heal;
        i = (long)cookie;

        if ((!IA_ISREG (sh->buf[sh->source].ia_type)) &&
            (!IA_ISDIR (sh->buf[sh->source].ia_type))) {
                afr_children_add_child (sh->fresh_children, i,
                                        priv->child_count);
        }
        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                if ((!IA_ISREG (sh->buf[sh->source].ia_type)) &&
                    (!IA_ISDIR (sh->buf[sh->source].ia_type))) {
                        afr_inode_set_read_ctx (this, sh->inode, sh->source,
                                                sh->fresh_children);
                }
                afr_sh_metadata_finish (frame, this);
        }

        return 0;
}


int
afr_sh_metadata_erase_pending (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int              call_count = 0;
        int              i = 0;
        dict_t          **erase_xattr = NULL;


        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        afr_sh_pending_to_delta (priv, sh->xattr, sh->delta_matrix,
                                 sh->success, priv->child_count,
                                 AFR_METADATA_TRANSACTION);

        erase_xattr = GF_CALLOC (sizeof (*erase_xattr), priv->child_count,
                                 gf_afr_mt_dict_t);
        if (!erase_xattr)
                return -ENOMEM;

        for (i = 0; i < priv->child_count; i++) {
                if (sh->xattr[i]) {
                        call_count++;

                        erase_xattr[i] = get_new_dict();
                        dict_ref (erase_xattr[i]);
                }
        }

        afr_sh_delta_to_xattr (priv, sh->delta_matrix, erase_xattr,
                               priv->child_count, AFR_METADATA_TRANSACTION);

        local->call_count = call_count;

        if (call_count == 0) {
                gf_log (this->name, GF_LOG_INFO,
                        "metadata of %s not healed on any subvolume",
                        local->loc.path);

                afr_sh_metadata_finish (frame, this);
        }

        for (i = 0; i < priv->child_count; i++) {
                if (!erase_xattr[i])
                        continue;

                gf_log (this->name, GF_LOG_TRACE,
                        "erasing pending flags from %s on %s",
                        local->loc.path, priv->children[i]->name);

                STACK_WIND_COOKIE (frame, afr_sh_metadata_erase_pending_cbk,
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

        return 0;
}


int
afr_sh_metadata_sync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno)
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

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_INFO,
                                "setting attributes failed for %s on %s (%s)",
                                local->loc.path,
                                priv->children[child_index]->name,
                                strerror (op_errno));

                        sh->success[child_index] = 0;
                }
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                afr_sh_metadata_erase_pending (frame, this);

        return 0;
}


int
afr_sh_metadata_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                             int32_t op_ret, int32_t op_errno,
                             struct iatt *preop, struct iatt *postop)
{
        afr_sh_metadata_sync_cbk (frame, cookie, this, op_ret, op_errno);

        return 0;
}


int
afr_sh_metadata_xattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno)
{
        afr_sh_metadata_sync_cbk (frame, cookie, this, op_ret, op_errno);

        return 0;
}


int
afr_sh_metadata_sync (call_frame_t *frame, xlator_t *this, dict_t *xattr)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int              source = 0;
        int              active_sinks = 0;
        int              call_count = 0;
        int              i = 0;

        struct iatt      stbuf = {0,};
        int32_t          valid = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        source = sh->source;
        active_sinks = sh->active_sinks;

        /*
         * 2 calls per sink - setattr, setxattr
         */
        if (xattr)
                call_count = active_sinks * 2;
        else
                call_count = active_sinks;

        local->call_count = call_count;

        stbuf.ia_atime = sh->buf[source].ia_atime;
        stbuf.ia_atime_nsec = sh->buf[source].ia_atime_nsec;
        stbuf.ia_mtime = sh->buf[source].ia_mtime;
        stbuf.ia_mtime_nsec = sh->buf[source].ia_mtime_nsec;

        stbuf.ia_uid = sh->buf[source].ia_uid;
        stbuf.ia_gid = sh->buf[source].ia_gid;

        stbuf.ia_type = sh->buf[source].ia_type;
        stbuf.ia_prot = sh->buf[source].ia_prot;

        valid = GF_SET_ATTR_MODE  |
                GF_SET_ATTR_UID   | GF_SET_ATTR_GID |
                GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME;

        for (i = 0; i < priv->child_count; i++) {
                if (call_count == 0) {
                        break;
                }
                if (sh->sources[i] || !local->child_up[i])
                        continue;

                gf_log (this->name, GF_LOG_DEBUG,
                        "self-healing metadata of %s from %s to %s",
                        local->loc.path, priv->children[source]->name,
                        priv->children[i]->name);

                STACK_WIND_COOKIE (frame, afr_sh_metadata_setattr_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->setattr,
                                   &local->loc, &stbuf, valid);

                call_count--;

                if (!xattr)
                        continue;

                STACK_WIND_COOKIE (frame, afr_sh_metadata_xattr_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->setxattr,
                                   &local->loc, xattr, 0);
                call_count--;
        }

        return 0;
}


int
afr_sh_metadata_getxattr_cbk (call_frame_t *frame, void *cookie,
                              xlator_t *this,
                              int32_t op_ret, int32_t op_errno, dict_t *xattr)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int              source = 0;

        int i;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        source = sh->source;

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "getxattr of %s failed on subvolume %s (%s). proceeding without xattr",
                        local->loc.path, priv->children[source]->name,
                        strerror (op_errno));

                afr_sh_metadata_sync (frame, this, NULL);
        } else {
                for (i = 0; i < priv->child_count; i++) {
                        dict_del (xattr, priv->pending_key[i]);
                }

                afr_sh_metadata_sync (frame, this, xattr);
        }

        return 0;
}


int
afr_sh_metadata_sync_prepare (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int              active_sinks = 0;
        int              source = 0;
        int              i = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        source = sh->source;

        for (i = 0; i < priv->child_count; i++) {
                if (sh->sources[i] == 0 && local->child_up[i] == 1) {
                        active_sinks++;
                        sh->success[i] = 1;
                }
        }
        sh->success[source] = 1;

        if (active_sinks == 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no active sinks for performing self-heal on file %s",
                        local->loc.path);
                afr_sh_metadata_finish (frame, this);
                return 0;
        }
        sh->active_sinks = active_sinks;

        gf_log (this->name, GF_LOG_TRACE,
                "syncing metadata of %s from subvolume %s to %d active sinks",
                local->loc.path, priv->children[source]->name, active_sinks);

        STACK_WIND (frame, afr_sh_metadata_getxattr_cbk,
                    priv->children[source],
                    priv->children[source]->fops->getxattr,
                    &local->loc, NULL);

        return 0;
}


int
afr_sh_metadata_fix (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int              nsources = 0;
        int              source = 0;
        int              i = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        nsources = afr_build_sources (this, sh->xattr, sh->buf,
                                      sh->pending_matrix, sh->sources,
                                      sh->success_children,
                                      AFR_METADATA_TRANSACTION);
        if (nsources == 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "No self-heal needed for %s",
                        local->loc.path);

                afr_sh_metadata_finish (frame, this);
                return 0;
        }

        if ((nsources == -1)
            && (priv->favorite_child != -1)
            && (sh->child_errno[priv->favorite_child] == 0)) {

                gf_log (this->name, GF_LOG_WARNING,
                        "Picking favorite child %s as authentic source to resolve conflicting metadata of %s",
                        priv->children[priv->favorite_child]->name,
                        local->loc.path);

                sh->sources[priv->favorite_child] = 1;

                nsources = afr_sh_source_count (sh->sources,
                                                priv->child_count);
        }

        if (nsources == -1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Unable to self-heal permissions/ownership of '%s' "
                        "(possible split-brain). Please fix the file on "
                        "all backend volumes", local->loc.path);

                local->govinda_gOvinda = 1;

                afr_sh_metadata_finish (frame, this);
                return 0;
        }

        source = afr_sh_select_source (sh->sources, priv->child_count);

        if (source == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "No active sources found.");

                afr_sh_metadata_finish (frame, this);
                return 0;
        }

        sh->source = source;

        /* detect changes not visible through pending flags -- JIC */
        for (i = 0; i < priv->child_count; i++) {
                if (i == source || sh->child_errno[i])
                        continue;

                if (PERMISSION_DIFFERS (&sh->buf[i], &sh->buf[source]))
                        sh->sources[i] = 0;

                if (OWNERSHIP_DIFFERS (&sh->buf[i], &sh->buf[source]))
                        sh->sources[i] = 0;
        }

        if ((!IA_ISREG (sh->buf[source].ia_type)) &&
            (!IA_ISDIR (sh->buf[source].ia_type))) {
                afr_reset_children (sh->fresh_children,
                                          priv->child_count);
                afr_get_fresh_children (sh->success_children, sh->sources,
                                        sh->fresh_children, priv->child_count);
                afr_inode_set_read_ctx (this, sh->inode, sh->source,
                                        sh->fresh_children);
        }

        afr_sh_metadata_sync_prepare (frame, this);

        return 0;
}


int
afr_sh_metadata_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                            int32_t op_ret, int32_t op_errno,
                            inode_t *inode, struct iatt *buf, dict_t *xattr,
                            struct iatt *postparent)
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

        LOCK (&frame->lock);
        {
                if (op_ret == 0) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "path %s on subvolume %s is of mode 0%o",
                                local->loc.path,
                                priv->children[child_index]->name,
                                buf->ia_type);

                        sh->buf[child_index] = *buf;
                        if (xattr)
                                sh->xattr[child_index] = dict_ref (xattr);
                        sh->success_children[sh->success_count] = child_index;
                        sh->success_count++;
                } else {
                        gf_log (this->name, GF_LOG_INFO,
                                "path %s on subvolume %s => -1 (%s)",
                                local->loc.path,
                                priv->children[child_index]->name,
                                strerror (op_errno));

                        sh->child_errno[child_index] = op_errno;
                }
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                afr_sh_metadata_fix (frame, this);

        return 0;
}

int
afr_sh_metadata_post_nonblocking_inodelk_cbk (call_frame_t *frame,
                                              xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        if (int_lock->lock_op_ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "Non Blocking metadata "
                        "inodelks failed for %s.", local->loc.path);
                gf_log (this->name, GF_LOG_ERROR, "Metadata self-heal "
                        "failed for %s.", local->loc.path);
                afr_sh_metadata_done (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG, "Non Blocking metadata "
                        "inodelks done for %s. Proceeding to FOP",
                        local->loc.path);
                afr_sh_common_lookup (frame, this, &local->loc,
                                      afr_sh_metadata_lookup_cbk, _gf_false);
        }

        return 0;
}

int
afr_sh_metadata_lock (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        int_lock->transaction_lk_type = AFR_SELFHEAL_LK;
        int_lock->selfheal_lk_type    = AFR_METADATA_SELF_HEAL_LK;

        afr_set_lock_number (frame, this);

        int_lock->lk_flock.l_start = 0;
        int_lock->lk_flock.l_len   = 0;
        int_lock->lk_flock.l_type  = F_WRLCK;
        int_lock->lock_cbk         = afr_sh_metadata_post_nonblocking_inodelk_cbk;

        afr_nonblocking_inodelk (frame, this);

        return 0;
}


int
afr_self_heal_metadata (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local = NULL;
        afr_private_t *priv = this->private;


        local = frame->local;

        if (local->self_heal.need_metadata_self_heal && priv->metadata_self_heal) {
                afr_sh_metadata_lock (frame, this);
        } else {
                afr_sh_metadata_done (frame, this);
        }

        return 0;
}
