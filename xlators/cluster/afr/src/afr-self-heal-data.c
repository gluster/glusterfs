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


int
afr_sh_data_done (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        /*
          TODO: cleanup sh->*
        */

        if (sh->healing_fd && !sh->healing_fd_opened) {
                /* unref only if we created the fd ourselves */

                fd_unref (sh->healing_fd);
                sh->healing_fd = NULL;
        }

        /* for (i = 0; i < priv->child_count; i++) */
        /*        sh->locked_nodes[i] = 0; */

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
                                "flush or setattr failed on %s on subvolume %s: %s",
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
afr_sh_data_setattr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, struct iatt *statpre,
                         struct iatt *statpost)
{
        afr_sh_data_flush_cbk (frame, cookie, this, op_ret, op_errno);

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

        if (sh->healing_fd_opened) {
                /* not our job to close the fd */

                afr_sh_data_done (frame, this);
                return 0;
        }

        if (!sh->healing_fd) {
                afr_sh_data_done (frame, this);
                return 0;
        }

        call_count        = (sh->active_sinks + 1) * 2;
        local->call_count = call_count;

        /* closed source */
        gf_log (this->name, GF_LOG_TRACE,
                "closing fd of %s on %s",
                local->loc.path, priv->children[sh->source]->name);

        STACK_WIND_COOKIE (frame, afr_sh_data_flush_cbk,
                           (void *) (long) sh->source,
                           priv->children[sh->source],
                           priv->children[sh->source]->fops->flush,
                           sh->healing_fd);
        call_count--;

        STACK_WIND_COOKIE (frame, afr_sh_data_setattr_cbk,
                           (void *) (long) sh->source,
                           priv->children[sh->source],
                           priv->children[sh->source]->fops->setattr,
                           &local->loc, &stbuf, valid);

        call_count--;

        if (call_count == 0)
                return 0;

        for (i = 0; i < priv->child_count; i++) {
                if (sh->sources[i] || !local->child_up[i])
                        continue;

                gf_log (this->name, GF_LOG_TRACE,
                        "closing fd of %s on %s",
                        local->loc.path, priv->children[i]->name);

                STACK_WIND_COOKIE (frame, afr_sh_data_flush_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->flush,
                                   sh->healing_fd);

                call_count--;

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
afr_sh_data_unlck_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno)
{
        afr_local_t * local = NULL;
        int           call_count = 0;
        int           child_index = (long) cookie;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == -1) {
                        gf_log (this->name, GF_LOG_INFO,
                                "locking inode of %s on child %d failed: %s",
                                local->loc.path, child_index,
                                strerror (op_errno));
                } else {
                        gf_log (this->name, GF_LOG_TRACE,
                                "inode of %s on child %d locked",
                                local->loc.path, child_index);
                }
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                afr_sh_data_close (frame, this);
        }

        return 0;
}


int
afr_sh_data_unlock (call_frame_t *frame, xlator_t *this)
{
        afr_local_t         *local    = NULL;
        afr_internal_lock_t *int_lock = NULL;
        afr_self_heal_t     *sh       = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;
        sh       = &local->self_heal;

        GF_ASSERT (!sh->data_lock_held);

        int_lock->lock_cbk = afr_sh_data_close;
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

        gf_log (this->name, GF_LOG_TRACE,
                "finishing data selfheal of %s", local->loc.path);

        if (!sh->data_lock_held)
                afr_sh_data_unlock (frame, this);
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

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                afr_sh_data_finish (frame, this);

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

        local->call_count = call_count;
        for (i = 0; i < priv->child_count; i++) {
                if (!erase_xattr[i])
                        continue;

                gf_log (this->name, GF_LOG_TRACE,
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
                        gf_log (this->name, GF_LOG_TRACE,
                                "ftruncate of %s on subvolume %s completed",
                                local->loc.path,
                                priv->children[child_index]->name);
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                afr_sh_data_erase_pending (frame, this);
        }

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
        afr_private_t   *priv = NULL;
        int              active_sinks = 0;
        int              source = 0;
        int              i = 0;
        struct afr_sh_algorithm *sh_algo = NULL;

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
                gf_log (this->name, GF_LOG_INFO,
                        "no active sinks for performing self-heal on file %s",
                        local->loc.path);
                afr_sh_data_finish (frame, this);
                return 0;
        }
        sh->active_sinks = active_sinks;

        gf_log (this->name, GF_LOG_DEBUG,
                "self-healing file %s from subvolume %s to %d other",
                local->loc.path, priv->children[source]->name, active_sinks);

        sh->algo_completion_cbk = afr_sh_data_trim_sinks;
        sh->algo_abort_cbk      = afr_sh_data_finish;

        sh_algo = afr_sh_data_pick_algo (frame, this);

        sh_algo->fn (frame, this);

        return 0;
}


