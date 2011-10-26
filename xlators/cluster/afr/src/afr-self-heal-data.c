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
#include "afr-self-heal-algorithm.h"


extern int
sh_loop_finish (call_frame_t *loop_frame, xlator_t *this);

int
afr_post_sh_big_lock_success (call_frame_t *frame, xlator_t *this);

int
afr_post_sh_big_lock_failure (call_frame_t *frame, xlator_t *this);

int
afr_sh_data_finish (call_frame_t *frame, xlator_t *this);

int
afr_sh_data_fxattrop (call_frame_t *frame, xlator_t *this,
                      afr_fxattrop_cbk_t fxattrop_cbk);

int
afr_post_sh_data_fxattrop_cbk (call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret, int32_t op_errno,
                               dict_t *xattr);

int
afr_sh_data_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;

        sh->completion_cbk (frame, this);

        return 0;
}


int
afr_sh_data_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno)
{
        afr_local_t   *local       = NULL;
        afr_private_t *priv        = NULL;
        int            call_count  = 0;
        int            child_index = (long) cookie;

        local = frame->local;
        priv = this->private;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_INFO,
                                "flush failed on %s on subvolume %s: %s",
                                local->loc.path, priv->children[child_index]->name,
                                strerror (op_errno));
                }
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                afr_sh_data_done (frame, this);
        }

        return 0;
}

int
afr_sh_data_close (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local      = NULL;
        afr_private_t   *priv       = NULL;
        afr_self_heal_t *sh         = NULL;
        int              i          = 0;
        int              call_count = 0;

        local = frame->local;
        sh    = &local->self_heal;
        priv  = this->private;

        call_count        = afr_set_elem_count_get (sh->success,
                                                    priv->child_count);
        local->call_count = call_count;

        if (call_count == 0) {
                afr_sh_data_done (frame, this);
                return 0;
        }

        for (i = 0; i < priv->child_count; i++) {
                if (!sh->success[i])
                        continue;
                gf_log (this->name, GF_LOG_DEBUG,
                        "closing fd of %s on %s",
                        local->loc.path, priv->children[i]->name);

                STACK_WIND_COOKIE (frame, afr_sh_data_flush_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->flush,
                                   sh->healing_fd);

                if (!--call_count)
                        break;
        }

        return 0;
}

int
afr_sh_data_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                         struct iatt *statpost)
{

        afr_local_t   *local       = NULL;
        afr_private_t *priv        = NULL;
        int            call_count  = 0;
        int            child_index = (long) cookie;

        local = frame->local;
        priv = this->private;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_INFO,
                                "setattr failed on %s on subvolume %s: %s",
                                local->loc.path, priv->children[child_index]->name,
                                strerror (op_errno));
                }
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                afr_sh_data_finish (frame, this);
        }

        return 0;
}

int
afr_sh_data_setattr (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local      = NULL;
        afr_private_t   *priv       = NULL;
        afr_self_heal_t *sh         = NULL;
        int              i          = 0;
        int              call_count = 0;
        int              source     = 0;
        int32_t          valid      = 0;
        struct iatt      stbuf      = {0,};

        local = frame->local;
        sh    = &local->self_heal;
        priv  = this->private;

        source = sh->source;

        valid |= (GF_SET_ATTR_ATIME | GF_SET_ATTR_MTIME);

        stbuf.ia_atime = sh->buf[source].ia_atime;
        stbuf.ia_atime_nsec = sh->buf[source].ia_atime_nsec;
        stbuf.ia_mtime = sh->buf[source].ia_mtime;
        stbuf.ia_mtime_nsec = sh->buf[source].ia_mtime_nsec;

        call_count        = afr_set_elem_count_get (sh->success,
                                                    priv->child_count);
        local->call_count = call_count;

        if (call_count == 0) {
                GF_ASSERT (0);
                afr_sh_data_finish (frame, this);
                return 0;
        }

        for (i = 0; i < priv->child_count; i++) {
                if (!sh->success[i])
                        continue;

                STACK_WIND_COOKIE (frame, afr_sh_data_setattr_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->setattr,
                                   &local->loc, &stbuf, valid);

                if (!--call_count)
                        break;
        }

        return 0;
}

