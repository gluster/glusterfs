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
#include "afr-transaction.h"


static int
afr_selfheal_entry_delete (call_frame_t *frame, xlator_t *this, inode_t *dir,
			   const char *name, inode_t *inode, int child,
			   struct afr_reply *replies)
{
	afr_private_t *priv = NULL;
	xlator_t *subvol = NULL;
	int ret = 0;
	loc_t loc = {0, };
	char g[64];

	priv = this->private;

	subvol = priv->children[child];

	loc.parent = inode_ref (dir);
	uuid_copy (loc.pargfid, dir->gfid);
	loc.name = name;
	loc.inode = inode_ref (inode);

	if (replies[child].valid && replies[child].op_ret == 0) {
		switch (replies[child].poststat.ia_type) {
		case IA_IFDIR:
			gf_log (this->name, GF_LOG_WARNING,
				"expunging dir %s/%s (%s) on %s",
				uuid_utoa (dir->gfid), name,
				uuid_utoa_r (replies[child].poststat.ia_gfid, g),
				subvol->name);
			ret = syncop_rmdir (subvol, &loc, 1);
			break;
		default:
			gf_log (this->name, GF_LOG_WARNING,
				"expunging file %s/%s (%s) on %s",
				uuid_utoa (dir->gfid), name,
				uuid_utoa_r (replies[child].poststat.ia_gfid, g),
				subvol->name);
			ret = syncop_unlink (subvol, &loc);
			break;
		}
	}

	loc_wipe (&loc);

	return ret;
}


int
afr_selfheal_recreate_entry (call_frame_t *frame, xlator_t *this, int dst,
			     int source, inode_t *dir, const char *name,
			     inode_t *inode, struct afr_reply *replies)
{
	int ret = 0;
	loc_t loc = {0,};
	loc_t srcloc = {0,};
	afr_private_t *priv = NULL;
	dict_t *xdata = NULL;
	struct iatt *iatt = NULL;
	char *linkname = NULL;
	mode_t mode = 0;
	struct iatt newent = {0,};

	priv = this->private;

	xdata = dict_new();
	if (!xdata)
		return -ENOMEM;

	loc.parent = inode_ref (dir);
	uuid_copy (loc.pargfid, dir->gfid);
	loc.name = name;
	loc.inode = inode_ref (inode);

	ret = afr_selfheal_entry_delete (frame, this, dir, name, inode, dst,
					 replies);
	if (ret)
		goto out;

	ret = dict_set_static_bin (xdata, "gfid-req",
				   replies[source].poststat.ia_gfid, 16);
	if (ret)
		goto out;

	iatt = &replies[source].poststat;

	srcloc.inode = inode_ref (inode);
	uuid_copy (srcloc.gfid, iatt->ia_gfid);

	mode = st_mode_from_ia (iatt->ia_prot, iatt->ia_type);

	switch (iatt->ia_type) {
	case IA_IFDIR:
		ret = syncop_mkdir (priv->children[dst], &loc, mode, xdata, 0);
		break;
	case IA_IFLNK:
		ret = syncop_lookup (priv->children[dst], &srcloc, 0, 0, 0, 0);
		if (ret == 0) {
			ret = syncop_link (priv->children[dst], &srcloc, &loc);
		} else {
			ret = syncop_readlink (priv->children[source], &srcloc,
					       &linkname, 4096);
			if (ret <= 0)
				goto out;
			ret = syncop_symlink (priv->children[dst], &loc, linkname,
					      xdata, NULL);
		}
		break;
	default:
		ret = dict_set_int32 (xdata, GLUSTERFS_INTERNAL_FOP_KEY, 1);
		if (ret)
			goto out;
		ret = syncop_mknod (priv->children[dst], &loc, mode,
				    iatt->ia_rdev, xdata, &newent);
		if (ret == 0 && iatt->ia_size && !newent.ia_size) {
			/* New entry created. Mark @dst pending on all sources */
			ret = 1;
		}
		break;
	}

out:
	if (xdata)
		dict_unref (xdata);
	loc_wipe (&loc);
	loc_wipe (&srcloc);
	return ret;
}