int
afr_sh_data_fix (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local      = NULL;
        afr_local_t *    orig_local = NULL;
        afr_self_heal_t *sh = NULL;
        afr_private_t   *priv = NULL;
        int              nsources = 0;
        int              source = 0;
        int              i = 0;

        local = frame->local;
        sh = &local->self_heal;
        priv = this->private;

        afr_build_pending_matrix (priv->pending_key, sh->pending_matrix,
                                  sh->xattr, AFR_DATA_TRANSACTION,
                                  priv->child_count);

        afr_sh_print_pending_matrix (sh->pending_matrix, this);

        nsources = afr_mark_sources (sh->sources, sh->pending_matrix, sh->buf,
                                     priv->child_count, AFR_SELF_HEAL_DATA,
                                     sh->child_success, this->name);

        if (nsources == 0) {
                gf_log (this->name, GF_LOG_TRACE,
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

                afr_sh_data_finish (frame, this);
                return 0;
        }

        source = afr_sh_select_source (sh->sources, priv->child_count);

        if (source == -1) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "No active sources found.");

                afr_sh_data_finish (frame, this);
                return 0;
        }

        sh->source     = source;
        sh->block_size = 65536; /* TODO: make it configurable or use macro */
        sh->file_size  = sh->buf[source].ia_size;

        if (FILE_HAS_HOLES (&sh->buf[source]))
                sh->file_has_holes = 1;

        orig_local = sh->orig_frame->local;
        orig_local->cont.lookup.buf.ia_size = sh->buf[source].ia_size;

        /* detect changes not visible through pending flags -- JIC */
        for (i = 0; i < priv->child_count; i++) {
                if (i == source || sh->child_errno[i])
                        continue;

                if (SIZE_DIFFERS (&sh->buf[i], &sh->buf[source]))
                        sh->sources[i] = 0;
        }

        afr_set_read_child (this, local->loc.inode, sh->source);

        /*
          quick-read might have read the file, so send xattr from
          the source subvolume (http://bugs.gluster.com/cgi-bin/bugzilla3/show_bug.cgi?id=815)
        */

        dict_unref (orig_local->cont.lookup.xattr);
        if (orig_local->cont.lookup.xattrs)
                orig_local->cont.lookup.xattr = dict_ref (orig_local->cont.lookup.xattrs[sh->source]);

        if (sh->background) {
                sh->unwind (sh->orig_frame, this);
                sh->unwound = _gf_true;
        }

        afr_sh_data_sync_prepare (frame, this);

        return 0;
}

gf_boolean_t
afr_is_fresh_read_child (int32_t *sources, int32_t child_count,
                         int32_t read_child)
{
        gf_boolean_t             is_fresh_child = _gf_false;

        GF_ASSERT (read_child < child_count);

        if ((read_child >= 0) && (read_child < child_count) &&
             sources[read_child]) {
                is_fresh_child = _gf_true;
        }
        return is_fresh_child;
}