int
afr_sh_data_setattr_fstat_cbk (call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret, int32_t op_errno,
                               struct iatt *buf)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        int child_index = (long) cookie;

        local = frame->local;
        sh = &local->self_heal;

        GF_ASSERT (sh->source == child_index);
        if (op_ret != -1)
                sh->buf[child_index] = *buf;
        afr_sh_data_setattr (frame, this);

        return 0;
}

/*
 * If there are any writes after the self-heal is triggered then the
 * stbuf stored in local->self_heal.buf[] will be invalid so we do one more
 * stat on the source and then set the [am]times
 */
int
afr_sh_set_timestamps (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local      = NULL;
        afr_private_t   *priv       = NULL;
        afr_self_heal_t *sh         = NULL;

        local = frame->local;
        sh    = &local->self_heal;
        priv  = this->private;

        STACK_WIND_COOKIE (frame, afr_sh_data_setattr_fstat_cbk,
                           (void *) (long) sh->source,
                           priv->children[sh->source],
                           priv->children[sh->source]->fops->fstat,
                           sh->healing_fd);
        return 0;
}

//Fun fact, lock_cbk is being used for both lock & unlock
int
afr_sh_data_unlock (call_frame_t *frame, xlator_t *this,
                    afr_lock_cbk_t lock_cbk)
{
        afr_local_t         *local    = NULL;
        afr_internal_lock_t *int_lock = NULL;
        afr_self_heal_t     *sh       = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;
        sh       = &local->self_heal;

        GF_ASSERT (sh->data_lock_held);

        sh->data_lock_held = _gf_false;
        int_lock->lock_cbk = lock_cbk;
        afr_unlock (frame, this);

        return 0;
}

int
afr_sh_data_finish (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;

        gf_log (this->name, GF_LOG_DEBUG,
                "finishing data selfheal of %s", local->loc.path);

        if (sh->data_lock_held)
                afr_sh_data_unlock (frame, this, afr_sh_data_close);
        else
                afr_sh_data_close (frame, this);

        return 0;
}

int
afr_sh_data_fail (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;

        gf_log (this->name, GF_LOG_DEBUG,
                "finishing failed data selfheal of %s", local->loc.path);

        sh->op_failed = 1;
        if (sh->data_lock_held)
                afr_sh_data_unlock (frame, this, afr_sh_data_close);
        else
                afr_sh_data_close (frame, this);
        return 0;
}

int
afr_sh_data_erase_pending_cbk (call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret,
                               int32_t op_errno, dict_t *xattr)
{
        int             call_count = 0;
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                local = frame->local;
                sh = &local->self_heal;
                if (!IA_ISREG (sh->type)) {
                        afr_sh_data_finish (frame, this);
                        goto out;
                }
                GF_ASSERT (sh->old_loop_frame);
                afr_sh_data_lock (frame, this, 0, 0,
                                  afr_post_sh_big_lock_success,
                                  afr_post_sh_big_lock_failure);
        }
out:
        return 0;
}

