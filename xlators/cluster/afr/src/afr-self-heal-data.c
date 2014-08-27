/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "afr.h"
#include "afr-self-heal.h"
#include "byte-order.h"

enum {
	AFR_SELFHEAL_DATA_FULL = 0,
	AFR_SELFHEAL_DATA_DIFF,
};


#define HAS_HOLES(i) ((i->ia_blocks * 512) < (i->ia_size))
static int
__checksum_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int op_ret, int op_errno, uint32_t weak, uint8_t *strong,
		dict_t *xdata)
{
	afr_local_t *local = NULL;
	int i = (long) cookie;

	local = frame->local;

	local->replies[i].valid = 1;
	local->replies[i].op_ret = op_ret;
	local->replies[i].op_errno = op_errno;
	if (strong)
		memcpy (local->replies[i].checksum, strong, MD5_DIGEST_LENGTH);

	syncbarrier_wake (&local->barrier);
	return 0;
}


static int
attr_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
	  int op_ret, int op_errno, struct iatt *pre, struct iatt *post,
	  dict_t *xdata)
{
	int i = (long) cookie;
	afr_local_t *local = NULL;

	local = frame->local;

	local->replies[i].valid = 1;
	local->replies[i].op_ret = op_ret;
	local->replies[i].op_errno = op_errno;
	if (pre)
		local->replies[i].prestat = *pre;
	if (post)
		local->replies[i].poststat = *post;
	if (xdata)
		local->replies[i].xdata = dict_ref (xdata);

	syncbarrier_wake (&local->barrier);

	return 0;
}


static gf_boolean_t
__afr_selfheal_data_checksums_match (call_frame_t *frame, xlator_t *this,
				     fd_t *fd, int source,
				     unsigned char *healed_sinks,
				     off_t offset, size_t size)
{
	afr_private_t *priv = NULL;
	afr_local_t *local = NULL;
	unsigned char *wind_subvols = NULL;
	int i = 0;

	priv = this->private;
	local = frame->local;

	wind_subvols = alloca0 (priv->child_count);
	for (i = 0; i < priv->child_count; i++) {
		if (i == source || healed_sinks[i])
			wind_subvols[i] = 1;
	}

	AFR_ONLIST (wind_subvols, frame, __checksum_cbk, rchecksum, fd,
		    offset, size, NULL);

	if (!local->replies[source].valid || local->replies[source].op_ret != 0)
		return _gf_false;

	for (i = 0; i < priv->child_count; i++) {
		if (i == source)
			continue;
		if (memcmp (local->replies[source].checksum,
			    local->replies[i].checksum,
			    MD5_DIGEST_LENGTH))
			return _gf_false;
	}

	return _gf_true;
}


static int
__afr_selfheal_data_read_write (call_frame_t *frame, xlator_t *this, fd_t *fd,
				int source, unsigned char *healed_sinks,
				off_t offset, size_t size,
				struct afr_reply *replies)
{
	struct iovec *iovec = NULL;
	int count = 0;
	struct iobref *iobref = NULL;
	int ret = 0;
	int i = 0;
	afr_private_t *priv = NULL;

	priv = this->private;

	ret = syncop_readv (priv->children[source], fd, size, offset, 0,
			    &iovec, &count, &iobref);
	if (ret <= 0)
		return ret;

	for (i = 0; i < priv->child_count; i++) {
		if (!healed_sinks[i])
			continue;

		/*
		 * TODO: Use fiemap() and discard() to heal holes
		 * in the future.
		 *
		 * For now,
		 *
		 * - if the source had any holes at all,
		 * AND
		 * - if we are writing past the original file size
		 *   of the sink
		 * AND
		 * - is NOT the last block of the source file. if
		 *   the block contains EOF, it has to be written
		 *   in order to set the file size even if the
		 *   last block is 0-filled.
		 * AND
		 * - if the read buffer is filled with only 0's
		 *
		 * then, skip writing to this source. We don't depend
		 * on the write to happen to update the size as we
		 * have performed an ftruncate() upfront anyways.
		 */
#define is_last_block(o,b,s) ((s >= o) && (s <= (o + b)))
		if (HAS_HOLES ((&replies[source].poststat)) &&
		    offset >= replies[i].poststat.ia_size &&
		    !is_last_block (offset, size,
				    replies[source].poststat.ia_size) &&
		    (iov_0filled (iovec, count) == 0))
			continue;

		ret = syncop_writev (priv->children[i], fd, iovec, count,
				     offset, iobref, 0);
		if (ret != iov_length (iovec, count)) {
			/* write() failed on this sink. unset the corresponding
			   member in sinks[] (which is healed_sinks[] in the
			   caller) so that this server does NOT get considered
			   as successfully healed.
			*/
			healed_sinks[i] = 0;
		}
	}
	if (iobref)
		iobref_unref (iobref);

