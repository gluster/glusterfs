/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include "afr.h"
#include "afr-self-heal.h"
#include "byte-order.h"
#include "afr-transaction.h"
#include "afr-messages.h"
#include "syncop-utils.h"
#include "events.h"

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
	gf_uuid_copy (loc.pargfid, dir->gfid);
	loc.name = name;
	loc.inode = inode_ref (inode);

	if (replies[child].valid && replies[child].op_ret == 0) {
		switch (replies[child].poststat.ia_type) {
		case IA_IFDIR:
		        gf_msg (this->name, GF_LOG_WARNING, 0,
                                AFR_MSG_EXPUNGING_FILE_OR_DIR,
				"expunging dir %s/%s (%s) on %s",
				uuid_utoa (dir->gfid), name,
				uuid_utoa_r (replies[child].poststat.ia_gfid, g),
				subvol->name);
			ret = syncop_rmdir (subvol, &loc, 1, NULL, NULL);
			break;
		default:
		        gf_msg (this->name, GF_LOG_WARNING, 0,
                                AFR_MSG_EXPUNGING_FILE_OR_DIR,
				"expunging file %s/%s (%s) on %s",
				uuid_utoa (dir->gfid), name,
				uuid_utoa_r (replies[child].poststat.ia_gfid, g),
				subvol->name);
			ret = syncop_unlink (subvol, &loc, NULL, NULL);
			break;
		}
	}

	loc_wipe (&loc);

	return ret;
}


int
afr_selfheal_recreate_entry (call_frame_t *frame, int dst, int source,
                             unsigned char *sources, inode_t *dir,
                             const char *name, inode_t *inode,
                             struct afr_reply *replies)
{
	int ret = 0;
	loc_t loc = {0,};
	loc_t srcloc = {0,};
        xlator_t *this = frame->this;
	afr_private_t *priv = NULL;
	dict_t *xdata = NULL;
	struct iatt *iatt = NULL;
	char *linkname = NULL;
	mode_t mode = 0;
	struct iatt newent = {0,};
        unsigned char *newentry = NULL;

        priv = this->private;
	xdata = dict_new();
	if (!xdata)
		return -ENOMEM;

        newentry = alloca0 (priv->child_count);
	loc.parent = inode_ref (dir);
	gf_uuid_copy (loc.pargfid, dir->gfid);
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
	gf_uuid_copy (srcloc.gfid, iatt->ia_gfid);
        if (iatt->ia_type != IA_IFDIR)
                ret = syncop_lookup (priv->children[dst], &srcloc, 0, 0, 0, 0);
        if (iatt->ia_type == IA_IFDIR || ret == -ENOENT || ret == -ESTALE) {
                newentry[dst] = 1;
                ret = afr_selfheal_newentry_mark (frame, this, inode, source,
                                                  replies, sources, newentry);
                if (ret)
                        goto out;
        }

	mode = st_mode_from_ia (iatt->ia_prot, iatt->ia_type);

	switch (iatt->ia_type) {
	case IA_IFDIR:
		ret = syncop_mkdir (priv->children[dst], &loc, mode, 0,
                                    xdata, NULL);
		break;
	case IA_IFLNK:
		if (!newentry[dst]) {
			ret = syncop_link (priv->children[dst], &srcloc, &loc,
					   &newent, NULL, NULL);
		} else {
			ret = syncop_readlink (priv->children[source], &srcloc,
					       &linkname, 4096, NULL, NULL);
			if (ret <= 0)
				goto out;
			ret = syncop_symlink (priv->children[dst], &loc,
                                              linkname, NULL, xdata, NULL);
                }
		break;
	default:
		ret = dict_set_int32 (xdata, GLUSTERFS_INTERNAL_FOP_KEY, 1);
		if (ret)
			goto out;
		ret = syncop_mknod (priv->children[dst], &loc, mode,
                    makedev (ia_major(iatt->ia_rdev), ia_minor (iatt->ia_rdev)),
                    &newent, xdata, NULL);
		break;
	}

out:
	if (xdata)
		dict_unref (xdata);
	GF_FREE (linkname);
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

