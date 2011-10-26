/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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


#include "glusterfs.h"
#include "afr.h"
#include "xlator.h"
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
#include "md5.h"

#include "afr-transaction.h"
#include "afr-self-heal.h"
#include "afr-self-heal-common.h"
#include "afr-self-heal-algorithm.h"

/*
  This file contains the various self-heal algorithms
*/

static int
sh_loop_driver (call_frame_t *sh_frame, xlator_t *this,
                gf_boolean_t is_first_call, call_frame_t *old_loop_frame);
static int
sh_loop_return (call_frame_t *sh_frame, xlator_t *this, call_frame_t *loop_frame,
                int32_t op_ret, int32_t op_errno);
static int
sh_destroy_frame (call_frame_t *frame, xlator_t *this)
{
        if (!frame)
                goto out;

        AFR_STACK_DESTROY (frame);
out:
        return 0;
}

static void
sh_private_cleanup (call_frame_t *frame, xlator_t *this)
{
        afr_local_t             *local   = NULL;
        afr_self_heal_t         *sh      = NULL;
        afr_sh_algo_private_t   *sh_priv = NULL;

        local = frame->local;
        sh    = &local->self_heal;

        sh_priv = sh->private;
        if (sh_priv)
                GF_FREE (sh_priv);
}

static int
sh_number_of_writes_needed (unsigned char *write_needed, int child_count)
{
        int writes = 0;
        int i      = 0;

        for (i = 0; i < child_count; i++) {
                if (write_needed[i])
                        writes++;
        }

        return writes;
}


static int
sh_loop_driver_done (call_frame_t *sh_frame, xlator_t *this,
                     call_frame_t *last_loop_frame)
{
        afr_local_t             *local        = NULL;
        afr_self_heal_t         *sh           = NULL;
        afr_sh_algo_private_t   *sh_priv      = NULL;
        int32_t                 total_blocks = 0;
        int32_t                 diff_blocks  = 0;

        local        = sh_frame->local;
        sh           = &local->self_heal;
        sh_priv      = sh->private;
        if (sh_priv) {
                total_blocks = sh_priv->total_blocks;
                diff_blocks  = sh_priv->diff_blocks;
        }

        sh_private_cleanup (sh_frame, this);
        if (sh->op_failed) {
                GF_ASSERT (!last_loop_frame);
                //loop_finish should have happened and the old_loop should be NULL
                gf_log (this->name, GF_LOG_INFO,
                        "self-heal aborting on %s",
                        local->loc.path);

                local->self_heal.algo_abort_cbk (sh_frame, this);
        } else {
                GF_ASSERT (last_loop_frame);
                if (diff_blocks == total_blocks) {
                        gf_log (this->name, GF_LOG_INFO, "full self-heal "
                                "completed on %s",local->loc.path);
                } else {
                        gf_log (this->name, GF_LOG_INFO,
                                "diff self-heal on %s: completed. "
                                "(%d blocks of %d were different (%.2f%%))",
                                local->loc.path, diff_blocks, total_blocks,
                                ((diff_blocks * 1.0)/total_blocks) * 100);
                }

                sh->old_loop_frame = last_loop_frame;
                local->self_heal.algo_completion_cbk (sh_frame, this);
        }

        return 0;
}

int
sh_loop_finish (call_frame_t *loop_frame, xlator_t *this)
{
        afr_local_t             *loop_local = NULL;
        afr_self_heal_t         *loop_sh = NULL;

        if (!loop_frame)
                goto out;

        loop_local = loop_frame->local;
        if (loop_local) {
                loop_sh = &loop_local->self_heal;
        }

        if (loop_sh && loop_sh->data_lock_held) {
                afr_sh_data_unlock (loop_frame, this,
                                    sh_destroy_frame);
        } else {
                sh_destroy_frame (loop_frame, this);
        }
out:
        return 0;
}

