/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/


#include "events.h"
#include "afr.h"
#include "afr-self-heal.h"
#include "afr-messages.h"

int
__afr_selfheal_assign_gfid (xlator_t *this, inode_t *parent, uuid_t pargfid,
                            const char *bname, inode_t *inode,
                            struct afr_reply *replies, void *gfid,
                            unsigned char *locked_on, int source,
                            unsigned char *sources, gf_boolean_t is_gfid_absent,
                            int *gfid_idx)
{
	int             ret          = 0;
        int             up_count     = 0;
        int             locked_count = 0;
	afr_private_t  *priv         = NULL;

	priv = this->private;

	gf_uuid_copy (parent->gfid, pargfid);

        if (is_gfid_absent) {
                /* Ensure all children of AFR are up before performing gfid heal, to
                 * guard against the possibility of gfid split brain. */

                up_count = AFR_COUNT (priv->child_up, priv->child_count);
                if (up_count != priv->child_count) {
                        ret = -EIO;
                        goto out;
                }

                locked_count = AFR_COUNT (locked_on, priv->child_count);
                if (locked_count != priv->child_count) {
                        ret = -EIO;
                        goto out;
                }
        }

        afr_lookup_and_heal_gfid (this, parent, bname, inode, replies, source,
                                  sources, gfid, gfid_idx);

out:
	return ret;
}

int
__afr_selfheal_name_impunge (call_frame_t *frame, xlator_t *this,
                             inode_t *parent, uuid_t pargfid,
                             const char *bname, inode_t *inode,
                             struct afr_reply *replies, int gfid_idx)
{
	int i = 0;
	afr_private_t *priv = NULL;
        int ret = 0;
        unsigned char *sources = NULL;

        priv = this->private;

	sources = alloca0 (priv->child_count);

	gf_uuid_copy (parent->gfid, pargfid);

	for (i = 0; i < priv->child_count; i++) {
                if (!replies[i].valid || replies[i].op_ret != 0)
			continue;

		if (gf_uuid_compare (replies[i].poststat.ia_gfid,
				  replies[gfid_idx].poststat.ia_gfid) == 0) {
                        sources[i] = 1;
			continue;
                }
        }

        for (i = 0; i < priv->child_count; i++) {
                if (sources[i])
                        continue;

		ret |= afr_selfheal_recreate_entry (frame, i, gfid_idx, sources,
                                                    parent, bname, inode,
                                                    replies);
	}

	return ret;
}


int
__afr_selfheal_name_expunge (xlator_t *this, inode_t *parent, uuid_t pargfid,
                             const char *bname, inode_t *inode,
			     struct afr_reply *replies)
{
	loc_t loc = {0, };
	int i = 0;
	afr_private_t *priv = NULL;
	char g[64];
	int ret = 0;

	priv = this->private;

	loc.parent = inode_ref (parent);
	gf_uuid_copy (loc.pargfid, pargfid);
	loc.name = bname;
	loc.inode = inode_ref (inode);

	for (i = 0; i < priv->child_count; i++) {
		if (!replies[i].valid)
			continue;

		if (replies[i].op_ret)
			continue;

		switch (replies[i].poststat.ia_type) {
		case IA_IFDIR:
		        gf_msg (this->name, GF_LOG_WARNING, 0,
                                AFR_MSG_EXPUNGING_FILE_OR_DIR,
			        "expunging dir %s/%s (%s) on %s",
			        uuid_utoa (pargfid), bname,
			        uuid_utoa_r (replies[i].poststat.ia_gfid, g),
			        priv->children[i]->name);

			ret |= syncop_rmdir (priv->children[i], &loc, 1, NULL,
                                             NULL);
			break;
		default:
		        gf_msg (this->name, GF_LOG_WARNING, 0,
                                AFR_MSG_EXPUNGING_FILE_OR_DIR,
		                "expunging file %s/%s (%s) on %s",
			        uuid_utoa (pargfid), bname,
			        uuid_utoa_r (replies[i].poststat.ia_gfid, g),
			        priv->children[i]->name);

			ret |= syncop_unlink (priv->children[i], &loc, NULL,
                                              NULL);
			break;
		}
	}

