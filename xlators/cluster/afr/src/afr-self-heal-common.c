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
#include "afr-messages.h"
#include "events.h"

void
afr_heal_synctask (xlator_t *this, afr_local_t *local);

int
afr_selfheal_post_op_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			  int op_ret, int op_errno, dict_t *xattr, dict_t *xdata)
{
	afr_local_t *local = NULL;

	local = frame->local;

        local->op_ret = op_ret;
        local->op_errno = op_errno;
	syncbarrier_wake (&local->barrier);

	return 0;
}


int
afr_selfheal_post_op (call_frame_t *frame, xlator_t *this, inode_t *inode,
		      int subvol, dict_t *xattr, dict_t *xdata)
{
	afr_private_t *priv = NULL;
	afr_local_t *local = NULL;
	loc_t loc = {0, };
        int ret = 0;

	priv = this->private;
	local = frame->local;

	loc.inode = inode_ref (inode);
	gf_uuid_copy (loc.gfid, inode->gfid);

        local->op_ret = 0;

	STACK_WIND (frame, afr_selfheal_post_op_cbk, priv->children[subvol],
		    priv->children[subvol]->fops->xattrop, &loc,
		    GF_XATTROP_ADD_ARRAY, xattr, xdata);

	syncbarrier_wait (&local->barrier, 1);
        if (local->op_ret < 0)
                ret = -local->op_errno;

        loc_wipe (&loc);
        local->op_ret = 0;

	return ret;
}

int
afr_check_stale_error (struct afr_reply *replies, afr_private_t *priv)
{
        int i = 0;
        int op_errno = 0;
        int tmp_errno = 0;
        int stale_count = 0;

        for (i = 0; i < priv->child_count; i++) {
                tmp_errno = replies[i].op_errno;
                if (tmp_errno == ENOENT || tmp_errno == ESTALE) {
                        op_errno = afr_higher_errno (op_errno, tmp_errno);
                        stale_count++;
                }
        }
        if (stale_count != priv->child_count)
                return -ENOTCONN;
        else
                return -op_errno;
}


dict_t *
afr_selfheal_output_xattr (xlator_t *this, gf_boolean_t is_full_crawl,
                           afr_transaction_type type, int *output_dirty,
                           int **output_matrix, int subvol,
                           int **full_heal_mtx_out)
{
	int                j     = 0;
	int                idx   = 0;
	int                d_idx = 0;
	int                ret   = 0;
	int               *raw   = 0;
	dict_t            *xattr = NULL;
	afr_private_t     *priv  = NULL;

	priv = this->private;
	idx = afr_index_for_transaction_type (type);
        d_idx = afr_index_for_transaction_type (AFR_DATA_TRANSACTION);

	xattr = dict_new ();
	if (!xattr)
		return NULL;

	/* clear dirty */
	raw = GF_CALLOC (sizeof(int), AFR_NUM_CHANGE_LOGS, gf_afr_mt_int32_t);
	if (!raw)
		goto err;

	raw[idx] = hton32 (output_dirty[subvol]);
	ret = dict_set_bin (xattr, AFR_DIRTY, raw,
			    sizeof(int) * AFR_NUM_CHANGE_LOGS);
	if (ret) {
                GF_FREE (raw);
		goto err;
        }

	/* clear/set pending */
	for (j = 0; j < priv->child_count; j++) {
		raw = GF_CALLOC (sizeof(int), AFR_NUM_CHANGE_LOGS,
				 gf_afr_mt_int32_t);
		if (!raw)
			goto err;

		raw[idx] = hton32 (output_matrix[subvol][j]);
                if (is_full_crawl)
                        raw[d_idx] = hton32 (full_heal_mtx_out[subvol][j]);

		ret = dict_set_bin (xattr, priv->pending_key[j],
				    raw, sizeof(int) * AFR_NUM_CHANGE_LOGS);
		if (ret) {
                        GF_FREE (raw);
			goto err;
                }
	}

	return xattr;
err:
	if (xattr)
		dict_unref (xattr);
	return NULL;
}


int
afr_selfheal_undo_pending (call_frame_t *frame, xlator_t *this, inode_t *inode,
			   unsigned char *sources, unsigned char *sinks,
			   unsigned char *healed_sinks,
                           unsigned char *undid_pending,
                           afr_transaction_type type, struct afr_reply *replies,
                           unsigned char *locked_on)
{
	afr_private_t *priv = NULL;
        afr_local_t *local = NULL;
	int i = 0;
	int j = 0;
	unsigned char *pending = NULL;
	int *input_dirty = NULL;
	int **input_matrix = NULL;
	int **full_heal_mtx_in = NULL;
	int **full_heal_mtx_out = NULL;
	int *output_dirty = NULL;
	int **output_matrix = NULL;
	dict_t *xattr = NULL;
	dict_t *xdata = NULL;

	priv = this->private;
        local = frame->local;

	pending = alloca0 (priv->child_count);

	input_dirty = alloca0 (priv->child_count * sizeof (int));
	input_matrix = ALLOC_MATRIX (priv->child_count, int);
	full_heal_mtx_in = ALLOC_MATRIX (priv->child_count, int);
	full_heal_mtx_out = ALLOC_MATRIX (priv->child_count, int);
	output_dirty = alloca0 (priv->child_count * sizeof (int));
	output_matrix = ALLOC_MATRIX (priv->child_count, int);

        xdata = dict_new ();
        if (!xdata)
                return -1;

	afr_selfheal_extract_xattr (this, replies, type, input_dirty,
				    input_matrix);

        if (local->need_full_crawl)
                afr_selfheal_extract_xattr (this, replies, AFR_DATA_TRANSACTION,
                                            NULL, full_heal_mtx_in);

	for (i = 0; i < priv->child_count; i++)
		if (sinks[i] && !healed_sinks[i])
			pending[i] = 1;

	for (i = 0; i < priv->child_count; i++) {
		for (j = 0; j < priv->child_count; j++) {
			if (pending[j]) {
				output_matrix[i][j] = 1;
                                if (type == AFR_ENTRY_TRANSACTION)
                                        full_heal_mtx_out[i][j] = 1;
			} else {
				output_matrix[i][j] = -input_matrix[i][j];
                                if (type == AFR_ENTRY_TRANSACTION)
                                        full_heal_mtx_out[i][j] = -full_heal_mtx_in[i][j];
                        }
		}
	}

	for (i = 0; i < priv->child_count; i++) {
		if (!pending[i])
			output_dirty[i] = -input_dirty[i];
	}

	for (i = 0; i < priv->child_count; i++) {
		if (!locked_on[i])
			/* perform post-op only on subvols we had locked
			   and inspected on.
			*/
			continue;
                if (undid_pending[i])
                        /* We already unset the pending xattrs in
                         * _afr_fav_child_reset_sink_xattrs(). */
                        continue;

		xattr = afr_selfheal_output_xattr (this, local->need_full_crawl,
                                                   type, output_dirty,
                                                   output_matrix, i,
                                                   full_heal_mtx_out);
		if (!xattr) {
			continue;
		}

                if ((type == AFR_ENTRY_TRANSACTION) && (priv->esh_granular)) {
                        if (xdata &&
                            dict_set_int8 (xdata, GF_XATTROP_PURGE_INDEX, 1))
                                gf_msg (this->name, GF_LOG_WARNING, 0,
                                        AFR_MSG_DICT_SET_FAILED, "Failed to set"
                                        " dict value for %s",
                                        GF_XATTROP_PURGE_INDEX);
                }

		afr_selfheal_post_op (frame, this, inode, i, xattr, xdata);
		dict_unref (xattr);
	}

        if (xdata)
                dict_unref (xdata);

	return 0;
}


void
afr_replies_copy (struct afr_reply *dst, struct afr_reply *src, int count)
{
	int i = 0;
	dict_t *xdata = NULL;

	if (dst == src)
		return;

	for (i = 0; i < count; i++) {
		dst[i].valid = src[i].valid;
		dst[i].op_ret = src[i].op_ret;
		dst[i].op_errno = src[i].op_errno;
		dst[i].prestat = src[i].prestat;
		dst[i].poststat = src[i].poststat;
		dst[i].preparent = src[i].preparent;
		dst[i].postparent = src[i].postparent;
		dst[i].preparent2 = src[i].preparent2;
		dst[i].postparent2 = src[i].postparent2;
		if (src[i].xdata)
			xdata = dict_ref (src[i].xdata);
		else
			xdata = NULL;
		if (dst[i].xdata)
			dict_unref (dst[i].xdata);
		dst[i].xdata = xdata;
		memcpy (dst[i].checksum, src[i].checksum,
			MD5_DIGEST_LENGTH);
	}
}