	priv = this->private;

	if (!replies[source].valid)
		return -EIO;

        /* Skip healing this entry if the last lookup on it failed for reasons
         * other than ENOENT.
         */
        if ((replies[source].op_ret < 0) &&
            (replies[source].op_errno != ENOENT))
                return -replies[source].op_errno;

	for (i = 0; i < priv->child_count; i++) {
		if (!healed_sinks[i])
			continue;
		if (replies[source].op_ret == -1 &&
		    replies[source].op_errno == ENOENT) {
			ret = afr_selfheal_entry_delete (this, fd->inode, name,
                                                         inode, i, replies);
                } else {
			if (!gf_uuid_compare (replies[i].poststat.ia_gfid,
					   replies[source].poststat.ia_gfid))
				continue;

			ret = afr_selfheal_recreate_entry (frame, i, source,
                                                           sources, fd->inode,
                                                           name, inode,
							   replies);
                }
		if (ret < 0)
			break;
	}

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

                if (gf_uuid_compare (replies[src_idx].poststat.ia_gfid,
                                  replies[i].poststat.ia_gfid)) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                AFR_MSG_SPLIT_BRAIN, "Gfid mismatch "
                                "detected for <gfid:%s>/%s>, %s on %s and %s on %s. "
                                "Skipping conservative merge on the file.",
                                uuid_utoa (pargfid), bname,
                                uuid_utoa_r (replies[i].poststat.ia_gfid, g1),
                                priv->children[i]->name,
                                uuid_utoa_r (replies[src_idx].poststat.ia_gfid,
                                g2), priv->children[src_idx]->name);
                        gf_event (EVENT_AFR_SPLIT_BRAIN,
                                 "subvol=%s;type=gfid;file=<gfid:%s>/%s>;count=2;"
                                 "child-%d=%s;gfid-%d=%s;child-%d=%s;gfid-%d=%s",
                                 this->name, uuid_utoa (pargfid), bname, i,
                                 priv->children[i]->name, i,
                                 uuid_utoa_r (replies[i].poststat.ia_gfid, g1),
                                src_idx, priv->children[src_idx]->name, src_idx,
                           uuid_utoa_r (replies[src_idx].poststat.ia_gfid, g2));
                        return -1;
                }

                if ((replies[src_idx].poststat.ia_type) !=
                    (replies[i].poststat.ia_type)) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                AFR_MSG_SPLIT_BRAIN, "Type mismatch "
                                "detected for <gfid:%s>/%s>, %s on %s and %s on %s. "
                                "Skipping conservative merge on the file.",
                                uuid_utoa (pargfid), bname,
                             gf_inode_type_to_str (replies[i].poststat.ia_type),
                                priv->children[i]->name,
                       gf_inode_type_to_str (replies[src_idx].poststat.ia_type),
                                priv->children[src_idx]->name);
                        gf_event (EVENT_AFR_SPLIT_BRAIN,
                                 "subvol=%s;type=file;file=<gfid:%s>/%s>;count=2;"
                                 "child-%d=%s;type-%d=%s;child-%d=%s;type-%d=%s",
                                 this->name, uuid_utoa (pargfid), bname, i,
                                 priv->children[i]->name, i,
                              gf_inode_type_to_str(replies[i].poststat.ia_type),
                                src_idx, priv->children[src_idx]->name, src_idx,
                       gf_inode_type_to_str(replies[src_idx].poststat.ia_type));
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
        afr_private_t  *priv      = NULL;

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

        /* Set all the sources as 1, otheriwse newentry_mark won't be set */
	for (i = 0; i < priv->child_count; i++) {
		if (replies[i].valid && replies[i].op_ret == 0) {
			sources[i] = 1;
		}
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

		ret = afr_selfheal_recreate_entry (frame, i, source, sources,
                                                   fd->inode, name, inode,
                                                   replies);
	}

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