	return ret;
}


static int
afr_selfheal_data_block (call_frame_t *frame, xlator_t *this, fd_t *fd,
			 int source, unsigned char *healed_sinks, off_t offset,
			 size_t size, int type, struct afr_reply *replies)
{
	int ret = -1;
	int sink_count = 0;
	afr_private_t *priv = NULL;
	unsigned char *data_lock = NULL;

	priv = this->private;
	sink_count = AFR_COUNT (healed_sinks, priv->child_count);
	data_lock = alloca0 (priv->child_count);

	ret = afr_selfheal_inodelk (frame, this, fd->inode, this->name,
				    offset, size, data_lock);
	{
		if (ret < sink_count) {
			ret = -ENOTCONN;
			goto unlock;
		}

		if (type == AFR_SELFHEAL_DATA_DIFF &&
		    __afr_selfheal_data_checksums_match (frame, this, fd, source,
							 healed_sinks, offset, size)) {
			ret = 0;
			goto unlock;
		}

		ret = __afr_selfheal_data_read_write (frame, this, fd, source,
						      healed_sinks, offset, size,
						      replies);
	}
unlock:
	afr_selfheal_uninodelk (frame, this, fd->inode, this->name,
				offset, size, data_lock);
	return ret;
}



static int
afr_selfheal_data_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd,
			 unsigned char *healed_sinks)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;

	local = frame->local;
	priv = this->private;

	AFR_ONLIST (healed_sinks, frame, attr_cbk, fsync, fd, 0, NULL);

	for (i = 0; i < priv->child_count; i++)
		if (healed_sinks[i] && local->replies[i].op_ret != 0)
			/* fsync() failed. Do NOT consider this server
			   as successfully healed. Mark it so.
			*/
			healed_sinks[i] = 0;
	return 0;
}


static int
afr_selfheal_data_restore_time (call_frame_t *frame, xlator_t *this,
				inode_t *inode, int source,
				unsigned char *healed_sinks,
				struct afr_reply *replies)
{
	loc_t loc = {0, };

	loc.inode = inode_ref (inode);
	uuid_copy (loc.gfid, inode->gfid);

	AFR_ONLIST (healed_sinks, frame, attr_cbk, setattr, &loc,
		    &replies[source].poststat,
		    (GF_SET_ATTR_ATIME|GF_SET_ATTR_MTIME), NULL);

	loc_wipe (&loc);

	return 0;
}

static int
afr_data_self_heal_type_get (afr_private_t *priv, unsigned char *healed_sinks,
                             int source, struct afr_reply *replies)
{
        int type = AFR_SELFHEAL_DATA_FULL;
        int i = 0;

        if (priv->data_self_heal_algorithm == NULL) {
                type = AFR_SELFHEAL_DATA_FULL;
                for (i = 0; i < priv->child_count; i++) {
                        if (!healed_sinks[i] && i != source)
                                continue;
                        if (replies[i].poststat.ia_size) {
                                type = AFR_SELFHEAL_DATA_DIFF;
                                break;
                        }
                }
        } else if (strcmp (priv->data_self_heal_algorithm, "full") == 0) {
                type = AFR_SELFHEAL_DATA_FULL;
        } else if (strcmp (priv->data_self_heal_algorithm, "diff") == 0) {
                type = AFR_SELFHEAL_DATA_DIFF;
        }
        return type;
}

static int
afr_selfheal_data_do (call_frame_t *frame, xlator_t *this, fd_t *fd,
		      int source, unsigned char *healed_sinks,
		      struct afr_reply *replies)
{
	afr_private_t *priv = NULL;
	int i = 0;
	off_t off = 0;
	size_t block = 128 * 1024;
	int type = AFR_SELFHEAL_DATA_FULL;
	int ret = -1;
	call_frame_t *iter_frame = NULL;
	char *sinks_str = NULL;
	char *p = NULL;

	priv = this->private;

	sinks_str = alloca0 (priv->child_count * 8);
	p = sinks_str;
	for (i = 0; i < priv->child_count; i++) {
		if (!healed_sinks[i])
			continue;
		p += sprintf (p, "%d ", i);
	}

	gf_log (this->name, GF_LOG_INFO, "performing data selfheal on %s. "
		"source=%d sinks=%s",
		uuid_utoa (fd->inode->gfid), source, sinks_str);