int
afr_sh_data_erase_pending (call_frame_t *frame, xlator_t *this)
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

        afr_sh_pending_to_delta (priv, sh->xattr, sh->delta_matrix, sh->success,
                                 priv->child_count, AFR_DATA_TRANSACTION);
        gf_log (this->name, GF_LOG_DEBUG, "Delta matrix for: %"PRIu64,
                frame->root->lk_owner);
        afr_sh_print_pending_matrix (sh->delta_matrix, this);

        erase_xattr = GF_CALLOC (sizeof (*erase_xattr), priv->child_count,
                                 gf_afr_mt_dict_t);

        for (i = 0; i < priv->child_count; i++) {
                if (sh->xattr[i]) {
                        call_count++;

                        erase_xattr[i] = get_new_dict();
                        dict_ref (erase_xattr[i]);
                }
        }

        afr_sh_delta_to_xattr (priv, sh->delta_matrix, erase_xattr,
                               priv->child_count, AFR_DATA_TRANSACTION);

        GF_ASSERT (call_count);
        local->call_count = call_count;
        for (i = 0; i < priv->child_count; i++) {
                if (!erase_xattr[i])
                        continue;

                gf_log (this->name, GF_LOG_DEBUG,
                        "erasing pending flags from %s on %s",
                        local->loc.path, priv->children[i]->name);

                STACK_WIND_COOKIE (frame, afr_sh_data_erase_pending_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->fxattrop,
                                   sh->healing_fd,
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


static struct afr_sh_algorithm *
sh_algo_from_name (xlator_t *this, char *name)
{
        int i = 0;

        while (afr_self_heal_algorithms[i].name) {
                if (!strcmp (name, afr_self_heal_algorithms[i].name)) {
                        return &afr_self_heal_algorithms[i];
                }

                i++;
        }

        return NULL;
}


static int
sh_zero_byte_files_exist (afr_self_heal_t *sh, int child_count)
{
        int i;
        int ret = 0;

        for (i = 0; i < child_count; i++) {
                if (sh->buf[i].ia_size == 0) {
                        ret = 1;
                        break;
                }
        }

        return ret;
}


struct afr_sh_algorithm *
afr_sh_data_pick_algo (call_frame_t *frame, xlator_t *this)
{
        afr_private_t *           priv  = NULL;
        struct afr_sh_algorithm * algo  = NULL;
        afr_local_t *             local = NULL;
        afr_self_heal_t *         sh    = NULL;

        priv  = this->private;
        local = frame->local;
        sh    = &local->self_heal;
        algo  = sh_algo_from_name (this, priv->data_self_heal_algorithm);

        if (algo == NULL) {
                /* option not set, so fall back on heuristics */

                if ((local->enoent_count != 0)
                    || sh_zero_byte_files_exist (sh, priv->child_count)
                    || (sh->file_size <= (priv->data_self_heal_window_size *
                                          this->ctx->page_size))) {

                        /*
                         * If the file does not exist on one of the subvolumes,
                         * or a zero-byte file exists (created by entry self-heal)
                         * the entire content has to be copied anyway, so there
                         * is no benefit from using the "diff" algorithm.
                         *
                         * If the file size is about the same as page size,
                         * the entire file can be read and written with a few
                         * (pipelined) STACK_WINDs, which will be faster
                         * than "diff" which has to read checksums and then
                         * read and write.
                         */

                        algo = sh_algo_from_name (this, "full");

                } else {
                        algo = sh_algo_from_name (this, "diff");
                }
        }

        return algo;
}


int
afr_sh_data_sync_prepare (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        struct afr_sh_algorithm *sh_algo = NULL;

        local = frame->local;
        sh = &local->self_heal;

        sh->algo_completion_cbk = afr_sh_data_erase_pending;
        sh->algo_abort_cbk      = afr_sh_data_fail;

        sh_algo = afr_sh_data_pick_algo (frame, this);

        sh_algo->fn (frame, this);

        return 0;
}

int
afr_sh_data_trim_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf)
{
        afr_private_t * priv = NULL;
        afr_local_t * local  = NULL;
        int              call_count = 0;
        int              child_index = 0;

        priv = this->private;
        local = frame->local;

        child_index = (long) cookie;

        LOCK (&frame->lock);
        {
                if (op_ret == -1)
                        gf_log (this->name, GF_LOG_INFO,
                                "ftruncate of %s on subvolume %s failed (%s)",
                                local->loc.path,
                                priv->children[child_index]->name,
                                strerror (op_errno));
                else
                        gf_log (this->name, GF_LOG_DEBUG,
                                "ftruncate of %s on subvolume %s completed",
                                local->loc.path,
                                priv->children[child_index]->name);
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                afr_sh_data_sync_prepare (frame, this);

        return 0;
}


int
afr_sh_data_trim_sinks (call_frame_t *frame, xlator_t *this)
{
        afr_private_t * priv = NULL;
        afr_local_t * local  = NULL;
        afr_self_heal_t *sh  = NULL;
        int             *sources = NULL;
        int              call_count = 0;
        int              i = 0;


        priv = this->private;
        local = frame->local;
        sh = &local->self_heal;

        sources = sh->sources;
        call_count = sh->active_sinks;

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (sources[i] || !local->child_up[i])
                        continue;

                STACK_WIND_COOKIE (frame, afr_sh_data_trim_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->ftruncate,
                                   sh->healing_fd, sh->file_size);

                if (!--call_count)
                        break;
        }

        return 0;
}

int
afr_sh_inode_set_read_ctx (afr_self_heal_t *sh, xlator_t *this)
{
        afr_private_t   *priv = NULL;
        int             ret = 0;

        priv = this->private;
        sh->source = afr_sh_select_source (sh->sources, priv->child_count);
        if (sh->source < 0) {
                ret = -1;
                goto out;
        }

        afr_reset_children (sh->fresh_children, priv->child_count);
        afr_get_fresh_children (sh->success_children, sh->sources,
                                sh->fresh_children, priv->child_count);
        afr_inode_set_read_ctx (this, sh->inode, sh->source,
                                sh->fresh_children);
out:
        return ret;
}

int
afr_sh_data_fix (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local      = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int              nsources = 0;
        int              source = 0;
        int              i = 0;
        int              ret = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        gf_log (this->name, GF_LOG_DEBUG, "Pending matrix for: %"PRIu64,
                frame->root->lk_owner);
        nsources = afr_build_sources (this, sh->xattr, sh->buf, sh->pending_matrix,
                                      sh->sources, sh->success_children,
                                      AFR_DATA_TRANSACTION);
        if (nsources == 0) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "No self-heal needed for %s",
                        local->loc.path);

                afr_sh_data_finish (frame, this);
                return 0;
        }

        if ((nsources == -1)
            && (priv->favorite_child != -1)
            && (sh->child_errno[priv->favorite_child] == 0)) {

                gf_log (this->name, GF_LOG_DEBUG,
                        "Picking favorite child %s as authentic source to "
                        "resolve conflicting data of %s",
                        priv->children[priv->favorite_child]->name,
                        local->loc.path);

                sh->sources[priv->favorite_child] = 1;

                nsources = afr_sh_source_count (sh->sources,
                                                priv->child_count);
        }

        if (nsources == -1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "Unable to self-heal contents of '%s' (possible "
                        "split-brain). Please delete the file from all but "
                        "the preferred subvolume.", local->loc.path);

                local->govinda_gOvinda = 1;

                afr_sh_data_fail (frame, this);
                return 0;
        }

        ret = afr_sh_inode_set_read_ctx (sh, this);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "No active sources found.");

                afr_sh_data_fail (frame, this);
                return 0;
        }

        source     = sh->source;
        sh->block_size = this->ctx->page_size;
        sh->file_size  = sh->buf[source].ia_size;

        if (FILE_HAS_HOLES (&sh->buf[source]))
                sh->file_has_holes = 1;

        /* detect changes not visible through pending flags -- JIC */
        for (i = 0; i < priv->child_count; i++) {
                if (i == source || sh->child_errno[i])
                        continue;

                if (SIZE_DIFFERS (&sh->buf[i], &sh->buf[source]))
                        sh->sources[i] = 0;
        }

        if (sh->background && sh->unwind) {
                sh->unwind (sh->orig_frame, this, sh->op_ret, sh->op_errno);
                sh->unwound = _gf_true;
        }

        afr_sh_mark_source_sinks (frame, this);
        if (sh->active_sinks == 0) {
                gf_log (this->name, GF_LOG_INFO,
                        "no active sinks for performing self-heal on file %s",
                        local->loc.path);
                afr_sh_data_finish (frame, this);
                return 0;
        }

        gf_log (this->name, GF_LOG_DEBUG,
                "self-healing file %s from subvolume %s to %d other",
                local->loc.path, priv->children[sh->source]->name,
                sh->active_sinks);
        afr_sh_data_trim_sinks (frame, this);

        return 0;
}