static gf_boolean_t
is_full_heal_marker_present (xlator_t *this, dict_t *xdata, int idx)
{
        int                  i           = 0;
        int                  pending[3]  = {0,};
        void                *pending_raw = NULL;
        afr_private_t       *priv        = NULL;

        priv = this->private;

        if (!xdata)
                return _gf_false;

        /* Iterate over each of the priv->pending_keys[] elements and then
         * see if any of them have data segment non-zero. If they do, return
         * true. Else return false.
         */
        for (i = 0; i < priv->child_count; i++) {
                if (dict_get_ptr (xdata, priv->pending_key[i], &pending_raw))
                        continue;

                if (!pending_raw)
                        continue;

                memcpy (pending, pending_raw, sizeof (pending));
                if (ntoh32 (pending[idx]))
                        return _gf_true;
        }

        return _gf_false;
}

static gf_boolean_t
afr_need_full_heal (xlator_t *this, struct afr_reply *replies, int source,
                    unsigned char *healed_sinks, afr_transaction_type type)
{
        int                i     = 0;
        int                idx   = 0;
        afr_private_t     *priv  = NULL;

        priv = this->private;

        if (!priv->esh_granular)
                return _gf_true;

        if (type != AFR_ENTRY_TRANSACTION)
                return _gf_true;

        priv = this->private;
        idx = afr_index_for_transaction_type (AFR_DATA_TRANSACTION);

        /* If there is a clear source, check whether the full-heal-indicator
         * is present in its xdata. Otherwise, we need to examine all the
         * participating bricks and then figure if *even* one of them has a
         * full-heal-indicator.
         */

        if (source != -1) {
                if (is_full_heal_marker_present (this, replies[source].xdata,
                                                 idx))
                        return _gf_true;
        }

        /* else ..*/

        for (i = 0; i < priv->child_count; i++) {
                if (!healed_sinks[i])
                        continue;

                if (is_full_heal_marker_present (this, replies[i].xdata, idx))
                        return _gf_true;
        }

        return _gf_false;
}

static int
__afr_selfheal_entry_finalize_source (xlator_t *this, unsigned char *sources,
				      unsigned char *healed_sinks,
                                      unsigned char *locked_on,
                                      struct afr_reply *replies,
                                      uint64_t *witness)
{
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

        source = afr_choose_source_by_policy (priv, sources,
                                              AFR_ENTRY_TRANSACTION);
	return source;
}

int
__afr_selfheal_entry_prepare (call_frame_t *frame, xlator_t *this,
                              inode_t *inode, unsigned char *locked_on,
                              unsigned char *sources, unsigned char *sinks,
                              unsigned char *healed_sinks,
			      struct afr_reply *replies, int *source_p,
                              gf_boolean_t *pflag)
{
	int ret = -1;
	int source = -1;
	afr_private_t *priv = NULL;
        uint64_t *witness = NULL;

	priv = this->private;

	ret = afr_selfheal_unlocked_discover (frame, inode, inode->gfid,
					      replies);
        if (ret)
                return ret;

        witness = alloca0 (sizeof (*witness) * priv->child_count);
	ret = afr_selfheal_find_direction (frame, this, replies,
					   AFR_ENTRY_TRANSACTION,
					   locked_on, sources, sinks, witness,
                                           pflag);
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
                           fd_t *fd, char *name, inode_t *parent_idx_inode,
                           xlator_t *subvol, gf_boolean_t full_crawl)
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
                        gf_msg_debug (this->name, 0, "%s: Skipping "
                                      "entry self-heal as only %d sub-volumes "
                                      " could be locked in %s domain",
                                      uuid_utoa (fd->inode->gfid),
                                      ret, this->name);
			ret = -ENOTCONN;
			goto unlock;
		}

                ret = __afr_selfheal_entry_prepare (frame, this, fd->inode,
                                                    locked_on,
                                                    sources, sinks,
                                                    healed_sinks, par_replies,
                                                    &source, NULL);
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

                if ((ret == 0) && (priv->esh_granular) && parent_idx_inode) {
                        ret = afr_shd_index_purge (subvol, parent_idx_inode,
                                                   name, inode->ia_type);
                        /* Why is ret force-set to 0? We do not care about
                         * index purge failing for full heal as it is quite
                         * possible during replace-brick that not all files
                         * and directories have their name indices present in
                         * entry-changes/.
                         */
                        ret = 0;
                }
	}