int
afr_selfheal_fill_dirty (xlator_t *this, int *dirty, int subvol,
			 int idx, dict_t *xdata)
{
	void *pending_raw = NULL;
	int pending[3] = {0, };

        if (!dirty)
                return 0;

	if (dict_get_ptr (xdata, AFR_DIRTY, &pending_raw))
		return -1;

	if (!pending_raw)
		return -1;

	memcpy (pending, pending_raw, sizeof(pending));

	dirty[subvol] = ntoh32 (pending[idx]);

	return 0;
}


int
afr_selfheal_fill_matrix (xlator_t *this, int **matrix, int subvol,
			  int idx, dict_t *xdata)
{
	int i = 0;
	void *pending_raw = NULL;
	int pending[3] = {0, };
	afr_private_t *priv = NULL;

	priv = this->private;

        if (!matrix)
                return 0;

	for (i = 0; i < priv->child_count; i++) {
		if (dict_get_ptr (xdata, priv->pending_key[i], &pending_raw))
			continue;

		if (!pending_raw)
			continue;

		memcpy (pending, pending_raw, sizeof(pending));

		matrix[subvol][i] = ntoh32 (pending[idx]);
	}

	return 0;
}


int
afr_selfheal_extract_xattr (xlator_t *this, struct afr_reply *replies,
			    afr_transaction_type type, int *dirty, int **matrix)
{
	afr_private_t *priv = NULL;
	int i = 0;
	dict_t *xdata = NULL;
	int idx = -1;

	idx = afr_index_for_transaction_type (type);

	priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
		if (!replies[i].xdata)
			continue;

		xdata = replies[i].xdata;

		afr_selfheal_fill_dirty (this, dirty, i, idx, xdata);
		afr_selfheal_fill_matrix (this, matrix, i, idx, xdata);
	}

	return 0;
}

/*
 * If by chance there are multiple sources with differing sizes, select
 * the largest file as the source.
 *
 * This can happen if data was directly modified in the backend or for snapshots
 */
void
afr_mark_largest_file_as_source (xlator_t *this, unsigned char *sources,
                                 struct afr_reply *replies)
{
        int i = 0;
        afr_private_t *priv = NULL;
        uint64_t size = 0;

        /* Find source with biggest file size */
        priv = this->private;
        for (i = 0; i < priv->child_count; i++) {
                if (!sources[i])
                        continue;
                if (!replies[i].valid || replies[i].op_ret != 0) {
                        sources[i] = 0;
                        continue;
                }
                if (size <= replies[i].poststat.ia_size) {
                        size = replies[i].poststat.ia_size;
                }
        }

        /* Mark sources with less size as not source */
        for (i = 0; i < priv->child_count; i++) {
                if (!sources[i])
                        continue;
                if (size > replies[i].poststat.ia_size)
                        sources[i] = 0;
        }
}

void
afr_mark_latest_mtime_file_as_source (xlator_t *this, unsigned char *sources,
                                 struct afr_reply *replies)
{
        int i = 0;
        afr_private_t *priv = NULL;
        uint32_t mtime = 0;
        uint32_t mtime_nsec = 0;

        priv = this->private;
        for (i = 0; i < priv->child_count; i++) {
                if (!sources[i])
                        continue;
                if (!replies[i].valid || replies[i].op_ret != 0) {
                        sources[i] = 0;
                        continue;
                }
                if ((mtime < replies[i].poststat.ia_mtime) ||
                    ((mtime == replies[i].poststat.ia_mtime) &&
                     (mtime_nsec < replies[i].poststat.ia_mtime_nsec))) {
                        mtime = replies[i].poststat.ia_mtime;
                        mtime_nsec = replies[i].poststat.ia_mtime_nsec;
                }
        }
        for (i = 0; i < priv->child_count; i++) {
                if (!sources[i])
                        continue;
                if ((mtime > replies[i].poststat.ia_mtime) ||
                    ((mtime == replies[i].poststat.ia_mtime) &&
                     (mtime_nsec > replies[i].poststat.ia_mtime_nsec))) {
                        sources[i] = 0;
                }
        }
}

void
afr_mark_active_sinks (xlator_t *this, unsigned char *sources,
                       unsigned char *locked_on, unsigned char *sinks)
{
        int i = 0;
        afr_private_t *priv = NULL;

        priv = this->private;

        memset (sinks, 0, sizeof (*sinks) * priv->child_count);
        for (i = 0; i < priv->child_count; i++) {
                if (!sources[i] && locked_on[i])
                        sinks[i] = 1;
        }
}

gf_boolean_t
afr_dict_contains_heal_op (call_frame_t *frame)
{
        afr_local_t   *local     = NULL;
        dict_t        *xdata_req = NULL;
        int            ret       = 0;
        int            heal_op   = -1;

        local = frame->local;
        xdata_req = local->xdata_req;
        ret = dict_get_int32 (xdata_req, "heal-op", &heal_op);
        if (ret)
                return _gf_false;
        if (local->xdata_rsp == NULL) {
                local->xdata_rsp = dict_new();
                if (!local->xdata_rsp)
                        return _gf_true;
        }
        ret = dict_set_str (local->xdata_rsp, "sh-fail-msg",
                            "File not in split-brain");

        return _gf_true;
}

gf_boolean_t
afr_can_decide_split_brain_source_sinks (struct afr_reply *replies,
                                         int child_count)
{
        int i = 0;

        for (i = 0; i < child_count; i++)
                if (replies[i].valid != 1 || replies[i].op_ret != 0)
                        return _gf_false;

        return _gf_true;
}

int
afr_mark_split_brain_source_sinks_by_heal_op (call_frame_t *frame,
                                   xlator_t *this, unsigned char *sources,
                                   unsigned char *sinks,
                                   unsigned char *healed_sinks,
                                   unsigned char *locked_on,
                                   struct afr_reply *replies,
                                   afr_transaction_type type, int heal_op)
{
        afr_local_t   *local     = NULL;
        afr_private_t *priv      = NULL;
        dict_t        *xdata_req = NULL;
        dict_t        *xdata_rsp = NULL;
        int            ret       = 0;
        int            i         = 0;
        char          *name      = NULL;
        int            source     = -1;

        local = frame->local;
        priv = this->private;
        xdata_req = local->xdata_req;

        for (i = 0; i < priv->child_count; i++) {
                if (locked_on[i])
                        if (sources[i] || !sinks[i] || !healed_sinks[i]) {
                                ret = -1;
                                goto out;
                        }
        }
        if (local->xdata_rsp == NULL) {
                local->xdata_rsp = dict_new();
                if (!local->xdata_rsp) {
                        ret = -1;
                        goto out;
                }
        }
        xdata_rsp = local->xdata_rsp;

        if (!afr_can_decide_split_brain_source_sinks (replies,
                                                      priv->child_count)) {
                ret = dict_set_str (xdata_rsp, "sh-fail-msg",
                                    SBRAIN_HEAL_NO_GO_MSG);
                ret = -1;
                goto out;
        }

        for (i = 0 ; i < priv->child_count; i++)
                if (locked_on[i])
                        sources[i] = 1;
        switch (heal_op) {
        case GF_SHD_OP_SBRAIN_HEAL_FROM_BIGGER_FILE:
                if (type == AFR_METADATA_TRANSACTION) {
                        ret = dict_set_str (xdata_rsp, "sh-fail-msg",
                                            "Use source-brick option to"
                                            " heal metadata split-brain");
                        if (!ret)
                                ret = -1;
                        goto out;
                }
                afr_mark_largest_file_as_source (this, sources, replies);
                if (AFR_COUNT (sources, priv->child_count) != 1) {
                        ret = dict_set_str (xdata_rsp, "sh-fail-msg",
                                            "No bigger file");
                        if (!ret)
                                ret = -1;
                        goto out;
                }
                break;
        case GF_SHD_OP_SBRAIN_HEAL_FROM_LATEST_MTIME:
                if (type == AFR_METADATA_TRANSACTION) {
                        ret = dict_set_str (xdata_rsp, "sh-fail-msg",
                                            "Use source-brick option to"
                                            " heal metadata split-brain");
                        if (!ret)
                                ret = -1;
                        goto out;
                }
                afr_mark_latest_mtime_file_as_source (this, sources, replies);
                if (AFR_COUNT (sources, priv->child_count) != 1) {
                        ret = dict_set_str (xdata_rsp, "sh-fail-msg",
                                            "No difference in mtime");
                        if (!ret)
                                ret = -1;
                        goto out;
                }
                break;
        case GF_SHD_OP_SBRAIN_HEAL_FROM_BRICK:
                ret = dict_get_str (xdata_req, "child-name", &name);
                if (ret)
                        goto out;
                source = afr_get_child_index_from_name (this, name);
                if (source < 0) {
                        ret = dict_set_str (xdata_rsp, "sh-fail-msg",
                                            "Invalid brick name");
                        if (!ret)
                                ret = -1;
                        goto out;
                }
                if (locked_on[source] != 1) {
                        ret = dict_set_str (xdata_rsp, "sh-fail-msg",
                                            "Brick is not up");
                        if (!ret)
                                ret = -1;
                        goto out;
                }
                memset (sources, 0, sizeof (*sources) * priv->child_count);
                sources[source] = 1;
                break;
        default:
                ret = -1;
                goto out;
        }
        for (i = 0 ; i < priv->child_count; i++) {
                if (sources[i]) {
                        source = i;
                        break;
                }
        }
        sinks[source] = 0;
        healed_sinks[source] = 0;
        ret = source;
out:
        if (ret < 0)
                memset (sources, 0, sizeof (*sources) * priv->child_count);
        return ret;

}