	loc_wipe (&loc);

	return ret;

}

static gf_boolean_t
afr_selfheal_name_need_heal_check (xlator_t *this, struct afr_reply *replies)
{
        int             i           = 0;
	int             first_idx   = -1;
        gf_boolean_t    need_heal   = _gf_false;
        afr_private_t  *priv        = NULL;

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

		if (gf_uuid_compare (replies[i].poststat.ia_gfid,
				  replies[first_idx].poststat.ia_gfid))
			need_heal = _gf_true;

                if ((replies[i].op_ret == 0) &&
                    (gf_uuid_is_null(replies[i].poststat.ia_gfid)))
                        need_heal = _gf_true;

	}

        return need_heal;
}

static int
afr_selfheal_name_type_mismatch_check (xlator_t *this, struct afr_reply *replies,
                                       int source, unsigned char *sources,
                                       uuid_t pargfid, const char *bname)
{
        int             i           = 0;
        int             type_idx    = -1;
        ia_type_t       inode_type  = IA_INVAL;
        ia_type_t       inode_type1 = IA_INVAL;
        afr_private_t  *priv        = NULL;

        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (!replies[i].valid || replies[i].op_ret != 0)
                        continue;

                if (replies[i].poststat.ia_type == IA_INVAL)
                        continue;

                if (inode_type == IA_INVAL) {
                        inode_type = replies[i].poststat.ia_type;
                        type_idx = i;
                        continue;
                }
                inode_type1 = replies[i].poststat.ia_type;
                if (sources[i] || source == -1) {
                        if ((sources[type_idx] || source == -1) &&
                            (inode_type != inode_type1)) {
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        AFR_MSG_SPLIT_BRAIN,
                                        "Type mismatch for <gfid:%s>/%s: "
                                        "%s on %s and %s on %s",
                                        uuid_utoa(pargfid), bname,
                                        gf_inode_type_to_str (inode_type1),
                                        priv->children[i]->name,
                                        gf_inode_type_to_str (inode_type),
                                        priv->children[type_idx]->name);
                                gf_event (EVENT_AFR_SPLIT_BRAIN,
                                         "subvol=%s;type=file;"
                                         "file=<gfid:%s>/%s;count=2;"
                                         "child-%d=%s;type-%d=%s;child-%d=%s;"
                                         "type-%d=%s", this->name,
                                         uuid_utoa (pargfid), bname, i,
                                         priv->children[i]->name, i,
                                         gf_inode_type_to_str (inode_type1),
                                         type_idx,
                                         priv->children[type_idx]->name,
                                         type_idx,
                                         gf_inode_type_to_str (inode_type));
                                return -EIO;
                        }
                        inode_type = replies[i].poststat.ia_type;
                        type_idx = i;
                }
        }
        return 0;
}

static int
afr_selfheal_name_gfid_mismatch_check (xlator_t *this, struct afr_reply *replies,
                                       int source, unsigned char *sources,
                                       int *gfid_idx, uuid_t pargfid,
                                       const char *bname, inode_t *inode,
                                       unsigned char *locked_on, dict_t *xdata)
{
        int             i             = 0;
	int             gfid_idx_iter = -1;
        int             ret           = -1;
        void           *gfid          = NULL;
        void           *gfid1         = NULL;
        afr_private_t  *priv          = NULL;

        priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
                if (!replies[i].valid || replies[i].op_ret != 0)
                        continue;

		if (gf_uuid_is_null (replies[i].poststat.ia_gfid))
			continue;

		if (!gfid) {
			gfid = &replies[i].poststat.ia_gfid;
			gfid_idx_iter = i;
			continue;
		}

                gfid1 = &replies[i].poststat.ia_gfid;
		if (sources[i] || source == -1) {
			if ((sources[gfid_idx_iter] || source == -1) &&
			    gf_uuid_compare (gfid, gfid1)) {
                                ret = afr_gfid_split_brain_source (this,
                                                                   replies,
                                                                   inode,
                                                                   pargfid,
                                                                   bname,
                                                                   gfid_idx_iter,
                                                                   i, locked_on,
                                                                   gfid_idx,
                                                                   xdata);
                                if (!ret && *gfid_idx >= 0) {
                                        ret = dict_set_str (xdata,
                                                             "gfid-heal-msg",
                                                             "GFID split-brain "
                                                             "resolved");
                                        if (ret)
                                                gf_msg (this->name,
                                                        GF_LOG_ERROR, 0,
                                                        AFR_MSG_DICT_SET_FAILED,
                                                        "Error setting gfid-"
                                                        "heal-msg dict");
                                }
                                return ret;
			}
                        gfid = &replies[i].poststat.ia_gfid;
			gfid_idx_iter = i;
		}
	}

        *gfid_idx = gfid_idx_iter;
        return 0;
}