unlock:
        afr_selfheal_unentrylk (frame, this, fd->inode, this->name, NULL,
                                locked_on, NULL);
	if (inode)
		inode_unref (inode);
        if (replies)
                afr_replies_wipe (replies, priv->child_count);
        if (par_replies)
                afr_replies_wipe (par_replies, priv->child_count);

	return ret;
}


static inode_t *
afr_shd_entry_changes_index_inode (xlator_t *this, xlator_t *subvol,
                                   uuid_t pargfid)
{
        int             ret         = -1;
        void           *index_gfid  = NULL;
        loc_t           rootloc     = {0,};
        loc_t           loc         = {0,};
        dict_t         *xattr       = NULL;
        inode_t        *inode       = NULL;
        struct iatt     iatt        = {0,};

        rootloc.inode = inode_ref (this->itable->root);
        gf_uuid_copy (rootloc.gfid, rootloc.inode->gfid);

        ret = syncop_getxattr (subvol, &rootloc, &xattr,
                               GF_XATTROP_ENTRY_CHANGES_GFID, NULL, NULL);
        if (ret || !xattr) {
                errno = -ret;
                goto out;
        }

        ret = dict_get_ptr (xattr, GF_XATTROP_ENTRY_CHANGES_GFID, &index_gfid);
        if (ret) {
                errno = EINVAL;
                goto out;
        }

        loc.inode = inode_new (this->itable);
        if (!loc.inode) {
                errno = ENOMEM;
                goto out;
        }

        gf_uuid_copy (loc.pargfid, index_gfid);
        loc.name = gf_strdup (uuid_utoa (pargfid));

        ret = syncop_lookup (subvol, &loc, &iatt, NULL, NULL, NULL);
        if (ret < 0) {
                errno = -ret;
                goto out;
        }

        inode = inode_link (loc.inode, NULL, NULL, &iatt);

out:
        if (xattr)
                dict_unref (xattr);
        loc_wipe (&rootloc);
        GF_FREE ((char *)loc.name);
        loc_wipe (&loc);

        return inode;
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
        gf_boolean_t mismatch = _gf_false;
        afr_local_t *local = NULL;
        loc_t loc = {0,};

	priv = this->private;
	subvol = priv->children[child];

	INIT_LIST_HEAD (&entries.list);

        local = frame->local;

	iter_frame = afr_copy_frame (frame);
	if (!iter_frame)
		return -ENOMEM;

        loc.inode = afr_shd_entry_changes_index_inode (this, subvol,
                                                       fd->inode->gfid);

	while ((ret = syncop_readdir (subvol, fd, 131072, offset, &entries,
                                      NULL, NULL))) {
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
                                                         entry->d_name,
                                                         loc.inode, subvol,
                                                        local->need_full_crawl);
			AFR_STACK_RESET (iter_frame);
			if (iter_frame->local == NULL) {
                                ret = -ENOTCONN;
                                break;
                        }

                        if (ret == -1) {
                                /* gfid or type mismatch. */
                                mismatch = _gf_true;
                                ret = 0;
                        }
			if (ret)
				break;
		}

		gf_dirent_free (&entries);
		if (ret)
			break;
	}

        loc_wipe (&loc);

	AFR_STACK_DESTROY (iter_frame);
        if (mismatch == _gf_true)
                /* undo pending will be skipped */
                ret = -1;
	return ret;
}