static int
afr_selfheal_newentry_mark (call_frame_t *frame, xlator_t *this, inode_t *inode,
			    int source, struct afr_reply *replies,
			    unsigned char *sources, unsigned char *newentry)
{
	int ret = 0;
	int i = 0;
	afr_private_t *priv = NULL;
	dict_t *xattr = NULL;
	int **changelog = NULL;
	int idx = 0;

	priv = this->private;

	idx = afr_index_for_transaction_type (AFR_DATA_TRANSACTION);

	uuid_copy (inode->gfid, replies[source].poststat.ia_gfid);

	changelog = afr_matrix_create (priv->child_count, AFR_NUM_CHANGE_LOGS);

	xattr = dict_new();
	if (!xattr)
		return -ENOMEM;

	for (i = 0; i < priv->child_count; i++) {
		if (!newentry[i])
			continue;
		changelog[i][idx] = hton32(1);
	}

	afr_set_pending_dict (priv, xattr, changelog);

	for (i = 0; i < priv->child_count; i++) {
		if (!sources[i])
			continue;
		afr_selfheal_post_op (frame, this, inode, i, xattr);
	}

	dict_unref (xattr);
	return ret;
}


static int
__afr_selfheal_heal_dirent (call_frame_t *frame, xlator_t *this, fd_t *fd,
			    char *name, inode_t *inode, int source,
			    unsigned char *sources, unsigned char *healed_sinks,
			    unsigned char *locked_on, struct afr_reply *replies)
{
	int ret = 0;
	afr_private_t *priv = NULL;
	int i = 0;
	unsigned char *newentry = NULL;

	priv = this->private;
	newentry = alloca0 (priv->child_count);

	if (!replies[source].valid)
		return -EIO;

	for (i = 0; i < priv->child_count; i++) {
		if (!healed_sinks[i])
			continue;
		if (replies[source].op_ret == -1 &&
		    replies[source].op_errno == ENOENT) {
			ret = afr_selfheal_entry_delete (frame, this, fd->inode,
							 name, inode, i, replies);
		} else {
			if (!uuid_compare (replies[i].poststat.ia_gfid,
					   replies[source].poststat.ia_gfid))
				continue;

			ret = afr_selfheal_recreate_entry (frame, this, i, source,
							   fd->inode, name, inode,
							   replies);
			if (ret > 0) {
				newentry[i] = 1;
				ret = 0;
			}
		}
		if (ret < 0)
			break;
	}

	if (AFR_COUNT (newentry, priv->child_count))
		afr_selfheal_newentry_mark (frame, this, inode, source, replies,
					    sources, newentry);
	return ret;
}


static int
__afr_selfheal_merge_dirent (call_frame_t *frame, xlator_t *this, fd_t *fd,
			     char *name, inode_t *inode, unsigned char *sources,
			     unsigned char *healed_sinks, unsigned char *locked_on,
			     struct afr_reply *replies)
{
	int ret = 0;
	afr_private_t *priv = NULL;
	int i = 0;
	int source = -1;

	priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
		if (replies[i].valid && replies[i].op_ret == 0) {
			source = i;
			break;
		}
	}

	if (source == -1) {
		/* entry got deleted in the mean time? */
		return 0;
	}

	for (i = 0; i < priv->child_count; i++) {
		if (i == source || !healed_sinks[i])
			continue;

		if (replies[i].op_errno != ENOENT)
			continue;

		ret = afr_selfheal_recreate_entry (frame, this, i, source,
						   fd->inode, name, inode,
						   replies);
	}

	return ret;
}


