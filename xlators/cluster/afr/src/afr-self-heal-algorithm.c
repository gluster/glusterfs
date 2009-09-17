/*
   Copyright (c) 2009 Z RESEARCH, Inc. <http://www.zresearch.com>
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

static int
sh_full_read_write_iter (call_frame_t *frame, xlator_t *this);

static int
sh_full_write_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	afr_private_t * priv = NULL;
	afr_local_t * local  = NULL;
	afr_self_heal_t *sh  = NULL;

	int child_index = (long) cookie;
	int call_count = 0;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	gf_log (this->name, GF_LOG_TRACE,
		"wrote %d bytes of data from %s to child %d, offset %"PRId64"",
		op_ret, local->loc.path, child_index, sh->offset - op_ret);

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_DEBUG,
				"write to %s failed on subvolume %s (%s)",
				local->loc.path,
				priv->children[child_index]->name,
				strerror (op_errno));
			sh->op_failed = 1;
		}
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		sh_full_read_write_iter (frame, this);
	}

	return 0;
}


static int
sh_full_read_cbk (call_frame_t *frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno,
                  struct iovec *vector, int32_t count, struct stat *buf,
                  struct iobref *iobref)
{
	afr_private_t * priv = NULL;
	afr_local_t * local  = NULL;
	afr_self_heal_t *sh  = NULL;

	int child_index = (long) cookie;
	int i = 0;
	int call_count = 0;

	off_t offset;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	call_count = sh->active_sinks;

	local->call_count = call_count;

	gf_log (this->name, GF_LOG_TRACE,
		"read %d bytes of data from %s on child %d, offset %"PRId64"",
		op_ret, local->loc.path, child_index, sh->offset);

	if (op_ret <= 0) {
		local->self_heal.algo_completion_cbk (frame, this);
		return 0;
	}

	/* what if we read less than block size? */
	offset = sh->offset;
	sh->offset += op_ret;

	if (sh->file_has_holes) {
		if (iov_0filled (vector, count) == 0) {
			/* the iter function depends on the
			   sh->offset already being updated
			   above
			*/

                        sh_full_read_write_iter (frame, this);
			goto out;
		}
	}

	for (i = 0; i < priv->child_count; i++) {
		if (sh->sources[i] || !local->child_up[i])
			continue;

		/* this is a sink, so write to it */
		STACK_WIND_COOKIE (frame, sh_full_write_cbk,
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
sh_full_read_write (call_frame_t *frame, xlator_t *this)
{
	afr_private_t * priv = NULL;
	afr_local_t * local  = NULL;
	afr_self_heal_t *sh  = NULL;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	STACK_WIND_COOKIE (frame, sh_full_read_cbk,
			   (void *) (long) sh->source,
			   priv->children[sh->source],
			   priv->children[sh->source]->fops->readv,
			   sh->healing_fd, sh->block_size,
			   sh->offset);

	return 0;
}


static int
sh_full_read_write_iter (call_frame_t *frame, xlator_t *this)
{
	afr_private_t * priv = NULL;
	afr_local_t * local  = NULL;
	afr_self_heal_t *sh  = NULL;

	priv = this->private;
	local = frame->local;
	sh = &local->self_heal;

	if (sh->op_failed) {
		local->self_heal.algo_abort_cbk (frame, this);
		goto out;
	}

	if (sh->offset >= sh->file_size) {
		gf_log (this->name, GF_LOG_TRACE,
			"closing fd's of %s",
			local->loc.path);

		local->self_heal.algo_completion_cbk (frame, this);

		goto out;
	}

	sh_full_read_write (frame, this);

out:
	return 0;
}


int
afr_sh_algo_full (call_frame_t *frame, xlator_t *this)
{
        sh_full_read_write (frame, this);
        return 0;
}


/*
 * The "diff" algorithm. Copies only those blocks whose checksums
 * don't match with those of source.
 */


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


static int
sh_diff_iter (call_frame_t *frame, xlator_t *this);


static int
sh_diff_write_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct stat *buf)
{
	afr_private_t *   priv  = NULL;
	afr_local_t *     local = NULL;
	afr_self_heal_t * sh    = NULL;

	int child_index = (long) cookie;
	int call_count  = 0;

	priv  = this->private;
	local = frame->local;
	sh    = &local->self_heal;

	gf_log (this->name, GF_LOG_TRACE,
		"wrote %d bytes of data from %s to child %d, offset %"PRId64"",
		op_ret, local->loc.path, child_index, sh->offset - op_ret);

	LOCK (&frame->lock);
	{
		if (op_ret == -1) {
			gf_log (this->name, GF_LOG_DEBUG,
				"write to %s failed on subvolume %s (%s)",
				local->loc.path,
				priv->children[child_index]->name,
				strerror (op_errno));

			sh->op_failed = 1;
		}
	}
	UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0) {
		sh_diff_iter (frame, this);
	}

	return 0;
}


static int
sh_diff_read_cbk (call_frame_t *frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno,
                  struct iovec *vector, int32_t count, struct stat *buf,
                  struct iobref *iobref)
{
	afr_private_t *   priv  = NULL;
	afr_local_t *     local = NULL;
	afr_self_heal_t * sh    = NULL;

        afr_sh_algo_diff_private_t *sh_priv = NULL;

	int child_index = (long) cookie;

	int i = 0;
	int call_count = 0;

	off_t offset;

	priv    = this->private;
	local   = frame->local;
	sh      = &local->self_heal;
        sh_priv = sh->private;

	call_count = sh_diff_number_of_writes_needed (sh_priv->write_needed,
                                                      priv->child_count);

	local->call_count = call_count;