static void
afr_destroy_pending_matrix (int32_t **pending_matrix, int32_t child_count)
{
        int             i = 0;
        GF_ASSERT (child_count > 0);
        if (pending_matrix) {
                for (i = 0; i < child_count; i++) {
                        if (pending_matrix[i])
                                GF_FREE (pending_matrix[i]);
                }
                GF_FREE (pending_matrix);
        }
}

static int32_t**
afr_create_pending_matrix (int32_t child_count)
{
        gf_boolean_t            cleanup = _gf_false;
        int32_t                 **pending_matrix = NULL;
        int                     i = 0;

        GF_ASSERT (child_count > 0);

        pending_matrix = GF_CALLOC (sizeof (*pending_matrix), child_count,
                                    gf_afr_mt_int32_t);
        if (NULL == pending_matrix)
                goto out;
        for (i = 0; i < child_count; i++) {
                pending_matrix[i] = GF_CALLOC (sizeof (**pending_matrix),
                                               child_count,
                                               gf_afr_mt_int32_t);
                if (NULL == pending_matrix[i]) {
                        cleanup = _gf_true;
                        goto out;
                }
        }
out:
        if (_gf_true == cleanup) {
                afr_destroy_pending_matrix (pending_matrix, child_count);
                pending_matrix = NULL;
        }
        return pending_matrix;
}