        type = afr_data_self_heal_type_get (priv, healed_sinks, source,
                                            replies);

	iter_frame = afr_copy_frame (frame);
	if (!iter_frame)
		return -ENOMEM;

	for (off = 0; off < replies[source].poststat.ia_size; off += block) {
		ret = afr_selfheal_data_block (iter_frame, this, fd, source,
					       healed_sinks, off, block, type,
					       replies);
		if (ret < 0)
			goto out;

		AFR_STACK_RESET (iter_frame);
	}

	afr_selfheal_data_restore_time (frame, this, fd->inode, source,
					healed_sinks, replies);

	ret = afr_selfheal_data_fsync (frame, this, fd, healed_sinks);

out:
	if (iter_frame)
		AFR_STACK_DESTROY (iter_frame);
	return ret;
}


static int
__afr_selfheal_truncate_sinks (call_frame_t *frame, xlator_t *this,
			       fd_t *fd, unsigned char *healed_sinks,
			       struct afr_reply *replies, uint64_t size)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	unsigned char *larger_sinks = 0;
	int i = 0;

	local = frame->local;
	priv = this->private;

	larger_sinks = alloca0 (priv->child_count);
	for (i = 0; i < priv->child_count; i++) {
		if (healed_sinks[i] && replies[i].poststat.ia_size > size)
			larger_sinks[i] = 1;
	}

	AFR_ONLIST (larger_sinks, frame, attr_cbk, ftruncate, fd, size, NULL);

	for (i = 0; i < priv->child_count; i++)
		if (healed_sinks[i] && local->replies[i].op_ret == -1)
			/* truncate() failed. Do NOT consider this server
			   as successfully healed. Mark it so.
			*/
			healed_sinks[i] = 0;
	return 0;
}

/*
 * If by chance there are multiple sources with differing sizes, select
 * the largest file as the source.
 *
 * This can only happen if data was directly modified in the backend.
 */
static int
__afr_selfheal_data_finalize_source (xlator_t *this, unsigned char *sources,
				     unsigned char *healed_sinks,
				     unsigned char *locked_on,
				     struct afr_reply *replies)
{
	int i = 0;
	afr_private_t *priv = NULL;
	uint64_t size = 0;
	int source = -1;
	int sources_count = 0;

	priv = this->private;

	sources_count = AFR_COUNT (sources, priv->child_count);

	if ((AFR_CMP (locked_on, healed_sinks, priv->child_count) == 0)
            || !sources_count) {
		/* split brain */
		return -EIO;
	}

	for (i = 0; i < priv->child_count; i++) {
		if (!sources[i])
			continue;
		if (size <= replies[i].poststat.ia_size) {
			size = replies[i].poststat.ia_size;
			source = i;
		}
	}

	for (i = 0; i < priv->child_count; i++) {
		if (!sources[i])
			continue;
		if (replies[i].poststat.ia_size < size) {
			sources[i] = 0;
			healed_sinks[i] = 1;
		}
	}

	return source;
}

/*
 * __afr_selfheal_data_prepare:
 *
 * This function inspects the on-disk xattrs and determines which subvols
 * are sources and sinks.
 *
 * The return value is the index of the subvolume to be used as the source
 * for self-healing, or -1 if no healing is necessary/split brain.
 */
static int
__afr_selfheal_data_prepare (call_frame_t *frame, xlator_t *this, fd_t *fd,
			     unsigned char *locked_on, unsigned char *sources,
			     unsigned char *sinks, unsigned char *healed_sinks,
			     struct afr_reply *replies)
{
	int ret = -1;
	int source = -1;
	afr_private_t *priv = NULL;

	priv = this->private;

	ret = afr_selfheal_unlocked_discover (frame, fd->inode, fd->inode->gfid,
					      replies);
	if (ret)
		return ret;

	ret = afr_selfheal_find_direction (frame, this, replies,
					   AFR_DATA_TRANSACTION,
					   locked_on, sources, sinks);
	if (ret)
		return ret;

        /* Initialize the healed_sinks[] array optimistically to
           the intersection of to-be-healed (i.e sinks[]) and
           the list of servers which are up (i.e locked_on[]).

           As we encounter failures in the healing process, we
           will unmark the respective servers in the healed_sinks[]
           array.
        */
        AFR_INTERSECT (healed_sinks, sinks, locked_on, priv->child_count);

	source = __afr_selfheal_data_finalize_source (this, sources,
                                                      healed_sinks, locked_on,
                                                      replies);
	if (source < 0)
		return -EIO;