static int
sh_loop_lock_success (call_frame_t *loop_frame, xlator_t *this)
{
        afr_local_t                 *loop_local = NULL;
        afr_self_heal_t             *loop_sh    = NULL;

        loop_local = loop_frame->local;
        loop_sh = &loop_local->self_heal;

        sh_loop_finish (loop_sh->old_loop_frame, this);
        loop_sh->old_loop_frame = NULL;

        gf_log (this->name, GF_LOG_DEBUG, "Aquired lock for range %"PRIu64
                " %"PRIu64, loop_sh->offset, loop_sh->block_size);
        loop_sh->data_lock_held = _gf_true;
        loop_sh->sh_data_algo_start (loop_frame, this);
        return 0;
}

static int
sh_loop_lock_failure (call_frame_t *loop_frame, xlator_t *this)
{
        call_frame_t                *sh_frame = NULL;
        afr_local_t                 *loop_local = NULL;
        afr_self_heal_t             *loop_sh    = NULL;

        loop_local = loop_frame->local;
        loop_sh = &loop_local->self_heal;
        sh_frame = loop_sh->sh_frame;

        gf_log (this->name, GF_LOG_ERROR, "failed lock for range %"PRIu64
                " %"PRIu64, loop_sh->offset, loop_sh->block_size);
        sh_loop_finish (loop_sh->old_loop_frame, this);
        loop_sh->old_loop_frame = NULL;
        sh_loop_return (sh_frame, this, loop_frame, -1, ENOTCONN);
        return 0;
}

static int
sh_loop_frame_create (call_frame_t *sh_frame, xlator_t *this,
                      call_frame_t *old_loop_frame, call_frame_t **loop_frame)
{
        call_frame_t                *new_loop_frame = NULL;
        afr_local_t                 *local          = NULL;
        afr_self_heal_t             *sh             = NULL;
        afr_local_t                 *new_loop_local = NULL;
        afr_self_heal_t             *new_loop_sh    = NULL;
        afr_private_t               *priv           = NULL;

        GF_ASSERT (sh_frame);
        GF_ASSERT (loop_frame);

        *loop_frame = NULL;
        local   = sh_frame->local;
        sh      = &local->self_heal;
        priv    = this->private;

        new_loop_frame = copy_frame (sh_frame);
        if (!new_loop_frame)
                goto out;
        //We want the frame to have same lk_owner as sh_frame
        //so that locks translator allows conflicting locks
        new_loop_local = afr_local_copy (local, this);
        if (!new_loop_local)
                goto out;
        new_loop_frame->local = new_loop_local;

        new_loop_sh = &new_loop_local->self_heal;
        new_loop_sh->sources = memdup (sh->sources,
                                       priv->child_count * sizeof (*sh->sources));
        if (!new_loop_sh->sources)
                goto out;
        new_loop_sh->write_needed = GF_CALLOC (priv->child_count,
                                               sizeof (*new_loop_sh->write_needed),
                                               gf_afr_mt_char);
        if (!new_loop_sh->write_needed)
                goto out;
        new_loop_sh->checksum = GF_CALLOC (priv->child_count, MD5_DIGEST_LEN,
                                           gf_afr_mt_uint8_t);
        if (!new_loop_sh->checksum)
                goto out;
        new_loop_sh->inode      = inode_ref (sh->inode);
        new_loop_sh->sh_data_algo_start = sh->sh_data_algo_start;
        new_loop_sh->source = sh->source;
        new_loop_sh->active_sinks = sh->active_sinks;
        new_loop_sh->healing_fd = fd_ref (sh->healing_fd);
        new_loop_sh->file_has_holes = sh->file_has_holes;
        new_loop_sh->old_loop_frame = old_loop_frame;
        new_loop_sh->sh_frame = sh_frame;
        *loop_frame = new_loop_frame;
        return 0;
out:
        sh_destroy_frame (new_loop_frame, this);
        return -ENOMEM;
}