int
afr_lookup_select_read_child_by_txn_type (xlator_t *this, afr_local_t *local,
                                          dict_t **xattr,
                                          afr_transaction_type txn_type)
{
        afr_private_t            *priv      = NULL;
        int                      read_child = -1;
        int32_t                  **pending_matrix = NULL;
        int32_t                  *sources         = NULL;
        int32_t                  *success_children   = NULL;
        struct iatt              *bufs            = NULL;
        int32_t                  nsources         = 0;
        int32_t                  prev_read_child  = -1;
        int32_t                  config_read_child = -1;

        priv = this->private;
        bufs = local->cont.lookup.bufs;
        success_children = local->cont.lookup.success_children;

        pending_matrix = afr_create_pending_matrix (priv->child_count);
        if (NULL == pending_matrix)
                goto out;

        sources = local->cont.lookup.sources;
        memset (sources, 0, sizeof (*sources) * priv->child_count);

        nsources = afr_build_sources (this, xattr, bufs, pending_matrix,
                                      sources, success_children, txn_type);
        if (nsources < 0)
                goto out;

        prev_read_child = local->read_child_index;
        config_read_child = priv->read_child;
        read_child = afr_select_read_child_from_policy (success_children,
                                                        priv->child_count,
                                                        prev_read_child,
                                                        config_read_child,
                                                        sources);
out:
        afr_destroy_pending_matrix (pending_matrix, priv->child_count);
        gf_log (this->name, GF_LOG_DEBUG, "returning read_child: %d",
                read_child);
        return read_child;
}

int
afr_sh_data_special_file_fix (call_frame_t *frame, xlator_t *this)
{
        afr_private_t   *priv = NULL;
        afr_self_heal_t *sh = NULL;
        afr_local_t     *local = NULL;
        int             i = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        for (i = 0; i < priv->child_count ; i++)
                if (1 == local->child_up[i])
                        sh->success[i] = 1;

        afr_sh_data_erase_pending (frame, this);

        return 0;
}