int
afr_sh_fav_by_majority (xlator_t *this, struct afr_reply *replies,
                        inode_t *inode)
{
        afr_private_t   *priv;
        int             vote_count = -1;
        int             fav_child = -1;
        int             i = 0;
        int             k = 0;

        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (replies[i].valid == 1) {
                        gf_msg_debug (this->name, 0, "Child:%s "
                                "mtime_sec = %d, size = %lu for gfid %s",
                                priv->children[i]->name,
                                replies[i].poststat.ia_mtime,
                                replies[i].poststat.ia_size,
                                uuid_utoa (inode->gfid));
                                vote_count = 0;
                        for (k = 0; k < priv->child_count; k++) {
                                if ((replies[k].poststat.ia_mtime ==
                                     replies[i].poststat.ia_mtime) &&
                                    (replies[k].poststat.ia_size ==
                                     replies[i].poststat.ia_size)
                                   ) {
                                        vote_count++;
                                }
                        }
                        if (vote_count > priv->child_count/2) {
                                fav_child = i;
                                break;
                        }
                }
        }
        return fav_child;
}

/*
 * afr_sh_fav_by_mtime: Choose favorite child by mtime.
 */
int
afr_sh_fav_by_mtime (xlator_t *this, struct afr_reply *replies, inode_t *inode)
{
        afr_private_t *priv;
        int fav_child = -1;
        int i = 0;
        uint32_t cmp_mtime = 0;
        uint32_t cmp_mtime_nsec = 0;

        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (replies[i].valid == 1) {
                        gf_msg_debug (this->name, 0, "Child:%s "
                                "mtime = %d, mtime_nsec = %d for gfid %s",
                                priv->children[i]->name,
                                replies[i].poststat.ia_mtime,
                                replies[i].poststat.ia_mtime_nsec,
                                uuid_utoa (inode->gfid));
                        if (replies[i].poststat.ia_mtime > cmp_mtime) {
                                cmp_mtime = replies[i].poststat.ia_mtime;
                                cmp_mtime_nsec =
                                        replies[i].poststat.ia_mtime_nsec;
                                fav_child = i;
                        } else if ((replies[i].poststat.ia_mtime == cmp_mtime)
                                    && (replies[i].poststat.ia_mtime_nsec >
                                    cmp_mtime_nsec)) {
                                cmp_mtime = replies[i].poststat.ia_mtime;
                                cmp_mtime_nsec =
                                        replies[i].poststat.ia_mtime_nsec;
                                fav_child = i;
                        }
                }
        }
        return fav_child;
}

/*
 * afr_sh_fav_by_ctime: Choose favorite child by ctime.
 */
int
afr_sh_fav_by_ctime (xlator_t *this, struct afr_reply *replies, inode_t *inode)
{
        afr_private_t *priv;
        int fav_child = -1;
        int i = 0;
        uint32_t cmp_ctime = 0;
        uint32_t cmp_ctime_nsec = 0;

        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (replies[i].valid == 1) {
                        gf_msg_debug (this->name, 0, "Child:%s "
                                "ctime = %d, ctime_nsec = %d for gfid %s",
                                priv->children[i]->name,
                                replies[i].poststat.ia_ctime,
                                replies[i].poststat.ia_ctime_nsec,
                                uuid_utoa (inode->gfid));
                        if (replies[i].poststat.ia_ctime > cmp_ctime) {
                                cmp_ctime = replies[i].poststat.ia_ctime;
                                cmp_ctime_nsec =
                                        replies[i].poststat.ia_ctime_nsec;
                                fav_child = i;
                        } else if ((replies[i].poststat.ia_ctime == cmp_ctime)
                                    && (replies[i].poststat.ia_ctime_nsec >
                                    cmp_ctime_nsec)) {
                                cmp_ctime = replies[i].poststat.ia_ctime;
                                cmp_ctime_nsec =
                                        replies[i].poststat.ia_ctime_nsec;
                                fav_child = i;
                        }
                }
        }
        return fav_child;
}

/*
 * afr_sh_fav_by_size: Choose favorite child by size.
 */
int
afr_sh_fav_by_size (xlator_t *this, struct afr_reply *replies, inode_t *inode)
{
        afr_private_t *priv;
        int fav_child = -1;
        int i = 0;
        uint64_t cmp_sz = 0;

        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (replies[i].valid == 1) {
                        gf_msg_debug (this->name, 0, "Child:%s "
                                "file size = %lu for gfid %s",
                                priv->children[i]->name,
                                replies[i].poststat.ia_size,
                                uuid_utoa (inode->gfid));
                        if (replies[i].poststat.ia_size > cmp_sz) {
                                cmp_sz = replies[i].poststat.ia_size;
                                fav_child = i;
                        }
                }
        }
        return fav_child;
}

int
afr_sh_get_fav_by_policy (xlator_t *this, struct afr_reply *replies,
                          inode_t *inode, char **policy_str)
{
        afr_private_t *priv = NULL;
        int fav_child = -1;

        priv = this->private;
        if (!afr_can_decide_split_brain_source_sinks (replies,
                                                      priv->child_count)) {
                return -1;
        }

        switch (priv->fav_child_policy) {
        case AFR_FAV_CHILD_BY_SIZE:
                fav_child = afr_sh_fav_by_size (this, replies, inode);
                if (policy_str && fav_child >= 0) {
                        *policy_str = "SIZE";
                }
                break;
        case AFR_FAV_CHILD_BY_CTIME:
                fav_child = afr_sh_fav_by_ctime (this, replies, inode);
                if (policy_str && fav_child >= 0) {
                        *policy_str = "CTIME";
                }
                break;
        case AFR_FAV_CHILD_BY_MTIME:
                fav_child = afr_sh_fav_by_mtime (this, replies, inode);
                if (policy_str && fav_child >= 0) {
                        *policy_str = "MTIME";
                }
                break;
        case AFR_FAV_CHILD_BY_MAJORITY:
                fav_child = afr_sh_fav_by_majority (this, replies, inode);
                if (policy_str && fav_child >= 0) {
                        *policy_str = "MAJORITY";
                }
                break;
        case AFR_FAV_CHILD_NONE:
        default:
                break;
        }

        return fav_child;
}

