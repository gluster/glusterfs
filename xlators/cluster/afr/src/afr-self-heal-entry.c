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
afr_selfheal_entry_delete (xlator_t *this, inode_t *dir, const char *name,
                           inode_t *inode, int child, struct afr_reply *replies)
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
afr_selfheal_recreate_entry (xlator_t *this, int dst, int source, inode_t *dir,
                             const char *name, inode_t *inode,
                             struct afr_reply *replies,
                             unsigned char *newentry)
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

	ret = afr_selfheal_entry_delete (this, dir, name, inode, dst, replies);
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
                if (ret == 0)
                        newentry[dst] = 1;
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
                        if (ret == 0)
                                newentry[dst] = 1;
                }
		break;
	default:
		ret = dict_set_int32 (xdata, GLUSTERFS_INTERNAL_FOP_KEY, 1);
		if (ret)
			goto out;
		ret = syncop_mknod (priv->children[dst], &loc, mode,
				    iatt->ia_rdev, xdata, &newent);
		if (ret == 0 && newent.ia_nlink == 1) {
			/* New entry created. Mark @dst pending on all sources */
                        newentry[dst] = 1;
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
			ret = afr_selfheal_entry_delete (this, fd->inode, name,
                                                         inode, i, replies);
		} else {
			if (!uuid_compare (replies[i].poststat.ia_gfid,
					   replies[source].poststat.ia_gfid))
				continue;

			ret = afr_selfheal_recreate_entry (this, i, source,
							   fd->inode, name, inode,
							   replies, newentry);
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
afr_selfheal_detect_gfid_and_type_mismatch (xlator_t *this,
                                            struct afr_reply *replies,
                                            uuid_t pargfid, char *bname,
                                            int src_idx)
{
        int             i      = 0;
        char            g1[64] = {0,};
        char            g2[64] = {0,};
        afr_private_t  *priv   = NULL;

        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (i == src_idx)
                        continue;

                if (!replies[i].valid)
                        continue;

                if (replies[i].op_ret != 0)
                        continue;

                if (uuid_compare (replies[src_idx].poststat.ia_gfid,
                                  replies[i].poststat.ia_gfid)) {
                        gf_log (this->name, GF_LOG_ERROR, "Gfid mismatch "
                                "detected for <%s/%s>, %s on %s and %s on %s. "
                                "Skipping conservative merge on the file.",
                                uuid_utoa (pargfid), bname,
                                uuid_utoa_r (replies[i].poststat.ia_gfid, g1),
                                priv->children[i]->name,
                                uuid_utoa_r (replies[src_idx].poststat.ia_gfid,
                                g2), priv->children[src_idx]->name);
                        return -1;
                }

                if ((replies[src_idx].poststat.ia_type) !=
                    (replies[i].poststat.ia_type)) {
                        gf_log (this->name, GF_LOG_ERROR, "Type mismatch "
                                "detected for <%s/%s>, %d on %s and %d on %s. "
                                "Skipping conservative merge on the file.",
                                uuid_utoa (pargfid), bname,
                                replies[i].poststat.ia_type,
                                priv->children[i]->name,
                                replies[src_idx].poststat.ia_type,
                                priv->children[src_idx]->name);
                        return -1;
                }
        }

        return 0;
}

static int
__afr_selfheal_merge_dirent (call_frame_t *frame, xlator_t *this, fd_t *fd,
			     char *name, inode_t *inode, unsigned char *sources,
			     unsigned char *healed_sinks, unsigned char *locked_on,
			     struct afr_reply *replies)
{
        int             ret       = 0;
        int             i         = 0;
        int             source    = -1;
        unsigned char  *newentry  = NULL;
        afr_private_t  *priv      = NULL;

	priv = this->private;

	newentry = alloca0 (priv->child_count);

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

        /* In case of a gfid or type mismatch on the entry, return -1.*/
        ret = afr_selfheal_detect_gfid_and_type_mismatch (this, replies,
                                                          fd->inode->gfid,
                                                          name, source);

        if (ret < 0)
                return ret;

