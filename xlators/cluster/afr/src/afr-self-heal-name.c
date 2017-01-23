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
                            unsigned char *locked_on,
                            gf_boolean_t is_gfid_absent)
{
	int             ret          = 0;
        int             up_count     = 0;
        int             locked_count = 0;
	afr_private_t  *priv         = NULL;
	dict_t         *xdata        = NULL;
	loc_t           loc          = {0, };
        call_frame_t   *new_frame    = NULL;
        afr_local_t    *new_local    = NULL;

	priv = this->private;

        new_frame = afr_frame_create (this);
        if (!new_frame) {
                ret = -ENOMEM;
                goto out;
        }

        new_local = new_frame->local;

	gf_uuid_copy (parent->gfid, pargfid);

	xdata = dict_new ();
	if (!xdata) {
                ret = -ENOMEM;
		goto out;
	}

	ret = dict_set_static_bin (xdata, "gfid-req", gfid, 16);
	if (ret) {
		ret = -ENOMEM;
		goto out;
	}

	loc.parent = inode_ref (parent);
	loc.inode = inode_ref (inode);
	gf_uuid_copy (loc.pargfid, pargfid);
	loc.name = bname;

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

        /* Clear out old replies here and wind lookup on all locked
         * subvolumes to achieve two things:
         *   a. gfid heal on those subvolumes that do not have gfid associated
         *      with the inode, and
         *   b. refresh replies, which can be consumed by
         *      __afr_selfheal_name_impunge().
         */

        AFR_ONLIST (locked_on, new_frame, afr_selfheal_discover_cbk, lookup,
                    &loc, xdata);

        afr_replies_wipe (replies, priv->child_count);

        afr_replies_copy (replies, new_local->replies, priv->child_count);

out:
	loc_wipe (&loc);
        if (xdata)
                dict_unref (xdata);
        if (new_frame)
                AFR_STACK_DESTROY (new_frame);

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
		if (!replies[i].valid)
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

/* This function is to be called after ensuring that there is no gfid mismatch
 * for the inode across multiple sources
 */
static int
afr_selfheal_gfid_idx_get (xlator_t *this, struct afr_reply *replies,
                           unsigned char *sources)
{
        int             i        =  0;
        int             gfid_idx = -1;
        afr_private_t  *priv     =  NULL;

        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (!replies[i].valid)
                        continue;

                if (!sources[i])
                        continue;

                if (gf_uuid_is_null (replies[i].poststat.ia_gfid))
                        continue;

                gfid_idx = i;
                break;
        }
        return gfid_idx;
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
                if (!replies[i].valid)
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
                                       const char *bname)
{
        int             i             = 0;
	int             gfid_idx_iter = -1;
        void           *gfid          = NULL;
        void           *gfid1         = NULL;
        afr_private_t  *priv          = NULL;
	char g1[64], g2[64];

        priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
		if (!replies[i].valid)
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
			        gf_msg (this->name, GF_LOG_WARNING, 0,
                                        AFR_MSG_SPLIT_BRAIN,
					"GFID mismatch for <gfid:%s>/%s "
					"%s on %s and %s on %s",
					uuid_utoa (pargfid), bname,
					uuid_utoa_r (gfid1, g1),
					priv->children[i]->name,
					uuid_utoa_r (gfid, g2),
					priv->children[gfid_idx_iter]->name);
                                gf_event (EVENT_AFR_SPLIT_BRAIN,
                                        "subvol=%s;type=gfid;"
                                        "file=<gfid:%s>/%s;count=2;"
                                        "child-%d=%s;gfid-%d=%s;child-%d=%s;"
                                        "gfid-%d=%s", this->name,
                                        uuid_utoa (pargfid), bname, i,
                                        priv->children[i]->name, i,
                                        uuid_utoa_r (gfid1, g1),
                                        gfid_idx_iter,
                                        priv->children[gfid_idx_iter]->name,
                                        gfid_idx_iter,
                                        uuid_utoa_r (gfid, g2));

				return -EIO;
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
                        void *gfid_req)
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
                                                     pargfid, bname);
        if (ret)
                return ret;

	if (gfid_idx == -1) {
                if (!gfid_req || gf_uuid_is_null (gfid_req))
                        return -1;
                gfid = gfid_req;
        } else {
                gfid = &replies[gfid_idx].poststat.ia_gfid;
        }

        is_gfid_absent = (gfid_idx == -1) ? _gf_true : _gf_false;
	ret = __afr_selfheal_assign_gfid (this, parent, pargfid, bname, inode,
                                          replies, gfid, locked_on,
                                          is_gfid_absent);
        if (ret)
                return ret;

        if (gfid_idx == -1) {
                gfid_idx = afr_selfheal_gfid_idx_get (this, replies, sources);
                if (gfid_idx == -1)
                        return -1;
        }

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
						      locked_on, replies,
                                                      witness);
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
                                              replies, gfid_req);
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
                    (replies[i].op_errno == ENODATA))
                        *need_heal = _gf_true;

		if (first_idx == -1) {
			first_idx = i;
			continue;
		}

		if (replies[i].op_ret != replies[first_idx].op_ret)
			*need_heal = _gf_true;

		if (gf_uuid_compare (replies[i].poststat.ia_gfid,
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

	if (need_heal) {
		ret = afr_selfheal_name_do (frame, this, parent, pargfid, bname,
                                            gfid_req);
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