static int
sh_loop_start (call_frame_t *sh_frame, xlator_t *this, off_t offset,
               call_frame_t *old_loop_frame)
{
        call_frame_t                *new_loop_frame = NULL;
        afr_local_t                 *local          = NULL;
        afr_self_heal_t             *sh             = NULL;
        afr_local_t                 *new_loop_local = NULL;
        afr_self_heal_t             *new_loop_sh    = NULL;
        int                         ret             = 0;

        GF_ASSERT (sh_frame);

        local   = sh_frame->local;
        sh      = &local->self_heal;

        ret = sh_loop_frame_create (sh_frame, this, old_loop_frame,
                                    &new_loop_frame);
        if (ret)
                goto out;
        new_loop_local = new_loop_frame->local;
        new_loop_sh = &new_loop_local->self_heal;
        new_loop_sh->offset = offset;
        new_loop_sh->block_size = sh->block_size;
        afr_sh_data_lock (new_loop_frame, this, offset, new_loop_sh->block_size,
                          sh_loop_lock_success, sh_loop_lock_failure);
        return 0;
out:
        sh->op_failed = 1;
        if (old_loop_frame)
                sh_loop_finish (old_loop_frame, this);
        sh_loop_return (sh_frame, this, new_loop_frame, -1, ENOMEM);
        return 0;
}

static int
sh_loop_driver (call_frame_t *sh_frame, xlator_t *this,
                gf_boolean_t is_first_call, call_frame_t *old_loop_frame)
{
        afr_local_t *               local          = NULL;
        afr_self_heal_t *           sh             = NULL;
        afr_sh_algo_private_t       *sh_priv        = NULL;
        gf_boolean_t                is_driver_done = _gf_false;
        blksize_t                   block_size     = 0;
        int                         loop           = 0;
        off_t                       offset         = 0;
        afr_private_t               *priv          = NULL;

        priv    = this->private;
        local   = sh_frame->local;
        sh      = &local->self_heal;
        sh_priv = sh->private;

        LOCK (&sh_priv->lock);
        {
                if (!is_first_call)
                        sh_priv->loops_running--;
                offset = sh_priv->offset;
                block_size = sh->block_size;
                while ((!sh->eof_reached) && (0 == sh->op_failed) &&
                       (sh_priv->loops_running < priv->data_self_heal_window_size)
                       && (sh_priv->offset < sh->file_size)) {

                        loop++;
                        sh_priv->offset += block_size;
                        sh_priv->loops_running++;

                        if (!is_first_call)
                                break;
                }
                if (0 == sh_priv->loops_running) {
                        is_driver_done = _gf_true;
                }
        }
        UNLOCK (&sh_priv->lock);

        if (0 == loop) {
                //loop finish does unlock, but the erasing of the pending
                //xattrs needs to happen before that so do not finish the loop
                if (is_driver_done && !sh->op_failed)
                        goto driver_done;
                if (old_loop_frame) {
                        sh_loop_finish (old_loop_frame, this);
                        old_loop_frame = NULL;
                }
        }

        //If we have more loops to form we should finish previous loop after
        //the next loop lock
        while (loop--) {
                if (sh->op_failed) {
                        // op failed in other loop, stop spawning more loops
                        if (old_loop_frame) {
                                sh_loop_finish (old_loop_frame, this);
                                old_loop_frame = NULL;
                        }
                        sh_loop_driver (sh_frame, this, _gf_false, NULL);
                } else {
                        gf_log (this->name, GF_LOG_TRACE, "spawning a loop "
                                "for offset %"PRId64, offset);

                        sh_loop_start (sh_frame, this, offset, old_loop_frame);
                        old_loop_frame = NULL;
                        offset += block_size;
                }
        }

driver_done:
        if (is_driver_done) {
                sh_loop_driver_done (sh_frame, this, old_loop_frame);
        }
        return 0;
}

static int
sh_loop_return (call_frame_t *sh_frame, xlator_t *this, call_frame_t *loop_frame,
                int32_t op_ret, int32_t op_errno)
{
        afr_local_t *               loop_local = NULL;
        afr_self_heal_t *           loop_sh    = NULL;
        afr_local_t *               sh_local = NULL;
        afr_self_heal_t            *sh       = NULL;

        sh_local = sh_frame->local;
        sh       = &sh_local->self_heal;

        if (loop_frame) {
                loop_local = loop_frame->local;
                if (loop_local)
                        loop_sh    = &loop_local->self_heal;
                if (loop_sh)
                        gf_log (this->name, GF_LOG_TRACE, "loop for offset "
                                "%"PRId64" returned", loop_sh->offset);
        }

        if (op_ret == -1) {
                sh->op_failed = 1;
                afr_sh_set_error (sh, op_errno);
                if (loop_frame) {
                        sh_loop_finish (loop_frame, this);
                        loop_frame = NULL;
                }
        }

        sh_loop_driver (sh_frame, this, _gf_false, loop_frame);

        return 0;
}