int
afr_mark_split_brain_source_sinks_by_policy (call_frame_t *frame,
                                             xlator_t *this,
                                             inode_t *inode,
                                             unsigned char *sources,
                                             unsigned char *sinks,
                                             unsigned char *healed_sinks,
                                             unsigned char *locked_on,
                                             struct afr_reply *replies,
                                             afr_transaction_type type)
{
        afr_private_t *priv = NULL;
        int fav_child = -1;
        char mtime_str[256];
        char ctime_str[256];
        char *policy_str = NULL;
        struct tm *tm_ptr;
        time_t time;

        priv = this->private;

        fav_child = afr_sh_get_fav_by_policy (this, replies, inode,
                                              &policy_str);
        if (fav_child > priv->child_count - 1) {
                gf_msg (this->name, GF_LOG_ERROR, 0,
                        AFR_MSG_SBRAIN_FAV_CHILD_POLICY, "Invalid child (%d) "
                        "selected by policy %s.", fav_child, policy_str);
        } else if (fav_child >= 0) {
                time = replies[fav_child].poststat.ia_mtime;
                tm_ptr = localtime (&time);
                strftime (mtime_str, sizeof (mtime_str), "%Y-%m-%d %H:%M:%S",
                          tm_ptr);
                time = replies[fav_child].poststat.ia_ctime;
                tm_ptr = localtime (&time);
                strftime (ctime_str, sizeof (ctime_str), "%Y-%m-%d %H:%M:%S",
                          tm_ptr);

                gf_msg (this->name, GF_LOG_WARNING, 0,
                        AFR_MSG_SBRAIN_FAV_CHILD_POLICY, "Source %s "
                        "selected as authentic to resolve conflicting "
                         "data in file (gfid:%s) by %s (%lu bytes @ %s mtime, "
                         "%s ctime).",
                         priv->children[fav_child]->name,
                         uuid_utoa (inode->gfid),
                         policy_str,
                         replies[fav_child].poststat.ia_size,
                         mtime_str,
                         ctime_str);

                sources[fav_child] = 1;
                sinks[fav_child] = 0;
                healed_sinks[fav_child] = 0;
        }
        return fav_child;
}

/* Return a source depending on the type of heal_op, and set sources[source],
 * sinks[source] and healed_sinks[source] to 1, 0 and 0 respectively. Do so
 * only if the following condition is met:
 * ∀i((i ∈ locked_on[] ∧ i=1)==>(sources[i]=0 ∧ sinks[i]=1 ∧ healed_sinks[i]=1))
 * i.e. for each locked node, sources[node] is 0; healed_sinks[node] and
 * sinks[node] are 1. This should be the case if the file is in split-brain.
 */
int
afr_mark_split_brain_source_sinks (call_frame_t *frame, xlator_t *this,
                                   inode_t *inode,
                                   unsigned char *sources,
                                   unsigned char *sinks,
                                   unsigned char *healed_sinks,
                                   unsigned char *locked_on,
                                   struct afr_reply *replies,
                                   afr_transaction_type type)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        dict_t *xdata_req = NULL;
        int heal_op = -1;
        int ret = -1;
        int source = -1;

        local = frame->local;
        priv = this->private;
        xdata_req = local->xdata_req;

        ret = dict_get_int32 (xdata_req, "heal-op", &heal_op);
        if (ret)
                goto autoheal;

        source = afr_mark_split_brain_source_sinks_by_heal_op (frame, this,
                                                            sources, sinks,
                                                            healed_sinks,
                                                            locked_on, replies,
                                                            type, heal_op);
        return source;

autoheal:
        /* Automatically heal if fav_child_policy is set. */
        if (priv->fav_child_policy != AFR_FAV_CHILD_NONE) {
                source = afr_mark_split_brain_source_sinks_by_policy (frame,
                                                                      this,
                                                                      inode,
                                                                      sources,
                                                                      sinks,
                                                                   healed_sinks,
                                                                      locked_on,
                                                                      replies,
                                                                      type);
                if (source != -1) {
                        ret = dict_set_int32 (xdata_req, "fav-child-policy", 1);
                        if (ret)
                                return -1;
                }
        }

        return source;
}

int
_afr_fav_child_reset_sink_xattrs (call_frame_t *frame, xlator_t *this,
                                  inode_t *inode, int source,
                                  unsigned char *healed_sinks,
                                  unsigned char *undid_pending,
                                  afr_transaction_type type,
                                  unsigned char *locked_on,
                                  struct afr_reply *replies)
{
        afr_private_t *priv = NULL;
        afr_local_t *local = NULL;
        int *input_dirty = NULL;
        int **input_matrix = NULL;
	int *output_dirty = NULL;
	int **output_matrix = NULL;
        dict_t *xattr = NULL;
        dict_t *xdata = NULL;
        int i = 0;

        priv = this->private;
        local = frame->local;

        if (!dict_get (local->xdata_req, "fav-child-policy"))
                return 0;

        xdata = dict_new();
        if (!xdata)
                return -1;

        input_dirty = alloca0 (priv->child_count * sizeof (int));
	input_matrix = ALLOC_MATRIX (priv->child_count, int);
	output_dirty = alloca0 (priv->child_count * sizeof (int));
	output_matrix = ALLOC_MATRIX (priv->child_count, int);

        afr_selfheal_extract_xattr (this, replies, type, input_dirty,
                                    input_matrix);

        for (i = 0; i < priv->child_count; i++) {
                if (i == source || !healed_sinks[i])
                        continue;
                output_dirty[i] = -input_dirty[i];
                output_matrix[i][source] = -input_matrix[i][source];
        }

        for (i = 0; i < priv->child_count; i++) {
                if (!healed_sinks[i] || !locked_on[i])
                        continue;
                xattr = afr_selfheal_output_xattr (this, _gf_false, type,
                                                   output_dirty, output_matrix,
                                                   i, NULL);

                afr_selfheal_post_op (frame, this, inode, i, xattr, xdata);

                undid_pending[i] = 1;
                dict_unref (xattr);
        }

        if (xdata)
                dict_unref (xdata);

        return 0;
}

gf_boolean_t
afr_does_witness_exist (xlator_t *this, uint64_t *witness)
{
        int i = 0;
        afr_private_t *priv = NULL;

        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if (witness[i])
                        return _gf_true;
        }
        return _gf_false;
}

/*
 * This function determines if a self-heal is required for a given inode,
 * and if needed, in what direction.
 *
 * locked_on[] is the array representing servers which have been locked and
 * from which xattrs have been fetched for analysis.
 *
 * The output of the function is by filling the arrays sources[] and sinks[].
 *
 * sources[i] is set if i'th server is an eligible source for a selfheal.
 *
 * sinks[i] is set if i'th server needs to be healed.
 *
 * if sources[0..N] are all set, there is no need for a selfheal.
 *
 * if sinks[0..N] are all set, the inode is in split brain.
 *
 */