static gf_boolean_t
afr_selfheal_name_source_empty_check (xlator_t *this, struct afr_reply *replies,
                                      unsigned char *sources, int source)
{
	int             i               = 0;
	afr_private_t  *priv            = NULL;
	gf_boolean_t    source_is_empty = _gf_true;

	priv = this->private;

        if (source == -1) {
                source_is_empty = _gf_false;
                goto out;
        }

	for (i = 0; i < priv->child_count; i++) {
                if (!sources[i])
                        continue;

                if (replies[i].op_ret == -1 && replies[i].op_errno == ENOENT)
                        continue;

                source_is_empty = _gf_false;
                break;
	}
out:
        return source_is_empty;
}

int
__afr_selfheal_name_do (call_frame_t *frame, xlator_t *this, inode_t *parent,
                        uuid_t pargfid, const char *bname, inode_t *inode,
                        unsigned char *sources, unsigned char *sinks,
			unsigned char *healed_sinks, int source,
			unsigned char *locked_on, struct afr_reply *replies,
                        void *gfid_req, dict_t *xdata)
{
	int             gfid_idx        = -1;
        int             ret             = -1;
	void           *gfid            = NULL;
	gf_boolean_t    source_is_empty = _gf_true;
	gf_boolean_t    need_heal       = _gf_false;
        gf_boolean_t    is_gfid_absent  = _gf_false;

        need_heal = afr_selfheal_name_need_heal_check (this, replies);
	if (!need_heal)
		return 0;

        source_is_empty = afr_selfheal_name_source_empty_check (this, replies,
                                                                sources,
                                                                source);
	if (source_is_empty) {
		ret = __afr_selfheal_name_expunge (this, parent, pargfid,
						    bname, inode, replies);
                if (ret == -EIO)
                        ret = -1;
                return ret;
        }

        ret = afr_selfheal_name_type_mismatch_check (this, replies, source,
                                                     sources, pargfid, bname);
        if (ret)
                return ret;

        ret = afr_selfheal_name_gfid_mismatch_check (this, replies, source,
                                                     sources, &gfid_idx,
                                                     pargfid, bname, inode,
                                                     locked_on, xdata);
        if (ret)
                return ret;

	if (gfid_idx == -1) {
                if (!gfid_req || gf_uuid_is_null (gfid_req))
                        return -1;
                gfid = gfid_req;
        } else {
                gfid = &replies[gfid_idx].poststat.ia_gfid;
                if (source == -1)
                        /* Either entry split-brain or dirty xattrs are
                        * present on parent.*/
                        source = gfid_idx;
        }

        is_gfid_absent = (gfid_idx == -1) ? _gf_true : _gf_false;
	ret = __afr_selfheal_assign_gfid (this, parent, pargfid, bname, inode,
                                          replies, gfid, locked_on, source,
                                          sources, is_gfid_absent, &gfid_idx);
        if (ret)
                return ret;

	ret = __afr_selfheal_name_impunge (frame, this, parent, pargfid,
                                           bname, inode, replies, gfid_idx);
        if (ret == -EIO)
                ret = -1;

        return ret;
}