	for (i = 0; i < priv->child_count; i++) {
		if (i == source || !healed_sinks[i])
			continue;

		if (replies[i].op_errno != ENOENT)
			continue;

		ret = afr_selfheal_recreate_entry (this, i, source, fd->inode,
                                                   name, inode, replies,
                                                   newentry);
	}

	if (AFR_COUNT (newentry, priv->child_count))
		afr_selfheal_newentry_mark (frame, this, inode, source, replies,
					    sources, newentry);
	return ret;
}


static int
__afr_selfheal_entry_dirent (call_frame_t *frame, xlator_t *this, fd_t *fd,
                             char *name, inode_t *inode, int source,
                             unsigned char *sources, unsigned char *healed_sinks,
                             unsigned char *locked_on,
                             struct afr_reply *replies)
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
__afr_selfheal_entry_finalize_source (xlator_t *this, unsigned char *sources,
				      unsigned char *healed_sinks,
                                      unsigned char *locked_on,
                                      struct afr_reply *replies,
                                      uint64_t *witness)
{
	int i = 0;
	afr_private_t *priv = NULL;
	int source = -1;
	int sources_count = 0;

	priv = this->private;

	sources_count = AFR_COUNT (sources, priv->child_count);

	if ((AFR_CMP (locked_on, healed_sinks, priv->child_count) == 0)
            || !sources_count || afr_does_witness_exist (this, witness)) {

                memset (sources, 0, sizeof (*sources) * priv->child_count);
                afr_mark_active_sinks (this, sources, locked_on, healed_sinks);
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
        uint64_t *witness = NULL;

	priv = this->private;

	ret = afr_selfheal_unlocked_discover (frame, fd->inode, fd->inode->gfid,
					      replies);
	if (ret)
		return ret;

        witness = alloca0 (sizeof (*witness) * priv->child_count);
	ret = afr_selfheal_find_direction (frame, this, replies,
					   AFR_ENTRY_TRANSACTION,
					   locked_on, sources, sinks, witness);
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
                                                       healed_sinks,
						       locked_on, replies,
                                                       witness);

	if (source < 0) {
		/* If source is < 0 (typically split-brain), we perform a
		   conservative merge of entries rather than erroring out */
	}
	*source_p = source;

	return ret;
}


static int
afr_selfheal_entry_dirent (call_frame_t *frame, xlator_t *this,
                           fd_t *fd, char *name)
{
        int                ret          = 0;
        int                source       = -1;
        unsigned char     *locked_on    = NULL;
        unsigned char     *sources      = NULL;
        unsigned char     *sinks        = NULL;
        unsigned char     *healed_sinks = NULL;
        inode_t           *inode        = NULL;
        struct afr_reply  *replies      = NULL;
        struct afr_reply  *par_replies  = NULL;
        afr_private_t     *priv         = NULL;

	priv = this->private;

        sources = alloca0 (priv->child_count);
        sinks   = alloca0 (priv->child_count);
        healed_sinks = alloca0 (priv->child_count);
	locked_on = alloca0 (priv->child_count);

	replies = alloca0 (priv->child_count * sizeof(*replies));
	par_replies = alloca0 (priv->child_count * sizeof(*par_replies));

        ret = afr_selfheal_entrylk (frame, this, fd->inode, this->name, NULL,
                                    locked_on);
	{
		if (ret < AFR_SH_MIN_PARTICIPANTS) {
                        gf_log (this->name, GF_LOG_DEBUG, "%s: Skipping "
                                "entry self-heal as only %d sub-volumes could "
                                "be locked in %s domain",
                                uuid_utoa (fd->inode->gfid), ret, this->name);
			ret = -ENOTCONN;
			goto unlock;
		}

                ret = __afr_selfheal_entry_prepare (frame, this, fd, locked_on,
                                                    sources, sinks,
                                                    healed_sinks, par_replies,
                                                    &source);
                if (ret < 0)
                        goto unlock;

		inode = afr_selfheal_unlocked_lookup_on (frame, fd->inode, name,
							 replies, locked_on,
                                                         NULL);
		if (!inode) {
			ret = -ENOMEM;
			goto unlock;
		}

		ret = __afr_selfheal_entry_dirent (frame, this, fd, name, inode,
						   source, sources, healed_sinks,
						   locked_on, replies);
	}
unlock:
        afr_selfheal_unentrylk (frame, this, fd->inode, this->name, NULL,
                                locked_on);
	if (inode)
		inode_unref (inode);
        if (replies)
                afr_replies_wipe (replies, priv->child_count);
        if (par_replies)
                afr_replies_wipe (par_replies, priv->child_count);

	return ret;
}