static int
afr_select_read_child_from_policy (int32_t *sources, int32_t child_count,
                                   int32_t prev_read_child,
                                   int32_t config_read_child,
                                   int32_t *valid_children)
{
        int32_t                  read_child = -1;
        int                      i          = 0;

        GF_ASSERT (sources);

        read_child = prev_read_child;
        if (_gf_true == afr_is_fresh_read_child (sources, child_count,
                                                 read_child))
                goto out;

        read_child = config_read_child;
        if (_gf_true == afr_is_fresh_read_child (sources, child_count,
                                                 read_child))
                goto out;

        for (i = 0; i < child_count; i++) {
                read_child = valid_children[i];
                if (read_child < 0)
                        break;
                if (_gf_true == afr_is_fresh_read_child (sources, child_count,
                                                         read_child))
                        goto out;
        }
        read_child = -1;

out:
        return read_child;
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
        int                      ret        = -1;
        afr_self_heal_type       sh_type    = AFR_SELF_HEAL_INVALID;
        int32_t                  **pending_matrix = NULL;
        int32_t                  *sources         = NULL;
        int32_t                  *valid_children  = NULL;
        struct iatt              *bufs            = NULL;
        int32_t                  nsources         = 0;
        int32_t                  prev_read_child  = -1;
        int32_t                  config_read_child = -1;
        afr_self_heal_t          *sh = NULL;

        priv = this->private;
        bufs = local->cont.lookup.bufs;
        valid_children = local->cont.lookup.child_success;
        sh = &local->self_heal;

        pending_matrix = afr_create_pending_matrix (priv->child_count);
        if (NULL == pending_matrix)
                goto out;

        sources = GF_CALLOC (sizeof (*sources), priv->child_count,
                             gf_afr_mt_int32_t);
        if (NULL == sources)
                goto out;

        afr_build_pending_matrix (priv->pending_key, pending_matrix,
                                  xattr, txn_type, priv->child_count);

        sh_type = afr_self_heal_type_for_transaction (txn_type);
        if (AFR_SELF_HEAL_INVALID == sh_type)
                goto out;

        nsources = afr_mark_sources (sources, pending_matrix, bufs,
                                     priv->child_count, sh_type,
                                     valid_children, this->name);
        if (nsources < 0) {
                ret = -1;
                goto out;
        }

        prev_read_child = local->read_child_index;
        config_read_child = priv->read_child;
        read_child = afr_select_read_child_from_policy (sources,
                                                        priv->child_count,
                                                        prev_read_child,
                                                        config_read_child,
                                                        valid_children);
        ret = 0;
        local->cont.lookup.sources = sources;
out:
        afr_destroy_pending_matrix (pending_matrix, priv->child_count);
        if (-1 == ret) {
                if (sources)
                        GF_FREE (sources);
        }
        gf_log (this->name, GF_LOG_DEBUG, "returning read_child: %d", read_child);
        return read_child;
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
                        sh->child_success[sh->success_count] = child_index;
                        sh->success_count++;
                }
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                afr_sh_data_fix (frame, this);
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

        call_count = afr_up_children_count (priv->child_count,
                                            local->child_up);

        local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++)
                sh->child_success[i] = -1;
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


int
afr_sh_data_fxattrop_cbk (call_frame_t *frame, void *cookie,
                          xlator_t *this, int32_t op_ret, int32_t op_errno,
                          dict_t *xattr)
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
                                "fxattrop of %s on %s succeeded",
                                local->loc.path,
                                priv->children[child_index]->name);

                        sh->xattr[child_index] = dict_ref (xattr);
                }
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                afr_sh_data_fstat (frame, this);
        }

        return 0;
}


