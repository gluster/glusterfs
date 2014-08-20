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

#define AFR_HEAL_ATTR (GF_SET_ATTR_UID|GF_SET_ATTR_GID|GF_SET_ATTR_MODE)

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
	uuid_copy (loc.gfid, inode->gfid);

	gf_log (this->name, GF_LOG_INFO, "performing metadata selfheal on %s",
		uuid_utoa (inode->gfid));

	ret = syncop_getxattr (priv->children[source], &loc, &xattr, NULL);
	if (ret < 0) {
		ret = -EIO;
                goto out;
	}

	afr_filter_xattrs (xattr);
	dict_del (xattr, GF_SELINUX_XATTR_KEY);

	for (i = 0; i < priv->child_count; i++) {
                if (old_xattr) {
                        dict_unref (old_xattr);
                        old_xattr = NULL;
                }

		if (!healed_sinks[i])
			continue;

		ret = syncop_setattr (priv->children[i], &loc,
				      &locked_replies[source].poststat,
				      AFR_HEAL_ATTR, NULL, NULL);
		if (ret)
			healed_sinks[i] = 0;

		ret = syncop_getxattr (priv->children[i], &loc, &old_xattr, 0);
		if (old_xattr) {
			dict_del (old_xattr, GF_SELINUX_XATTR_KEY);
			afr_filter_xattrs (old_xattr);
			ret = syncop_removexattr (priv->children[i], &loc, "",
						  old_xattr);
		}

		ret = syncop_setxattr (priv->children[i], &loc, xattr, 0);
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


/*
 * Look for mismatching uid/gid or mode or user xattrs even if
 * AFR xattrs don't say so, and pick one arbitrarily as winner. */

static int
__afr_selfheal_metadata_finalize_source (call_frame_t *frame, xlator_t *this,
                                         unsigned char *sources,
					 unsigned char *healed_sinks,
					 unsigned char *locked_on,
					 struct afr_reply *replies)
{
	int i = 0;
	afr_private_t *priv = NULL;
	struct iatt first = {0, };
	int source = -1;
	int sources_count = 0;

	priv = this->private;

	sources_count = AFR_COUNT (sources, priv->child_count);

	if ((AFR_CMP (locked_on, healed_sinks, priv->child_count) == 0)
            || !sources_count) {
		if (!priv->metadata_splitbrain_forced_heal) {
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

	for (i = 0; i < priv->child_count; i++) {
		if (!sources[i])
			continue;
		if (source == -1) {
			source = i;
			first = replies[i].poststat;
                        break;
		}
	}

	for (i = 0; i < priv->child_count; i++) {
		if (!sources[i] || i == source)
			continue;
		if (!IA_EQUAL (first, replies[i].poststat, type) ||
		    !IA_EQUAL (first, replies[i].poststat, uid) ||
		    !IA_EQUAL (first, replies[i].poststat, gid) ||
		    !IA_EQUAL (first, replies[i].poststat, prot)) {
                        gf_log (this->name, GF_LOG_DEBUG, "%s: iatt mismatch "
                                "for source(%d) vs (%d)",
                                uuid_utoa (replies[source].poststat.ia_gfid),
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
                        gf_log (this->name, GF_LOG_DEBUG, "%s: xattr mismatch "
                                "for source(%d) vs (%d)",
                                uuid_utoa (replies[source].poststat.ia_gfid),
                                source, i);
                        sources[i] = 0;
                        healed_sinks[i] = 1;
                }
        }

	return source;
}

static int
__afr_selfheal_metadata_prepare (call_frame_t *frame, xlator_t *this, inode_t *inode,
				 unsigned char *locked_on, unsigned char *sources,
				 unsigned char *sinks, unsigned char *healed_sinks,
				 struct afr_reply *replies)
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

	source = __afr_selfheal_metadata_finalize_source (frame, this, sources,
                                                          healed_sinks,
                                                          locked_on, replies);

	if (source < 0)
		return -EIO;

	return source;
}

static int
__afr_selfheal_metadata (call_frame_t *frame, xlator_t *this, inode_t *inode,
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
						       locked_replies);
		if (ret < 0)
			goto unlock;

		source = ret;

                if (AFR_COUNT (healed_sinks, priv->child_count) == 0) {
                        ret = -ENOTCONN;
                        goto unlock;
                }

                ret = __afr_selfheal_metadata_do (frame, this, inode, source,
                                                  healed_sinks, locked_replies);
                if (ret)
                        goto unlock;

                ret = afr_selfheal_undo_pending (frame, this, inode, sources,
                                                 sinks, healed_sinks,
                                                 AFR_METADATA_TRANSACTION,
                                                 locked_replies, data_lock);
	}
unlock:
	afr_selfheal_uninodelk (frame, this, inode, this->name,
				LLONG_MAX -1, 0, data_lock);

        afr_log_selfheal (inode->gfid, this, ret, "metadata", source,
                          healed_sinks);

        if (locked_replies)
                afr_replies_wipe (locked_replies, priv->child_count);
	return ret;
}


int
afr_selfheal_metadata (call_frame_t *frame, xlator_t *this, inode_t *inode)
{
	afr_private_t *priv = NULL;
	unsigned char *locked_on = NULL;
	int ret = 0;

	priv = this->private;

	locked_on = alloca0 (priv->child_count);

	ret = afr_selfheal_tryinodelk (frame, this, inode, priv->sh_domain, 0, 0,
				       locked_on);
	{
		if (ret < AFR_SH_MIN_PARTICIPANTS) {
			/* Either less than two subvols available, or another
			   selfheal (from another server) is in progress. Skip
			   for now in any case there isn't anything to do.
			*/
			ret = -ENOTCONN;
			goto unlock;
		}

		ret = __afr_selfheal_metadata (frame, this, inode, locked_on);
	}
unlock:
	afr_selfheal_uninodelk (frame, this, inode, priv->sh_domain, 0, 0, locked_on);

	return ret;
}
