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
#include "protocol-common.h"
#include "events.h"

#define AFR_HEAL_ATTR (GF_SET_ATTR_UID|GF_SET_ATTR_GID|GF_SET_ATTR_MODE)

static gf_boolean_t
_afr_ignorable_key_match (dict_t *d, char *k, data_t *val, void *mdata)
{
        return afr_is_xattr_ignorable (k);
}

void
afr_delete_ignorable_xattrs (dict_t *xattr)
{
        dict_foreach_match (xattr, _afr_ignorable_key_match, NULL,
                            dict_remove_foreach_fn, NULL);
}

int
__afr_selfheal_metadata_do (call_frame_t *frame, xlator_t *this, inode_t *inode,
                            int source, unsigned char *healed_sinks,
                            struct afr_reply *locked_replies)
{
	int ret = -1;
	loc_t loc = {0,};
	dict_t *xattr = NULL;
	dict_t *old_xattr = NULL;
	afr_private_t *priv = NULL;
	int i = 0;

	priv = this->private;

	loc.inode = inode_ref (inode);
	gf_uuid_copy (loc.gfid, inode->gfid);

        gf_msg (this->name, GF_LOG_INFO, 0,
                AFR_MSG_SELF_HEAL_INFO, "performing metadata selfheal on %s",
		uuid_utoa (inode->gfid));

	ret = syncop_getxattr (priv->children[source], &loc, &xattr, NULL,
                               NULL, NULL);
	if (ret < 0) {
		ret = -EIO;
                goto out;
	}

	afr_delete_ignorable_xattrs (xattr);

	for (i = 0; i < priv->child_count; i++) {
                if (old_xattr) {
                        dict_unref (old_xattr);
                        old_xattr = NULL;
                }

		if (!healed_sinks[i])
			continue;

		ret = syncop_setattr (priv->children[i], &loc,
				      &locked_replies[source].poststat,
				      AFR_HEAL_ATTR, NULL, NULL, NULL, NULL);
		if (ret)
			healed_sinks[i] = 0;

		ret = syncop_getxattr (priv->children[i], &loc, &old_xattr, 0,
                                       NULL, NULL);
		if (old_xattr) {
			afr_delete_ignorable_xattrs (old_xattr);
			ret = syncop_removexattr (priv->children[i], &loc, "",
						  old_xattr, NULL);
                        if (ret)
                                healed_sinks[i] = 0;
		}

		ret = syncop_setxattr (priv->children[i], &loc, xattr, 0, NULL,
                                       NULL);
		if (ret)
			healed_sinks[i] = 0;
	}
        ret = 0;

out:
	loc_wipe (&loc);
	if (xattr)
		dict_unref (xattr);
        if (old_xattr)
                dict_unref (old_xattr);

	return ret;
}

static uint64_t
mtime_ns(struct iatt *ia)
{
        uint64_t ret;

        ret = (((uint64_t)(ia->ia_mtime)) * 1000000000)
            + (uint64_t)(ia->ia_mtime_nsec);

        return ret;
}

/*
 * When directory content is modified, [mc]time is updated. On
 * Linux, the filesystem does it, while at least on NetBSD, the
 * kernel file-system independent code does it. This means that
 * when entries are added while bricks are down, the kernel sends
 * a SETATTR [mc]time which will cause metadata split brain for
 * the directory. In this case, clear the split brain by finding
 * the source with the most recent modification date.
 */
static int
afr_dirtime_splitbrain_source (call_frame_t *frame, xlator_t *this,
                               struct afr_reply *replies,
                               unsigned char *locked_on)
{
        afr_private_t *priv  = NULL;
        int            source = -1;
        struct iatt    source_ia;
        struct iatt    child_ia;
        uint64_t       mtime = 0;
        int            i;
        int            ret   = -1;

        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (!locked_on[i])
                        continue;

                if (!replies[i].valid)
                        continue;

                if (replies[i].op_ret != 0)
                        continue;

                if (mtime_ns(&replies[i].poststat) <= mtime)
                        continue;

                mtime = mtime_ns(&replies[i].poststat);
                source = i;
        }

        if (source == -1)
                goto out;

        source_ia = replies[source].poststat;
        if (source_ia.ia_type != IA_IFDIR)
                goto out;

        for (i = 0; i < priv->child_count; i++) {
                if (i == source)
                        continue;

                if (!replies[i].valid)
                        continue;

                if (replies[i].op_ret != 0)
                        continue;

                child_ia = replies[i].poststat;

                if (!IA_EQUAL(source_ia, child_ia, gfid) ||
                    !IA_EQUAL(source_ia, child_ia, type) ||
                    !IA_EQUAL(source_ia, child_ia, prot) ||
                    !IA_EQUAL(source_ia, child_ia, uid) ||
                    !IA_EQUAL(source_ia, child_ia, gid) ||
                    !afr_xattrs_are_equal (replies[source].xdata,
                                           replies[i].xdata))
                        goto out;
        }

        /*
         * Metadata split brain is just about [amc]time
         * We return our source.
         */
        ret = source;