static int
afr_selfheal_entry_granular_dirent (xlator_t *subvol, gf_dirent_t *entry,
                                    loc_t *parent, void *data)
{
        int                      ret  = 0;
        loc_t                    loc  = {0,};
        struct iatt              iatt = {0,};
        afr_granular_esh_args_t *args = data;

        /* Look up the actual inode associated with entry. If the lookup returns
         * ESTALE or ENOENT, then it means we have a stale index. Remove it.
         * This is analogous to the check in afr_shd_index_heal() except that
         * here it is achieved through LOOKUP and in afr_shd_index_heal() through
         * a GETXATTR.
         */

        loc.inode = inode_new (args->xl->itable);
        loc.parent = inode_ref (args->heal_fd->inode);
        gf_uuid_copy (loc.pargfid, loc.parent->gfid);
        loc.name = entry->d_name;

        ret = syncop_lookup (args->xl, &loc, &iatt, NULL, NULL, NULL);
        if ((ret == -ENOENT) || (ret == -ESTALE)) {
                /* The name indices under the pgfid index dir are guaranteed
                 * to be regular files. Hence the hardcoding.
                 */
                afr_shd_index_purge (subvol, parent->inode, entry->d_name,
                                     IA_IFREG);
                ret = 0;
                goto out;
        }
        /* TBD: afr_shd_zero_xattrop? */

        ret = afr_selfheal_entry_dirent (args->frame, args->xl, args->heal_fd,
                                         entry->d_name, parent->inode, subvol,
                                         _gf_false);
        AFR_STACK_RESET (args->frame);
        if (args->frame->local == NULL)
                ret = -ENOTCONN;

        if (ret == -1)
                args->mismatch = _gf_true;

out:
        loc_wipe (&loc);
        return 0;
}

static int
afr_selfheal_entry_granular (call_frame_t *frame, xlator_t *this, fd_t *fd,
                             int subvol_idx, gf_boolean_t is_src)
{
        int                         ret    = 0;
        loc_t                       loc    = {0,};
        xlator_t                   *subvol = NULL;
        afr_private_t              *priv   = NULL;
        afr_granular_esh_args_t     args   = {0,};

        priv = this->private;
        subvol = priv->children[subvol_idx];

        args.frame = afr_copy_frame (frame);
        args.xl = this;
        /* args.heal_fd represents the fd associated with the original directory
         * on which entry heal is being attempted.
         */
        args.heal_fd = fd;

        /* @subvol here represents the subvolume of AFR where
         * indices/entry-changes/<pargfid> will be processed
         */
        loc.inode = afr_shd_entry_changes_index_inode (this, subvol,
                                                       fd->inode->gfid);
        if (!loc.inode) {
                /* If granular heal failed on the sink (as it might sometimes
                 * because it is the src that would mostly contain the granular
                 * changelogs and the sink's entry-changes would be empty),
                 * do not treat heal as failure.
                 */
                if (is_src)
                        return -errno;
                else
                        return 0;
        }

        ret = syncop_dir_scan (subvol, &loc, GF_CLIENT_PID_SELF_HEALD,
                               &args, afr_selfheal_entry_granular_dirent);

        loc_wipe (&loc);

        if (args.mismatch == _gf_true)
                ret = -1;

        return ret;
}