static int
sh_loop_write_cbk (call_frame_t *loop_frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct iatt *buf,
                   struct iatt *postbuf)
{
        afr_private_t *             priv        = NULL;
        afr_local_t *               loop_local    = NULL;
        afr_self_heal_t *           loop_sh       = NULL;
        call_frame_t               *sh_frame    = NULL;
        afr_local_t *               sh_local    = NULL;
        afr_self_heal_t            *sh          = NULL;
        int                         call_count  = 0;
        int                         child_index = 0;

        priv     = this->private;
        loop_local = loop_frame->local;
        loop_sh    = &loop_local->self_heal;

        sh_frame = loop_sh->sh_frame;
        sh_local = sh_frame->local;
        sh       = &sh_local->self_heal;

        child_index =  (long) cookie;

        gf_log (this->name, GF_LOG_TRACE,
                "wrote %d bytes of data from %s to child %d, offset %"PRId64"",
                op_ret, sh_local->loc.path, child_index, loop_sh->offset);

        if (op_ret == -1) {
                gf_log (this->name, GF_LOG_ERROR,
                        "write to %s failed on subvolume %s (%s)",
                        sh_local->loc.path,
                        priv->children[child_index]->name,
                        strerror (op_errno));

                sh->op_failed = 1;
                afr_sh_set_error (loop_sh, op_errno);
        }

        call_count = afr_frame_return (loop_frame);

        if (call_count == 0) {
                sh_loop_return (sh_frame, this, loop_frame,
                                loop_sh->op_ret, loop_sh->op_errno);
        }

        return 0;
}


static int
sh_loop_read_cbk (call_frame_t *loop_frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno,
                  struct iovec *vector, int32_t count, struct iatt *buf,
                  struct iobref *iobref)
{
        afr_private_t *               priv       = NULL;
        afr_local_t *                 loop_local   = NULL;
        afr_self_heal_t *             loop_sh      = NULL;
        call_frame_t                 *sh_frame   = NULL;
        int                           i          = 0;
        int                           call_count = 0;
        afr_local_t *                 sh_local   = NULL;
        afr_self_heal_t *             sh      = NULL;

        priv       = this->private;
        loop_local = loop_frame->local;
        loop_sh    = &loop_local->self_heal;

        sh_frame = loop_sh->sh_frame;
        sh_local = sh_frame->local;
        sh       = &sh_local->self_heal;

        gf_log (this->name, GF_LOG_TRACE,
                "read %d bytes of data from %s, offset %"PRId64"",
                op_ret, loop_local->loc.path, loop_sh->offset);

        if (op_ret <= 0) {
                if (op_ret < 0) {
                        sh->op_failed = 1;
                        gf_log (this->name, GF_LOG_ERROR, "read failed on %d "
                                "for %s reason :%s", sh->source,
                                sh_local->loc.path, strerror (errno));
                } else {
                        sh->eof_reached = _gf_true;
                        gf_log (this->name, GF_LOG_DEBUG, "Eof reached for %s",
                                sh_local->loc.path);
                }
                sh_loop_return (sh_frame, this, loop_frame, op_ret, op_errno);
                goto out;
        }

        if (loop_sh->file_has_holes && iov_0filled (vector, count) == 0) {
                        gf_log (this->name, GF_LOG_DEBUG, "0 filled block");
                        sh_loop_return (sh_frame, this, loop_frame,
                                        op_ret, op_errno);
                        goto out;
        }

        call_count = sh_number_of_writes_needed (loop_sh->write_needed,
                                                 priv->child_count);
        GF_ASSERT (call_count > 0);
        loop_local->call_count = call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (!loop_sh->write_needed[i])
                        continue;
                STACK_WIND_COOKIE (loop_frame, sh_loop_write_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->writev,
                                   loop_sh->healing_fd, vector, count,
                                   loop_sh->offset, iobref);