out:
        return ret;
}


/*
 * Look for mismatching uid/gid or mode or user xattrs even if
 * AFR xattrs don't say so, and pick one arbitrarily as winner. */

static int
__afr_selfheal_metadata_finalize_source (call_frame_t *frame, xlator_t *this,
                                         inode_t *inode,
                                         unsigned char *sources,
                                         unsigned char *sinks,
					 unsigned char *healed_sinks,
					 unsigned char *undid_pending,
					 unsigned char *locked_on,
					 struct afr_reply *replies)
{
	int i = 0;
	afr_private_t *priv = NULL;
	struct iatt srcstat = {0, };
	int source = -1;
	int sources_count = 0;

	priv = this->private;

	sources_count = AFR_COUNT (sources, priv->child_count);

	if ((AFR_CMP (locked_on, healed_sinks, priv->child_count) == 0)
	    || !sources_count) {

                source = afr_mark_split_brain_source_sinks (frame, this, inode,
                                                            sources, sinks,
                                                            healed_sinks,
                                                            locked_on, replies,
                                                      AFR_METADATA_TRANSACTION);
                if (source >= 0) {
                        _afr_fav_child_reset_sink_xattrs (frame, this, inode,
                                                         source, healed_sinks,
                                                         undid_pending,
                                                       AFR_METADATA_TRANSACTION,
                                                         locked_on, replies);
                        return source;
                }

		/* If this is a directory mtime/ctime only split brain
		   use the most recent */
		source = afr_dirtime_splitbrain_source (frame, this,
							replies, locked_on);
		if (source != -1) {
		        gf_msg (this->name, GF_LOG_INFO, 0,
                                AFR_MSG_SPLIT_BRAIN, "clear time "
				"split brain on %s",
				 uuid_utoa (replies[source].poststat.ia_gfid));
			sources[source] = 1;
			healed_sinks[source] = 0;
			return source;
		}

		if (!priv->metadata_splitbrain_forced_heal) {
                        gf_event (EVENT_AFR_SPLIT_BRAIN, "subvol=%s;"
                                  "type=metadata;file=%s",
                                  this->name, uuid_utoa(inode->gfid));
			return -EIO;
		}

		/* Metadata split brain, select one subvol
		   arbitrarily */
		for (i = 0; i < priv->child_count; i++) {
			if (locked_on[i] && healed_sinks[i]) {
				sources[i] = 1;
				healed_sinks[i] = 0;
				break;
			}
		}
	}

        /* No split brain at this point. If we were called from
         * afr_heal_splitbrain_file(), abort.*/
        if (afr_dict_contains_heal_op(frame))
                return -EIO;

        source = afr_choose_source_by_policy (priv, sources,
                                              AFR_METADATA_TRANSACTION);
        srcstat = replies[source].poststat;

	for (i = 0; i < priv->child_count; i++) {
		if (!sources[i] || i == source)
			continue;
		if (!IA_EQUAL (srcstat, replies[i].poststat, type) ||
		    !IA_EQUAL (srcstat, replies[i].poststat, uid) ||
		    !IA_EQUAL (srcstat, replies[i].poststat, gid) ||
		    !IA_EQUAL (srcstat, replies[i].poststat, prot)) {
                        gf_msg_debug (this->name, 0, "%s: iatt mismatch "
                                      "for source(%d) vs (%d)",
                                      uuid_utoa
                                      (replies[source].poststat.ia_gfid),
                                      source, i);
			sources[i] = 0;
			healed_sinks[i] = 1;
		}
	}

        for (i =0; i < priv->child_count; i++) {
		if (!sources[i] || i == source)
			continue;
                if (!afr_xattrs_are_equal (replies[source].xdata,
                                           replies[i].xdata)) {
                        gf_msg_debug (this->name, 0, "%s: xattr mismatch "
                                      "for source(%d) vs (%d)",
                                      uuid_utoa
                                      (replies[source].poststat.ia_gfid),
                                      source, i);
                        sources[i] = 0;
                        healed_sinks[i] = 1;
                }
        }

	return source;
}


int
__afr_selfheal_metadata_prepare (call_frame_t *frame, xlator_t *this, inode_t *inode,
				 unsigned char *locked_on, unsigned char *sources,
				 unsigned char *sinks, unsigned char *healed_sinks,
                                 unsigned char *undid_pending,
				 struct afr_reply *replies, gf_boolean_t *pflag)
{
	int ret = -1;
	int source = -1;
	afr_private_t *priv = NULL;
	int i = 0;
        uint64_t *witness = NULL;