static int
afr_selfheal_entry_do (call_frame_t *frame, xlator_t *this, fd_t *fd,
		       int source, unsigned char *sources,
		       unsigned char *healed_sinks)
{
	int i                   = 0;
	int ret                 = 0;
        gf_boolean_t   mismatch = _gf_false;
        afr_local_t   *local    = NULL;
	afr_private_t *priv     = NULL;

	priv = this->private;
        local = frame->local;

        gf_msg (this->name, GF_LOG_INFO, 0,
                AFR_MSG_SELF_HEAL_INFO, "performing entry selfheal on %s",
		uuid_utoa (fd->inode->gfid));

	for (i = 0; i < priv->child_count; i++) {
                /* Expunge */
		if (!healed_sinks[i])
			continue;

                if (!local->need_full_crawl)
                /* Why call afr_selfheal_entry_granular() on a "healed sink",
                 * given that it is the source that contains the granular
                 * indices?
                 * If the index for this directory is non-existent or empty on
                 * this subvol (=> clear sink), then it will return early
                 * without failure status.
                 * If the index is non-empty and it is yet a 'healed sink', then
                 * it is due to a split-brain in which case we anyway need to
                 * crawl the indices/entry-changes/pargfid directory.
                 */
                        ret = afr_selfheal_entry_granular (frame, this, fd, i,
                                                           _gf_false);
                else
                        ret = afr_selfheal_entry_do_subvol (frame, this, fd, i);

                if (ret == -1) {
                        /* gfid or type mismatch. */
                        mismatch = _gf_true;
                        ret = 0;
                }
                if (ret)
                        break;
	}

        if (!ret && source != -1) {
                /* Impunge */
                if (local->need_full_crawl)
                        ret = afr_selfheal_entry_do_subvol (frame, this, fd,
                                                            source);
                else
                        ret = afr_selfheal_entry_granular (frame, this, fd,
                                                           source, _gf_true);
        }

        if (mismatch == _gf_true)
                /* undo pending will be skipped */
                ret = -1;
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
	unsigned char          *undid_pending         = NULL;
	struct afr_reply       *locked_replies        = NULL;
        afr_local_t            *local                 = NULL;
	afr_private_t          *priv                  = NULL;
        gf_boolean_t            did_sh                = _gf_true;

	priv = this->private;
        local = frame->local;

	sources = alloca0 (priv->child_count);
	sinks = alloca0 (priv->child_count);
	healed_sinks = alloca0 (priv->child_count);
	undid_pending = alloca0 (priv->child_count);
	data_lock = alloca0 (priv->child_count);
        postop_lock = alloca0 (priv->child_count);

	locked_replies = alloca0 (sizeof (*locked_replies) * priv->child_count);

	ret = afr_selfheal_entrylk (frame, this, fd->inode, this->name, NULL,
				    data_lock);
	{
		if (ret < AFR_SH_MIN_PARTICIPANTS) {
                        gf_msg_debug (this->name, 0, "%s: Skipping "
                                      "entry self-heal as only %d sub-volumes could "
                                      "be locked in %s domain",
                                      uuid_utoa (fd->inode->gfid), ret,
                                      this->name);
			ret = -ENOTCONN;
			goto unlock;
		}

		ret = __afr_selfheal_entry_prepare (frame, this, fd->inode,
                                                    data_lock, sources, sinks,
                                                    healed_sinks,
						    locked_replies, &source,
                                                    NULL);
                if (AFR_COUNT(healed_sinks, priv->child_count) == 0) {
                        did_sh = _gf_false;
                        goto unlock;
                }

                local->need_full_crawl = afr_need_full_heal (this,
                                                             locked_replies,
                                                             source,
                                                             healed_sinks,
                                                         AFR_ENTRY_TRANSACTION);
	}
unlock:
	afr_selfheal_unentrylk (frame, this, fd->inode, this->name, NULL,
				data_lock, NULL);
	if (ret < 0)
		goto out;

        if (!did_sh)
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
                        gf_msg_debug (this->name, 0, "%s: Skipping "
                                      "post-op after entry self-heal as %d "
                                      "sub-volumes, as opposed to %d, "
                                      "could be locked in %s domain",
                                      uuid_utoa (fd->inode->gfid),
                                      ret, AFR_COUNT (data_lock,
                                      priv->child_count), this->name);
                        ret = -ENOTCONN;
                        goto postop_unlock;
                }

                ret = afr_selfheal_undo_pending (frame, this, fd->inode,
                                                 sources, sinks, healed_sinks,
                                                 undid_pending,
                                                 AFR_ENTRY_TRANSACTION,
                                                 locked_replies, postop_lock);
        }
postop_unlock:
        afr_selfheal_unentrylk (frame, this, fd->inode, this->name, NULL,
                                postop_lock, NULL);
out:
        if (did_sh)
                afr_log_selfheal (fd->inode->gfid, this, ret, "entry", source,
                                  sources, healed_sinks);
        else
                ret = 1;

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
	gf_uuid_copy (loc.gfid, inode->gfid);

	ret = syncop_opendir (this, &loc, fd, NULL, NULL);
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

	ret = afr_selfheal_tie_breaker_entrylk (frame, this, inode,
	                                        priv->sh_domain, NULL,
                                                locked_on);
	{
		if (ret < AFR_SH_MIN_PARTICIPANTS) {
                        gf_msg_debug (this->name, 0, "%s: Skipping "
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
	afr_selfheal_unentrylk (frame, this, inode, priv->sh_domain, NULL,
                                locked_on, NULL);

	if (fd)
		fd_unref (fd);

	return ret;
}