int
afr_selfheal_find_direction (call_frame_t *frame, xlator_t *this,
                             struct afr_reply *replies,
                             afr_transaction_type type,
                             unsigned char *locked_on, unsigned char *sources,
                             unsigned char *sinks, uint64_t *witness,
                             gf_boolean_t *pflag)
{
        afr_private_t *priv = NULL;
        int i = 0;
        int j = 0;
        int *dirty = NULL; /* Denotes if dirty xattr is set */
        int **matrix = NULL;/* Changelog matrix */
        char *accused = NULL;/* Accused others without any self-accusal */
        char *pending = NULL;/* Have pending operations on others */
        char *self_accused = NULL; /* Accused itself */

	priv = this->private;

	dirty = alloca0 (priv->child_count * sizeof (int));
	accused = alloca0 (priv->child_count);
        pending = alloca0 (priv->child_count);
        self_accused = alloca0 (priv->child_count);
	matrix = ALLOC_MATRIX(priv->child_count, int);
        memset (witness, 0, sizeof (*witness) * priv->child_count);

	/* First construct the pending matrix for further analysis */
	afr_selfheal_extract_xattr (this, replies, type, dirty, matrix);

        if (pflag) {
                for (i = 0; i < priv->child_count; i++) {
                        for (j = 0; j < priv->child_count; j++)
                                if (matrix[i][j])
                                        *pflag = _gf_true;
                        if (*pflag)
                                break;
                }
        }

        if (afr_success_count (replies,
                               priv->child_count) < AFR_SH_MIN_PARTICIPANTS) {
                /* Treat this just like locks not being acquired */
                return -ENOTCONN;
        }

        /* short list all self-accused */
        for (i = 0; i < priv->child_count; i++) {
                if (matrix[i][i])
                        self_accused[i] = 1;
        }

	/* Next short list all accused to exclude them from being sources */
        /* Self-accused can't accuse others as they are FOOLs */
	for (i = 0; i < priv->child_count; i++) {
		for (j = 0; j < priv->child_count; j++) {
                        if (matrix[i][j]) {
                                 if (!self_accused[i])
                                         accused[j] = 1;

                                 if (i != j)
                                         pending[i] = 1;
                         }
		}
	}

	/* Short list all non-accused as sources */
	memset (sources, 0, priv->child_count);
	for (i = 0; i < priv->child_count; i++) {
		if (!accused[i] && locked_on[i])
			sources[i] = 1;
	}

        /* Everyone accused by non-self-accused sources are sinks */
        memset (sinks, 0, priv->child_count);
        for (i = 0; i < priv->child_count; i++) {
                if (!sources[i])
                        continue;
                if (self_accused[i])
                        continue;
                for (j = 0; j < priv->child_count; j++) {
                        if (matrix[i][j])
                                sinks[j] = 1;
                }
        }

        /* For breaking ties provide with number of fops they witnessed */

        /*
         * count the pending fops witnessed from itself to others when it is
         * self-accused
         */
        for (i = 0; i < priv->child_count; i++) {
                if (!self_accused[i])
                        continue;
                for (j = 0; j < priv->child_count; j++) {
                        if (i == j)
                                continue;
                        witness[i] += matrix[i][j];
                }
        }

         /* If no sources, all locked nodes are sinks - split brain */
         if (AFR_COUNT (sources, priv->child_count) == 0) {
                for (i = 0; i < priv->child_count; i++) {
                        if (locked_on[i])
                                sinks[i] = 1;
                }
        }

        /* One more class of witness similar to dirty in v2 is where no pending
         * exists but we have self-accusing markers. This can happen in afr-v1
         * if the brick crashes just after doing xattrop on self but
         * before xattrop on the other xattrs on the brick in pre-op. */
        if (AFR_COUNT (pending, priv->child_count) == 0) {
                for (i = 0; i < priv->child_count; i++) {
                        if (self_accused[i])
                                witness[i] += matrix[i][i];
                }
        } else {
                /* In afr-v1 if a file is self-accused and has pending
                 * operations on others then it is similar to 'dirty' in afr-v2.
                 * Consider such cases as witness.
                 */
                for (i = 0; i < priv->child_count; i++) {
                        if (self_accused[i] && pending[i])
                                witness[i] += matrix[i][i];
                }
        }


        /* count the number of dirty fops witnessed */
        for (i = 0; i < priv->child_count; i++)
                witness[i] += dirty[i];

	return 0;
}

void
afr_log_selfheal (uuid_t gfid, xlator_t *this, int ret, char *type,
                  int source, unsigned char *sources,
                  unsigned char *healed_sinks)
{
        char *status = NULL;
        char *sinks_str = NULL;
        char *p = NULL;
        char *sources_str = NULL;
        char *q = NULL;
        afr_private_t *priv = NULL;
        gf_loglevel_t loglevel = GF_LOG_NONE;
        int i = 0;

        priv = this->private;
        sinks_str = alloca0 (priv->child_count * 8);
        p = sinks_str;
        sources_str = alloca0 (priv->child_count * 8);
        q = sources_str;
        for (i = 0; i < priv->child_count; i++) {
                if (healed_sinks[i])
                        p += sprintf (p, "%d ", i);
                if (sources[i]) {
                        if (source == i) {
                                q += sprintf (q, "[%d] ", i);
                        } else {
                                q += sprintf (q, "%d ", i);
                        }
                }
        }

        if (ret < 0) {
                status = "Failed";
                loglevel = GF_LOG_DEBUG;
        } else {
                status = "Completed";
                loglevel = GF_LOG_INFO;
        }

        gf_msg (this->name, loglevel, 0,
                AFR_MSG_SELF_HEAL_INFO, "%s %s selfheal on %s. "
                "sources=%s sinks=%s", status, type, uuid_utoa (gfid),
                sources_str, sinks_str);
}

int
afr_selfheal_discover_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			   int op_ret, int op_errno, inode_t *inode,
			   struct iatt *buf, dict_t *xdata, struct iatt *parbuf)
{
	afr_local_t *local = NULL;
	int i = -1;
        GF_UNUSED int ret = -1;
	int8_t need_heal = 1;

	local = frame->local;
	i = (long) cookie;

	local->replies[i].valid = 1;
	local->replies[i].op_ret = op_ret;
	local->replies[i].op_errno = op_errno;
	if (buf)
		local->replies[i].poststat = *buf;
	if (parbuf)
		local->replies[i].postparent = *parbuf;
	if (xdata) {
		local->replies[i].xdata = dict_ref (xdata);
                ret = dict_get_int8 (xdata, "link-count", &need_heal);
                local->replies[i].need_heal = need_heal;
        } else {
                local->replies[i].need_heal = need_heal;
        }

	syncbarrier_wake (&local->barrier);

	return 0;
}


inode_t *
afr_selfheal_unlocked_lookup_on (call_frame_t *frame, inode_t *parent,
				 const char *name, struct afr_reply *replies,
				 unsigned char *lookup_on, dict_t *xattr)
{
	loc_t loc = {0, };
	dict_t *xattr_req = NULL;
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	inode_t *inode = NULL;

	local = frame->local;
	priv = frame->this->private;

	xattr_req = dict_new ();
	if (!xattr_req)
		return NULL;

        if (xattr)
                dict_copy (xattr, xattr_req);

	if (afr_xattr_req_prepare (frame->this, xattr_req) != 0) {
		dict_unref (xattr_req);
		return NULL;
	}

	inode = inode_new (parent->table);
	if (!inode) {
		dict_unref (xattr_req);
		return NULL;
	}

	loc.parent = inode_ref (parent);
	gf_uuid_copy (loc.pargfid, parent->gfid);
	loc.name = name;
	loc.inode = inode_ref (inode);

	AFR_ONLIST (lookup_on, frame, afr_selfheal_discover_cbk, lookup, &loc,
		    xattr_req);

	afr_replies_copy (replies, local->replies, priv->child_count);

	loc_wipe (&loc);
	dict_unref (xattr_req);

	return inode;
}

int
afr_selfheal_unlocked_discover_on (call_frame_t *frame, inode_t *inode,
				   uuid_t gfid, struct afr_reply *replies,
				   unsigned char *discover_on)
{
	loc_t loc = {0, };
	dict_t *xattr_req = NULL;
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;

	local = frame->local;
	priv = frame->this->private;

	xattr_req = dict_new ();
	if (!xattr_req)
		return -ENOMEM;

	if (afr_xattr_req_prepare (frame->this, xattr_req) != 0) {
		dict_unref (xattr_req);
		return -ENOMEM;
	}

	loc.inode = inode_ref (inode);
	gf_uuid_copy (loc.gfid, gfid);

	AFR_ONLIST (discover_on, frame, afr_selfheal_discover_cbk, lookup, &loc,
		    xattr_req);

	afr_replies_copy (replies, local->replies, priv->child_count);

	loc_wipe (&loc);
	dict_unref (xattr_req);

	return 0;
}

int
afr_selfheal_unlocked_discover (call_frame_t *frame, inode_t *inode,
				uuid_t gfid, struct afr_reply *replies)
{
	afr_private_t *priv = NULL;

	priv = frame->this->private;

	return afr_selfheal_unlocked_discover_on (frame, inode, gfid, replies,
						  priv->child_up);
}

unsigned int
afr_success_count (struct afr_reply *replies, unsigned int count)
{
        int     i = 0;
        unsigned int success = 0;

        for (i = 0; i < count; i++)
                if (replies[i].valid && replies[i].op_ret == 0)
                        success++;
        return success;
}

int
afr_selfheal_lock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		       int op_ret, int op_errno, dict_t *xdata)
{
	afr_local_t *local = NULL;
	int i = 0;

	local = frame->local;
	i = (long) cookie;

	local->replies[i].valid = 1;
	local->replies[i].op_ret = op_ret;
	local->replies[i].op_errno = op_errno;

	syncbarrier_wake (&local->barrier);

	return 0;
}