int
afr_sh_data_fstat_cbk (call_frame_t *frame, void *cookie,
                       xlator_t *this, int32_t op_ret, int32_t op_errno,
                       struct iatt *buf)
{
        afr_private_t   *priv  = NULL;
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        int call_count  = -1;
        int child_index = (long) cookie;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        LOCK (&frame->lock);
        {
                if (op_ret != -1) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "fstat of %s on %s succeeded",
                                local->loc.path,
                                priv->children[child_index]->name);

                        sh->buf[child_index] = *buf;
                        sh->success_children[sh->success_count] = child_index;
                        sh->success_count++;
                }
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                /* Previous versions of glusterfs might have set
                 * the pending data xattrs which need to be erased
                 */
                if (IA_ISREG (buf->ia_type))
                        afr_sh_data_fix (frame, this);
                else
                        afr_sh_data_special_file_fix (frame, this);

        }

        return 0;
}


int
afr_sh_data_fstat (call_frame_t *frame, xlator_t *this)
{
        afr_self_heal_t *sh    = NULL;
        afr_local_t     *local = NULL;
        afr_private_t   *priv  = NULL;
        int call_count = 0;
        int i = 0;

        priv  = this->private;
        local = frame->local;
        sh    = &local->self_heal;

        call_count = afr_up_children_count (local->child_up,
                                            priv->child_count);

        local->call_count = call_count;

        afr_reset_children (sh->success_children, priv->child_count);
        sh->success_count = 0;
        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_sh_data_fstat_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->fstat,
                                           sh->healing_fd);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}

void
afr_sh_common_fxattrop_resp_handler (call_frame_t *frame, void *cookie,
                                     xlator_t *this, int32_t op_ret,
                                     int32_t op_errno, dict_t *xattr)
{
        afr_private_t   *priv  = NULL;
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        int child_index = (long) cookie;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        LOCK (&frame->lock);
        {
                if (op_ret != -1) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "fxattrop of %s on %s succeeded",
                                local->loc.path,
                                priv->children[child_index]->name);

                        sh->xattr[child_index] = dict_ref (xattr);
                        sh->success_children[sh->success_count] = child_index;
                        sh->success_count++;
                }
        }
        UNLOCK (&frame->lock);
}

int
afr_post_sh_data_fxattrop_cbk (call_frame_t *frame, void *cookie,
                               xlator_t *this, int32_t op_ret, int32_t op_errno,
                               dict_t *xattr)
{
        int             call_count  = -1;
        int             ret = 0;
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;

        afr_sh_common_fxattrop_resp_handler (frame, cookie, this, op_ret,
                                             op_errno, xattr);

        local = frame->local;
        sh = &local->self_heal;
        call_count = afr_frame_return (frame);
        if (call_count == 0) {
                (void) afr_build_sources (this, sh->xattr, NULL,
                                          sh->pending_matrix,
                                          sh->sources, sh->success_children,
                                          AFR_DATA_TRANSACTION);
                ret = afr_sh_inode_set_read_ctx (sh, this);
                if (ret)
                        afr_sh_data_fail (frame, this);
                else
                        afr_sh_set_timestamps (frame, this);
        }

        return 0;
}

int
afr_sh_data_fxattrop_cbk (call_frame_t *frame, void *cookie,
                          xlator_t *this, int32_t op_ret, int32_t op_errno,
                          dict_t *xattr)
{
        int call_count  = -1;

        afr_sh_common_fxattrop_resp_handler (frame, cookie, this, op_ret,
                                             op_errno, xattr);

        call_count = afr_frame_return (frame);
        if (call_count == 0) {
                afr_sh_data_fstat (frame, this);
        }

        return 0;
}