	gf_log (this->name, GF_LOG_TRACE,
		"read %d bytes of data from %s on child %d, offset %"PRId64"",
		op_ret, local->loc.path, child_index, sh->offset);

	if (op_ret <= 0) {
		local->self_heal.algo_completion_cbk (frame, this);
		return 0;
	}

	/* what if we read less than block size? */
	offset      = sh->offset;
	sh->offset += sh_priv->block_size;

	if (sh->file_has_holes) {
		if (iov_0filled (vector, count) == 0) {
			/*
                           the iter function depends on the
			   sh->offset already being updated
			   above
			*/

                        sh_diff_iter (frame, this);
			goto out;
		}
	}

	for (i = 0; i < priv->child_count; i++) {
                if (sh_priv->write_needed[i]) {
                        STACK_WIND_COOKIE (frame, sh_diff_write_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->writev,
                                           sh->healing_fd, vector, count, offset,
                                           iobref);

                        sh_priv->write_needed[i] = 0;

                        if (!--call_count)
                                break;
                }
        }

out:
	return 0;
}


static int
sh_diff_read (call_frame_t *frame, xlator_t *this)
{
	afr_private_t *   priv  = NULL;
	afr_local_t *     local = NULL;
	afr_self_heal_t * sh    = NULL;

        afr_sh_algo_diff_private_t * sh_priv = NULL;

	priv    = this->private;
	local   = frame->local;
	sh      = &local->self_heal;
        sh_priv = sh->private;

	STACK_WIND_COOKIE (frame, sh_diff_read_cbk,
			   (void *) (long) sh->source,
			   priv->children[sh->source],
			   priv->children[sh->source]->fops->readv,
			   sh->healing_fd, sh_priv->block_size,
			   sh->offset);

	return 0;
}



static int
sh_diff_checksum_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno,
                      uint32_t weak_checksum, uint8_t *strong_checksum)
{
	afr_private_t *              priv    = NULL;
	afr_local_t *                local   = NULL;
	afr_self_heal_t *            sh      = NULL;
        afr_sh_algo_diff_private_t * sh_priv = NULL;

        int child_index  = (long) cookie;
        int call_count   = 0;
        int i            = 0;
        int write_needed = 0;

	priv  = this->private;
	local = frame->local;
	sh    = &local->self_heal;

        sh_priv = sh->private;

        if (op_ret < 0) {
                gf_log (this->name, GF_LOG_ERROR,
                        "checksum on %s failed on subvolume %s (%s)",
                        local->loc.path, priv->children[child_index]->name,
                        strerror (op_errno));

                sh->op_failed = 1;

                local->self_heal.algo_abort_cbk (frame, this);
                return 0;
        }

        memcpy ((void *) sh_priv->checksum + (child_index * MD5_DIGEST_LEN),
                strong_checksum,
                MD5_DIGEST_LEN);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                for (i = 0; i < priv->child_count; i++) {
                        if (sh->sources[i] || !local->child_up[i])
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
                                        priv->children[i]->name, sh->offset);

                                write_needed = sh_priv->write_needed[i] = 1;
                        }
                }

                if (write_needed) {
                        sh_diff_read (frame, this);
                } else {
                        sh->offset += sh_priv->block_size;

                        sh_diff_iter (frame, this);
                }
        }

        return 0;
}


static int
sh_diff_checksum (call_frame_t *frame, xlator_t *this)
{
	afr_private_t *   priv  = NULL;
	afr_local_t *     local = NULL;
	afr_self_heal_t * sh    = NULL;

        afr_sh_algo_diff_private_t * sh_priv = NULL;

	priv    = this->private;
	local   = frame->local;
	sh      = &local->self_heal;
        sh_priv = sh->private;

        int call_count = 0;
        int i          = 0;

        call_count = sh->active_sinks + 1;  /* sinks and source */

        local->call_count = call_count;

        STACK_WIND_COOKIE (frame, sh_diff_checksum_cbk,
                           (void *) (long) i,
                           priv->children[sh->source],
                           priv->children[sh->source]->fops->rchecksum,
                           sh->healing_fd,
                           sh->offset, sh_priv->block_size);

        for (i = 0; i < priv->child_count; i++) {
                if (sh->sources[i] || !local->child_up[i])
                        continue;

                STACK_WIND_COOKIE (frame, sh_diff_checksum_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->rchecksum,
                                   sh->healing_fd,
                                   sh->offset, sh_priv->block_size);
                if (!--call_count)
                        break;
        }

        return 0;
}


static int
sh_diff_iter (call_frame_t *frame, xlator_t *this)
{
	afr_private_t *   priv  = NULL;
	afr_local_t *     local = NULL;
	afr_self_heal_t * sh    = NULL;

	priv  = this->private;
	local = frame->local;
	sh    = &local->self_heal;

	if (sh->op_failed) {
		local->self_heal.algo_abort_cbk (frame, this);
		goto out;
	}

	if (sh->offset >= sh->file_size) {
		gf_log (this->name, GF_LOG_TRACE,
			"closing fd's of %s",
			local->loc.path);

		local->self_heal.algo_completion_cbk (frame, this);

		goto out;
	}

        sh_diff_checksum (frame, this);
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

        sh_priv->write_needed = CALLOC (priv->child_count,
                                        sizeof (unsigned char));

        sh_priv->checksum = CALLOC (priv->child_count, MD5_DIGEST_LEN);

        sh_priv->block_size = this->ctx->page_size;

        sh->private = sh_priv;

        sh_diff_checksum (frame, this);

        return 0;
}


struct afr_sh_algorithm afr_self_heal_algorithms[] = {
        {.name = "full",  .fn = afr_sh_algo_full},
        {.name = "diff",  .fn = afr_sh_algo_diff},
};