int
afr_locked_fill (call_frame_t *frame, xlator_t *this,
                 unsigned char *locked_on)
{
	int i = 0;
	afr_private_t *priv = NULL;
	afr_local_t *local = NULL;
	int count = 0;

	local = frame->local;
	priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
		if (local->replies[i].valid && local->replies[i].op_ret == 0) {
			locked_on[i] = 1;
			count++;
		} else {
			locked_on[i] = 0;
		}
	}

	return count;
}


int
afr_selfheal_tryinodelk (call_frame_t *frame, xlator_t *this, inode_t *inode,
			 char *dom, off_t off, size_t size,
			 unsigned char *locked_on)
{
	loc_t loc = {0,};
	struct gf_flock flock = {0, };

	loc.inode = inode_ref (inode);
	gf_uuid_copy (loc.gfid, inode->gfid);

	flock.l_type = F_WRLCK;
	flock.l_start = off;
	flock.l_len = size;

	AFR_ONALL (frame, afr_selfheal_lock_cbk, inodelk, dom,
		    &loc, F_SETLK, &flock, NULL);

	loc_wipe (&loc);

	return afr_locked_fill (frame, this, locked_on);
}


int
afr_selfheal_inodelk (call_frame_t *frame, xlator_t *this, inode_t *inode,
		      char *dom, off_t off, size_t size,
		      unsigned char *locked_on)
{
	loc_t loc = {0,};
	struct gf_flock flock = {0, };
	afr_local_t *local = NULL;
	int i = 0;
	afr_private_t *priv = NULL;

	priv = this->private;
	local = frame->local;

	loc.inode = inode_ref (inode);
	gf_uuid_copy (loc.gfid, inode->gfid);

	flock.l_type = F_WRLCK;
	flock.l_start = off;
	flock.l_len = size;

	AFR_ONALL (frame, afr_selfheal_lock_cbk, inodelk, dom,
		    &loc, F_SETLK, &flock, NULL);

	for (i = 0; i < priv->child_count; i++) {
		if (local->replies[i].op_ret == -1 &&
		    local->replies[i].op_errno == EAGAIN) {
			afr_locked_fill (frame, this, locked_on);
			afr_selfheal_uninodelk (frame, this, inode, dom, off,
						size, locked_on);

			AFR_SEQ (frame, afr_selfheal_lock_cbk, inodelk, dom,
				 &loc, F_SETLKW, &flock, NULL);
			break;
		}
	}

	loc_wipe (&loc);

	return afr_locked_fill (frame, this, locked_on);
}

static void
afr_get_lock_and_eagain_counts (afr_private_t *priv, struct afr_reply *replies,
                                int *lock_count, int *eagain_count)
{
	int i = 0;

	for (i = 0; i < priv->child_count; i++) {
	        if (!replies[i].valid)
	                continue;
	        if (replies[i].op_ret == 0) {
	                (*lock_count)++;
                } else if (replies[i].op_ret == -1 &&
		         replies[i].op_errno == EAGAIN) {
		        (*eagain_count)++;
                }
	}
}

/*Do blocking locks if number of locks acquired is majority and there were some
 * EAGAINs. Useful for odd-way replication*/
int
afr_selfheal_tie_breaker_inodelk (call_frame_t *frame, xlator_t *this,
                                  inode_t *inode, char *dom, off_t off,
                                  size_t size, unsigned char *locked_on)
{
	loc_t loc = {0,};
	struct gf_flock flock = {0, };
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int lock_count = 0;
	int eagain_count = 0;

	priv = this->private;
	local = frame->local;

	loc.inode = inode_ref (inode);
	gf_uuid_copy (loc.gfid, inode->gfid);

	flock.l_type = F_WRLCK;
	flock.l_start = off;
	flock.l_len = size;

	AFR_ONALL (frame, afr_selfheal_lock_cbk, inodelk, dom,
		   &loc, F_SETLK, &flock, NULL);

        afr_get_lock_and_eagain_counts (priv, local->replies, &lock_count,
                                        &eagain_count);

	if (lock_count > priv->child_count/2 && eagain_count) {
                afr_locked_fill (frame, this, locked_on);
                afr_selfheal_uninodelk (frame, this, inode, dom, off,
                                        size, locked_on);

                AFR_SEQ (frame, afr_selfheal_lock_cbk, inodelk, dom,
                         &loc, F_SETLKW, &flock, NULL);
        }

	loc_wipe (&loc);

	return afr_locked_fill (frame, this, locked_on);
}

int
afr_selfheal_uninodelk (call_frame_t *frame, xlator_t *this, inode_t *inode,
			char *dom, off_t off, size_t size,
			const unsigned char *locked_on)
{
	loc_t loc = {0,};
	struct gf_flock flock = {0, };


	loc.inode = inode_ref (inode);
	gf_uuid_copy (loc.gfid, inode->gfid);

	flock.l_type = F_UNLCK;
	flock.l_start = off;
	flock.l_len = size;

	AFR_ONLIST (locked_on, frame, afr_selfheal_lock_cbk, inodelk,
		    dom, &loc, F_SETLK, &flock, NULL);

	loc_wipe (&loc);

	return 0;
}


int
afr_selfheal_tryentrylk (call_frame_t *frame, xlator_t *this, inode_t *inode,
			 char *dom, const char *name, unsigned char *locked_on)
{
	loc_t loc = {0,};

	loc.inode = inode_ref (inode);
	gf_uuid_copy (loc.gfid, inode->gfid);

	AFR_ONALL (frame, afr_selfheal_lock_cbk, entrylk, dom,
		   &loc, name, ENTRYLK_LOCK_NB, ENTRYLK_WRLCK, NULL);

	loc_wipe (&loc);

	return afr_locked_fill (frame, this, locked_on);
}


int
afr_selfheal_entrylk (call_frame_t *frame, xlator_t *this, inode_t *inode,
		      char *dom, const char *name, unsigned char *locked_on)
{
	loc_t loc = {0,};
	afr_local_t *local = NULL;
	int i = 0;
	afr_private_t *priv = NULL;

	priv = this->private;
	local = frame->local;

	loc.inode = inode_ref (inode);
	gf_uuid_copy (loc.gfid, inode->gfid);

	AFR_ONALL (frame, afr_selfheal_lock_cbk, entrylk, dom, &loc,
		   name, ENTRYLK_LOCK_NB, ENTRYLK_WRLCK, NULL);

	for (i = 0; i < priv->child_count; i++) {
		if (local->replies[i].op_ret == -1 &&
		    local->replies[i].op_errno == EAGAIN) {
			afr_locked_fill (frame, this, locked_on);
			afr_selfheal_unentrylk (frame, this, inode, dom, name,
						locked_on, NULL);

			AFR_SEQ (frame, afr_selfheal_lock_cbk, entrylk, dom,
				 &loc, name, ENTRYLK_LOCK, ENTRYLK_WRLCK, NULL);
			break;
		}
	}

	loc_wipe (&loc);

	return afr_locked_fill (frame, this, locked_on);
}

int
afr_selfheal_tie_breaker_entrylk (call_frame_t *frame, xlator_t *this,
                                  inode_t *inode, char *dom, const char *name,
                                  unsigned char *locked_on)
{
	loc_t loc = {0,};
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int lock_count = 0;
	int eagain_count = 0;

	priv = this->private;
	local = frame->local;

	loc.inode = inode_ref (inode);
	gf_uuid_copy (loc.gfid, inode->gfid);

	AFR_ONALL (frame, afr_selfheal_lock_cbk, entrylk, dom, &loc,
		   name, ENTRYLK_LOCK_NB, ENTRYLK_WRLCK, NULL);

        afr_get_lock_and_eagain_counts (priv, local->replies, &lock_count,
                                        &eagain_count);

	if (lock_count > priv->child_count/2 && eagain_count) {
                afr_locked_fill (frame, this, locked_on);
                afr_selfheal_unentrylk (frame, this, inode, dom, name,
                                        locked_on, NULL);

                AFR_SEQ (frame, afr_selfheal_lock_cbk, entrylk, dom,
                         &loc, name, ENTRYLK_LOCK, ENTRYLK_WRLCK, NULL);
	}

	loc_wipe (&loc);

	return afr_locked_fill (frame, this, locked_on);
}