int
afr_sh_data_fxattrop (call_frame_t *frame, xlator_t *this,
                      afr_fxattrop_cbk_t fxattrop_cbk)
{
        afr_self_heal_t *sh    = NULL;
        afr_local_t     *local = NULL;
        afr_private_t   *priv  = NULL;
        dict_t          *xattr_req = NULL;
        int32_t         *zero_pending = NULL;
        int call_count = 0;
        int i = 0;
        int ret = 0;

        priv  = this->private;
        local = frame->local;
        sh    = &local->self_heal;

        call_count = afr_up_children_count (local->child_up,
                                            priv->child_count);

        local->call_count = call_count;

        xattr_req = dict_new();
        if (!xattr_req) {
                ret = -1;
                goto out;
        }

        for (i = 0; i < priv->child_count; i++) {
                zero_pending = GF_CALLOC (3, sizeof (*zero_pending),
                                          gf_afr_mt_int32_t);
                if (!zero_pending) {
                        ret = -1;
                        goto out;
                }
                ret = dict_set_dynptr (xattr_req, priv->pending_key[i],
                                       zero_pending,
                                       3 * sizeof (*zero_pending));
                if (ret < 0) {
                        gf_log (this->name, GF_LOG_WARNING,
                                "Unable to set dict value");
                        goto out;
                } else {
                        zero_pending = NULL;
                }
        }

        afr_reset_xattr (sh->xattr, priv->child_count);
        afr_reset_children (sh->success_children, priv->child_count);
        sh->success_count = 0;
        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, fxattrop_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->fxattrop,
                                           sh->healing_fd, GF_XATTROP_ADD_ARRAY,
                                           xattr_req);

                        if (!--call_count)
                                break;
                }
        }

out:
        if (xattr_req)
                dict_unref (xattr_req);

        if (ret) {
                if (zero_pending)
                        GF_FREE (zero_pending);
                sh->op_failed = 1;
                afr_sh_data_done (frame, this);
        }

        return 0;
}

int
afr_sh_data_big_lock_success (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;

        sh->data_lock_held = _gf_true;
        afr_sh_data_fxattrop (frame, this, afr_sh_data_fxattrop_cbk);
        return 0;
}

int
afr_sh_data_post_blocking_inodelk_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_self_heal_t     *sh       = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;
        sh       = &local->self_heal;

        if (int_lock->lock_op_ret < 0) {
                gf_log (this->name, GF_LOG_ERROR, "Blocking data inodelks "
                        "failed for %s. by %"PRIu64,
                        local->loc.path, frame->root->lk_owner);
                sh->data_lock_failure_handler (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG, "Blocking data inodelks "
                        "done for %s by %"PRIu64". Proceding to self-heal",
                        local->loc.path, frame->root->lk_owner);
                sh->data_lock_success_handler (frame, this);
        }

        return 0;
}

int
afr_sh_data_post_nonblocking_inodelk_cbk (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_self_heal_t     *sh       = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;
        sh       = &local->self_heal;

        if (int_lock->lock_op_ret < 0) {
                gf_log (this->name, GF_LOG_DEBUG, "Non Blocking data inodelks "
                        "failed for %s. by %"PRIu64,
                        local->loc.path, frame->root->lk_owner);
                int_lock->lock_cbk = afr_sh_data_post_blocking_inodelk_cbk;
                afr_blocking_lock (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG, "Non Blocking data inodelks "
                        "done for %s by %"PRIu64". Proceeding to self-heal",
                        local->loc.path, frame->root->lk_owner);
                sh->data_lock_success_handler (frame, this);
        }

        return 0;
}

int
afr_sh_data_lock_rec (call_frame_t *frame, xlator_t *this, off_t start, off_t len)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;

        int_lock->transaction_lk_type = AFR_SELFHEAL_LK;
        int_lock->selfheal_lk_type    = AFR_DATA_SELF_HEAL_LK;

        afr_set_lock_number (frame, this);

        int_lock->lk_flock.l_start = start;
        int_lock->lk_flock.l_len   = len;
        int_lock->lk_flock.l_type  = F_WRLCK;
        int_lock->lock_cbk         = afr_sh_data_post_nonblocking_inodelk_cbk;

        afr_nonblocking_inodelk (frame, this);

        return 0;
}