int
__afr_selfheal_name_finalize_source (xlator_t *this, unsigned char *sources,
				     unsigned char *healed_sinks,
				     unsigned char *locked_on,
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
        uint64_t *witness = NULL;

	priv = this->private;

        replies = alloca0 (priv->child_count * sizeof(*replies));

	ret = afr_selfheal_unlocked_discover (frame, parent, pargfid, replies);
	if (ret)
		goto out;

        witness = alloca0 (sizeof (*witness) * priv->child_count);
	ret = afr_selfheal_find_direction (frame, this, replies,
					   AFR_ENTRY_TRANSACTION,
					   locked_on, sources, sinks, witness,
                                           NULL);
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
                                                      healed_sinks,
						      locked_on, witness);
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
		      uuid_t pargfid, const char *bname, void *gfid_req,
                      dict_t *xdata)
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
        dict_t *xattr = NULL;

        xattr = dict_new ();
        if (!xattr)
                return -ENOMEM;

        ret = dict_set_int32 (xattr, GF_GFIDLESS_LOOKUP, 1);
        if (ret) {
                dict_unref (xattr);
                return -1;
        }

	priv = this->private;

	locked_on = alloca0 (priv->child_count);
	sources = alloca0 (priv->child_count);
	sinks = alloca0 (priv->child_count);
	healed_sinks = alloca0 (priv->child_count);

	replies = alloca0 (priv->child_count * sizeof(*replies));

	ret = afr_selfheal_entrylk (frame, this, parent, this->name, bname,
				    locked_on);
	{
		if (ret < AFR_SH_MIN_PARTICIPANTS) {
			ret = -ENOTCONN;
			goto unlock;
		}

		ret = __afr_selfheal_name_prepare (frame, this, parent, pargfid,
						   locked_on, sources, sinks,
						   healed_sinks, &source);
		if (ret)
			goto unlock;

                inode = afr_selfheal_unlocked_lookup_on (frame, parent, bname,
						         replies, locked_on,
                                                         xattr);
		if (!inode) {
			ret = -ENOMEM;
			goto unlock;
		}

		ret = __afr_selfheal_name_do (frame, this, parent, pargfid,
                                              bname, inode, sources, sinks,
                                              healed_sinks, source, locked_on,
                                              replies, gfid_req, xdata);
	}
unlock:
	afr_selfheal_unentrylk (frame, this, parent, this->name, bname,
				locked_on, NULL);
	if (inode)
		inode_unref (inode);

        if (replies)
                afr_replies_wipe (replies, priv->child_count);
        if (xattr)
                dict_unref (xattr);

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
						 replies, priv->child_up, NULL);
	if (!inode)
		return -ENOMEM;

	for (i = 0; i < priv->child_count; i++) {
		if (!replies[i].valid)
			continue;

                if ((replies[i].op_ret == -1) &&
                    (replies[i].op_errno == ENODATA)) {
                        *need_heal = _gf_true;
                        break;
                }

		if (first_idx == -1) {
			first_idx = i;
			continue;
		}

		if (replies[i].op_ret != replies[first_idx].op_ret) {
			*need_heal = _gf_true;
                        break;
                }

		if (gf_uuid_compare (replies[i].poststat.ia_gfid,
				  replies[first_idx].poststat.ia_gfid)) {
			*need_heal = _gf_true;
                        break;
                }
	}

	if (inode)
		inode_unref (inode);
        if (replies)
                afr_replies_wipe (replies, priv->child_count);
	return 0;
}

int
afr_selfheal_name (xlator_t *this, uuid_t pargfid, const char *bname,
                   void *gfid_req, dict_t *xdata)
{
	inode_t *parent = NULL;
	call_frame_t *frame = NULL;
	int ret = -1;
	gf_boolean_t need_heal = _gf_false;

	parent = afr_inode_find (this, pargfid);
	if (!parent)
		goto out;

	frame = afr_frame_create (this, NULL);
	if (!frame)
		goto out;

	ret = afr_selfheal_name_unlocked_inspect (frame, this, parent, pargfid,
						  bname, &need_heal);
	if (ret)
		goto out;

	if (need_heal) {
		ret = afr_selfheal_name_do (frame, this, parent, pargfid, bname,
                                            gfid_req, xdata);
                if (ret)
                        goto out;
        }

        ret = 0;
out:
	if (parent)
		inode_unref (parent);
	if (frame)
		AFR_STACK_DESTROY (frame);

	return ret;
}