static int
__afr_selfheal_entry_dirent (call_frame_t *frame, xlator_t *this, fd_t *fd,
			     char *name, inode_t *inode, int source,
			     unsigned char *sources, unsigned char *healed_sinks,
			     unsigned char *locked_on, struct afr_reply *replies)
{
	int ret = -1;

	if (source < 0)
		ret = __afr_selfheal_merge_dirent (frame, this, fd, name, inode,
						   sources, healed_sinks,
						   locked_on, replies);
	else
		ret = __afr_selfheal_heal_dirent (frame, this, fd, name, inode,
						  source, sources, healed_sinks,
						  locked_on, replies);
	return ret;
}


static int
afr_selfheal_entry_dirent (call_frame_t *frame, xlator_t *this, fd_t *fd,
			   int source, unsigned char *sources,
			   unsigned char *healed_sinks, char *name)
{
	afr_private_t *priv = NULL;
	int ret = 0;
	unsigned char *locked_on = NULL;
	struct afr_reply *replies = NULL;
	inode_t *inode = NULL;

	priv = this->private;

	locked_on = alloca0 (priv->child_count);

	replies = alloca0 (priv->child_count * sizeof(*replies));

	ret = afr_selfheal_entrylk (frame, this, fd->inode, this->name,
				    name, locked_on);
	{
		if (ret < 2) {
			ret = -ENOTCONN;
			goto unlock;
		}

		inode = afr_selfheal_unlocked_lookup_on (frame, fd->inode, name,
							 replies, locked_on);
		if (!inode) {
			ret = -ENOMEM;
			goto unlock;
		}

		ret = __afr_selfheal_entry_dirent (frame, this, fd, name, inode,
						   source, sources, healed_sinks,
						   locked_on, replies);
	}
unlock:
	afr_selfheal_unentrylk (frame, this, fd->inode, this->name, name,
				locked_on);
	if (inode)
		inode_unref (inode);
        if (replies)
                afr_replies_wipe (replies, priv->child_count);
	return ret;
}


static int
afr_selfheal_entry_do_subvol (call_frame_t *frame, xlator_t *this, fd_t *fd,
			      int child, int source, unsigned char *sources,
			      unsigned char *healed_sinks)
{
	int ret = 0;
	gf_dirent_t entries;
	gf_dirent_t *entry = NULL;
	off_t offset = 0;
	call_frame_t *iter_frame = NULL;
	xlator_t *subvol = NULL;
	afr_private_t *priv = NULL;

	priv = this->private;
	subvol = priv->children[child];

	INIT_LIST_HEAD (&entries.list);

	iter_frame = afr_copy_frame (frame);
	if (!iter_frame)
		return -ENOMEM;

	while ((ret = syncop_readdir (subvol, fd, 131072, offset, &entries))) {
		if (ret > 0)
			ret = 0;
		list_for_each_entry (entry, &entries.list, list) {
			offset = entry->d_off;

			if (!strcmp (entry->d_name, ".") ||
			    !strcmp (entry->d_name, ".."))
				continue;

			if (__is_root_gfid (fd->inode->gfid) &&
			    !strcmp (entry->d_name, GF_REPLICATE_TRASH_DIR))
				continue;

			ret = afr_selfheal_entry_dirent (iter_frame, this, fd,
							 source, sources,
							 healed_sinks,
							 entry->d_name);
			AFR_STACK_RESET (iter_frame);

			if (ret)
				break;
		}

		gf_dirent_free (&entries);
		if (ret)
			break;
	}

	AFR_STACK_DESTROY (iter_frame);
	return ret;
}

static int
afr_selfheal_entry_do (call_frame_t *frame, xlator_t *this, fd_t *fd,
		       int source, unsigned char *sources,
		       unsigned char *healed_sinks)
{
	int i = 0;
	afr_private_t *priv = NULL;
	int ret = 0;

	priv = this->private;

	gf_log (this->name, GF_LOG_INFO, "performing entry selfheal on %s",
		uuid_utoa (fd->inode->gfid));

	for (i = 0; i < priv->child_count; i++) {
		if (i != source && !healed_sinks[i])
			continue;
		ret = afr_selfheal_entry_do_subvol (frame, this, fd, i, source,
						    sources, healed_sinks);
		if (ret)
			break;
	}
	return ret;
}


