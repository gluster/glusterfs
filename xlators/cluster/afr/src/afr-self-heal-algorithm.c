/*
   Copyright (c) 2009 Gluster, Inc. <http://www.gluster.com>
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


/*
  The "full" algorithm. Copies the entire file from
  source to sinks.
*/


static void
sh_full_private_cleanup (call_frame_t *frame, xlator_t *this)
{
        afr_private_t *             priv    = NULL;
        afr_local_t *               local   = NULL;
        afr_self_heal_t *           sh      = NULL;
        afr_sh_algo_full_private_t *sh_priv = NULL;

        priv  = this->private;
        local = frame->local;
        sh    = &local->self_heal;

        sh_priv = sh->private;

        if (sh_priv)
                FREE (sh_priv);
}


static int
sh_full_loop_driver (call_frame_t *frame, xlator_t *this);

static int
sh_full_loop_return (call_frame_t *rw_frame, xlator_t *this, off_t offset)
{
        afr_private_t *             priv       = NULL;
        afr_local_t *               rw_local   = NULL;
        afr_self_heal_t *           rw_sh      = NULL;

        call_frame_t *sh_frame              = NULL;
	afr_local_t * sh_local              = NULL;
	afr_self_heal_t *sh                 = NULL;
        afr_sh_algo_full_private_t *sh_priv = NULL;

        priv  = this->private;

        rw_local = rw_frame->local;
        rw_sh    = &rw_local->self_heal;

        sh_frame = rw_sh->sh_frame;
        sh_local = sh_frame->local;
        sh       = &sh_local->self_heal;
        sh_priv  = sh->private;

        LOCK (&sh_priv->lock);
        {
                sh_priv->loops_running--;
        }
        UNLOCK (&sh_priv->lock);

        gf_log (this->name, GF_LOG_TRACE,
                "loop for offset %"PRId64" returned", offset);

        AFR_STACK_DESTROY (rw_frame);

        sh_full_loop_driver (sh_frame, this);

        return 0;
}


static int
sh_full_write_cbk (call_frame_t *rw_frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct stat *prebuf,
                   struct stat *postbuf)
{
	afr_private_t * priv    = NULL;
	afr_local_t * rw_local  = NULL;
	afr_self_heal_t *rw_sh  = NULL;

        call_frame_t *sh_frame  = NULL;
	afr_local_t * sh_local  = NULL;
	afr_self_heal_t *sh     = NULL;

	int child_index = (long) cookie;
	int call_count = 0;

	priv = this->private;

	rw_local = rw_frame->local;
	rw_sh    = &rw_local->self_heal;

        sh_frame = rw_sh->sh_frame;
        sh_local = sh_frame->local;
        sh       = &sh_local->self_heal;

	gf_log (this->name, GF_LOG_TRACE,
		"wrote %d bytes of data from %s to child %d, offset %"PRId64"",
		op_ret, sh_local->loc.path, child_index,
                rw_sh->offset - op_ret);

	LOCK (&sh_frame->lock);
	{
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_DEBUG,
				"write to %s failed on subvolume %s (%s)",
				sh_local->loc.path,
				priv->children[child_index]->name,
				strerror (op_errno));

			sh->op_failed = 1;
		}
	}
	UNLOCK (&sh_frame->lock);

	call_count = afr_frame_return (rw_frame);

	if (call_count == 0) {
		sh_full_loop_return (rw_frame, this, rw_sh->offset - op_ret);
	}

	return 0;
}


static int
sh_full_read_cbk (call_frame_t *rw_frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno,
                  struct iovec *vector, int32_t count, struct stat *buf,
                  struct iobref *iobref)
{
	afr_private_t * priv    = NULL;
	afr_local_t * rw_local  = NULL;
	afr_self_heal_t *rw_sh  = NULL;

        call_frame_t *sh_frame  = NULL;
	afr_local_t * sh_local  = NULL;
	afr_self_heal_t *sh     = NULL;

	int i = 0;
	int call_count = 0;

	off_t offset = (long) cookie;