int
afr_selfheal_unentrylk (call_frame_t *frame, xlator_t *this, inode_t *inode,
			char *dom, const char *name, unsigned char *locked_on,
                        dict_t *xdata)
{
	loc_t loc = {0,};

	loc.inode = inode_ref (inode);
	gf_uuid_copy (loc.gfid, inode->gfid);

	AFR_ONLIST (locked_on, frame, afr_selfheal_lock_cbk, entrylk,
		    dom, &loc, name, ENTRYLK_UNLOCK, ENTRYLK_WRLCK, xdata);

	loc_wipe (&loc);

	return 0;
}


gf_boolean_t
afr_is_pending_set (xlator_t *this, dict_t *xdata, int type)
{
	int idx = -1;
	afr_private_t *priv = NULL;
	void *pending_raw = NULL;
	int *pending_int = NULL;
	int i = 0;

	priv = this->private;
	idx = afr_index_for_transaction_type (type);

	if (dict_get_ptr (xdata, AFR_DIRTY, &pending_raw) == 0) {
		if (pending_raw) {
			pending_int = pending_raw;

			if (ntoh32 (pending_int[idx]))
				return _gf_true;
		}
	}

	for (i = 0; i < priv->child_count; i++) {
		if (dict_get_ptr (xdata, priv->pending_key[i],
				  &pending_raw))
			continue;
		if (!pending_raw)
			continue;
		pending_int = pending_raw;

		if (ntoh32 (pending_int[idx]))
			return _gf_true;
	}

	return _gf_false;
}


gf_boolean_t
afr_is_data_set (xlator_t *this, dict_t *xdata)
{
	return afr_is_pending_set (this, xdata, AFR_DATA_TRANSACTION);
}

gf_boolean_t
afr_is_metadata_set (xlator_t *this, dict_t *xdata)
{
	return afr_is_pending_set (this, xdata, AFR_METADATA_TRANSACTION);
}

gf_boolean_t
afr_is_entry_set (xlator_t *this, dict_t *xdata)
{
	return afr_is_pending_set (this, xdata, AFR_ENTRY_TRANSACTION);
}

/*
 * This function inspects the looked up replies (in an unlocked manner)
 * and decides whether a locked verification and possible healing is
 * required or not. It updates the three booleans for each type
 * of healing. If the boolean flag gets set to FALSE, then we are sure
 * no healing is required. If the boolean flag gets set to TRUE then
 * we have to proceed with locked reinspection.
 */

int
afr_selfheal_unlocked_inspect (call_frame_t *frame, xlator_t *this,
			       uuid_t gfid, inode_t **link_inode,
			       gf_boolean_t *data_selfheal,
			       gf_boolean_t *metadata_selfheal,
			       gf_boolean_t *entry_selfheal)
{
	afr_private_t *priv = NULL;
        inode_t *inode = NULL;
	int i = 0;
	int valid_cnt = 0;
	struct iatt first = {0, };
        int first_idx = 0;
	struct afr_reply *replies = NULL;
	int ret = -1;

	priv = this->private;

        inode = afr_inode_find (this, gfid);
        if (!inode)
                goto out;

	replies = alloca0 (sizeof (*replies) * priv->child_count);

	ret = afr_selfheal_unlocked_discover (frame, inode, gfid, replies);
	if (ret)
                goto out;

	for (i = 0; i < priv->child_count; i++) {
		if (!replies[i].valid)
			continue;
		if (replies[i].op_ret == -1)
			continue;

                /* The data segment of the changelog can be non-zero to indicate
                 * the directory needs a full heal. So the check below ensures
                 * it's not a directory before setting the data_selfheal boolean.
                 */
		if (data_selfheal && !IA_ISDIR (replies[i].poststat.ia_type) &&
                    afr_is_data_set (this, replies[i].xdata))
			*data_selfheal = _gf_true;

		if (metadata_selfheal &&
                    afr_is_metadata_set (this, replies[i].xdata))
			*metadata_selfheal = _gf_true;

		if (entry_selfheal && afr_is_entry_set (this, replies[i].xdata))
			*entry_selfheal = _gf_true;

		valid_cnt++;
		if (valid_cnt == 1) {
			first = replies[i].poststat;
                        first_idx = i;
			continue;
		}

		if (!IA_EQUAL (first, replies[i].poststat, type)) {
		        gf_msg (this->name, GF_LOG_ERROR, 0,
                                AFR_MSG_SPLIT_BRAIN,
				"TYPE mismatch %d vs %d on %s for gfid:%s",
				(int) first.ia_type,
				(int) replies[i].poststat.ia_type,
				priv->children[i]->name,
				uuid_utoa (replies[i].poststat.ia_gfid));
                        gf_event (EVENT_AFR_SPLIT_BRAIN, "subvol=%s;"
                                "type=file;gfid=%s;"
                                "ia_type-%d=%s;ia_type-%d=%s",
                                this->name,
                                uuid_utoa (replies[i].poststat.ia_gfid),
                                first_idx,
                                gf_inode_type_to_str (first.ia_type), i,
                            gf_inode_type_to_str (replies[i].poststat.ia_type));
                        ret = -EIO;
                        goto out;
		}

		if (!IA_EQUAL (first, replies[i].poststat, uid)) {
		        gf_msg_debug (this->name, 0,
				      "UID mismatch "
                                      "%d vs %d on %s for gfid:%s",
				      (int) first.ia_uid,
				      (int) replies[i].poststat.ia_uid,
				      priv->children[i]->name,
				      uuid_utoa (replies[i].poststat.ia_gfid));

                        if (metadata_selfheal)
                                *metadata_selfheal = _gf_true;
		}

		if (!IA_EQUAL (first, replies[i].poststat, gid)) {
		        gf_msg_debug (this->name, 0,
				      "GID mismatch "
                                      "%d vs %d on %s for gfid:%s",
				      (int) first.ia_uid,
				      (int) replies[i].poststat.ia_uid,
				      priv->children[i]->name,
				      uuid_utoa (replies[i].poststat.ia_gfid));

                        if (metadata_selfheal)
                                *metadata_selfheal = _gf_true;
		}

		if (!IA_EQUAL (first, replies[i].poststat, prot)) {
		        gf_msg_debug (this->name, 0,
			              "MODE mismatch "
                                      "%d vs %d on %s for gfid:%s",
				      (int) st_mode_from_ia (first.ia_prot, 0),
				      (int) st_mode_from_ia
                                      (replies[i].poststat.ia_prot, 0),
				      priv->children[i]->name,
				      uuid_utoa (replies[i].poststat.ia_gfid));

                        if (metadata_selfheal)
                                *metadata_selfheal = _gf_true;
		}

		if (IA_ISREG(first.ia_type) &&
		    !IA_EQUAL (first, replies[i].poststat, size)) {
		        gf_msg_debug (this->name, 0,
                                      "SIZE mismatch "
                                      "%lld vs %lld on %s for gfid:%s",
                                      (long long) first.ia_size,
                                      (long long) replies[i].poststat.ia_size,
                                      priv->children[i]->name,
                                      uuid_utoa (replies[i].poststat.ia_gfid));

                        if (data_selfheal)
                                *data_selfheal = _gf_true;
		}
	}

	if (valid_cnt > 0 && link_inode) {
		*link_inode = inode_link (inode, NULL, NULL, &first);
                if (!*link_inode) {
                        ret = -EINVAL;
                        goto out;
                }
        } else if (valid_cnt < 2) {
                ret = afr_check_stale_error (replies, priv);
                goto out;
        }

        ret = 0;
out:
        if (inode)
                inode_unref (inode);
        if (replies)
                afr_replies_wipe (replies, priv->child_count);

	return ret;
}


inode_t *
afr_inode_find (xlator_t *this, uuid_t gfid)
{
	inode_table_t *table = NULL;
	inode_t *inode = NULL;

	table = this->itable;
	if (!table)
		return NULL;

	inode = inode_find (table, gfid);
	if (inode)
		return inode;

	inode = inode_new (table);
	if (!inode)
		return NULL;

	gf_uuid_copy (inode->gfid, gfid);

	return inode;
}