	return source;
}


static int
__afr_selfheal_data (call_frame_t *frame, xlator_t *this, fd_t *fd,
		     unsigned char *locked_on)
{
	afr_private_t *priv = NULL;
	int ret = -1;
	unsigned char *sources = NULL;
	unsigned char *sinks = NULL;
	unsigned char *data_lock = NULL;
	unsigned char *healed_sinks = NULL;
	struct afr_reply *locked_replies = NULL;
	int source = -1;
	gf_boolean_t compat = _gf_false;
	unsigned char *compat_lock = NULL;

	priv = this->private;

	sources = alloca0 (priv->child_count);
	sinks = alloca0 (priv->child_count);
	healed_sinks = alloca0 (priv->child_count);
	data_lock = alloca0 (priv->child_count);
	compat_lock = alloca0 (priv->child_count);

	locked_replies = alloca0 (sizeof (*locked_replies) * priv->child_count);

	ret = afr_selfheal_inodelk (frame, this, fd->inode, this->name, 0, 0,
				    data_lock);
	{
		if (ret < 2) {
			ret = -ENOTCONN;
			goto unlock;
		}

		ret = __afr_selfheal_data_prepare (frame, this, fd, data_lock,
						   sources, sinks, healed_sinks,
						   locked_replies);
		if (ret < 0)
			goto unlock;

		source = ret;

		ret = __afr_selfheal_truncate_sinks (frame, this, fd, healed_sinks,
						     locked_replies,
						     locked_replies[source].poststat.ia_size);
		if (ret < 0)
			goto unlock;

		ret = 0;

		/* Locking from (LLONG_MAX - 2) to (LLONG_MAX - 1) is for
		   compatibility with older self-heal clients which do not
		   hold a lock in the @priv->sh_domain domain to guard
		   against concurrent ongoing self-heals
		*/
		afr_selfheal_inodelk (frame, this, fd->inode, this->name,
				      LLONG_MAX - 2, 1, compat_lock);
		compat = _gf_true;
	}
unlock:
	afr_selfheal_uninodelk (frame, this, fd->inode, this->name, 0, 0,
				data_lock);
	if (ret < 0)
		goto out;

	ret = afr_selfheal_data_do (frame, this, fd, source, healed_sinks,
				    locked_replies);
	if (ret)
		goto out;

	ret = afr_selfheal_undo_pending (frame, this, fd->inode, sources, sinks,
					 healed_sinks, AFR_DATA_TRANSACTION,
					 locked_replies, data_lock);
out:
	if (compat)
		afr_selfheal_uninodelk (frame, this, fd->inode, this->name,
					LLONG_MAX - 2, 1, compat_lock);

        if (locked_replies)
                afr_replies_wipe (locked_replies, priv->child_count);

	return ret;
}


static fd_t *
afr_selfheal_data_open (xlator_t *this, inode_t *inode)
{
	loc_t loc = {0,};
	int ret = 0;
	fd_t *fd = NULL;

	fd = fd_create (inode, 0);
	if (!fd)
		return NULL;

	loc.inode = inode_ref (inode);
	uuid_copy (loc.gfid, inode->gfid);

	ret = syncop_open (this, &loc, O_RDWR|O_LARGEFILE, fd);
	if (ret) {
		fd_unref (fd);
		fd = NULL;
	} else {
		fd_bind (fd);
	}

	loc_wipe (&loc);

	return fd;
}


int
afr_selfheal_data (call_frame_t *frame, xlator_t *this, inode_t *inode)
{
	afr_private_t *priv = NULL;
	unsigned char *locked_on = NULL;
	int ret = 0;
	fd_t *fd = NULL;

	priv = this->private;

	fd = afr_selfheal_data_open (this, inode);
	if (!fd)
		return -EIO;

	locked_on = alloca0 (priv->child_count);

	ret = afr_selfheal_tryinodelk (frame, this, inode, priv->sh_domain, 0, 0,
				       locked_on);
	{
		if (ret < 2) {
			/* Either less than two subvols available, or another
			   selfheal (from another server) is in progress. Skip
			   for now in any case there isn't anything to do.
			*/
			ret = -ENOTCONN;
			goto unlock;
		}

		ret = __afr_selfheal_data (frame, this, fd, locked_on);
	}
unlock:
	afr_selfheal_uninodelk (frame, this, inode, priv->sh_domain, 0, 0, locked_on);

	if (fd)
		fd_unref (fd);

	return ret;
}