	priv = this->private;
	rw_local = rw_frame->local;
	rw_sh    = &rw_local->self_heal;

        sh_frame = rw_sh->sh_frame;
        sh_local = sh_frame->local;
        sh       = &sh_local->self_heal;

	call_count = sh->active_sinks;

        rw_local->call_count = call_count;

	gf_log (this->name, GF_LOG_TRACE,
		"read %d bytes of data from %s, offset %"PRId64"",
		op_ret, sh_local->loc.path, offset);

	if (op_ret <= 0) {
                sh->op_failed = 1;

                sh_full_loop_return (rw_frame, this, offset);
		return 0;
	}

	rw_sh->offset += op_ret;

	if (sh->file_has_holes) {
		if (iov_0filled (vector, count) == 0) {
			/* the iter function depends on the
			   sh->offset already being updated
			   above
			*/

                        sh_full_loop_return (rw_frame, this, offset);
			goto out;
		}
	}

	for (i = 0; i < priv->child_count; i++) {
		if (sh->sources[i] || !sh_local->child_up[i])
			continue;

		/* this is a sink, so write to it */

		STACK_WIND_COOKIE (rw_frame, sh_full_write_cbk,
				   (void *) (long) i,
				   priv->children[i],
				   priv->children[i]->fops->writev,
				   sh->healing_fd, vector, count, offset,
                                   iobref);

		if (!--call_count)
			break;
	}

out:
	return 0;
}


static int
sh_full_read_write (call_frame_t *frame, xlator_t *this, off_t offset)
{
	afr_private_t * priv    = NULL;
	afr_local_t * local     = NULL;
	afr_local_t * rw_local  = NULL;
	afr_self_heal_t *rw_sh  = NULL;
	afr_self_heal_t *sh     = NULL;

        call_frame_t *rw_frame = NULL;

        int32_t op_errno = 0;

	priv  = this->private;
        local = frame->local;
        sh    = &local->self_heal;

        rw_frame = copy_frame (frame);
        if (!rw_frame)
                goto out;

        ALLOC_OR_GOTO (rw_local, afr_local_t, out);

        rw_frame->local = rw_local;
	rw_sh           = &rw_local->self_heal;

        rw_sh->offset       = sh->offset;
        rw_sh->sh_frame     = frame;

	STACK_WIND_COOKIE (rw_frame, sh_full_read_cbk,
			   (void *) (long) offset,
			   priv->children[sh->source],
			   priv->children[sh->source]->fops->readv,
			   sh->healing_fd, sh->block_size,
			   offset);
        return 0;

out:
        sh->op_failed = 1;

        sh_full_loop_driver (frame, this);

	return 0;
}


static int
sh_full_loop_driver (call_frame_t *frame, xlator_t *this)
{
	afr_private_t * priv = NULL;
	afr_local_t * local  = NULL;
	afr_self_heal_t *sh  = NULL;
        afr_sh_algo_full_private_t *sh_priv = NULL;

        int   loop    = 0;
        off_t offset  = 0;

	priv    = this->private;
	local   = frame->local;
	sh      = &local->self_heal;
        sh_priv = sh->private;

	if (sh->op_failed) {
                if (sh_priv->loops_running == 0) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "full self-heal aborting on %s",
                                local->loc.path);

                        sh_full_private_cleanup (frame, this);
                        local->self_heal.algo_abort_cbk (frame, this);
                }

		goto out;
	}

	if (sh_priv->offset >= sh->file_size) {
                if (sh_priv->loops_running == 0) {

                        gf_log (this->name, GF_LOG_TRACE,
                                "full self-heal completed on %s",
                                local->loc.path);

                        sh_full_private_cleanup (frame, this);
                        local->self_heal.algo_completion_cbk (frame, this);
                }

		goto out;
	}