	priv = this->private;

	ret = afr_selfheal_unlocked_discover (frame, inode, inode->gfid,
					      replies);
        if (ret)
                return ret;

        witness = alloca0 (sizeof (*witness) * priv->child_count);
        ret = afr_selfheal_find_direction (frame, this, replies,
					   AFR_METADATA_TRANSACTION,
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

        /* If any source has witness, pick first
         * witness source and make everybody else sinks */
        for (i = 0; i < priv->child_count; i++) {
                if (sources[i] && witness[i]) {
                        source = i;
                        break;
                }
        }

        if (source != -1) {
                for (i = 0; i < priv->child_count; i++) {
                        if (i != source && sources[i]) {
                                sources[i] = 0;
                                healed_sinks[i] = 1;
                        }
                }
        }

	source = __afr_selfheal_metadata_finalize_source (frame, this, inode,
                                                          sources, sinks,
                                                          healed_sinks,
                                                          undid_pending,
                                                          locked_on, replies);

	if (source < 0)
		return -EIO;

	return source;
}

int
afr_selfheal_metadata (call_frame_t *frame, xlator_t *this, inode_t *inode)
{
	afr_private_t *priv = NULL;
	int ret = -1;
	unsigned char *sources = NULL;
	unsigned char *sinks = NULL;
	unsigned char *data_lock = NULL;
	unsigned char *healed_sinks = NULL;
	unsigned char *undid_pending = NULL;
	struct afr_reply *locked_replies = NULL;
        gf_boolean_t did_sh = _gf_true;
	int source = -1;

	priv = this->private;

	sources = alloca0 (priv->child_count);
	sinks = alloca0 (priv->child_count);
	healed_sinks = alloca0 (priv->child_count);
	undid_pending = alloca0 (priv->child_count);
	data_lock = alloca0 (priv->child_count);

	locked_replies = alloca0 (sizeof (*locked_replies) * priv->child_count);

	ret = afr_selfheal_inodelk (frame, this, inode, this->name,
				    LLONG_MAX - 1, 0, data_lock);
	{
		if (ret < AFR_SH_MIN_PARTICIPANTS) {
			ret = -ENOTCONN;
			goto unlock;
		}

		ret = __afr_selfheal_metadata_prepare (frame, this, inode,
                                                       data_lock, sources,
                                                       sinks, healed_sinks,
                                                       undid_pending,
						       locked_replies, NULL);
		if (ret < 0)
			goto unlock;

		source = ret;

                if (AFR_COUNT (healed_sinks, priv->child_count) == 0) {
                        did_sh = _gf_false;
                        goto unlock;
                }

                ret = __afr_selfheal_metadata_do (frame, this, inode, source,
                                                  healed_sinks, locked_replies);
                if (ret)
                        goto unlock;

                ret = afr_selfheal_undo_pending (frame, this, inode, sources,
                                                 sinks, healed_sinks,
                                                 undid_pending,
                                                 AFR_METADATA_TRANSACTION,
                                                 locked_replies, data_lock);
	}
unlock:
	afr_selfheal_uninodelk (frame, this, inode, this->name,
				LLONG_MAX -1, 0, data_lock);

        if (did_sh)
                afr_log_selfheal (inode->gfid, this, ret, "metadata", source,
                                  sources, healed_sinks);
        else
                ret = 1;

        if (locked_replies)
                afr_replies_wipe (locked_replies, priv->child_count);
	return ret;
}

int
afr_selfheal_metadata_by_stbuf (xlator_t *this, struct iatt *stbuf)
{
        inode_t      *inode      = NULL;
        inode_t      *link_inode = NULL;
        call_frame_t *frame      = NULL;
        int          ret         = 0;

        if (gf_uuid_is_null (stbuf->ia_gfid)) {
                ret = -EINVAL;
                goto out;
        }

        inode = inode_new (this->itable);
        if (!inode) {
                ret = -ENOMEM;
                goto out;
        }

        link_inode = inode_link (inode, NULL, NULL, stbuf);
        if (!link_inode) {
                ret = -ENOMEM;
                goto out;
        }

        frame = afr_frame_create (this);
        if (!frame) {
                ret = -ENOMEM;
                goto out;
        }

        ret = afr_selfheal_metadata (frame, this, link_inode);
out:
        if (inode)
                inode_unref (inode);
        if (link_inode)
                inode_unref (link_inode);
        if (frame)
                AFR_STACK_DESTROY (frame);
        return ret;
}
