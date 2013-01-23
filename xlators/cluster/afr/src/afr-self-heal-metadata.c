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

        local = frame->local;
        sh = &local->self_heal;

        afr_sh_reset (frame, this);
        if (IA_ISDIR (sh->type)) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "proceeding to entry check on %s",
                        local->loc.path);
                afr_self_heal_entry (frame, this);
        } else {
                gf_log (this->name, GF_LOG_DEBUG,
                        "proceeding to data check on %s",
                        local->loc.path);
                afr_self_heal_data (frame, this);
        }

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
afr_sh_metadata_fail (call_frame_t *frame, xlator_t *this)
{
        afr_local_t         *local    = NULL;
        afr_self_heal_t     *sh       = NULL;

        local    = frame->local;
        sh       = &local->self_heal;

        sh->op_failed = 1;
        afr_sh_metadata_finish (frame, this);
        return 0;
}

int
afr_sh_metadata_erase_pending_cbk (call_frame_t *frame, void *cookie,
                                   xlator_t *this, int32_t op_ret,
                                   int32_t op_errno, dict_t *xattr, dict_t *xdata)
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
         afr_sh_erase_pending (frame, this, AFR_METADATA_TRANSACTION,
                               afr_sh_metadata_erase_pending_cbk,
                               afr_sh_metadata_finish);
         return 0;
}


int
afr_sh_metadata_sync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                          int32_t op_ret, int32_t op_errno, dict_t *xdata)
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
                             struct iatt *preop, struct iatt *postop, dict_t *xdata)
{
        afr_sh_metadata_sync_cbk (frame, cookie, this, op_ret, op_errno, xdata);

        return 0;
}


int
afr_sh_metadata_xattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                           int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_sh_metadata_sync_cbk (frame, cookie, this, op_ret, op_errno, xdata);

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
                                   &local->loc, &stbuf, valid, NULL);

                call_count--;

                if (!xattr)
                        continue;

                STACK_WIND_COOKIE (frame, afr_sh_metadata_xattr_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->setxattr,
                                   &local->loc, xattr, 0, NULL);
                call_count--;
        }

        return 0;
}


int
afr_sh_metadata_getxattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                              int32_t op_ret, int32_t op_errno, dict_t *xattr,
                              dict_t *xdata)
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
        int              source = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        source = sh->source;

        afr_sh_mark_source_sinks (frame, this);
        if (sh->active_sinks == 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "no active sinks for performing self-heal on file %s",
                        local->loc.path);
                afr_sh_metadata_finish (frame, this);
                return 0;
        }

        gf_log (this->name, GF_LOG_TRACE,
                "syncing metadata of %s from subvolume %s to %d active sinks",
                local->loc.path, priv->children[source]->name,
                sh->active_sinks);

        sh->actual_sh_started = _gf_true;
        STACK_WIND (frame, afr_sh_metadata_getxattr_cbk,
                    priv->children[source],
                    priv->children[source]->fops->getxattr,
                    &local->loc, NULL, NULL);

        return 0;
}


void
afr_sh_metadata_fix (call_frame_t *frame, xlator_t *this,
                     int32_t op_ret, int32_t op_errno)
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

        if (op_ret < 0) {
                sh->op_failed = 1;
                afr_sh_set_error (sh, op_errno);
                afr_sh_metadata_finish (frame, this);
                goto out;
        }
        nsources = afr_build_sources (this, sh->xattr, sh->buf,
                                      sh->pending_matrix, sh->sources,
                                      sh->success_children,
                                      AFR_METADATA_TRANSACTION, NULL, _gf_false);
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
                afr_sh_print_split_brain_log (sh->pending_matrix, this,
                                              local->loc.path);
                afr_set_split_brain (this, sh->inode, SPB, DONT_KNOW);
                afr_sh_metadata_fail (frame, this);
                goto out;
        }

        afr_set_split_brain (this, sh->inode, NO_SPB, DONT_KNOW);
        if (nsources == 0) {
                gf_log (this->name, GF_LOG_TRACE,
                        "No self-heal needed for %s",
                        local->loc.path);

                afr_sh_metadata_finish (frame, this);
                goto out;
        }

        source = afr_sh_select_source (sh->sources, priv->child_count);

        if (source == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "No active sources found.");

                afr_sh_metadata_finish (frame, this);
                goto out;
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
                afr_reset_children (sh->fresh_children, priv->child_count);
                afr_get_fresh_children (sh->success_children, sh->sources,
                                        sh->fresh_children, priv->child_count);
                afr_inode_set_read_ctx (this, sh->inode, sh->source,
                                        sh->fresh_children);
        }

        if (sh->do_metadata_self_heal && priv->metadata_self_heal)
                afr_sh_metadata_sync_prepare (frame, this);
        else
                afr_sh_metadata_finish (frame, this);
out:
        return;
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
                gf_log (this->name, GF_LOG_DEBUG, "Non Blocking metadata "
                        "inodelks failed for %s.", local->loc.path);
                gf_log (this->name, GF_LOG_DEBUG, "Metadata self-heal "
                        "failed for %s.", local->loc.path);
                afr_sh_metadata_done (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG, "Non Blocking metadata "
                        "inodelks done for %s. Proceeding to FOP",
                        local->loc.path);
                afr_sh_common_lookup (frame, this, &local->loc,
                                      afr_sh_metadata_fix, NULL,
                                      AFR_LOOKUP_FAIL_CONFLICTS |
                                      AFR_LOOKUP_FAIL_MISSING_GFIDS,
                                      NULL);
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

        int_lock->lk_flock.l_start = LLONG_MAX - 1;
        int_lock->lk_flock.l_len   = 0;
        int_lock->lk_flock.l_type  = F_WRLCK;
        int_lock->lock_cbk         = afr_sh_metadata_post_nonblocking_inodelk_cbk;

        afr_nonblocking_inodelk (frame, this);

        return 0;
}

gf_boolean_t
afr_can_start_metadata_self_heal (afr_self_heal_t *sh, afr_private_t *priv)
{
        if (sh->force_confirm_spb)
                return _gf_true;
        if (sh->do_metadata_self_heal && priv->metadata_self_heal)
                return _gf_true;
        return _gf_false;
}

int
afr_self_heal_metadata (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local = NULL;
        afr_private_t *priv = this->private;
        afr_self_heal_t *sh = &local->self_heal;

        local = frame->local;
        sh = &local->self_heal;

        if (afr_can_start_metadata_self_heal (sh, priv)) {
                afr_sh_metadata_lock (frame, this);
        } else {
                afr_sh_metadata_done (frame, this);
        }

        return 0;
}