spawn:
        loop = 0;

        LOCK (&sh_priv->lock);
        {
                if ((sh_priv->loops_running < priv->data_self_heal_window_size)
                    && (sh_priv->offset < sh->file_size)) {

                        gf_log (this->name, GF_LOG_TRACE,
                                "spawning a loop for offset %"PRId64,
                                sh_priv->offset);

                        offset           = sh_priv->offset;
                        sh_priv->offset += sh->block_size;

                        sh_priv->loops_running++;

                        if (sh_priv->offset < sh->file_size)
                                loop = 1;
                }
        }
        UNLOCK (&sh_priv->lock);

        if (loop) {
                sh_full_read_write (frame, this, offset);
                goto spawn;
        }

out:
	return 0;
}


int
afr_sh_algo_full (call_frame_t *frame, xlator_t *this)
{
        afr_private_t *             priv    = NULL;
        afr_local_t *               local   = NULL;
        afr_self_heal_t *           sh      = NULL;
        afr_sh_algo_full_private_t *sh_priv = NULL;

        priv  = this->private;
        local = frame->local;
        sh    = &local->self_heal;

        sh_priv = CALLOC (1, sizeof (*sh_priv));

        LOCK_INIT (&sh_priv->lock);

        sh->private = sh_priv;

        local->call_count = 0;

        sh_full_loop_driver (frame, this);
        return 0;
}


/*
 * The "diff" algorithm. Copies only those blocks whose checksums
 * don't match with those of source.
 */


static void
sh_diff_private_cleanup (call_frame_t *frame, xlator_t *this)
{
        afr_private_t *             priv    = NULL;
        afr_local_t *               local   = NULL;
        afr_self_heal_t *           sh      = NULL;
        afr_sh_algo_diff_private_t *sh_priv = NULL;

        priv  = this->private;
        local = frame->local;
        sh    = &local->self_heal;

        sh_priv = sh->private;

        if (sh_priv) {
                if (sh_priv->checksum)
                        FREE (sh_priv->checksum);

                FREE (sh_priv);
        }
}


static int
sh_diff_number_of_writes_needed (unsigned char *write_needed, int child_count)
{
        int writes = 0;
        int i;

        for (i = 0; i < child_count; i++) {
                if (write_needed[i])
                        writes++;
        }

        return writes;
}


struct sh_diff_loop_state {
        off_t   offset;
        int32_t child_index;
        unsigned char *write_needed;
};


static int
sh_diff_loop_driver (call_frame_t *frame, xlator_t *this);


static int
sh_diff_loop_return (call_frame_t *rw_frame, xlator_t *this,
                     struct sh_diff_loop_state *loop_state)
{
        afr_private_t *             priv       = NULL;
        afr_local_t *               rw_local   = NULL;
        afr_self_heal_t *           rw_sh      = NULL;

        call_frame_t *sh_frame              = NULL;
	afr_local_t * sh_local              = NULL;
	afr_self_heal_t *sh                 = NULL;
        afr_sh_algo_diff_private_t *sh_priv = NULL;

        priv  = this->private;

        rw_local = rw_frame->local;
        rw_sh    = &rw_local->self_heal;

        sh_frame = rw_sh->sh_frame;
        sh_local = sh_frame->local;
        sh       = &sh_local->self_heal;
        sh_priv  = sh->private;

        LOCK (&sh_priv->lock);
        {
                sh_priv->loops_running--;
        }
        UNLOCK (&sh_priv->lock);

        gf_log (this->name, GF_LOG_TRACE,
                "loop for offset %"PRId64" returned", loop_state->offset);

        AFR_STACK_DESTROY (rw_frame);

        if (loop_state) {
                if (loop_state->write_needed)
                        FREE (loop_state->write_needed);

                FREE (loop_state);
        }

        sh_diff_loop_driver (sh_frame, this);

        return 0;
}


static int
sh_diff_write_cbk (call_frame_t *rw_frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct stat *buf,
                   struct stat *postbuf)
{
	afr_private_t *   priv     = NULL;
	afr_local_t *     rw_local = NULL;
	afr_self_heal_t * rw_sh    = NULL;

        call_frame_t *sh_frame  = NULL;
	afr_local_t * sh_local  = NULL;
	afr_self_heal_t *sh     = NULL;

