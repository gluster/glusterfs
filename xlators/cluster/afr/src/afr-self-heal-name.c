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


int
__afr_selfheal_assign_gfid (call_frame_t *frame, xlator_t *this, inode_t *parent,
			    uuid_t pargfid, const char *bname, inode_t *inode,
			    struct afr_reply *replies, void *gfid)
{
	int i = 0;
	afr_private_t *priv = NULL;
	dict_t *xdata = NULL;
	int ret = 0;
	loc_t loc = {0, };

	priv = this->private;

	uuid_copy (parent->gfid, pargfid);

	xdata = dict_new ();
	if (!xdata) {
		return -ENOMEM;
	}

	ret = dict_set_static_bin (xdata, "gfid-req", gfid, 16);
	if (ret) {
		dict_destroy (xdata);
		return -ENOMEM;
	}

	loc.parent = inode_ref (parent);
	loc.inode = inode_ref (inode);
	uuid_copy (loc.pargfid, pargfid);
	loc.name = bname;

	for (i = 0; i < priv->child_count; i++) {
		if (replies[i].op_ret == 0 || replies[i].op_errno != ENODATA)
			continue;

		ret = syncop_lookup (priv->children[i], &loc, xdata, 0, 0, 0);
	}

	loc_wipe (&loc);
	dict_unref (xdata);

	return ret;
}


int
__afr_selfheal_name_impunge (call_frame_t *frame, xlator_t *this, inode_t *parent,
			     uuid_t pargfid, const char *bname, inode_t *inode,
			     struct afr_reply *replies, int gfid_idx)
{
	int i = 0;
	afr_private_t *priv = NULL;
	int ret = 0;

	priv = this->private;

	uuid_copy (parent->gfid, pargfid);

	for (i = 0; i < priv->child_count; i++) {
		if (!replies[i].valid)
			continue;

		if (uuid_compare (replies[i].poststat.ia_gfid,
				  replies[gfid_idx].poststat.ia_gfid) == 0)
			continue;

		ret |= afr_selfheal_recreate_entry (frame, this, i, gfid_idx,
						    parent, bname, inode, replies);
	}

	return ret;
}


int
__afr_selfheal_name_expunge (call_frame_t *frame, xlator_t *this, inode_t *parent,
			     uuid_t pargfid, const char *bname, inode_t *inode,
			     struct afr_reply *replies)
{
	loc_t loc = {0, };
	int i = 0;
	afr_private_t *priv = NULL;
	char g[64];
	int ret = 0;

	priv = this->private;

	loc.parent = inode_ref (parent);
	uuid_copy (loc.pargfid, pargfid);
	loc.name = bname;
	loc.inode = inode_ref (inode);

	for (i = 0; i < priv->child_count; i++) {
		if (!replies[i].valid)
			continue;

		if (replies[i].op_ret)
			continue;

		switch (replies[i].poststat.ia_type) {
		case IA_IFDIR:
			gf_log (this->name, GF_LOG_WARNING,
				"expunging dir %s/%s (%s) on %s",
				uuid_utoa (pargfid), bname,
				uuid_utoa_r (replies[i].poststat.ia_gfid, g),
				priv->children[i]->name);
			ret |= syncop_rmdir (priv->children[i], &loc, 1);
			break;
		default:
			gf_log (this->name, GF_LOG_WARNING,
				"expunging file %s/%s (%s) on %s",
				uuid_utoa (pargfid), bname,
				uuid_utoa_r (replies[i].poststat.ia_gfid, g),
				priv->children[i]->name);
			ret |= syncop_unlink (priv->children[i], &loc);
			break;
		}
	}

	loc_wipe (&loc);

	return ret;

}