static int
__afr_selfheal_entry_finalize_source (xlator_t *this, unsigned char *sources,
				      unsigned char *healed_sinks,
				      unsigned char *locked_on)
{
	int i = 0;
	afr_private_t *priv = NULL;
	int source = -1;
	int sources_count = 0;

	priv = this->private;

	sources_count = AFR_COUNT (sources, priv->child_count);

	if ((AFR_CMP (locked_on, healed_sinks, priv->child_count) == 0)
            || !sources_count) {
		return -1;
	}

	for (i = 0; i < priv->child_count; i++) {
		if (sources[i]) {
			source = i;
			break;
		}
	}

	return source;
}


static int
__afr_selfheal_entry_prepare (call_frame_t *frame, xlator_t *this, fd_t *fd,
			      unsigned char *locked_on, unsigned char *sources,
			      unsigned char *sinks, unsigned char *healed_sinks,
			      struct afr_reply *replies, int *source_p)
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
					   AFR_ENTRY_TRANSACTION,
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

	source = __afr_selfheal_entry_finalize_source (this, sources,
                                                       healed_sinks, locked_on);
	if (source < 0) {
		/* If source is < 0 (typically split-brain), we perform a
		   conservative merge of entries rather than erroring out */
	}
	*source_p = source;

	return ret;
}


static int
__afr_selfheal_entry (call_frame_t *frame, xlator_t *this, fd_t *fd,
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

	priv = this->private;

	sources = alloca0 (priv->child_count);
	sinks = alloca0 (priv->child_count);
	healed_sinks = alloca0 (priv->child_count);
	data_lock = alloca0 (priv->child_count);

	locked_replies = alloca0 (sizeof (*locked_replies) * priv->child_count);

	ret = afr_selfheal_entrylk (frame, this, fd->inode, this->name, NULL,
				    data_lock);
	{
		if (ret < 2) {
			ret = -ENOTCONN;
			goto unlock;
		}

		ret = __afr_selfheal_entry_prepare (frame, this, fd, data_lock,
						    sources, sinks, healed_sinks,
						    locked_replies, &source);
	}
unlock:
	afr_selfheal_unentrylk (frame, this, fd->inode, this->name, NULL,
				data_lock);
	if (ret < 0)
		goto out;

	ret = afr_selfheal_entry_do (frame, this, fd, source, sources,
				     healed_sinks);
	if (ret)
		goto out;

	ret = afr_selfheal_undo_pending (frame, this, fd->inode, sources, sinks,
					 healed_sinks, AFR_ENTRY_TRANSACTION,
					 locked_replies, data_lock);
out:
        if (locked_replies)
                afr_replies_wipe (locked_replies, priv->child_count);
	return ret;
}


static fd_t *
afr_selfheal_data_opendir (xlator_t *this, inode_t *inode)
{
	loc_t loc = {0,};
	int ret = 0;
	fd_t *fd = NULL;

	fd = fd_create (inode, 0);
	if (!fd)
		return NULL;

	loc.inode = inode_ref (inode);
	uuid_copy (loc.gfid, inode->gfid);

	ret = syncop_opendir (this, &loc, fd);
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
afr_selfheal_entry (call_frame_t *frame, xlator_t *this, inode_t *inode)
{
	afr_private_t *priv = NULL;
	unsigned char *locked_on = NULL;
	fd_t *fd = NULL;
	int ret = 0;

	priv = this->private;

	fd = afr_selfheal_data_opendir (this, inode);
	if (!fd)
		return -EIO;

	locked_on = alloca0 (priv->child_count);

	ret = afr_selfheal_tryentrylk (frame, this, inode, priv->sh_domain, NULL,
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

		ret = __afr_selfheal_entry (frame, this, fd, locked_on);
	}
unlock:
	afr_selfheal_unentrylk (frame, this, inode, priv->sh_domain, NULL, locked_on);

	if (fd)
		fd_unref (fd);

	return ret;
}