        struct sh_diff_loop_state *loop_state = (struct sh_diff_loop_state *) cookie;

	int call_count = 0;

	priv     = this->private;
	rw_local = rw_frame->local;
	rw_sh    = &rw_local->self_heal;

        sh_frame = rw_sh->sh_frame;
        sh_local = sh_frame->local;
        sh       = &sh_local->self_heal;

	gf_log (this->name, GF_LOG_TRACE,
		"wrote %d bytes of data from %s to child %d, offset %"PRId64"",
		op_ret, sh_local->loc.path, loop_state->child_index,
                loop_state->offset);

	LOCK (&sh_frame->lock);
	{
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_DEBUG,
				"write to %s failed on subvolume %s (%s)",
				sh_local->loc.path,
				priv->children[loop_state->child_index]->name,
				strerror (op_errno));

			sh->op_failed = 1;
		}
	}
	UNLOCK (&sh_frame->lock);

	call_count = afr_frame_return (rw_frame);

	if (call_count == 0) {
		sh_diff_loop_return (rw_frame, this, loop_state);
	}

	return 0;
}


static int
sh_diff_read_cbk (call_frame_t *rw_frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno,
                  struct iovec *vector, int32_t count, struct stat *buf,
                  struct iobref *iobref)
{
	afr_private_t *   priv     = NULL;
	afr_local_t *     rw_local = NULL;
	afr_self_heal_t * rw_sh    = NULL;

        afr_sh_algo_diff_private_t * sh_priv = NULL;

        call_frame_t *sh_frame  = NULL;
	afr_local_t * sh_local  = NULL;
	afr_self_heal_t *sh     = NULL;

        struct sh_diff_loop_state *loop_state = (struct sh_diff_loop_state *) cookie;

	int i = 0;
	int call_count = 0;

	priv     = this->private;
	rw_local = rw_frame->local;
	rw_sh    = &rw_local->self_heal;

        sh_frame = rw_sh->sh_frame;
        sh_local = sh_frame->local;
        sh       = &sh_local->self_heal;
        sh_priv  = sh->private;

	call_count = sh_diff_number_of_writes_needed (loop_state->write_needed,
                                                      priv->child_count);

	rw_local->call_count = call_count;

	gf_log (this->name, GF_LOG_TRACE,
		"read %d bytes of data from %s, offset %"PRId64"",
		op_ret, sh_local->loc.path, sh->offset);

	if (op_ret <= 0) {
                sh_diff_loop_return (rw_frame, this, loop_state);

		return 0;
	}

	if (sh->file_has_holes) {
		if (iov_0filled (vector, count) == 0) {

                        sh_diff_loop_return (rw_frame, this, loop_state);
			goto out;
		}
	}

	for (i = 0; i < priv->child_count; i++) {
                if (loop_state->write_needed[i]) {
                        STACK_WIND_COOKIE (rw_frame, sh_diff_write_cbk,
                                           (void *) (long) loop_state,
                                           priv->children[i],
                                           priv->children[i]->fops->writev,
                                           sh->healing_fd, vector, count,
                                           loop_state->offset, iobref);

                        if (!--call_count)
                                break;
                }
        }

out:
	return 0;
}


static int
sh_diff_read (call_frame_t *rw_frame, xlator_t *this,
              struct sh_diff_loop_state *loop_state)
{
	afr_private_t *   priv     = NULL;
	afr_local_t *     rw_local = NULL;
	afr_self_heal_t * rw_sh    = NULL;

        afr_sh_algo_diff_private_t * sh_priv = NULL;

        call_frame_t *sh_frame  = NULL;
	afr_local_t * sh_local  = NULL;
	afr_self_heal_t *sh     = NULL;

	priv     = this->private;
	rw_local = rw_frame->local;
	rw_sh    = &rw_local->self_heal;

        sh_frame = rw_sh->sh_frame;
        sh_local = sh_frame->local;
        sh       = &sh_local->self_heal;
        sh_priv  = sh->private;