int
afr_post_sh_big_lock_success (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local     = NULL;
        afr_self_heal_t *sh        = NULL;

        local = frame->local;
        sh = &local->self_heal;

        GF_ASSERT (sh->old_loop_frame);
        sh_loop_finish (sh->old_loop_frame, this);
        sh->old_loop_frame = NULL;
        sh->data_lock_held = _gf_true;
        afr_sh_data_fxattrop (frame, this, afr_post_sh_data_fxattrop_cbk);
        return 0;
}

int
afr_post_sh_big_lock_failure (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local     = NULL;
        afr_self_heal_t *sh        = NULL;

        local = frame->local;
        sh = &local->self_heal;

        GF_ASSERT (sh->old_loop_frame);
        sh_loop_finish (sh->old_loop_frame, this);
        sh->old_loop_frame = NULL;
        afr_sh_set_timestamps (frame, this);
        return 0;
}


int
afr_sh_data_lock (call_frame_t *frame, xlator_t *this,
                  off_t start, off_t len,
                  afr_lock_cbk_t success_handler,
                  afr_lock_cbk_t failure_handler)
{
        afr_local_t *   local = NULL;
        afr_self_heal_t * sh  = NULL;

        local = frame->local;
        sh    = &local->self_heal;

        sh->data_lock_success_handler = success_handler;
        sh->data_lock_failure_handler = failure_handler;
        return afr_sh_data_lock_rec (frame, this, start, len);
}

int
afr_sh_data_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
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
                                "open of %s failed on child %s (%s)",
                                local->loc.path,
                                priv->children[child_index]->name,
                                strerror (op_errno));
                        sh->op_failed = 1;
                }

                gf_log (this->name, GF_LOG_TRACE,
                        "open of %s succeeded on child %s",
                        local->loc.path,
                        priv->children[child_index]->name);
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                if (sh->op_failed) {
                        afr_sh_data_fail (frame, this);
                        return 0;
                }

                gf_log (this->name, GF_LOG_TRACE,
                        "fd for %s opened, commencing sync",
                        local->loc.path);

                afr_sh_data_lock (frame, this, 0, 0,
                                  afr_sh_data_big_lock_success,
                                  afr_sh_data_fail);
        }

        return 0;
}


int
afr_sh_data_open (call_frame_t *frame, xlator_t *this)
{
        int i = 0;
        int call_count = 0;
        fd_t *fd = NULL;
        afr_local_t *   local = NULL;
        afr_private_t * priv  = NULL;
        afr_self_heal_t *sh = NULL;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        call_count = afr_up_children_count (local->child_up, priv->child_count);
        local->call_count = call_count;

        fd = fd_create (local->loc.inode, frame->root->pid);
        sh->healing_fd = fd;

        /* open sinks */
        for (i = 0; i < priv->child_count; i++) {
                if(!local->child_up[i])
                        continue;

                STACK_WIND_COOKIE (frame, afr_sh_data_open_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->open,
                                   &local->loc,
                                   O_RDWR|O_LARGEFILE, fd, 0);

                if (!--call_count)
                        break;
        }

        return 0;
}


int
afr_self_heal_data (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t *priv = this->private;

        local = frame->local;
        sh = &local->self_heal;

        if (sh->do_data_self_heal &&
            afr_data_self_heal_enabled (priv->data_self_heal)) {
                afr_sh_data_open (frame, this);
        } else {
                gf_log (this->name, GF_LOG_TRACE,
                        "not doing data self heal on %s",
                        local->loc.path);
                afr_sh_data_done (frame, this);
        }

        return 0;
}