int
afr_sh_data_fxattrop (call_frame_t *frame, xlator_t *this)
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

        call_count = afr_up_children_count (priv->child_count,
                                            local->child_up);

        local->call_count = call_count;

        xattr_req = dict_new();
        if (!xattr_req) {
                gf_log (this->name, GF_LOG_ERROR, "Out of memory");
                goto out;
        }

        for (i = 0; i < priv->child_count; i++) {
                zero_pending = GF_CALLOC (3, sizeof (int32_t), gf_common_mt_int32_t);
                if (!zero_pending) {
                        gf_log (this->name, GF_LOG_ERROR, "Out of memory");
                        goto out;
                }
                ret = dict_set_dynptr (xattr_req, priv->pending_key[i],
                                           zero_pending, 3 * sizeof (int32_t));
                if (ret < 0) {
                        GF_FREE (zero_pending);
                        gf_log (this->name, GF_LOG_WARNING,
                                "Unable to set dict value");
                }
        }

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_sh_data_fxattrop_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->fxattrop,
                                           sh->healing_fd, GF_XATTROP_ADD_ARRAY,
                                           xattr_req);

                        if (!--call_count)
                                break;
                }
        }

        dict_unref (xattr_req);
        return 0;

out:
        if (xattr_req)
                dict_unref (xattr_req);
        sh->op_failed = 1;
        afr_sh_data_done (frame, this);

        return 0;
}


int
afr_sh_data_lock_rec (call_frame_t *frame, xlator_t *this);

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
                gf_log (this->name, GF_LOG_ERROR, "Non Blocking data inodelks "
                        "failed for %s.", local->loc.path);
                sh->op_failed = 1;
                afr_sh_data_done (frame, this);
        } else {

                gf_log (this->name, GF_LOG_DEBUG, "Non Blocking data inodelks "
                        "done for %s. Proceeding to FOP", local->loc.path);
                afr_sh_data_fxattrop (frame, this);
        }

        return 0;
}

int
afr_sh_data_lock_rec (call_frame_t *frame, xlator_t *this)
{
        afr_internal_lock_t *int_lock = NULL;
        afr_local_t         *local    = NULL;
        afr_self_heal_t     *sh       = NULL;

        local    = frame->local;
        int_lock = &local->internal_lock;
        sh       = &local->self_heal;

        int_lock->transaction_lk_type = AFR_SELFHEAL_LK;
        int_lock->selfheal_lk_type    = AFR_DATA_SELF_HEAL_LK;

        afr_set_lock_number (frame, this);

        int_lock->lk_flock.l_start = 0;
        int_lock->lk_flock.l_len   = 0;
        int_lock->lk_flock.l_type  = F_WRLCK;
        int_lock->lock_cbk         = afr_sh_data_post_nonblocking_inodelk_cbk;

        afr_nonblocking_inodelk (frame, this);

        return 0;
}


int
afr_sh_data_lock (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *   local = NULL;
        afr_private_t * priv  = NULL;
        afr_self_heal_t * sh  = NULL;


        local = frame->local;
        sh    = &local->self_heal;
        priv  = this->private;

        if (sh->data_lock_held) {
                /* caller has held the lock already,
                   so skip locking */

                afr_sh_data_fxattrop (frame, this);
                return 0;
        }

        return afr_sh_data_lock_rec (frame, this);
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
                        afr_sh_data_finish (frame, this);
                        return 0;
                }

                gf_log (this->name, GF_LOG_TRACE,
                        "fd for %s opened, commencing sync",
                        local->loc.path);

                afr_sh_data_lock (frame, this);
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

        if (sh->healing_fd_opened) {
                /* caller has opened the fd for us already, so skip open */

                afr_sh_data_lock (frame, this);
                return 0;
        }

        call_count = afr_up_children_count (priv->child_count, local->child_up);
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

        if (sh->need_data_self_heal && priv->data_self_heal) {
                afr_sh_data_open (frame, this);
        } else {
                gf_log (this->name, GF_LOG_TRACE,
                        "not doing data self heal on %s",
                        local->loc.path);
                afr_sh_data_done (frame, this);
        }

        return 0;
}