call_frame_t *
afr_frame_create (xlator_t *this)
{
	call_frame_t *frame    = NULL;
	afr_local_t  *local    = NULL;
	int           op_errno = 0;
	pid_t         pid      = GF_CLIENT_PID_SELF_HEALD;

	frame = create_frame (this, this->ctx->pool);
	if (!frame)
		return NULL;

	local = AFR_FRAME_INIT (frame, op_errno);
	if (!local) {
		STACK_DESTROY (frame->root);
		return NULL;
	}

	syncopctx_setfspid (&pid);

	frame->root->pid = pid;

	afr_set_lk_owner (frame, this, frame->root);

	return frame;
}

int
afr_selfheal_newentry_mark (call_frame_t *frame, xlator_t *this, inode_t *inode,
			    int source, struct afr_reply *replies,
			    unsigned char *sources, unsigned char *newentry)
{
	int ret = 0;
	int i = 0;
	afr_private_t *priv = NULL;
	dict_t *xattr = NULL;
	int **changelog = NULL;

	priv = this->private;

	gf_uuid_copy (inode->gfid, replies[source].poststat.ia_gfid);

	xattr = dict_new();
	if (!xattr)
		return -ENOMEM;

        changelog = afr_mark_pending_changelog (priv, newentry, xattr,
                                            replies[source].poststat.ia_type);

        if (!changelog) {
                ret = -ENOMEM;
                goto out;
        }

	for (i = 0; i < priv->child_count; i++) {
		if (!sources[i])
			continue;
		ret |= afr_selfheal_post_op (frame, this, inode, i, xattr,
                                             NULL);
	}
out:
        if (changelog)
                afr_matrix_cleanup (changelog, priv->child_count);
        if (xattr)
                dict_unref (xattr);
	return ret;
}

int
afr_selfheal_do (call_frame_t *frame, xlator_t *this, uuid_t gfid)
{
	int           ret               = -1;
        int           entry_ret         = 1;
        int           metadata_ret      = 1;
        int           data_ret          = 1;
        int           or_ret            = 0;
        inode_t      *inode             = NULL;
	gf_boolean_t  data_selfheal     = _gf_false;
	gf_boolean_t  metadata_selfheal = _gf_false;
	gf_boolean_t  entry_selfheal    = _gf_false;
        afr_private_t *priv            = NULL;
        gf_boolean_t dataheal_enabled   = _gf_false;

        priv = this->private;

        ret = gf_string2boolean (priv->data_self_heal, &dataheal_enabled);
        if (ret)
                goto out;

	ret = afr_selfheal_unlocked_inspect (frame, this, gfid, &inode,
					     &data_selfheal,
					     &metadata_selfheal,
					     &entry_selfheal);
	if (ret)
		goto out;

        if (!(data_selfheal || metadata_selfheal || entry_selfheal)) {
                ret = 2;
                goto out;
        }

	if (data_selfheal && dataheal_enabled)
                data_ret = afr_selfheal_data (frame, this, inode);

	if (metadata_selfheal && priv->metadata_self_heal)
                metadata_ret = afr_selfheal_metadata (frame, this, inode);

	if (entry_selfheal && priv->entry_self_heal)
                entry_ret = afr_selfheal_entry (frame, this, inode);

        or_ret = (data_ret | metadata_ret | entry_ret);

        if (data_ret == -EIO || metadata_ret == -EIO || entry_ret == -EIO)
                ret = -EIO;
        else if (data_ret == 1 && metadata_ret == 1 && entry_ret == 1)
                ret = 1;
        else if (or_ret < 0)
                ret = or_ret;
        else
                ret = 0;

out:
        if (inode)
                inode_unref (inode);
        return ret;
}
/*
 * This is the entry point for healing a given GFID. The return values for this
 * function are as follows:
 * '0' if the self-heal is successful
 * '1' if the afr-xattrs are non-zero (due to on-going IO) and no heal is needed
 * '2' if the afr-xattrs are all-zero and no heal is needed
 * $errno if the heal on the gfid failed.
 */

int
afr_selfheal (xlator_t *this, uuid_t gfid)
{
        int           ret   = -1;
	call_frame_t *frame = NULL;
        afr_local_t *local = NULL;

	frame = afr_frame_create (this);
	if (!frame)
		return ret;

        local = frame->local;
        local->xdata_req = dict_new();

        ret = afr_selfheal_do (frame, this, gfid);

	if (frame)
		AFR_STACK_DESTROY (frame);

	return ret;
}

afr_local_t*
__afr_dequeue_heals (afr_private_t *priv)
{
        afr_local_t *local = NULL;

        if (list_empty (&priv->heal_waiting))
                goto none;
        if ((priv->background_self_heal_count > 0) &&
            (priv->healers >= priv->background_self_heal_count))
                goto none;

        local = list_entry (priv->heal_waiting.next, afr_local_t, healer);
        priv->heal_waiters--;
        GF_ASSERT (priv->heal_waiters >= 0);
        list_del_init(&local->healer);
        list_add(&local->healer, &priv->healing);
        priv->healers++;
        return local;
none:
        gf_msg_debug (THIS->name, 0, "Nothing dequeued. "
                      "Num healers: %d, Num Waiters: %d",
                      priv->healers, priv->heal_waiters);
        return NULL;
}

int
afr_refresh_selfheal_wrap (void *opaque)
{
        call_frame_t *heal_frame = opaque;
        afr_local_t *local = heal_frame->local;
        int ret = 0;

        ret = afr_selfheal (heal_frame->this, local->refreshinode->gfid);
        return ret;
}

int
afr_refresh_heal_done (int ret, call_frame_t *frame, void *opaque)
{
        call_frame_t *heal_frame = opaque;
        xlator_t *this = heal_frame->this;
        afr_private_t *priv = this->private;
        afr_local_t *local = heal_frame->local;

        LOCK (&priv->lock);
        {
                list_del_init(&local->healer);
                priv->healers--;
                GF_ASSERT (priv->healers >= 0);
                local = __afr_dequeue_heals (priv);
        }
        UNLOCK (&priv->lock);

        if (heal_frame)
                AFR_STACK_DESTROY (heal_frame);

        if (local)
                afr_heal_synctask (this, local);
        return 0;

}

void
afr_heal_synctask (xlator_t *this, afr_local_t *local)
{
        int ret = 0;
        call_frame_t *heal_frame = NULL;

        heal_frame = local->heal_frame;
        ret = synctask_new (this->ctx->env, afr_refresh_selfheal_wrap,
                            afr_refresh_heal_done, heal_frame, heal_frame);
        if (ret < 0)
                /* Heal not launched. Will be queued when the next inode
                 * refresh happens and shd hasn't healed it yet. */
                afr_refresh_heal_done (ret, heal_frame, heal_frame);
}

gf_boolean_t
afr_throttled_selfheal (call_frame_t *frame, xlator_t *this)
{
        gf_boolean_t can_heal = _gf_true;
        afr_private_t *priv = this->private;
        afr_local_t *local = frame->local;

        LOCK (&priv->lock);
        {
                if ((priv->background_self_heal_count > 0) &&
                    (priv->heal_wait_qlen + priv->background_self_heal_count) >
                    (priv->heal_waiters + priv->healers)) {
                        list_add_tail(&local->healer, &priv->heal_waiting);
                        priv->heal_waiters++;
                        local = __afr_dequeue_heals (priv);
                } else {
                        can_heal = _gf_false;
                }
        }
        UNLOCK (&priv->lock);

        if (can_heal) {
                if (local)
                        afr_heal_synctask (this, local);
                else
                        gf_msg_debug (this->name, 0, "Max number of heals are "
                                      "pending, background self-heal rejected.");
        }

        return can_heal;
}

int
afr_choose_source_by_policy (afr_private_t *priv, unsigned char *sources,
                             afr_transaction_type type)
{
        int source = -1;
        int i      = 0;

        /* Give preference to local child to save on bandwidth */
        for (i = 0; i < priv->child_count; i++) {
                if (priv->local[i] && sources[i]) {
                        if ((type == AFR_DATA_TRANSACTION) &&
                            AFR_IS_ARBITER_BRICK (priv, i))
                                continue;

                        source = i;
                        goto out;
                }
        }

        for (i = 0; i < priv->child_count; i++) {
                if (sources[i]) {
                        source = i;
                        goto out;
                }
        }
out:
        return source;
}