                if (!--call_count)
                        break;
        }

out:
        return 0;
}


static int
sh_loop_read (call_frame_t *loop_frame, xlator_t *this)
{
        afr_private_t           *priv       = NULL;
        afr_local_t             *loop_local   = NULL;
        afr_self_heal_t         *loop_sh      = NULL;

        priv     = this->private;
        loop_local = loop_frame->local;
        loop_sh    = &loop_local->self_heal;

        STACK_WIND_COOKIE (loop_frame, sh_loop_read_cbk,
                           (void *) (long) loop_sh->source,
                           priv->children[loop_sh->source],
                           priv->children[loop_sh->source]->fops->readv,
                           loop_sh->healing_fd, loop_sh->block_size,
                           loop_sh->offset);

        return 0;
}


static int
sh_diff_checksum_cbk (call_frame_t *loop_frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      uint32_t weak_checksum, uint8_t *strong_checksum)
{
        afr_private_t                 *priv         = NULL;
        afr_local_t                   *loop_local   = NULL;
        afr_self_heal_t               *loop_sh      = NULL;
        call_frame_t                  *sh_frame     = NULL;
        afr_local_t                   *sh_local     = NULL;
        afr_self_heal_t               *sh           = NULL;
        afr_sh_algo_private_t         *sh_priv      = NULL;
        int                           child_index  = 0;
        int                           call_count   = 0;
        int                           i            = 0;
        int                           write_needed = 0;

        priv  = this->private;

        loop_local = loop_frame->local;
        loop_sh    = &loop_local->self_heal;

        sh_frame = loop_sh->sh_frame;
        sh_local = sh_frame->local;
        sh       = &sh_local->self_heal;

        sh_priv = sh->private;

        child_index = (long) cookie;

        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "checksum on %s failed on subvolume %s (%s)",
                        sh_local->loc.path, priv->children[child_index]->name,
                        strerror (op_errno));
                sh->op_failed = 1;
        } else {
                memcpy (loop_sh->checksum + child_index * MD5_DIGEST_LEN,
                        strong_checksum, MD5_DIGEST_LEN);
        }

        call_count = afr_frame_return (loop_frame);

        if (call_count == 0) {
                for (i = 0; i < priv->child_count; i++) {
                        if (sh->sources[i] || !sh_local->child_up[i])
                                continue;

                        if (memcmp (loop_sh->checksum + (i * MD5_DIGEST_LEN),
                                    loop_sh->checksum + (sh->source * MD5_DIGEST_LEN),
                                    MD5_DIGEST_LEN)) {
                                /*
                                  Checksums differ, so this block
                                  must be written to this sink
                                */

                                gf_log (this->name, GF_LOG_DEBUG,
                                        "checksum on subvolume %s at offset %"
                                        PRId64" differs from that on source",
                                        priv->children[i]->name, loop_sh->offset);

                                write_needed = loop_sh->write_needed[i] = 1;
                        }
                }

                LOCK (&sh_priv->lock);
                {
                        sh_priv->total_blocks++;
                        if (write_needed)
                                sh_priv->diff_blocks++;
                }
                UNLOCK (&sh_priv->lock);

                if (write_needed && !sh->op_failed) {
                        sh_loop_read (loop_frame, this);
                } else {
                        sh_loop_return (sh_frame, this, loop_frame,
                                        op_ret, op_errno);
                }
        }

        return 0;
}

static int
sh_diff_checksum (call_frame_t *loop_frame, xlator_t *this)
{
        afr_private_t           *priv         = NULL;
        afr_local_t             *loop_local   = NULL;
        afr_self_heal_t         *loop_sh      = NULL;
        int                     call_count    = 0;
        int                     i             = 0;

        priv         = this->private;
        loop_local   = loop_frame->local;
        loop_sh      = &loop_local->self_heal;

        call_count = loop_sh->active_sinks + 1;  /* sinks and source */

        loop_local->call_count = call_count;

        STACK_WIND_COOKIE (loop_frame, sh_diff_checksum_cbk,
                           (void *) (long) loop_sh->source,
                           priv->children[loop_sh->source],
                           priv->children[loop_sh->source]->fops->rchecksum,
                           loop_sh->healing_fd,
                           loop_sh->offset, loop_sh->block_size);

        for (i = 0; i < priv->child_count; i++) {
                if (loop_sh->sources[i] || !loop_local->child_up[i])
                        continue;

                STACK_WIND_COOKIE (loop_frame, sh_diff_checksum_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->rchecksum,
                                   loop_sh->healing_fd,
                                   loop_sh->offset, loop_sh->block_size);

                if (!--call_count)
                        break;
        }

        return 0;
}