	STACK_WIND_COOKIE (rw_frame, sh_diff_read_cbk,
			   (void *) (long) loop_state,
			   priv->children[sh->source],
			   priv->children[sh->source]->fops->readv,
			   sh->healing_fd, sh_priv->block_size,
			   loop_state->offset);

	return 0;
}


static int
sh_diff_checksum_cbk (call_frame_t *rw_frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      uint32_t weak_checksum, uint8_t *strong_checksum)
{
	afr_private_t * priv    = NULL;
	afr_local_t * rw_local  = NULL;
	afr_self_heal_t *rw_sh  = NULL;

        call_frame_t *sh_frame  = NULL;
	afr_local_t * sh_local  = NULL;
	afr_self_heal_t *sh     = NULL;

        afr_sh_algo_diff_private_t * sh_priv = NULL;

        struct sh_diff_loop_state *loop_state = (struct sh_diff_loop_state *) cookie;

        int call_count   = 0;
        int i            = 0;
        int write_needed = 0;

	priv  = this->private;

	rw_local = rw_frame->local;
	rw_sh    = &rw_local->self_heal;

        sh_frame = rw_sh->sh_frame;
        sh_local = sh_frame->local;
        sh       = &sh_local->self_heal;

        sh_priv = sh->private;

        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "checksum on %s failed on subvolume %s (%s)",
                        sh_local->loc.path, priv->children[loop_state->child_index]->name,
                        strerror (op_errno));

                sh->op_failed = 1;

                sh_diff_loop_return (rw_frame, this, loop_state);

                return 0;
        }

        memcpy ((void *) sh_priv->checksum + loop_state->child_index * MD5_DIGEST_LEN,
                strong_checksum,
                MD5_DIGEST_LEN);

        call_count = afr_frame_return (rw_frame);

        if (call_count == 0) {
                for (i = 0; i < priv->child_count; i++) {
                        if (sh->sources[i] || !sh_local->child_up[i])
                                continue;

                        if (memcmp ((const void *) sh_priv->checksum + (i * MD5_DIGEST_LEN),
                                    (const void *) sh_priv->checksum + (sh->source * MD5_DIGEST_LEN),
                                    MD5_DIGEST_LEN)) {
                                /*
                                   Checksums differ, so this block
                                   must be written to this sink
                                */

                                gf_log (this->name, GF_LOG_TRACE,
                                        "checksum on subvolume %s at offset %"
                                        PRId64" differs from that on source",
                                        priv->children[i]->name, loop_state->offset);

                                write_needed = loop_state->write_needed[i] = 1;
                        }
                }

                if (write_needed) {
                        sh_diff_read (rw_frame, this, loop_state);
                } else {
                        sh->offset += sh_priv->block_size;

                        sh_diff_loop_return (rw_frame, this, loop_state);
                }
        }

        return 0;
}


static int
sh_diff_checksum (call_frame_t *frame, xlator_t *this, off_t offset)
{
	afr_private_t *   priv     = NULL;
	afr_local_t *     local    = NULL;
	afr_local_t *     rw_local = NULL;
	afr_self_heal_t * sh       = NULL;
	afr_self_heal_t * rw_sh    = NULL;

        afr_sh_algo_diff_private_t * sh_priv = NULL;

        call_frame_t *rw_frame = NULL;

        struct sh_diff_loop_state *loop_state = NULL;

        int32_t op_errno = 0;

        int call_count = 0;
        int i          = 0;

	priv    = this->private;
	local   = frame->local;
	sh      = &local->self_heal;

        sh_priv = sh->private;

        rw_frame = copy_frame (frame);
        if (!rw_frame)
                goto out;

        ALLOC_OR_GOTO (rw_local, afr_local_t, out);

        rw_frame->local = rw_local;
	rw_sh           = &rw_local->self_heal;

        rw_sh->offset       = sh->offset;
        rw_sh->sh_frame     = frame;

        call_count = sh->active_sinks + 1;  /* sinks and source */

        rw_local->call_count = call_count;

        loop_state = CALLOC (1, sizeof (*loop_state));
        loop_state->child_index  = sh->source;
        loop_state->offset       = offset;
        loop_state->write_needed = CALLOC (priv->child_count,
                                           sizeof (*loop_state->write_needed));

        STACK_WIND_COOKIE (rw_frame, sh_diff_checksum_cbk,
                           (void *) (long) loop_state,
                           priv->children[sh->source],
                           priv->children[sh->source]->fops->rchecksum,
                           sh->healing_fd,
                           sh->offset, sh_priv->block_size);

        for (i = 0; i < priv->child_count; i++) {
                if (sh->sources[i] || !local->child_up[i])
                        continue;

                STACK_WIND_COOKIE (rw_frame, sh_diff_checksum_cbk,
                                   (void *) (long) loop_state,
                                   priv->children[i],
                                   priv->children[i]->fops->rchecksum,
                                   sh->healing_fd,
                                   sh->offset, sh_priv->block_size);

                if (!--call_count)
                        break;
        }

        return 0;

out:
        sh->op_failed = 1;

        sh_diff_loop_driver (frame, this);

        return 0;
}