int
__afr_selfheal_name_do (call_frame_t *frame, xlator_t *this, inode_t *parent,
			uuid_t pargfid, const char *bname, inode_t *inode,
			unsigned char *sources, unsigned char *sinks,
			unsigned char *healed_sinks, int source,
			unsigned char *locked_on, struct afr_reply *replies,
                        void *gfid_req)
{
	int i = 0;
	afr_private_t *priv = NULL;
	void* gfid = NULL;
	int gfid_idx = -1;
	gf_boolean_t source_is_empty = _gf_true;
	gf_boolean_t need_heal = _gf_false;
	int first_idx = -1;
	char g1[64],g2[64];

	priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
		if (!replies[i].valid)
			continue;

                if ((replies[i].op_ret == -1) &&
                    (replies[i].op_errno == ENODATA))
                        need_heal = _gf_true;

		if (first_idx == -1) {
			first_idx = i;
			continue;
		}

		if (replies[i].op_ret != replies[first_idx].op_ret)
			need_heal = _gf_true;

		if (uuid_compare (replies[i].poststat.ia_gfid,
				  replies[first_idx].poststat.ia_gfid))
			need_heal = _gf_true;
	}

	if (!need_heal)
		return 0;

	for (i = 0; i < priv->child_count; i++) {
                if (!sources[i])
                        continue;

                if (replies[i].op_ret == -1 && replies[i].op_errno == ENOENT)
                        continue;

                source_is_empty = _gf_false;
                break;
	}

        if (source == -1)
                source_is_empty = _gf_false;

	if (source_is_empty) {
		return __afr_selfheal_name_expunge (frame, this, parent, pargfid,
						    bname, inode, replies);
	}

	for (i = 0; i < priv->child_count; i++) {
		if (!replies[i].valid)
			continue;

		if (uuid_is_null (replies[i].poststat.ia_gfid))
			continue;

		if (!gfid) {
			gfid = &replies[i].poststat.ia_gfid;
			gfid_idx = i;
			continue;
		}

		if (sources[i] || source == -1) {
			if (gfid_idx != -1 &&
			    (sources[gfid_idx] || source == -1) &&
			    uuid_compare (gfid, replies[i].poststat.ia_gfid)) {
				gf_msg (this->name, GF_LOG_WARNING, 0,
                                        AFR_MSG_SPLIT_BRAIN,
					"GFID mismatch for <gfid:%s>/%s "
					"%s on %s and %s on %s",
					uuid_utoa (pargfid), bname,
					uuid_utoa_r (replies[i].poststat.ia_gfid, g1),
					priv->children[i]->name,
					uuid_utoa_r (replies[gfid_idx].poststat.ia_gfid, g2),
					priv->children[gfid_idx]->name);
				return -1;
			}

                        gfid = &replies[i].poststat.ia_gfid;
			gfid_idx = i;
			continue;
		}
	}

	if (gfid_idx == -1) {
                if (!gfid_req || uuid_is_null (gfid_req))
                        return -1;
                gfid = gfid_req;
        }

	__afr_selfheal_assign_gfid (frame, this, parent, pargfid, bname, inode,
				    replies, gfid);
        /*TODO:
         * once the gfid is assigned refresh the replies and carry on with
         * impunge. i.e. gfid_idx won't be -1.
         */
        if (gfid_idx == -1)
                return -1;

	return __afr_selfheal_name_impunge (frame, this, parent, pargfid,
					    bname, inode, replies, gfid_idx);
}