static int
sh_full_read_write_to_sinks (call_frame_t *loop_frame, xlator_t *this)
{
        afr_private_t           *priv         = NULL;
        afr_local_t             *loop_local   = NULL;
        afr_self_heal_t         *loop_sh      = NULL;
        int                     i             = 0;

        priv         = this->private;
        loop_local   = loop_frame->local;
        loop_sh      = &loop_local->self_heal;

        for (i = 0; i < priv->child_count; i++) {
                if (loop_sh->sources[i] || !loop_local->child_up[i])
                        continue;
                loop_sh->write_needed[i] = 1;
        }
        sh_loop_read (loop_frame, this);
        return 0;
}

afr_sh_algo_private_t*
afr_sh_priv_init ()
{
        afr_sh_algo_private_t   *sh_priv = NULL;

        sh_priv = GF_CALLOC (1, sizeof (*sh_priv),
                             gf_afr_mt_afr_private_t);
        if (!sh_priv)
                goto out;

        LOCK_INIT (&sh_priv->lock);
out:
        return sh_priv;
}

void
afr_sh_transfer_lock (call_frame_t *dst, call_frame_t *src,
                      unsigned int child_count)
{
        afr_local_t             *dst_local   = NULL;
        afr_self_heal_t         *dst_sh      = NULL;
        afr_local_t             *src_local   = NULL;
        afr_self_heal_t         *src_sh      = NULL;

        dst_local = dst->local;
        dst_sh = &dst_local->self_heal;
        src_local = src->local;
        src_sh = &src_local->self_heal;
        GF_ASSERT (src_sh->data_lock_held);
        GF_ASSERT (!dst_sh->data_lock_held);
        afr_lk_transfer_datalock (dst, src, child_count);
        src_sh->data_lock_held = _gf_false;
        dst_sh->data_lock_held = _gf_true;
}

int
afr_sh_start_loops (call_frame_t *sh_frame, xlator_t *this,
                    afr_sh_algo_fn sh_data_algo_start)
{
        call_frame_t            *first_loop_frame = NULL;
        afr_local_t             *local   = NULL;
        afr_self_heal_t         *sh      = NULL;
        int                     ret      = 0;
        afr_private_t           *priv    = NULL;

        local = sh_frame->local;
        sh    = &local->self_heal;
        priv  = this->private;

        sh->sh_data_algo_start = sh_data_algo_start;
        local->call_count = 0;
        ret = sh_loop_frame_create (sh_frame, this, NULL, &first_loop_frame);
        if (ret)
                goto out;
        afr_sh_transfer_lock (first_loop_frame, sh_frame, priv->child_count);
        sh->private = afr_sh_priv_init ();
        if (!sh->private) {
                ret = -1;
                goto out;
        }
        sh_loop_driver (sh_frame, this, _gf_true, first_loop_frame);
        ret = 0;
out:
        if (ret) {
                sh->op_failed = 1;
                sh_loop_driver_done (sh_frame, this, NULL);
        }
        return 0;
}

int
afr_sh_algo_diff (call_frame_t *sh_frame, xlator_t *this)
{
        afr_sh_start_loops (sh_frame, this, sh_diff_checksum);
        return 0;
}

int
afr_sh_algo_full (call_frame_t *sh_frame, xlator_t *this)
{
        afr_sh_start_loops (sh_frame, this, sh_full_read_write_to_sinks);
        return 0;
}

struct afr_sh_algorithm afr_self_heal_algorithms[] = {
        {.name = "full",  .fn = afr_sh_algo_full},
        {.name = "diff",  .fn = afr_sh_algo_diff},
        {0, 0},
};