static int
sh_diff_loop_driver (call_frame_t *frame, xlator_t *this)
{
	afr_private_t *   priv  = NULL;
	afr_local_t *     local = NULL;
	afr_self_heal_t * sh    = NULL;
        afr_sh_algo_diff_private_t *sh_priv = NULL;

        int   loop   = 0;
        off_t offset = 0;
 
	priv    = this->private;
	local   = frame->local;
	sh      = &local->self_heal;
        sh_priv = sh->private;

	if (sh->op_failed) {
                if (sh_priv->loops_running == 0) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "diff self-heal aborting on %s",
                                local->loc.path);

                        sh_diff_private_cleanup (frame, this);
                        local->self_heal.algo_abort_cbk (frame, this);
                }

		goto out;
	}

	if (sh_priv->offset >= sh->file_size) {
                if (sh_priv->loops_running == 0) {
                        gf_log (this->name, GF_LOG_TRACE,
                                "full self-heal completed on %s",
                                local->loc.path);


                        sh_diff_private_cleanup (frame, this);
                        local->self_heal.algo_completion_cbk (frame, this);
                }

		goto out;
	}

spawn:
        loop = 0;

        LOCK (&sh_priv->lock);
        {
                if ((sh_priv->loops_running < priv->data_self_heal_window_size)
                    && (sh_priv->offset < sh->file_size)) {

                        gf_log (this->name, GF_LOG_TRACE,
                                "spawning a loop for offset %"PRId64,
                                sh_priv->offset);

                        offset           = sh_priv->offset;
                        sh_priv->offset += sh_priv->block_size;

                        sh_priv->loops_running++;

                        if (sh_priv->offset < sh->file_size)
                                loop = 1;
                }
        }
        UNLOCK (&sh_priv->lock);

        if (loop) {
                sh_diff_checksum (frame, this, offset);
                goto spawn;
        }

out:
        return 0;
}


int
afr_sh_algo_diff (call_frame_t *frame, xlator_t *this)
{
        afr_private_t *             priv    = NULL;
        afr_local_t *               local   = NULL;
        afr_self_heal_t *           sh      = NULL;
        afr_sh_algo_diff_private_t *sh_priv = NULL;

        priv  = this->private;
        local = frame->local;
        sh    = &local->self_heal;

        sh_priv = CALLOC (1, sizeof (*sh_priv));

        sh_priv->checksum = CALLOC (priv->child_count, MD5_DIGEST_LEN);

        sh_priv->block_size = this->ctx->page_size;

        sh->private = sh_priv;

        LOCK_INIT (&sh_priv->lock);

        local->call_count = 0;

        sh_diff_loop_driver (frame, this);

        return 0;
}


struct afr_sh_algorithm afr_self_heal_algorithms[] = {
        {.name = "full",  .fn = afr_sh_algo_full},
        {.name = "diff",  .fn = afr_sh_algo_diff},
        {0, 0},
};