static int
afr_selfheal_entry_do_subvol (call_frame_t *frame, xlator_t *this,
                              fd_t *fd, int child)
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
		ret = afr_selfheal_entry_do_subvol (frame, this, fd, i);
		if (ret)
			break;
	}
	return ret;
}



static int
__afr_selfheal_entry (call_frame_t *frame, xlator_t *this, fd_t *fd,
		      unsigned char *locked_on)
{
	int                     ret                   = -1;
	int                     source                = -1;
	unsigned char          *sources               = NULL;
	unsigned char          *sinks                 = NULL;
	unsigned char          *data_lock             = NULL;
        unsigned char          *postop_lock           = NULL;
	unsigned char          *healed_sinks          = NULL;
	struct afr_reply       *locked_replies        = NULL;
	afr_private_t          *priv                  = NULL;

	priv = this->private;

	sources = alloca0 (priv->child_count);
	sinks = alloca0 (priv->child_count);
	healed_sinks = alloca0 (priv->child_count);
	data_lock = alloca0 (priv->child_count);
        postop_lock = alloca0 (priv->child_count);

	locked_replies = alloca0 (sizeof (*locked_replies) * priv->child_count);

	ret = afr_selfheal_entrylk (frame, this, fd->inode, this->name, NULL,
				    data_lock);
	{
		if (ret < AFR_SH_MIN_PARTICIPANTS) {
                        gf_log (this->name, GF_LOG_DEBUG, "%s: Skipping "
                                "entry self-heal as only %d sub-volumes could "
                                "be locked in %s domain",
                                uuid_utoa (fd->inode->gfid), ret,
                                this->name);
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

        /* Take entrylks in xlator domain before doing post-op (undo-pending) in
         * entry self-heal. This is to prevent a parallel name self-heal on
         * an entry under @fd->inode from reading pending xattrs while it is
         * being modified by SHD after entry sh below, given that
         * name self-heal takes locks ONLY in xlator domain and is free to read
         * pending changelog in the absence of the following locking.
         */
        ret = afr_selfheal_entrylk (frame, this, fd->inode, this->name, NULL,
                                    postop_lock);
        {
                if (AFR_CMP (data_lock, postop_lock, priv->child_count) != 0) {
                        gf_log (this->name, GF_LOG_DEBUG, "%s: Skipping "
                                "post-op after entry self-heal as %d "
                                "sub-volumes, as opposed to %d, could be locked"
                                " in %s domain", uuid_utoa (fd->inode->gfid),
                                ret, AFR_COUNT (data_lock, priv->child_count),
                                this->name);
                        ret = -ENOTCONN;
                        goto postop_unlock;
                }

                ret = afr_selfheal_undo_pending (frame, this, fd->inode,
                                                 sources, sinks, healed_sinks,
                                                 AFR_ENTRY_TRANSACTION,
                                                 locked_replies, postop_lock);
        }
postop_unlock:
        afr_selfheal_unentrylk (frame, this, fd->inode, this->name, NULL,
                                postop_lock);
out:
        afr_log_selfheal (fd->inode->gfid, this, ret, "entry", source,
                          healed_sinks);

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
		if (ret < AFR_SH_MIN_PARTICIPANTS) {
                        gf_log (this->name, GF_LOG_DEBUG, "%s: Skipping "
                                "entry self-heal as only %d sub-volumes could "
                                "be locked in %s domain",
                                uuid_utoa (fd->inode->gfid), ret,
                                priv->sh_domain);
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