int
__afr_selfheal_name_finalize_source (xlator_t *this, unsigned char *sources,
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


int
__afr_selfheal_name_prepare (call_frame_t *frame, xlator_t *this, inode_t *parent,
			     uuid_t pargfid, unsigned char *locked_on,
			     unsigned char *sources, unsigned char *sinks,
			     unsigned char *healed_sinks, int *source_p)
{
	int ret = -1;
	int source = -1;
	afr_private_t *priv = NULL;
        struct afr_reply *replies = NULL;

	priv = this->private;

        replies = alloca0 (priv->child_count * sizeof(*replies));

	ret = afr_selfheal_unlocked_discover (frame, parent, pargfid, replies);
	if (ret)
		goto out;

	ret = afr_selfheal_find_direction (frame, this, replies,
					   AFR_ENTRY_TRANSACTION,
					   locked_on, sources, sinks);
	if (ret)
		goto out;

        /* Initialize the healed_sinks[] array optimistically to
           the intersection of to-be-healed (i.e sinks[]) and
           the list of servers which are up (i.e locked_on[]).

           As we encounter failures in the healing process, we
           will unmark the respective servers in the healed_sinks[]
           array.
        */
        AFR_INTERSECT (healed_sinks, sinks, locked_on, priv->child_count);

	source = __afr_selfheal_name_finalize_source (this, sources,
                                                      healed_sinks, locked_on);
	if (source < 0) {
		/* If source is < 0 (typically split-brain), we perform a
		   conservative merge of entries rather than erroring out */
	}
	*source_p = source;

out:
        if (replies)
                afr_replies_wipe (replies, priv->child_count);

	return ret;
}


int
afr_selfheal_name_do (call_frame_t *frame, xlator_t *this, inode_t *parent,
		      uuid_t pargfid, const char *bname, void *gfid_req)
{
	afr_private_t *priv = NULL;
	unsigned char *sources = NULL;
	unsigned char *sinks = NULL;
	unsigned char *healed_sinks = NULL;
	unsigned char *locked_on = NULL;
	int source = -1;
        struct afr_reply *replies = NULL;
	int ret = -1;
	inode_t *inode = NULL;

	priv = this->private;

	locked_on = alloca0 (priv->child_count);
	sources = alloca0 (priv->child_count);
	sinks = alloca0 (priv->child_count);
	healed_sinks = alloca0 (priv->child_count);

	replies = alloca0 (priv->child_count * sizeof(*replies));

	ret = afr_selfheal_entrylk (frame, this, parent, this->name, bname,
				    locked_on);
	{
		if (ret < 2) {
			ret = -ENOTCONN;
			goto unlock;
		}

		ret = __afr_selfheal_name_prepare (frame, this, parent, pargfid,
						   locked_on, sources, sinks,
						   healed_sinks, &source);
		if (ret)
			goto unlock;

		inode = afr_selfheal_unlocked_lookup_on (frame, parent, bname,
							 replies, locked_on);
		if (!inode) {
			ret = -ENOMEM;
			goto unlock;
		}

		ret = __afr_selfheal_name_do (frame, this, parent, pargfid, bname,
					      inode, sources, sinks, healed_sinks,
					      source, locked_on, replies,
                                              gfid_req);
	}
unlock:
	afr_selfheal_unentrylk (frame, this, parent, this->name, bname,
				locked_on);
	if (inode)
		inode_unref (inode);

        if (replies)
                afr_replies_wipe (replies, priv->child_count);

	return ret;
}


int
afr_selfheal_name_unlocked_inspect (call_frame_t *frame, xlator_t *this,
				    inode_t *parent, uuid_t pargfid,
				    const char *bname, gf_boolean_t *need_heal)
{
	afr_private_t *priv = NULL;
	int i = 0;
	struct afr_reply *replies = NULL;
	inode_t *inode = NULL;
	int first_idx = -1;

	priv = this->private;

	replies = alloca0 (sizeof (*replies) * priv->child_count);

	inode = afr_selfheal_unlocked_lookup_on (frame, parent, bname,
						 replies, priv->child_up);
	if (!inode)
		return -ENOMEM;

	for (i = 0; i < priv->child_count; i++) {
		if (!replies[i].valid)
			continue;

                if ((replies[i].op_ret == -1) &&
                    (replies[i].op_errno == ENODATA))
                        *need_heal = _gf_true;

		if (first_idx == -1) {
			first_idx = i;
			continue;
		}

		if (replies[i].op_ret != replies[first_idx].op_ret)
			*need_heal = _gf_true;

		if (uuid_compare (replies[i].poststat.ia_gfid,
				  replies[first_idx].poststat.ia_gfid))
			*need_heal = _gf_true;
	}

	if (inode)
		inode_unref (inode);
        if (replies)
                afr_replies_wipe (replies, priv->child_count);
	return 0;
}

int
afr_selfheal_name (xlator_t *this, uuid_t pargfid, const char *bname,
                   void *gfid_req)
{
	inode_t *parent = NULL;
	call_frame_t *frame = NULL;
	int ret = -1;
	gf_boolean_t need_heal = _gf_false;

	parent = afr_inode_find (this, pargfid);
	if (!parent)
		goto out;

	frame = afr_frame_create (this);
	if (!frame)
		goto out;

	ret = afr_selfheal_name_unlocked_inspect (frame, this, parent, pargfid,
						  bname, &need_heal);
	if (ret)
		goto out;

	if (need_heal)
		afr_selfheal_name_do (frame, this, parent, pargfid, bname,
                                      gfid_req);
out:
	if (parent)
		inode_unref (parent);
	if (frame)
		AFR_STACK_DESTROY (frame);

	return ret;
}
