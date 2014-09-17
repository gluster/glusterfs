/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "afr.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"
#include "list.h"
#include "call-stub.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat-errno.h"
#include "compat.h"
#include "byte-order.h"
#include "statedump.h"
#include "inode.h"

#include "fd.h"

#include "afr-inode-read.h"
#include "afr-inode-write.h"
#include "afr-dir-read.h"
#include "afr-dir-write.h"
#include "afr-transaction.h"
#include "afr-self-heal.h"
#include "afr-self-heald.h"
#include "afr-messages.h"

call_frame_t *
afr_copy_frame (call_frame_t *base)
{
	afr_local_t *local = NULL;
	call_frame_t *frame = NULL;
	int op_errno = 0;

	frame = copy_frame (base);
	if (!frame)
		return NULL;
	local = AFR_FRAME_INIT (frame, op_errno);
	if (!local) {
		AFR_STACK_DESTROY (frame);
		return NULL;
	}

	return frame;
}

/*
 * INODE CTX 64-bit VALUE FORMAT FOR SMALL (<= 16) SUBVOL COUNTS:
 *
 * |<----------   64bit   ------------>|
 *  63           32 31    16 15       0
 * |   EVENT_GEN   |  DATA  | METADATA |
 *
 *
 *  METADATA (bit-0 .. bit-15): bitmap representing subvolumes from which
 *                              metadata can be attempted to be read.
 *
 *                              bit-0 => priv->subvolumes[0]
 *                              bit-1 => priv->subvolumes[1]
 *                              ... etc. till bit-15
 *
 *  DATA (bit-16 .. bit-31): bitmap representing subvolumes from which data
 *                           can be attempted to be read.
 *
 *                           bit-16 => priv->subvolumes[0]
 *                           bit-17 => priv->subvolumes[1]
 *                           ... etc. till bit-31
 *
 *  EVENT_GEN (bit-32 .. bit-63): event generation (i.e priv->event_generation)
 *                                when DATA and METADATA was last updated.
 *
 *                                If EVENT_GEN is < priv->event_generation,
 *                                or is 0, it means afr_inode_refresh() needs
 *                                to be called to recalculate the bitmaps.
 */

int
__afr_inode_read_subvol_get_small (inode_t *inode, xlator_t *this,
				   unsigned char *data, unsigned char *metadata,
				   int *event_p)
{
	afr_private_t *priv = NULL;
	int ret = -1;
	uint16_t datamap = 0;
	uint16_t metadatamap = 0;
	uint32_t event = 0;
	uint64_t val = 0;
	int i = 0;

	priv = this->private;

	ret = __inode_ctx_get (inode, this, &val);
	if (ret < 0)
		return ret;

	metadatamap = (val & 0x000000000000ffff);
	datamap =     (val & 0x00000000ffff0000) >> 16;
	event =       (val & 0xffffffff00000000) >> 32;

	for (i = 0; i < priv->child_count; i++) {
		if (metadata)
			metadata[i] = (metadatamap >> i) & 1;
		if (data)
			data[i] = (datamap >> i) & 1;
	}

	if (event_p)
		*event_p = event;
	return ret;
}


int
__afr_inode_read_subvol_set_small (inode_t *inode, xlator_t *this,
				   unsigned char *data, unsigned char *metadata,
				   int event)
{
	afr_private_t *priv = NULL;
	uint16_t datamap = 0;
	uint16_t metadatamap = 0;
	uint64_t val = 0;
	int i = 0;

	priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
		if (data[i])
			datamap |= (1 << i);
		if (metadata[i])
			metadatamap |= (1 << i);
	}

	val = ((uint64_t) metadatamap) |
		(((uint64_t) datamap) << 16) |
		(((uint64_t) event) << 32);

	return __inode_ctx_set (inode, this, &val);
}


int
__afr_inode_read_subvol_reset_small (inode_t *inode, xlator_t *this)
{
	int ret = -1;
	uint16_t datamap = 0;
	uint16_t metadatamap = 0;
	uint32_t event = 0;
	uint64_t val = 0;

	ret = __inode_ctx_get (inode, this, &val);
	(void) ret;

	metadatamap = (val & 0x000000000000ffff) >> 0;
	datamap =     (val & 0x00000000ffff0000) >> 16;
	event = 0;

	val = ((uint64_t) metadatamap) |
		(((uint64_t) datamap) << 16) |
		(((uint64_t) event) << 32);

	return __inode_ctx_set (inode, this, &val);
}


int
__afr_inode_read_subvol_get (inode_t *inode, xlator_t *this,
			     unsigned char *data, unsigned char *metadata,
			     int *event_p)
{
	afr_private_t *priv = NULL;
	int ret = -1;

	priv = this->private;

	if (priv->child_count <= 16)
		ret = __afr_inode_read_subvol_get_small (inode, this, data,
							 metadata, event_p);
	else
		/* TBD: allocate structure with array and read from it */
		ret = -1;

	return ret;
}


int
__afr_inode_read_subvol_set (inode_t *inode, xlator_t *this, unsigned char *data,
			     unsigned char *metadata, int event)
{
	afr_private_t *priv = NULL;
	int ret = -1;

	priv = this->private;

	if (priv->child_count <= 16)
		ret = __afr_inode_read_subvol_set_small (inode, this, data,
							 metadata, event);
	else
		ret = -1;

	return ret;
}


int
__afr_inode_read_subvol_reset (inode_t *inode, xlator_t *this)
{
	afr_private_t *priv = NULL;
	int ret = -1;

	priv = this->private;

	if (priv->child_count <= 16)
		ret = __afr_inode_read_subvol_reset_small (inode, this);
	else
		ret = -1;

	return ret;
}


int
afr_inode_read_subvol_get (inode_t *inode, xlator_t *this, unsigned char *data,
			   unsigned char *metadata, int *event_p)
{
	int ret = -1;

	LOCK(&inode->lock);
	{
		ret = __afr_inode_read_subvol_get (inode, this, data,
						   metadata, event_p);
	}
	UNLOCK(&inode->lock);

	return ret;
}


int
afr_inode_read_subvol_set (inode_t *inode, xlator_t *this, unsigned char *data,
			   unsigned char *metadata, int event)
{
	int ret = -1;

	LOCK(&inode->lock);
	{
		ret = __afr_inode_read_subvol_set (inode, this, data, metadata,
						   event);
	}
	UNLOCK(&inode->lock);

	return ret;
}


int
afr_inode_read_subvol_reset (inode_t *inode, xlator_t *this)
{
	int ret = -1;

	LOCK(&inode->lock);
	{
		ret = __afr_inode_read_subvol_reset (inode, this);
	}
	UNLOCK(&inode->lock);

	return ret;
}


int
afr_accused_fill (xlator_t *this, dict_t *xdata, unsigned char *accused,
		  afr_transaction_type type)
{
	afr_private_t *priv = NULL;
	int i = 0;
	int idx = afr_index_for_transaction_type (type);
	void *pending_raw = NULL;
	int pending[3];
	int ret = 0;

	priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
		ret = dict_get_ptr (xdata, priv->pending_key[i],
				    &pending_raw);
		if (ret) /* no pending flags */
			continue;
		memcpy (pending, pending_raw, sizeof(pending));

		if (ntoh32 (pending[idx]))
			accused[i] = 1;
	}

	return 0;
}


int
afr_accuse_smallfiles (xlator_t *this, struct afr_reply *replies,
		       unsigned char *data_accused)
{
	int i = 0;
	afr_private_t *priv = NULL;
	uint64_t maxsize = 0;

	priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
		if (data_accused[i])
			continue;
		if (replies[i].poststat.ia_size > maxsize)
			maxsize = replies[i].poststat.ia_size;
	}

	for (i = 0; i < priv->child_count; i++) {
		if (data_accused[i])
			continue;
		if (replies[i].poststat.ia_size < maxsize)
			data_accused[i] = 1;
	}

	return 0;
}


int
afr_replies_interpret (call_frame_t *frame, xlator_t *this, inode_t *inode)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	struct afr_reply *replies = NULL;
	int event_generation = 0;
	int i = 0;
	unsigned char *data_accused = NULL;
	unsigned char *metadata_accused = NULL;
	unsigned char *data_readable = NULL;
	unsigned char *metadata_readable = NULL;
	int ret = 0;

	local = frame->local;
	priv = this->private;
	replies = local->replies;
	event_generation = local->event_generation;

	data_accused = alloca0 (priv->child_count);
	data_readable = alloca0 (priv->child_count);
	metadata_accused = alloca0 (priv->child_count);
	metadata_readable = alloca0 (priv->child_count);

	for (i = 0; i < priv->child_count; i++) {
		data_readable[i] = 1;
		metadata_readable[i] = 1;
	}

	for (i = 0; i < priv->child_count; i++) {
		if (!replies[i].valid) {
			data_readable[i] = 0;
			metadata_readable[i] = 0;
			continue;
		}

		if (replies[i].op_ret == -1) {
			data_readable[i] = 0;
			metadata_readable[i] = 0;
			continue;
		}

		afr_accused_fill (this, replies[i].xdata, data_accused,
				  (inode->ia_type == IA_IFDIR) ?
				   AFR_ENTRY_TRANSACTION : AFR_DATA_TRANSACTION);

		afr_accused_fill (this, replies[i].xdata,
				  metadata_accused, AFR_METADATA_TRANSACTION);

	}

	if (inode->ia_type != IA_IFDIR)
		afr_accuse_smallfiles (this, replies, data_accused);

	for (i = 0; i < priv->child_count; i++) {
		if (data_accused[i]) {
			data_readable[i] = 0;
			ret = 1;
		}
		if (metadata_accused[i]) {
			metadata_readable[i] = 0;
			ret = 1;
		}
	}

	afr_inode_read_subvol_set (inode, this, data_readable,
				   metadata_readable, event_generation);
	return ret;
}



int
afr_refresh_selfheal_done (int ret, call_frame_t *heal, void *opaque)
{
	if (heal)
		STACK_DESTROY (heal->root);
	return 0;
}

int
afr_inode_refresh_err (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;
	int err = 0;

	local = frame->local;
	priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
		if (local->replies[i].valid && !local->replies[i].op_ret) {
			err = 0;
			goto ret;
		}
	}

	err = afr_final_errno (local, priv);
ret:
	return -err;
}


int
afr_refresh_selfheal_wrap (void *opaque)
{
	call_frame_t *frame = opaque;
	afr_local_t *local = NULL;
	xlator_t *this = NULL;
	int err = 0;

	local = frame->local;
	this = frame->this;

	afr_selfheal (frame->this, local->refreshinode->gfid);

	afr_selfheal_unlocked_discover (frame, local->refreshinode,
					local->refreshinode->gfid,
					local->replies);

	afr_replies_interpret (frame, this, local->refreshinode);

	err = afr_inode_refresh_err (frame, this);

        afr_local_replies_wipe (local, this->private);

	local->refreshfn (frame, this, err);

	return 0;
}


gf_boolean_t
afr_selfheal_enabled (xlator_t *this)
{
	afr_private_t *priv = NULL;
	gf_boolean_t data = _gf_false;

	priv = this->private;

	gf_string2boolean (priv->data_self_heal, &data);

	return data || priv->metadata_self_heal || priv->entry_self_heal;
}



int
afr_inode_refresh_done (call_frame_t *frame, xlator_t *this)
{
	call_frame_t *heal = NULL;
	afr_local_t *local = NULL;
	int ret = 0;
	int err = 0;

	local = frame->local;

	ret = afr_replies_interpret (frame, this, local->refreshinode);

	err = afr_inode_refresh_err (frame, this);

        afr_local_replies_wipe (local, this->private);

	if (ret && afr_selfheal_enabled (this)) {
		heal = copy_frame (frame);
		if (heal)
			heal->root->pid = GF_CLIENT_PID_AFR_SELF_HEALD;
		ret = synctask_new (this->ctx->env, afr_refresh_selfheal_wrap,
				    afr_refresh_selfheal_done, heal, frame);
		if (ret)
			goto refresh_done;
	} else {
	refresh_done:
		local->refreshfn (frame, this, err);
	}

	return 0;
}


int
afr_inode_refresh_subvol_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			      int op_ret, int op_errno, inode_t *inode,
			      struct iatt *buf, dict_t *xdata, struct iatt *par)
{
	afr_local_t *local = NULL;
	int call_child = (long) cookie;
	int call_count = 0;

	local = frame->local;

	local->replies[call_child].valid = 1;
	local->replies[call_child].op_ret = op_ret;
	local->replies[call_child].op_errno = op_errno;
	if (op_ret != -1) {
		local->replies[call_child].poststat = *buf;
		local->replies[call_child].postparent = *par;
		local->replies[call_child].xdata = dict_ref (xdata);
	}

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		afr_inode_refresh_done (frame, this);

	return 0;
}


int
afr_inode_refresh_subvol (call_frame_t *frame, xlator_t *this, int i,
			  inode_t *inode, dict_t *xdata)
{
	loc_t loc = {0, };
	afr_private_t *priv = NULL;

	priv = this->private;

	loc.inode = inode;
	uuid_copy (loc.gfid, inode->gfid);

	STACK_WIND_COOKIE (frame, afr_inode_refresh_subvol_cbk,
			   (void *) (long) i, priv->children[i],
			   priv->children[i]->fops->lookup, &loc, xdata);
	return 0;
}


int
afr_inode_refresh_do (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int call_count = 0;
	int i = 0;
	dict_t *xdata = NULL;

	priv = this->private;
	local = frame->local;

        afr_local_replies_wipe (local, priv);

	xdata = dict_new ();
	if (!xdata) {
		afr_inode_refresh_done (frame, this);
		return 0;
	}

	if (afr_xattr_req_prepare (this, xdata) != 0) {
		dict_unref (xdata);
		afr_inode_refresh_done (frame, this);
		return 0;
	}

	local->call_count = AFR_COUNT (local->child_up, priv->child_count);

	call_count = local->call_count;
	for (i = 0; i < priv->child_count; i++) {
		if (!local->child_up[i])
			continue;

		afr_inode_refresh_subvol (frame, this, i, local->refreshinode,
					  xdata);

		if (!--call_count)
			break;
	}

	dict_unref (xdata);

	return 0;
}


int
afr_inode_refresh (call_frame_t *frame, xlator_t *this, inode_t *inode,
		   afr_inode_refresh_cbk_t refreshfn)
{
	afr_local_t *local = NULL;

	local = frame->local;

	local->refreshfn = refreshfn;

	if (local->refreshinode) {
		inode_unref (local->refreshinode);
		local->refreshinode = NULL;
	}

	local->refreshinode = inode_ref (inode);

	afr_inode_refresh_do (frame, this);

	return 0;
}


int
afr_xattr_req_prepare (xlator_t *this, dict_t *xattr_req)
{
        int             i           = 0;
        afr_private_t   *priv       = NULL;
        int             ret         = 0;

        priv   = this->private;

        for (i = 0; i < priv->child_count; i++) {
                ret = dict_set_uint64 (xattr_req, priv->pending_key[i],
                                       AFR_NUM_CHANGE_LOGS * sizeof(int));
                if (ret < 0)
                        gf_log (this->name, GF_LOG_WARNING,
                                "Unable to set dict value for %s",
                                priv->pending_key[i]);
                /* 3 = data+metadata+entry */
        }
        ret = dict_set_uint64 (xattr_req, AFR_DIRTY,
			       AFR_NUM_CHANGE_LOGS * sizeof(int));
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG, "failed to set dirty "
                        "query flag");
        }

        ret = dict_set_int32 (xattr_req, "list-xattr", 1);
        if (ret) {
                gf_log (this->name, GF_LOG_DEBUG,
                        "Unable to set list-xattr in dict ");
        }

	return ret;
}

int
afr_lookup_xattr_req_prepare (afr_local_t *local, xlator_t *this,
                              dict_t *xattr_req, loc_t *loc)
{
        int     ret = -ENOMEM;

        if (!local->xattr_req)
                local->xattr_req = dict_new ();

        if (!local->xattr_req)
                goto out;

        if (xattr_req != local->xattr_req)
                dict_copy (xattr_req, local->xattr_req);

        ret = afr_xattr_req_prepare (this, local->xattr_req);
	if (ret < 0) {
		gf_log (this->name, GF_LOG_WARNING,
			"%s: Unable to prepare xattr_req", loc->path);
	}

        ret = dict_set_uint64 (local->xattr_req, GLUSTERFS_INODELK_COUNT, 0);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: Unable to set dict value for %s",
                        loc->path, GLUSTERFS_INODELK_COUNT);
        }
        ret = dict_set_uint64 (local->xattr_req, GLUSTERFS_ENTRYLK_COUNT, 0);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: Unable to set dict value for %s",
                        loc->path, GLUSTERFS_ENTRYLK_COUNT);
        }

        ret = dict_set_uint32 (local->xattr_req, GLUSTERFS_PARENT_ENTRYLK, 0);
        if (ret < 0) {
                gf_log (this->name, GF_LOG_WARNING,
                        "%s: Unable to set dict value for %s",
                        loc->path, GLUSTERFS_PARENT_ENTRYLK);
        }

        ret = 0;
out:
        return ret;
}


int
afr_hash_child (inode_t *inode, int32_t child_count, int hashmode)
{
        uuid_t gfid_copy = {0,};
        pid_t pid;

        if (!hashmode) {
                return -1;
        }

        if (inode) {
               uuid_copy (gfid_copy, inode->gfid);
        }

        if (hashmode > 1) {
                /*
                 * Why getpid?  Because it's one of the cheapest calls
                 * available - faster than gethostname etc. - and returns a
                 * constant-length value that's sure to be shorter than a UUID.
                 * It's still very unlikely to be the same across clients, so
                 * it still provides good mixing.  We're not trying for
                 * perfection here.  All we need is a low probability that
                 * multiple clients won't converge on the same subvolume.
                 */
                pid = getpid();
                memcpy (gfid_copy, &pid, sizeof(pid));
        }

        return SuperFastHash((char *)gfid_copy,
                             sizeof(gfid_copy)) % child_count;
}


int
afr_read_subvol_select_by_policy (inode_t *inode, xlator_t *this,
				  unsigned char *readable)
{
	afr_private_t *priv = NULL;
	int read_subvol = -1;
	int i = 0;

	priv = this->private;

	/* first preference - explicitly specified or local subvolume */
	if (priv->read_child >= 0 && readable[priv->read_child])
		return priv->read_child;

	/* second preference - use hashed mode */
	read_subvol = afr_hash_child (inode, priv->child_count,
				      priv->hash_mode);
	if (read_subvol >= 0 && readable[read_subvol])
		return read_subvol;

	for (i = 0; i < priv->child_count; i++) {
		if (readable[i])
			return i;
	}

	/* no readable subvolumes, either split brain or all subvols down */

	return -1;
}


int
afr_inode_read_subvol_type_get (inode_t *inode, xlator_t *this,
				unsigned char *readable, int *event_p,
				int type)
{
	int ret = -1;

	if (type == AFR_METADATA_TRANSACTION)
		ret = afr_inode_read_subvol_get (inode, this, 0, readable,
						 event_p);
	else
		ret = afr_inode_read_subvol_get (inode, this, readable, 0,
						 event_p);
	return ret;
}


int
afr_read_subvol_get (inode_t *inode, xlator_t *this, int *subvol_p,
		     int *event_p, afr_transaction_type type)
{
	afr_private_t *priv = NULL;
	unsigned char *data_readable = NULL;
	unsigned char *metadata_readable = NULL;
	unsigned char *readable = NULL;
	unsigned char *intersection = NULL;
	int subvol = -1;
	int event = 0;

	priv = this->private;

	readable = alloca0 (priv->child_count);
	data_readable = alloca0 (priv->child_count);
	metadata_readable = alloca0 (priv->child_count);
	intersection = alloca0 (priv->child_count);

	afr_inode_read_subvol_type_get (inode, this, readable, &event, type);

	afr_inode_read_subvol_get (inode, this, data_readable, metadata_readable,
				   &event);

	AFR_INTERSECT (intersection, data_readable, metadata_readable,
		       priv->child_count);

	if (AFR_COUNT (intersection, priv->child_count) > 0)
		subvol = afr_read_subvol_select_by_policy (inode, this,
							   intersection);
	else
		subvol = afr_read_subvol_select_by_policy (inode, this,
							   readable);
	if (subvol_p)
		*subvol_p = subvol;
	if (event_p)
		*event_p = event;
	return subvol;
}


void
afr_local_transaction_cleanup (afr_local_t *local, xlator_t *this)
{
        afr_private_t *priv    = NULL;
        int           i        = 0;

        priv = this->private;

        afr_matrix_cleanup (local->pending, priv->child_count);

        GF_FREE (local->internal_lock.locked_nodes);

        for (i = 0; local->internal_lock.inodelk[i].domain; i++) {
                GF_FREE (local->internal_lock.inodelk[i].locked_nodes);
        }

        GF_FREE (local->internal_lock.lower_locked_nodes);

        afr_entry_lockee_cleanup (&local->internal_lock);

        GF_FREE (local->transaction.pre_op);
        GF_FREE (local->transaction.eager_lock);
        GF_FREE (local->transaction.fop_subvols);
        GF_FREE (local->transaction.failed_subvols);

        GF_FREE (local->transaction.basename);
        GF_FREE (local->transaction.new_basename);

        loc_wipe (&local->transaction.parent_loc);
        loc_wipe (&local->transaction.new_parent_loc);

}


void
afr_replies_wipe (struct afr_reply *replies, int count)
{
        int i = 0;

        for (i = 0; i < count; i++) {
                if (replies[i].xdata) {
                        dict_unref (replies[i].xdata);
                        replies[i].xdata = NULL;
                }
        }
}

void
afr_local_replies_wipe (afr_local_t *local, afr_private_t *priv)
{

	if (!local->replies)
		return;

        afr_replies_wipe (local->replies, priv->child_count);

	memset (local->replies, 0, sizeof(*local->replies) * priv->child_count);
}

void
afr_remove_eager_lock_stub (afr_local_t *local)
{
        LOCK (&local->fd->lock);
        {
                list_del_init (&local->transaction.eager_locked);
        }
        UNLOCK (&local->fd->lock);
}

void
afr_local_cleanup (afr_local_t *local, xlator_t *this)
{
        afr_private_t * priv = NULL;

        if (!local)
                return;

	syncbarrier_destroy (&local->barrier);

        if (local->transaction.eager_lock_on &&
            !list_empty (&local->transaction.eager_locked))
                afr_remove_eager_lock_stub (local);

        afr_local_transaction_cleanup (local, this);

        priv = this->private;

        loc_wipe (&local->loc);
        loc_wipe (&local->newloc);

        if (local->fd)
                fd_unref (local->fd);

        if (local->xattr_req)
                dict_unref (local->xattr_req);

        if (local->dict)
                dict_unref (local->dict);

        afr_local_replies_wipe (local, priv);
	GF_FREE(local->replies);

        GF_FREE (local->child_up);

        GF_FREE (local->read_attempted);

        GF_FREE (local->readable);

	if (local->inode)
		inode_unref (local->inode);

	if (local->parent)
		inode_unref (local->parent);

	if (local->parent2)
		inode_unref (local->parent2);

	if (local->refreshinode)
		inode_unref (local->refreshinode);

        { /* getxattr */
                GF_FREE (local->cont.getxattr.name);
        }

        { /* lk */
                GF_FREE (local->cont.lk.locked_nodes);
        }

        { /* create */
                if (local->cont.create.fd)
                        fd_unref (local->cont.create.fd);
                if (local->cont.create.params)
                        dict_unref (local->cont.create.params);
        }

        { /* mknod */
                if (local->cont.mknod.params)
                        dict_unref (local->cont.mknod.params);
        }

        { /* mkdir */
                if (local->cont.mkdir.params)
                        dict_unref (local->cont.mkdir.params);
        }

        { /* symlink */
                if (local->cont.symlink.params)
                        dict_unref (local->cont.symlink.params);
        }

        { /* writev */
                GF_FREE (local->cont.writev.vector);
                if (local->cont.writev.iobref)
                        iobref_unref (local->cont.writev.iobref);
        }

        { /* setxattr */
                if (local->cont.setxattr.dict)
                        dict_unref (local->cont.setxattr.dict);
        }

        { /* fsetxattr */
                if (local->cont.fsetxattr.dict)
                        dict_unref (local->cont.fsetxattr.dict);
        }

        { /* removexattr */
                GF_FREE (local->cont.removexattr.name);
        }
        { /* xattrop */
                if (local->cont.xattrop.xattr)
                        dict_unref (local->cont.xattrop.xattr);
        }
        { /* fxattrop */
                if (local->cont.fxattrop.xattr)
                        dict_unref (local->cont.fxattrop.xattr);
        }
        { /* symlink */
                GF_FREE (local->cont.symlink.linkpath);
        }

        { /* opendir */
                GF_FREE (local->cont.opendir.checksum);
        }

        { /* readdirp */
                if (local->cont.readdir.dict)
                        dict_unref (local->cont.readdir.dict);
        }

        if (local->xdata_req)
                dict_unref (local->xdata_req);

        if (local->xdata_rsp)
                dict_unref (local->xdata_rsp);
}


int
afr_frame_return (call_frame_t *frame)
{
        afr_local_t *local = NULL;
        int          call_count = 0;

        local = frame->local;

        LOCK (&frame->lock);
        {
                call_count = --local->call_count;
        }
        UNLOCK (&frame->lock);

        return call_count;
}


gf_boolean_t
afr_is_entry_possibly_under_txn (afr_local_t *local, xlator_t *this)
{
	int i = 0;
	int tmp = 0;
	afr_private_t *priv = NULL;

	priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
		if (!local->replies[i].xdata)
			continue;
		if (dict_get_int32 (local->replies[i].xdata,
				    GLUSTERFS_PARENT_ENTRYLK,
				    &tmp) == 0)
			if (tmp)
				return _gf_true;
	}

	return _gf_false;
}


/*
 * Quota size xattrs are not maintained by afr. There is a
 * possibility that they differ even when both the directory changelog xattrs
 * suggest everything is fine. So if there is at least one 'source' check among
 * the sources which has the maximum quota size. Otherwise check among all the
 * available ones for maximum quota size. This way if there is a source and
 * stale copies it always votes for the 'source'.
 * */

static void
afr_handle_quota_size (call_frame_t *frame, xlator_t *this)
{
	unsigned char *readable = NULL;
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	struct afr_reply *replies = NULL;
	int i = 0;
	uint64_t size = 0;
	uint64_t max_size = 0;
	int readable_cnt = 0;

	local = frame->local;
	priv = this->private;
	replies = local->replies;

	readable = alloca0 (priv->child_count);

	afr_inode_read_subvol_get (local->inode, this, readable, 0, 0);

	readable_cnt = AFR_COUNT (readable, priv->child_count);

	for (i = 0; i < priv->child_count; i++) {
		if (!replies[i].valid || replies[i].op_ret == -1)
			continue;
		if (readable_cnt && !readable[i])
			continue;
		if (!replies[i].xdata)
			continue;
		if (dict_get_uint64 (replies[i].xdata, QUOTA_SIZE_KEY, &size))
			continue;
		if (size > max_size)
			max_size = size;
	}

	if (!max_size)
		return;

	for (i = 0; i < priv->child_count; i++) {
		if (!replies[i].valid || replies[i].op_ret == -1)
			continue;
		if (readable_cnt && !readable[i])
			continue;
		if (!replies[i].xdata)
			continue;
		if (dict_set_uint64 (replies[i].xdata, QUOTA_SIZE_KEY, max_size))
			continue;
	}
}

static char *afr_ignore_xattrs[] = {
        GLUSTERFS_OPEN_FD_COUNT,
        GLUSTERFS_PARENT_ENTRYLK,
        GLUSTERFS_ENTRYLK_COUNT,
        GLUSTERFS_INODELK_COUNT,
        GF_SELINUX_XATTR_KEY,
        QUOTA_SIZE_KEY,
        NULL
};

static  gf_boolean_t
afr_lookup_xattr_ignorable (char *key)
{
        int i = 0;

        if (!strncmp (key, AFR_XATTR_PREFIX, strlen(AFR_XATTR_PREFIX)))
                return _gf_true;
        for (i = 0; afr_ignore_xattrs[i]; i++) {
                if (!strcmp (key, afr_ignore_xattrs[i]))
                       return _gf_true;
        }
        return _gf_false;
}

int
xattr_is_equal (dict_t *this, char *key1, data_t *value1, void *data)
{
        dict_t *xattr2 = (dict_t *)data;
        data_t *value2 = NULL;

        if (afr_lookup_xattr_ignorable (key1))
                return 0;

        value2 = dict_get (xattr2, key1);
        if (!value2)
                return -1;

        if (value1->len != value2->len)
                return -1;
        if(memcmp(value1->data, value2->data, value1->len))
                return -1;
        else
                return 0;

}

/* To conclude that both dicts are equal, we need to check if
 * 1) For every key-val pair in dict1, a match is present in dict2
 * 2) For every key-val pair in dict2, a match is present in dict1
 * We need to do both because ignoring glusterfs' internal xattrs
 * happens only in xattr_is_equal().
 */
gf_boolean_t
afr_xattrs_are_equal (dict_t *dict1, dict_t *dict2)
{
        int ret = 0;

        ret = dict_foreach (dict1, xattr_is_equal, dict2);
        if (ret == -1)
                return _gf_false;

        ret = dict_foreach (dict2, xattr_is_equal, dict1);
        if (ret == -1)
                 return _gf_false;

        return _gf_true;
}

static void
afr_lookup_done (call_frame_t *frame, xlator_t *this)
{
        afr_private_t       *priv  = NULL;
        afr_local_t         *local = NULL;
	int                 i = -1;
	int                 op_errno = 0;
	int                 read_subvol = 0;
	unsigned char      *readable = NULL;
	int                 event = 0;
	struct afr_reply   *replies = NULL;
	uuid_t              read_gfid = {0, };
	gf_boolean_t        locked_entry = _gf_false;
	gf_boolean_t        can_interpret = _gf_true;

        priv  = this->private;
        local = frame->local;
	replies = local->replies;

	locked_entry = afr_is_entry_possibly_under_txn (local, this);

	readable = alloca0 (priv->child_count);

	afr_inode_read_subvol_get (local->loc.parent, this, readable,
				   NULL, &event);

	/* First, check if we have a gfid-change from somewhere,
	   If so, propagate that so that a fresh lookup can be
	   issued
	*/
        if (local->cont.lookup.needs_fresh_lookup) {
                local->op_ret = -1;
                local->op_errno = ESTALE;
                goto unwind;
        }

	op_errno = afr_final_errno (frame->local, this->private);
	local->op_errno = op_errno;

	read_subvol = -1;
	for (i = 0; i < priv->child_count; i++) {
		if (!replies[i].valid)
			continue;

		if (locked_entry && replies[i].op_ret == -1 &&
		    replies[i].op_errno == ENOENT) {
			/* Second, check entry is still
			   "underway" in creation */
			local->op_ret = -1;
			local->op_errno = ENOENT;
			read_subvol = i;
			goto unwind;
		}

		if (replies[i].op_ret == -1)
			continue;

		if (read_subvol == -1 || !readable[read_subvol]) {
			read_subvol = i;
			uuid_copy (read_gfid, replies[i].poststat.ia_gfid);
			local->op_ret = 0;
		}
	}

	if (read_subvol == -1)
		goto unwind;
	/* We now have a read_subvol, which is readable[] (if there
	   were any). Next we look for GFID mismatches. We don't
	   consider a GFID mismatch as an error if read_subvol is
	   readable[] but the mismatching GFID subvol is not.
	*/
	for (i = 0; i < priv->child_count; i++) {
		if (!replies[i].valid || replies[i].op_ret == -1) {
			if (priv->child_up[i])
				can_interpret = _gf_false;
			continue;
		}

		if (!uuid_compare (replies[i].poststat.ia_gfid, read_gfid))
                        continue;

		can_interpret = _gf_false;

		if (locked_entry)
			continue;

		/* Now GFIDs mismatch. It's OK as long as this subvol
		   is not readable[] but read_subvol is */
		if (readable[read_subvol] && !readable[i])
			continue;

		/* LOG ERROR */
		local->op_ret = -1;
		local->op_errno = EIO;
		goto unwind;
	}

	/* Forth, for the finalized GFID, pick the best subvolume
	   to return stats from.
	*/
	if (can_interpret) {
		/* It is safe to call afr_replies_interpret() because we have
		   a response from all the UP subvolumes and all of them resolved
		   to the same GFID
		*/
		if (afr_replies_interpret (frame, this, local->inode)) {
			read_subvol = afr_data_subvol_get (local->inode, this,
							   0, 0);
			afr_inode_read_subvol_reset (local->inode, this);
			goto cant_interpret;
		} else {
			read_subvol = afr_data_subvol_get (local->inode, this,
							   0, 0);
		}
	} else {
	cant_interpret:
		if (read_subvol == -1)
			dict_del (replies[0].xdata, GF_CONTENT_KEY);
		else
			dict_del (replies[read_subvol].xdata, GF_CONTENT_KEY);
	}

	afr_handle_quota_size (frame, this);

unwind:
	if (read_subvol == -1)
		read_subvol = 0;

	AFR_STACK_UNWIND (lookup, frame, local->op_ret, local->op_errno,
			  local->inode, &local->replies[read_subvol].poststat,
			  local->replies[read_subvol].xdata,
			  &local->replies[read_subvol].postparent);
}

/*
 * During a lookup, some errors are more "important" than
 * others in that they must be given higher priority while
 * returning to the user.
 *
 * The hierarchy is ENODATA > ENOENT > ESTALE > others
 */

int
afr_higher_errno (int32_t old_errno, int32_t new_errno)
{
	if (old_errno == ENODATA || new_errno == ENODATA)
		return ENODATA;
        if (old_errno == ENOENT || new_errno == ENOENT)
                return ENOENT;
	if (old_errno == ESTALE || new_errno == ESTALE)
		return ESTALE;

	return new_errno;
}


int
afr_final_errno (afr_local_t *local, afr_private_t *priv)
{
	int i = 0;
	int op_errno = 0;
	int tmp_errno = 0;

	for (i = 0; i < priv->child_count; i++) {
		if (!local->replies[i].valid)
			continue;
		if (local->replies[i].op_ret == 0)
			continue;
		tmp_errno = local->replies[i].op_errno;
		op_errno = afr_higher_errno (op_errno, tmp_errno);
	}

	return op_errno;
}

static int
get_pathinfo_host (char *pathinfo, char *hostname, size_t size)
{
        char    *start = NULL;
        char    *end = NULL;
        int     ret  = -1;
        int     i    = 0;

        if (!pathinfo)
                goto out;

        start = strchr (pathinfo, ':');
        if (!start)
                goto out;
        end = strrchr (pathinfo, ':');
        if (start == end)
                goto out;

        memset (hostname, 0, size);
        i = 0;
        while (++start != end)
                hostname[i++] = *start;
        ret = 0;
out:
        return ret;
}

int
afr_local_pathinfo (char *pathinfo, gf_boolean_t *local)
{
        int             ret   = 0;
        char            pathinfohost[1024] = {0};
        char            localhost[1024] = {0};
        xlator_t        *this = THIS;

        *local = _gf_false;
        ret = get_pathinfo_host (pathinfo, pathinfohost, sizeof (pathinfohost));
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "Invalid pathinfo: %s",
                        pathinfo);
                goto out;
        }

        ret = gethostname (localhost, sizeof (localhost));
        if (ret) {
                gf_log (this->name, GF_LOG_ERROR, "gethostname() failed, "
                        "reason: %s", strerror (errno));
                goto out;
        }

        if (!strcmp (localhost, pathinfohost))
                *local = _gf_true;
out:
        return ret;
}

static int32_t
afr_local_discovery_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
			 int32_t op_ret, int32_t op_errno, dict_t *dict,
			 dict_t *xdata)
{
        int              ret            = 0;
        char            *pathinfo       = NULL;
        gf_boolean_t     is_local        = _gf_false;
        afr_private_t   *priv           = NULL;
        int32_t          child_index    = -1;

        if (op_ret != 0) {
                goto out;
        }

	priv = this->private;
	child_index = (int32_t)(long)cookie;

        ret = dict_get_str (dict, GF_XATTR_PATHINFO_KEY, &pathinfo);
        if (ret != 0) {
                goto out;
        }

        ret = afr_local_pathinfo (pathinfo, &is_local);
        if (ret) {
                goto out;
        }

        /*
         * Note that one local subvolume will override another here.  The only
         * way to avoid that would be to retain extra information about whether
         * the previous read_child is local, and it's just not worth it.  Even
         * the slowest local subvolume is far preferable to a remote one.
         */
        if (is_local) {
                gf_log (this->name, GF_LOG_INFO,
                        "selecting local read_child %s",
                        priv->children[child_index]->name);
                priv->read_child = child_index;
        }
out:
        STACK_DESTROY(frame->root);
        return 0;
}

static void
afr_attempt_local_discovery (xlator_t *this, int32_t child_index)
{
        call_frame_t    *newframe = NULL;
        loc_t            tmploc = {0,};
        afr_private_t   *priv = this->private;

        newframe = create_frame(this,this->ctx->pool);
        if (!newframe) {
                return;
        }

        tmploc.gfid[sizeof(tmploc.gfid)-1] = 1;
        STACK_WIND_COOKIE (newframe, afr_local_discovery_cbk,
                           (void *)(long)child_index,
                           priv->children[child_index],
                           priv->children[child_index]->fops->getxattr,
                           &tmploc, GF_XATTR_PATHINFO_KEY, NULL);
}

int
afr_lookup_sh_metadata_wrap (void *opaque)
{
        call_frame_t *frame       = opaque;
        afr_local_t  *local       = NULL;
        xlator_t     *this        = NULL;
        inode_t      *inode       = NULL;
        afr_private_t *priv       = NULL;
        struct afr_reply *replies = NULL;
        int i= 0, first = -1;

        local = frame->local;
        this  = frame->this;
        priv  = this->private;
        replies = local->replies;

        for (i =0; i < priv->child_count; i++) {
                if(!replies[i].valid || replies[i].op_ret == -1)
                        continue;
                first = i;
                break;
        }
        if (first == -1)
                goto out;

        inode = afr_inode_link (local->inode,&replies[first].poststat);
        if(!inode)
                goto out;

        afr_selfheal_metadata (frame, this, inode);
        inode_forget (inode, 1);
        inode_unref (inode);

        afr_local_replies_wipe (local, this->private);
        inode = afr_selfheal_unlocked_lookup_on (frame, local->loc.parent,
                                                 local->loc.name, local->replies,
                                                 local->child_up, NULL);
        if (inode)
                inode_unref (inode);
out:
        afr_lookup_done (frame, this);

        return 0;
}

static gf_boolean_t
afr_can_start_metadata_self_heal(call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        struct afr_reply *replies = NULL;
        int i = 0, first = -1;
        gf_boolean_t start = _gf_false;
        struct iatt stbuf = {0, };

        local = frame->local;
        replies = local->replies;
        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if(!replies[i].valid || replies[i].op_ret == -1)
                        continue;
                if (first == -1) {
                        first = i;
                        stbuf = replies[i].poststat;
                        continue;
                }

                if (uuid_compare (stbuf.ia_gfid, replies[i].poststat.ia_gfid)) {
                        start = _gf_false;
                        break;
                }
                if (!IA_EQUAL (stbuf, replies[i].poststat, type)) {
                        start = _gf_false;
                        break;
                }

                /*Check if iattrs need heal*/
                if ((!IA_EQUAL (stbuf, replies[i].poststat, uid)) ||
                    (!IA_EQUAL (stbuf, replies[i].poststat, gid)) ||
                    (!IA_EQUAL (stbuf, replies[i].poststat, prot))) {
                        start = _gf_true;
                        continue;
                }

                /*Check if xattrs need heal*/
                if (!afr_xattrs_are_equal (replies[first].xdata,
                                           replies[i].xdata))
                        start = _gf_true;
        }

        return start;
}

int
afr_lookup_metadata_heal_check (call_frame_t *frame, xlator_t *this)

{
        call_frame_t *heal = NULL;
        int ret            = 0;

        if (!afr_can_start_metadata_self_heal (frame, this))
                goto out;

        heal = copy_frame (frame);
        if (heal)
                heal->root->pid = GF_CLIENT_PID_AFR_SELF_HEALD;
        ret = synctask_new (this->ctx->env, afr_lookup_sh_metadata_wrap,
                            afr_refresh_selfheal_done, heal, frame);
        if(ret)
                goto out;
        return ret;
out:
        afr_lookup_done (frame, this);
        return ret;
}

int
afr_lookup_selfheal_wrap (void *opaque)
{
        int ret = 0;
	call_frame_t *frame = opaque;
	afr_local_t *local = NULL;
	xlator_t *this = NULL;
	inode_t *inode = NULL;

	local = frame->local;
	this = frame->this;

	ret = afr_selfheal_name (frame->this, local->loc.pargfid,
                                 local->loc.name, &local->cont.lookup.gfid_req);
        if (ret == -EIO)
                goto unwind;

        afr_local_replies_wipe (local, this->private);

	inode = afr_selfheal_unlocked_lookup_on (frame, local->loc.parent,
						 local->loc.name, local->replies,
						 local->child_up, NULL);
	if (inode)
		inode_unref (inode);

        afr_lookup_metadata_heal_check(frame, this);
        return 0;

unwind:
	AFR_STACK_UNWIND (lookup, frame, -1, EIO, NULL, NULL, NULL, NULL);
        return 0;
}


int
afr_lookup_entry_heal (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	call_frame_t *heal = NULL;
	int i = 0, first = -1;
	gf_boolean_t need_heal = _gf_false;
	struct afr_reply *replies = NULL;
	int ret = 0;

	local = frame->local;
	replies = local->replies;
	priv = this->private;

	for (i = 0; i < priv->child_count; i++) {
		if (!replies[i].valid)
			continue;

                if ((replies[i].op_ret == -1) &&
                    (replies[i].op_errno == ENODATA))
                        need_heal = _gf_true;

		if (first == -1) {
			first = i;
			continue;
		}

		if (replies[i].op_ret != replies[first].op_ret) {
			need_heal = _gf_true;
			break;
		}

		if (uuid_compare (replies[i].poststat.ia_gfid,
				  replies[first].poststat.ia_gfid)) {
			need_heal = _gf_true;
			break;
		}
	}

	if (need_heal) {
		heal = copy_frame (frame);
		if (heal)
			heal->root->pid = GF_CLIENT_PID_AFR_SELF_HEALD;
		ret = synctask_new (this->ctx->env, afr_lookup_selfheal_wrap,
				    afr_refresh_selfheal_done, heal, frame);
		if (ret)
			goto metadata_heal;
                return ret;
	}
metadata_heal:
        ret = afr_lookup_metadata_heal_check (frame, this);

	return ret;
}


int
afr_lookup_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		int op_ret, int op_errno, inode_t *inode, struct iatt *buf,
		dict_t *xdata, struct iatt *postparent)
{
        afr_local_t *   local = NULL;
        int             call_count      = -1;
        int             child_index     = -1;

	child_index = (long) cookie;

	local = frame->local;

	local->replies[child_index].valid = 1;
	local->replies[child_index].op_ret = op_ret;
	local->replies[child_index].op_errno = op_errno;
        /*
         * On revalidate lookup if the gfid-changed, afr should unwind the fop
         * with ESTALE so that a fresh lookup will be sent by the top xlator.
         * So remember it.
         */
        if (xdata && dict_get (xdata, "gfid-changed"))
                local->cont.lookup.needs_fresh_lookup = _gf_true;

	if (op_ret != -1) {
		local->replies[child_index].poststat = *buf;
		local->replies[child_index].postparent = *postparent;
		if (xdata)
			local->replies[child_index].xdata = dict_ref (xdata);
	}

        call_count = afr_frame_return (frame);
        if (call_count == 0) {
		afr_lookup_entry_heal (frame, this);
        }

	return 0;
}



static void
afr_discover_done (call_frame_t *frame, xlator_t *this)
{
        afr_private_t       *priv  = NULL;
        afr_local_t         *local = NULL;
	int                 i = -1;
	int                 op_errno = 0;
	int                 read_subvol = 0;

        priv  = this->private;
        local = frame->local;

	for (i = 0; i < priv->child_count; i++) {
		if (!local->replies[i].valid)
			continue;
		if (local->replies[i].op_ret == 0)
			local->op_ret = 0;
	}

	op_errno = afr_final_errno (frame->local, this->private);

        if (local->op_ret < 0) {
		local->op_errno = op_errno;
		local->op_ret = -1;
                goto unwind;
	}

	afr_replies_interpret (frame, this, local->inode);

	read_subvol = afr_data_subvol_get (local->inode, this, 0, 0);
	if (read_subvol == -1) {
		gf_log (this->name, GF_LOG_WARNING, "no read subvols for %s",
			local->loc.path);

		for (i = 0; i < priv->child_count; i++) {
			if (!local->replies[i].valid ||
			    local->replies[i].op_ret == -1)
				continue;
			read_subvol = i;
			break;
		}
	}

unwind:
	if (read_subvol == -1)
		read_subvol = 0;

	AFR_STACK_UNWIND (lookup, frame, local->op_ret, local->op_errno,
			  local->inode, &local->replies[read_subvol].poststat,
			  local->replies[read_subvol].xdata,
			  &local->replies[read_subvol].postparent);
}


int
afr_discover_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int op_ret, int op_errno, inode_t *inode, struct iatt *buf,
		  dict_t *xdata, struct iatt *postparent)
{
        afr_local_t *   local = NULL;
        int             call_count      = -1;
        int             child_index     = -1;

	child_index = (long) cookie;

	local = frame->local;

	local->replies[child_index].valid = 1;
	local->replies[child_index].op_ret = op_ret;
	local->replies[child_index].op_errno = op_errno;
	if (op_ret != -1) {
		local->replies[child_index].poststat = *buf;
		local->replies[child_index].postparent = *postparent;
		if (xdata)
			local->replies[child_index].xdata = dict_ref (xdata);
	}

        if (local->do_discovery && (op_ret == 0))
                afr_attempt_local_discovery (this, child_index);

        call_count = afr_frame_return (frame);
        if (call_count == 0) {
               afr_discover_done (frame, this);
        }

	return 0;
}


int
afr_discover_do (call_frame_t *frame, xlator_t *this, int err)
{
	int ret = 0;
	int i = 0;
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int call_count = 0;

	local = frame->local;
	priv = this->private;

	if (err) {
		local->op_errno = -err;
		ret = -1;
		goto out;
	}

	call_count = local->call_count = AFR_COUNT (local->child_up,
						    priv->child_count);

        ret = afr_lookup_xattr_req_prepare (local, this, local->xattr_req,
					    &local->loc);
        if (ret) {
                local->op_errno = -ret;
		ret = -1;
                goto out;
        }

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_discover_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->lookup,
                                           &local->loc, local->xattr_req);
                        if (!--call_count)
                                break;
                }
        }

	return 0;
out:
	AFR_STACK_UNWIND (lookup, frame, -1, local->op_errno, 0, 0, 0, 0);
	return 0;
}


int
afr_discover (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xattr_req)
{
	int op_errno = ENOMEM;
	afr_private_t *priv = NULL;
	afr_local_t *local = NULL;
	int event = 0;

	priv = this->private;

	local = AFR_FRAME_INIT (frame, op_errno);
	if (!local)
		goto out;

        if (!local->call_count) {
                op_errno = ENOTCONN;
                goto out;
        }

	if (__is_root_gfid (loc->inode->gfid)) {
		if (!this->itable)
			this->itable = loc->inode->table;
		if (!priv->root_inode)
			priv->root_inode = inode_ref (loc->inode);

		if (priv->choose_local && !priv->did_discovery) {
			/* Logic to detect which subvolumes of AFR are
			   local, in order to prefer them for reads
			*/
			local->do_discovery = _gf_true;
                        priv->did_discovery = _gf_true;
                }
	}

        local->op = GF_FOP_LOOKUP;

        loc_copy (&local->loc, loc);

	local->inode = inode_ref (loc->inode);

	if (xattr_req)
		/* If xattr_req was null, afr_lookup_xattr_req_prepare() will
		   allocate one for us */
		local->xattr_req = dict_ref (xattr_req);

	if (uuid_is_null (loc->inode->gfid)) {
		afr_discover_do (frame, this, 0);
		return 0;
	}

	afr_read_subvol_get (loc->inode, this, NULL, &event,
			     AFR_DATA_TRANSACTION);

	if (event != local->event_generation)
		afr_inode_refresh (frame, this, loc->inode, afr_discover_do);
	else
		afr_discover_do (frame, this, 0);

	return 0;
out:
	AFR_STACK_UNWIND (lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);
	return 0;
}


int
afr_lookup_do (call_frame_t *frame, xlator_t *this, int err)
{
	int ret = 0;
	int i = 0;
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int call_count = 0;

	local = frame->local;
	priv = this->private;

	if (err < 0) {
		local->op_errno = -err;
		ret = -1;
		goto out;
	}

	call_count = local->call_count = AFR_COUNT (local->child_up,
						    priv->child_count);

        ret = afr_lookup_xattr_req_prepare (local, this, local->xattr_req,
					    &local->loc);
        if (ret) {
                local->op_errno = -ret;
		ret = -1;
                goto out;
        }

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_lookup_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->lookup,
                                           &local->loc, local->xattr_req);
                        if (!--call_count)
                                break;
                }
        }
	return 0;
out:
	AFR_STACK_UNWIND (lookup, frame, -1, local->op_errno, 0, 0, 0, 0);
	return 0;
}

/*
 * afr_lookup()
 *
 * The goal here is to figure out what the element getting looked up is.
 * i.e what is the GFID, inode type and a conservative estimate of the
 * inode attributes are.
 *
 * As we lookup, operations may be underway on the entry name and the
 * inode. In lookup() we are primarily concerned only with the entry
 * operations. If the entry is getting unlinked or renamed, we detect
 * what operation is underway by querying for on-going transactions and
 * pending self-healing on the entry through xdata.
 *
 * If the entry is a file/dir, it may need self-heal and/or in a
 * split-brain condition. Lookup is not the place to worry about these
 * conditions. Outcast marking will naturally handle them in the read
 * paths.
 *
 * Here is a brief goal of what we are trying to achieve:
 *
 * - LOOKUP on all subvolumes concurrently, querying on-going transaction
 *   and pending self-heal info from the servers.
 *
 * - If all servers reply the same inode type and GFID, the overall call
 *   MUST be a success.
 *
 * - If inode types or GFIDs mismatch, and there IS either an on-going
 *   transaction or pending self-heal, inspect what the nature of the
 *   transaction or pending heal is, and select the appropriate subvolume's
 *   reply as the winner.
 *
 * - If inode types or GFIDs mismatch, and there are no on-going transactions
 *   or pending self-heal on the entry name on any of the servers, fail the
 *   lookup with EIO. Something has gone wrong beyond reasonable action.
 */

int
afr_lookup (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xattr_req)
{
        afr_local_t   *local = NULL;
        int32_t        op_errno = 0;
	int            event = 0;
        void          *gfid_req = NULL;
        int            ret = 0;

	if (!loc->parent && uuid_is_null (loc->pargfid)) {
                if (xattr_req)
                        dict_del (xattr_req, "gfid-req");
		afr_discover (frame, this, loc, xattr_req);
		return 0;
	}

	if (__is_root_gfid (loc->parent->gfid)) {
		if (!strcmp (loc->name, GF_REPLICATE_TRASH_DIR)) {
			op_errno = EPERM;
			goto out;
		}
	}

	local = AFR_FRAME_INIT (frame, op_errno);
	if (!local)
		goto out;

        if (!local->call_count) {
                op_errno = ENOTCONN;
                goto out;
        }

        local->op = GF_FOP_LOOKUP;

        loc_copy (&local->loc, loc);

	local->inode = inode_ref (loc->inode);

	if (xattr_req) {
		/* If xattr_req was null, afr_lookup_xattr_req_prepare() will
		   allocate one for us */
                ret = dict_get_ptr (xattr_req, "gfid-req", &gfid_req);
                if (ret == 0) {
                        uuid_copy (local->cont.lookup.gfid_req, gfid_req);
                        dict_del (xattr_req, "gfid-req");
                }
		local->xattr_req = dict_ref (xattr_req);
        }

	afr_read_subvol_get (loc->parent, this, NULL, &event,
			     AFR_DATA_TRANSACTION);

	if (event != local->event_generation)
		afr_inode_refresh (frame, this, loc->parent, afr_lookup_do);
	else
		afr_lookup_do (frame, this, 0);

	return 0;
out:
	AFR_STACK_UNWIND (lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);

        return 0;
}


/* {{{ open */

afr_fd_ctx_t *
__afr_fd_ctx_get (fd_t *fd, xlator_t *this)
{
        uint64_t       ctx = 0;
        int            ret = 0;
        afr_fd_ctx_t  *fd_ctx = NULL;

        ret = __fd_ctx_get (fd, this, &ctx);

        if (ret < 0) {
                ret = __afr_fd_ctx_set (this, fd);
                if (ret < 0)
                        goto out;

                ret = __fd_ctx_get (fd, this, &ctx);
                if (ret < 0)
                        goto out;
        }

        fd_ctx = (afr_fd_ctx_t *)(long) ctx;
out:
        return fd_ctx;
}


afr_fd_ctx_t *
afr_fd_ctx_get (fd_t *fd, xlator_t *this)
{
        afr_fd_ctx_t  *fd_ctx = NULL;

        LOCK(&fd->lock);
        {
                fd_ctx = __afr_fd_ctx_get (fd, this);
        }
        UNLOCK(&fd->lock);

        return fd_ctx;
}


int
__afr_fd_ctx_set (xlator_t *this, fd_t *fd)
{
        afr_private_t * priv   = NULL;
        int             ret    = -1;
        uint64_t        ctx    = 0;
        afr_fd_ctx_t *  fd_ctx = NULL;
	int             i = 0;

        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (fd, out);

        priv = this->private;

        ret = __fd_ctx_get (fd, this, &ctx);

        if (ret == 0)
                goto out;

        fd_ctx = GF_CALLOC (1, sizeof (afr_fd_ctx_t),
                            gf_afr_mt_afr_fd_ctx_t);
        if (!fd_ctx) {
                ret = -ENOMEM;
                goto out;
        }

	for (i = 0; i < AFR_NUM_CHANGE_LOGS; i++) {
		fd_ctx->pre_op_done[i] = GF_CALLOC (sizeof (*fd_ctx->pre_op_done[i]),
						    priv->child_count,
						    gf_afr_mt_int32_t);
		if (!fd_ctx->pre_op_done[i]) {
			ret = -ENOMEM;
			goto out;
		}
	}

        fd_ctx->opened_on = GF_CALLOC (sizeof (*fd_ctx->opened_on),
                                       priv->child_count,
                                       gf_afr_mt_int32_t);
        if (!fd_ctx->opened_on) {
                ret = -ENOMEM;
                goto out;
        }

	for (i = 0; i < priv->child_count; i++) {
		if (fd_is_anonymous (fd))
			fd_ctx->opened_on[i] = AFR_FD_OPENED;
		else
			fd_ctx->opened_on[i] = AFR_FD_NOT_OPENED;
	}

        fd_ctx->lock_piggyback = GF_CALLOC (sizeof (*fd_ctx->lock_piggyback),
                                            priv->child_count,
                                            gf_afr_mt_char);
        if (!fd_ctx->lock_piggyback) {
                ret = -ENOMEM;
                goto out;
        }

        fd_ctx->lock_acquired = GF_CALLOC (sizeof (*fd_ctx->lock_acquired),
                                           priv->child_count,
                                           gf_afr_mt_char);
        if (!fd_ctx->lock_acquired) {
                ret = -ENOMEM;
                goto out;
        }

	pthread_mutex_init (&fd_ctx->delay_lock, NULL);

        INIT_LIST_HEAD (&fd_ctx->eager_locked);

        ret = __fd_ctx_set (fd, this, (uint64_t)(long) fd_ctx);
        if (ret)
                gf_log (this->name, GF_LOG_DEBUG,
                        "failed to set fd ctx (%p)", fd);
out:
        return ret;
}


int
afr_fd_ctx_set (xlator_t *this, fd_t *fd)
{
        int ret = -1;

        LOCK (&fd->lock);
        {
                ret = __afr_fd_ctx_set (this, fd);
        }
        UNLOCK (&fd->lock);

        return ret;
}

/* {{{ flush */

int
afr_flush_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret != -1) {
			local->op_ret = op_ret;
			if (!local->xdata_rsp && xdata)
				local->xdata_rsp = dict_ref (xdata);
		} else {
			local->op_errno = op_errno;
		}
        }
        UNLOCK (&frame->lock);

	call_count = afr_frame_return (frame);

	if (call_count == 0)
		AFR_STACK_UNWIND (flush, frame, local->op_ret,
				  local->op_errno, local->xdata_rsp);

        return 0;
}

static int
afr_flush_wrapper (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        int           i      = 0;
        afr_local_t   *local = NULL;
        afr_private_t *priv  = NULL;
        int call_count       = -1;

        priv = this->private;
        local = frame->local;
        call_count = local->call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_flush_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->flush,
                                           local->fd, xdata);
                        if (!--call_count)
                                break;

                }
        }

        return 0;
}

int
afr_flush (call_frame_t *frame, xlator_t *this, fd_t *fd, dict_t *xdata)
{
        afr_local_t   *local = NULL;
        call_stub_t   *stub = NULL;
        int            op_errno   = ENOMEM;

	local = AFR_FRAME_INIT (frame, op_errno);
	if (!local)
		goto out;

	if (!local->call_count) {
		op_errno = ENOTCONN;
		goto out;
	}

	local->fd = fd_ref(fd);

        stub = fop_flush_stub (frame, afr_flush_wrapper, fd, xdata);
        if (!stub)
                goto out;

        afr_delayed_changelog_wake_resume (this, fd, stub);

	return 0;
out:
	AFR_STACK_UNWIND (flush, frame, -1, op_errno, NULL);
        return 0;
}

/* }}} */


int
afr_cleanup_fd_ctx (xlator_t *this, fd_t *fd)
{
        uint64_t        ctx = 0;
        afr_fd_ctx_t    *fd_ctx = NULL;
        int             ret = 0;
	int             i = 0;

        ret = fd_ctx_get (fd, this, &ctx);
        if (ret < 0)
                goto out;

        fd_ctx = (afr_fd_ctx_t *)(long) ctx;

        if (fd_ctx) {
                //no need to take any locks
                if (!list_empty (&fd_ctx->eager_locked))
                        gf_log (this->name, GF_LOG_WARNING, "%s: Stale "
                                "Eager-lock stubs found",
                                uuid_utoa (fd->inode->gfid));

		for (i = 0; i < AFR_NUM_CHANGE_LOGS; i++)
			GF_FREE (fd_ctx->pre_op_done[i]);

                GF_FREE (fd_ctx->opened_on);

                GF_FREE (fd_ctx->lock_piggyback);

                GF_FREE (fd_ctx->lock_acquired);

		pthread_mutex_destroy (&fd_ctx->delay_lock);

                GF_FREE (fd_ctx);
        }

out:
        return 0;
}


int
afr_release (xlator_t *this, fd_t *fd)
{
        afr_cleanup_fd_ctx (this, fd);

        return 0;
}


/* {{{ fsync */

int
afr_fsync_unwind_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
                      struct iatt *postbuf, dict_t *xdata)
{
        AFR_STACK_UNWIND (fsync, frame, op_ret, op_errno, prebuf, postbuf,
                          xdata);
        return 0;
}

int
afr_fsync_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *prebuf,
               struct iatt *postbuf, dict_t *xdata)
{
        afr_local_t *local = NULL;
        int call_count = -1;
        int child_index = (long) cookie;
	int read_subvol = 0;
	call_stub_t *stub = NULL;

        local = frame->local;

	read_subvol = afr_data_subvol_get (local->inode, this, 0, 0);

        LOCK (&frame->lock);
        {
                if (op_ret == 0) {
                        if (local->op_ret == -1) {
				local->op_ret = 0;

                                local->cont.inode_wfop.prebuf  = *prebuf;
                                local->cont.inode_wfop.postbuf = *postbuf;

				if (xdata)
					local->xdata_rsp = dict_ref (xdata);
                        }

                        if (child_index == read_subvol) {
                                local->cont.inode_wfop.prebuf  = *prebuf;
                                local->cont.inode_wfop.postbuf = *postbuf;
				if (xdata) {
					if (local->xdata_rsp)
						dict_unref (local->xdata_rsp);
					local->xdata_rsp = dict_ref (xdata);
				}
                        }
                } else {
			local->op_errno = op_errno;
		}
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
		/* Make a stub out of the frame, and register it
		   with the waking up post-op. When the call-stub resumes,
		   we are guaranteed that there was no post-op pending
		   (i.e changelogs were unset in the server). This is an
		   essential "guarantee", that fsync() returns only after
		   completely finishing EVERYTHING, including the delayed
		   post-op. This guarantee is expected by FUSE graph switching
		   for example.
		*/
		stub = fop_fsync_cbk_stub (frame, afr_fsync_unwind_cbk,
                                           local->op_ret, local->op_errno,
                                           &local->cont.inode_wfop.prebuf,
                                           &local->cont.inode_wfop.postbuf,
                                           local->xdata_rsp);
		if (!stub) {
			AFR_STACK_UNWIND (fsync, frame, -1, ENOMEM, 0, 0, 0);
			return 0;
		}

		/* If no new unstable writes happened between the
		   time we cleared the unstable write witness flag in afr_fsync
		   and now, calling afr_delayed_changelog_wake_up() should
		   wake up and skip over the fsync phase and go straight to
		   afr_changelog_post_op_now()
		*/
		afr_delayed_changelog_wake_resume (this, local->fd, stub);
        }

        return 0;
}


int
afr_fsync (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
	   dict_t *xdata)
{
	afr_private_t *priv = NULL;
        afr_local_t *local = NULL;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_errno = ENOMEM;

	priv = this->private;

	local = AFR_FRAME_INIT (frame, op_errno);
	if (!local)
		goto out;

        call_count = local->call_count;
	if (!call_count) {
		op_errno = ENOTCONN;
		goto out;
	}

        local->fd = fd_ref (fd);

	if (afr_fd_has_witnessed_unstable_write (this, fd)) {
		/* don't care. we only wanted to CLEAR the bit */
	}

	local->inode = inode_ref (fd->inode);

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_fsync_cbk,
                                           (void *) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->fsync,
                                           fd, datasync, xdata);
                        if (!--call_count)
                                break;
                }
        }

	return 0;
out:
	AFR_STACK_UNWIND (fsync, frame, -1, op_errno, NULL, NULL, NULL);

        return 0;
}

/* }}} */

/* {{{ fsync */

int
afr_fsyncdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0) {
                        local->op_ret = 0;
			if (!local->xdata_rsp && xdata)
				local->xdata_rsp = dict_ref (xdata);
		} else {
			local->op_errno = op_errno;
		}
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (fsyncdir, frame, local->op_ret,
				  local->op_errno, local->xdata_rsp);

        return 0;
}


int
afr_fsyncdir (call_frame_t *frame, xlator_t *this, fd_t *fd, int32_t datasync,
	      dict_t *xdata)
{
	afr_private_t *priv = NULL;
        afr_local_t *local = NULL;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_errno = ENOMEM;

	priv = this->private;

        local = AFR_FRAME_INIT (frame, op_errno);
	if (!local)
		goto out;

        call_count = local->call_count;
	if (!call_count) {
		op_errno = ENOTCONN;
		goto out;
	}

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_fsyncdir_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->fsyncdir,
                                    fd, datasync, xdata);
                        if (!--call_count)
                                break;
                }
        }

	return 0;
out:
	AFR_STACK_UNWIND (fsyncdir, frame, -1, op_errno, NULL);

        return 0;
}

/* }}} */

/* {{{ xattrop */

int32_t
afr_xattrop_cbk (call_frame_t *frame, void *cookie,
                 xlator_t *this, int32_t op_ret, int32_t op_errno,
                 dict_t *xattr, dict_t *xdata)
{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0) {
                        if (!local->cont.xattrop.xattr)
                                local->cont.xattrop.xattr = dict_ref (xattr);

			if (!local->xdata_rsp && xdata)
				local->xdata_rsp = dict_ref (xdata);

                        local->op_ret = 0;
                }

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (xattrop, frame, local->op_ret, local->op_errno,
                local->cont.xattrop.xattr, local->xdata_rsp);

        return 0;
}


int32_t
afr_xattrop (call_frame_t *frame, xlator_t *this, loc_t *loc,
             gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_errno = ENOMEM;

        priv = this->private;

	local = AFR_FRAME_INIT (frame, op_errno);
	if (!local)
		goto out;

        call_count = local->call_count;
	if (!call_count) {
		op_errno = ENOTCONN;
		goto out;
	}

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_xattrop_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->xattrop,
                                    loc, optype, xattr, xdata);
                        if (!--call_count)
                                break;
                }
        }

	return 0;
out:
	AFR_STACK_UNWIND (xattrop, frame, -1, op_errno, NULL, NULL);

        return 0;
}

/* }}} */

/* {{{ fxattrop */

int32_t
afr_fxattrop_cbk (call_frame_t *frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno,
                  dict_t *xattr, dict_t *xdata)
{
        afr_local_t *local = NULL;

        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0) {
                        if (!local->cont.fxattrop.xattr)
                                local->cont.fxattrop.xattr = dict_ref (xattr);

			if (!local->xdata_rsp && xdata)
				local->xdata_rsp = dict_ref (xdata);
                        local->op_ret = 0;
                }

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (fxattrop, frame, local->op_ret, local->op_errno,
                                  local->cont.fxattrop.xattr, local->xdata_rsp);

        return 0;
}


int32_t
afr_fxattrop (call_frame_t *frame, xlator_t *this, fd_t *fd,
              gf_xattrop_flags_t optype, dict_t *xattr, dict_t *xdata)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_errno = 0;

        priv = this->private;

	local = AFR_FRAME_INIT (frame, op_errno);
	if (!local)
                goto out;

        call_count = local->call_count;
	if (!call_count) {
		op_errno = ENOTCONN;
		goto out;
	}

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_fxattrop_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->fxattrop,
                                    fd, optype, xattr, xdata);
                        if (!--call_count)
                                break;
                }
        }

	return 0;
out:
	AFR_STACK_UNWIND (fxattrop, frame, -1, op_errno, NULL, NULL);

        return 0;
}

/* }}} */

int32_t
afr_unlock_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)

{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int call_count = -1;
        int child_index = (long)cookie;
        uuid_t  gfid = {0};

        local = frame->local;
        priv = this->private;

        if (op_ret < 0 && op_errno != ENOTCONN) {
                loc_gfid (&local->loc, gfid);
                gf_log (this->name, GF_LOG_ERROR, "%s: Failed to unlock %s "
                        "with lk_owner: %s (%s)", uuid_utoa (gfid),
                        priv->children[child_index]->name,
                        lkowner_utoa (&frame->root->lk_owner),
                        strerror (op_errno));
        }

        call_count = afr_frame_return (frame);
        if (call_count == 0) {
                AFR_STACK_UNWIND (inodelk, frame, local->op_ret,
                                  local->op_errno, local->xdata_rsp);
        }

        return 0;
}

int32_t
afr_unlock_inodelks_and_unwind (call_frame_t *frame, xlator_t *this,
                                int call_count)
{
        int i = 0;
        afr_private_t *priv = NULL;
        afr_local_t *local = NULL;

        local = frame->local;
        priv = this->private;
        local->call_count = call_count;
        local->cont.inodelk.flock.l_type = F_UNLCK;

        for (i = 0; i < priv->child_count; i++) {
                if (!local->replies[i].valid)
                        continue;

                if (local->replies[i].op_ret == -1)
                        continue;

                STACK_WIND_COOKIE (frame, afr_unlock_inodelk_cbk,
                                   (void*) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->inodelk,
                                   local->cont.inodelk.volume,
                                   &local->loc, local->cont.inodelk.cmd,
                                   &local->cont.inodelk.flock, 0);

                if (!--call_count)
                        break;
        }

        return 0;
}

int32_t
afr_inodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		 int32_t op_ret, int32_t op_errno, dict_t *xdata)

{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int call_count = -1;
        int child_index = (long)cookie;
        int i = 0;
        int lock_count = 0;

        local = frame->local;
        priv = this->private;

        local->replies[child_index].valid = 1;
        local->replies[child_index].op_ret = op_ret;
        local->replies[child_index].op_errno = op_errno;
        if (op_ret == 0 && xdata) {
                local->replies[child_index].xdata = dict_ref (xdata);
                LOCK (&frame->lock);
                {
                        if (!local->xdata_rsp)
                                local->xdata_rsp = dict_ref (xdata);
                }
                UNLOCK (&frame->lock);
        }

        call_count = afr_frame_return (frame);

        if (call_count == 0) {
                for (i = 0; i < priv->child_count; i++) {
                        if (!local->replies[i].valid)
                                continue;

                        if (local->replies[i].op_ret == 0)
                                lock_count++;

                        if (local->op_ret == -1 && local->op_errno == EAGAIN)
                                continue;

                        if ((local->replies[i].op_ret == -1) &&
                            (local->replies[i].op_errno == EAGAIN)) {
                                local->op_ret = -1;
                                local->op_errno = EAGAIN;
                                continue;
                        }

                        if (local->replies[i].op_ret == 0)
                                local->op_ret = 0;

                        local->op_errno = local->replies[i].op_errno;
                }

                if (lock_count && local->cont.inodelk.flock.l_type != F_UNLCK &&
                    (local->op_ret == -1 && local->op_errno == EAGAIN)) {
                        afr_unlock_inodelks_and_unwind (frame, this,
                                                        lock_count);
                } else {
                        AFR_STACK_UNWIND (inodelk, frame, local->op_ret,
                                          local->op_errno, local->xdata_rsp);
                }
        }

        return 0;
}


int32_t
afr_inodelk (call_frame_t *frame, xlator_t *this,
             const char *volume, loc_t *loc, int32_t cmd,
             struct gf_flock *flock, dict_t *xdata)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_errno = ENOMEM;

        priv = this->private;

        local = AFR_FRAME_INIT (frame, op_errno);
        if (!local)
                goto out;

        loc_copy (&local->loc, loc);
        local->cont.inodelk.volume = volume;
        local->cont.inodelk.cmd = cmd;
        local->cont.inodelk.flock = *flock;

        call_count = local->call_count;
	if (!call_count) {
		op_errno = ENOMEM;
		goto out;
	}

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND_COOKIE (frame, afr_inodelk_cbk,
                                           (void*) (long) i,
                                           priv->children[i],
                                           priv->children[i]->fops->inodelk,
                                           volume, loc, cmd, flock, xdata);

                        if (!--call_count)
                                break;
                }
        }

	return 0;
out:
	AFR_STACK_UNWIND (inodelk, frame, -1, op_errno, NULL);

        return 0;
}


int32_t
afr_finodelk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int32_t op_ret, int32_t op_errno, dict_t *xdata)

{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0)
                        local->op_ret = 0;

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (finodelk, frame, local->op_ret,
                                  local->op_errno, xdata);

        return 0;
}


int32_t
afr_finodelk (call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
	      int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_errno = ENOMEM;

        priv = this->private;

	local = AFR_FRAME_INIT (frame, op_errno);
	if (!local)
		goto out;

        call_count = local->call_count;
	if (!call_count) {
		op_errno = ENOTCONN;
		goto out;
	}

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_finodelk_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->finodelk,
                                    volume, fd, cmd, flock, xdata);

                        if (!--call_count)
                                break;
                }
        }

	return 0;
out:
	AFR_STACK_UNWIND (finodelk, frame, -1, op_errno, NULL);

        return 0;
}


int32_t
afr_entrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0)
                        local->op_ret = 0;

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (entrylk, frame, local->op_ret,
                                  local->op_errno, xdata);

        return 0;
}


int
afr_entrylk (call_frame_t *frame, xlator_t *this, const char *volume,
	     loc_t *loc, const char *basename, entrylk_cmd cmd,
	     entrylk_type type, dict_t *xdata)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_errno = 0;

        priv = this->private;

	local = AFR_FRAME_INIT (frame, op_errno);
	if (!local)
		goto out;

        call_count = local->call_count;
	if (!call_count) {
		op_errno = ENOTCONN;
		goto out;
	}

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_entrylk_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->entrylk,
                                    volume, loc, basename, cmd, type, xdata);

                        if (!--call_count)
                                break;
                }
        }

	return 0;
out:
	AFR_STACK_UNWIND (entrylk, frame, -1, op_errno, NULL);

        return 0;
}



int
afr_fentrylk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
		  int32_t op_ret, int32_t op_errno, dict_t *xdata)

{
        afr_local_t *local = NULL;
        int call_count = -1;

        local = frame->local;

        LOCK (&frame->lock);
        {
                if (op_ret == 0)
                        local->op_ret = 0;

                local->op_errno = op_errno;
        }
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (fentrylk, frame, local->op_ret,
                                  local->op_errno, xdata);

        return 0;
}


int
afr_fentrylk (call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
              const char *basename, entrylk_cmd cmd, entrylk_type type,
	      dict_t *xdata)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int i = 0;
        int32_t call_count = 0;
        int32_t op_errno = ENOMEM;

        priv = this->private;

        local = AFR_FRAME_INIT (frame, op_errno);
	if (!local)
		goto out;

        call_count = local->call_count;
	if (!call_count) {
		op_errno = ENOTCONN;
		goto out;
	}

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_fentrylk_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->fentrylk,
                                    volume, fd, basename, cmd, type, xdata);

                        if (!--call_count)
                                break;
                }
        }

	return 0;
out:
	AFR_STACK_UNWIND (fentrylk, frame, -1, op_errno, NULL);

        return 0;
}


int
afr_statfs_cbk (call_frame_t *frame, void *cookie, xlator_t *this, int op_ret,
		int op_errno, struct statvfs *statvfs, dict_t *xdata)
{
        afr_local_t *local = NULL;
        int call_count = 0;
	struct statvfs *buf = NULL;

        LOCK (&frame->lock);
        {
                local = frame->local;

                if (op_ret != 0) {
                        local->op_errno = op_errno;
			goto unlock;
		}

		local->op_ret = op_ret;

		buf = &local->cont.statfs.buf;
		if (local->cont.statfs.buf_set) {
			if (statvfs->f_bavail < buf->f_bavail) {
				*buf = *statvfs;
				if (xdata) {
					if (local->xdata_rsp)
						dict_unref (local->xdata_rsp);
					local->xdata_rsp = dict_ref (xdata);
				}
			}
		} else {
			*buf = *statvfs;
			local->cont.statfs.buf_set = 1;
			if (xdata)
				local->xdata_rsp = dict_ref (xdata);
		}
        }
unlock:
        UNLOCK (&frame->lock);

        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (statfs, frame, local->op_ret, local->op_errno,
                                  &local->cont.statfs.buf, local->xdata_rsp);

        return 0;
}


int
afr_statfs (call_frame_t *frame, xlator_t *this, loc_t *loc, dict_t *xdata)
{
        afr_local_t   *  local       = NULL;
	afr_private_t   *priv        = NULL;
        int              i           = 0;
        int              call_count = 0;
        int32_t          op_errno    = ENOMEM;

	priv = this->private;

	local = AFR_FRAME_INIT (frame, op_errno);
	if (!local)
		goto out;

        call_count = local->call_count;
	if (!call_count) {
		op_errno = ENOTCONN;
		goto out;
	}

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        STACK_WIND (frame, afr_statfs_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->statfs,
                                    loc, xdata);
                        if (!--call_count)
                                break;
                }
        }

	return 0;
out:
	AFR_STACK_UNWIND (statfs, frame, -1, op_errno, NULL, NULL);

        return 0;
}


int32_t
afr_lk_unlock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                   int32_t op_ret, int32_t op_errno, struct gf_flock *lock,
                   dict_t *xdata)
{
        afr_local_t * local = NULL;
        int call_count = -1;

        local = frame->local;
        call_count = afr_frame_return (frame);

        if (call_count == 0)
                AFR_STACK_UNWIND (lk, frame, local->op_ret, local->op_errno,
                                  lock, xdata);

        return 0;
}


int32_t
afr_lk_unlock (call_frame_t *frame, xlator_t *this)
{
        afr_local_t   * local = NULL;
        afr_private_t * priv  = NULL;
        int i = 0;
        int call_count = 0;

        local = frame->local;
        priv  = this->private;

        call_count = afr_locked_nodes_count (local->cont.lk.locked_nodes,
                                             priv->child_count);

        if (call_count == 0) {
                AFR_STACK_UNWIND (lk, frame, local->op_ret, local->op_errno,
                                  &local->cont.lk.ret_flock, NULL);
                return 0;
        }

        local->call_count = call_count;

        local->cont.lk.user_flock.l_type = F_UNLCK;

        for (i = 0; i < priv->child_count; i++) {
                if (local->cont.lk.locked_nodes[i]) {
                        STACK_WIND (frame, afr_lk_unlock_cbk,
                                    priv->children[i],
                                    priv->children[i]->fops->lk,
                                    local->fd, F_SETLK,
                                    &local->cont.lk.user_flock, NULL);

                        if (!--call_count)
                                break;
                }
        }

        return 0;
}


int32_t
afr_lk_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
            int32_t op_ret, int32_t op_errno, struct gf_flock *lock, dict_t *xdata)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int child_index = -1;
/*        int            ret  = 0; */


        local = frame->local;
        priv  = this->private;

        child_index = (long) cookie;

        if (!child_went_down (op_ret, op_errno) && (op_ret == -1)) {
                local->op_ret   = -1;
                local->op_errno = op_errno;

                afr_lk_unlock (frame, this);
                return 0;
        }

        if (op_ret == 0) {
                local->op_ret        = 0;
                local->op_errno      = 0;
                local->cont.lk.locked_nodes[child_index] = 1;
                local->cont.lk.ret_flock = *lock;
        }

        child_index++;

        if (child_index < priv->child_count) {
                STACK_WIND_COOKIE (frame, afr_lk_cbk, (void *) (long) child_index,
                                   priv->children[child_index],
                                   priv->children[child_index]->fops->lk,
                                   local->fd, local->cont.lk.cmd,
                                   &local->cont.lk.user_flock, xdata);
        } else if (local->op_ret == -1) {
                /* all nodes have gone down */

                AFR_STACK_UNWIND (lk, frame, -1, ENOTCONN,
                                  &local->cont.lk.ret_flock, NULL);
        } else {
                AFR_STACK_UNWIND (lk, frame, local->op_ret, local->op_errno,
                                  &local->cont.lk.ret_flock, NULL);
        }

        return 0;
}


int
afr_lk (call_frame_t *frame, xlator_t *this,
        fd_t *fd, int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
        afr_private_t *priv = NULL;
        afr_local_t *local = NULL;
        int i = 0;
        int32_t op_errno = ENOMEM;

        priv = this->private;

        local = AFR_FRAME_INIT (frame, op_errno);
        if (!local)
                goto out;

        local->cont.lk.locked_nodes = GF_CALLOC (priv->child_count,
                                                 sizeof (*local->cont.lk.locked_nodes),
                                                 gf_afr_mt_char);

        if (!local->cont.lk.locked_nodes) {
                op_errno = ENOMEM;
                goto out;
        }

        local->fd            = fd_ref (fd);
        local->cont.lk.cmd   = cmd;
        local->cont.lk.user_flock = *flock;
        local->cont.lk.ret_flock = *flock;

        STACK_WIND_COOKIE (frame, afr_lk_cbk, (void *) (long) 0,
                           priv->children[i],
                           priv->children[i]->fops->lk,
                           fd, cmd, flock, xdata);

	return 0;
out:
	AFR_STACK_UNWIND (lk, frame, -1, op_errno, NULL, NULL);

        return 0;
}

int
afr_forget (xlator_t *this, inode_t *inode)
{
        return 0;
}

int
afr_priv_dump (xlator_t *this)
{
        afr_private_t *priv = NULL;
        char  key_prefix[GF_DUMP_MAX_BUF_LEN];
        char  key[GF_DUMP_MAX_BUF_LEN];
        int   i = 0;


        GF_ASSERT (this);
        priv = this->private;

        GF_ASSERT (priv);
        snprintf(key_prefix, GF_DUMP_MAX_BUF_LEN, "%s.%s", this->type, this->name);
        gf_proc_dump_add_section(key_prefix);
        gf_proc_dump_write("child_count", "%u", priv->child_count);
        for (i = 0; i < priv->child_count; i++) {
                sprintf (key, "child_up[%d]", i);
                gf_proc_dump_write(key, "%d", priv->child_up[i]);
                sprintf (key, "pending_key[%d]", i);
                gf_proc_dump_write(key, "%s", priv->pending_key[i]);
        }
        gf_proc_dump_write("data_self_heal", "%s", priv->data_self_heal);
        gf_proc_dump_write("metadata_self_heal", "%d", priv->metadata_self_heal);
        gf_proc_dump_write("entry_self_heal", "%d", priv->entry_self_heal);
        gf_proc_dump_write("data_change_log", "%d", priv->data_change_log);
        gf_proc_dump_write("metadata_change_log", "%d", priv->metadata_change_log);
        gf_proc_dump_write("entry-change_log", "%d", priv->entry_change_log);
        gf_proc_dump_write("read_child", "%d", priv->read_child);
        gf_proc_dump_write("favorite_child", "%d", priv->favorite_child);
        gf_proc_dump_write("wait_count", "%u", priv->wait_count);

        return 0;
}


/**
 * find_child_index - find the child's index in the array of subvolumes
 * @this: AFR
 * @child: child
 */

static int
find_child_index (xlator_t *this, xlator_t *child)
{
        afr_private_t *priv = NULL;
        int i = -1;

        priv = this->private;

        for (i = 0; i < priv->child_count; i++) {
                if ((xlator_t *) child == priv->children[i])
                        break;
        }

        return i;
}

int32_t
afr_notify (xlator_t *this, int32_t event,
            void *data, void *data2)
{
        afr_private_t   *priv               = NULL;
        int             i                   = -1;
        int             up_children         = 0;
        int             down_children       = 0;
        int             propagate           = 0;
        int             had_heard_from_all  = 0;
        int             have_heard_from_all = 0;
        int             idx                 = -1;
        int             ret                 = -1;
        int             call_psh            = 0;
        int             up_child            = -1;
        dict_t          *input              = NULL;
        dict_t          *output             = NULL;
        gf_boolean_t    had_quorum          = _gf_false;
        gf_boolean_t    has_quorum          = _gf_false;

        priv = this->private;

        if (!priv)
                return 0;

        /*
         * We need to reset this in case children come up in "staggered"
         * fashion, so that we discover a late-arriving local subvolume.  Note
         * that we could end up issuing N lookups to the first subvolume, and
         * O(N^2) overall, but N is small for AFR so it shouldn't be an issue.
         */
        priv->did_discovery = _gf_false;

        had_heard_from_all = 1;
        for (i = 0; i < priv->child_count; i++) {
                if (!priv->last_event[i]) {
                        had_heard_from_all = 0;
                }
        }

        /* parent xlators dont need to know about every child_up, child_down
         * because of afr ha. If all subvolumes go down, child_down has
         * to be triggered. In that state when 1 subvolume comes up child_up
         * needs to be triggered. dht optimizes revalidate lookup by sending
         * it only to one of its subvolumes. When child up/down happens
         * for afr's subvolumes dht should be notified by child_modified. The
         * subsequent revalidate lookup happens on all the dht's subvolumes
         * which triggers afr self-heals if any.
         */
        idx = find_child_index (this, data);
        if (idx < 0) {
                gf_msg (this->name, GF_LOG_ERROR, 0, AFR_MSG_INVALID_CHILD_UP,
                        "Received child_up from invalid subvolume");
                goto out;
        }

        had_quorum = priv->quorum_count && afr_has_quorum (priv->child_up,
                                                           this);
        switch (event) {
        case GF_EVENT_CHILD_UP:
                LOCK (&priv->lock);
                {
                        /*
                         * This only really counts if the child was never up
                         * (value = -1) or had been down (value = 0).  See
                         * comment at GF_EVENT_CHILD_DOWN for a more detailed
                         * explanation.
                         */
                        if (priv->child_up[idx] != 1) {
                                priv->up_count++;
				priv->event_generation++;
                        }
                        priv->child_up[idx] = 1;

                        call_psh = 1;
                        up_child = idx;
                        for (i = 0; i < priv->child_count; i++)
                                if (priv->child_up[i] == 1)
                                        up_children++;
                        if (up_children == 1) {
                                gf_msg (this->name, GF_LOG_INFO, 0,
                                        AFR_MSG_SUBVOL_UP,
                                        "Subvolume '%s' came back up; "
                                        "going online.", ((xlator_t *)data)->name);
                        } else {
                                event = GF_EVENT_CHILD_MODIFIED;
                        }

                        priv->last_event[idx] = event;
                }
                UNLOCK (&priv->lock);

                break;

        case GF_EVENT_CHILD_DOWN:
                LOCK (&priv->lock);
                {
                        /*
                         * If a brick is down when we start, we'll get a
                         * CHILD_DOWN to indicate its initial state.  There
                         * was never a CHILD_UP in this case, so if we
                         * increment "down_count" the difference between than
                         * and "up_count" will no longer be the number of
                         * children that are currently up.  This has serious
                         * implications e.g. for quorum enforcement, so we
                         * don't increment these values unless the event
                         * represents an actual state transition between "up"
                         * (value = 1) and anything else.
                         */
                        if (priv->child_up[idx] == 1) {
                                priv->down_count++;
				priv->event_generation++;
                        }
                        priv->child_up[idx] = 0;

                        for (i = 0; i < priv->child_count; i++)
                                if (priv->child_up[i] == 0)
                                        down_children++;
                        if (down_children == priv->child_count) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        AFR_MSG_ALL_SUBVOLS_DOWN,
                                        "All subvolumes are down. Going offline "
                                        "until atleast one of them comes back up.");
                        } else {
                                event = GF_EVENT_CHILD_MODIFIED;
                        }

                        priv->last_event[idx] = event;
                }
                UNLOCK (&priv->lock);

                break;

        case GF_EVENT_CHILD_CONNECTING:
                LOCK (&priv->lock);
                {
                        priv->last_event[idx] = event;
                }
                UNLOCK (&priv->lock);

                break;

        case GF_EVENT_TRANSLATOR_OP:
                input = data;
                output = data2;
                if (!had_heard_from_all) {
                        ret = -1;
                        goto out;
                }
                ret = afr_xl_op (this, input, output);
                goto out;
                break;

        default:
                propagate = 1;
                break;
        }

        if (priv->quorum_count) {
                has_quorum = afr_has_quorum (priv->child_up, this);
                if (!had_quorum && has_quorum)
                        gf_msg (this->name, GF_LOG_INFO, 0, AFR_MSG_QUORUM_MET,
                                "Client-quorum is met");
                if (had_quorum && !has_quorum)
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                AFR_MSG_QUORUM_FAIL,
                                "Client-quorum is not met");
        }

        /* have all subvolumes reported status once by now? */
        have_heard_from_all = 1;
        for (i = 0; i < priv->child_count; i++) {
                if (!priv->last_event[i])
                        have_heard_from_all = 0;
        }

        /* if all subvols have reported status, no need to hide anything
           or wait for anything else. Just propagate blindly */
        if (have_heard_from_all)
                propagate = 1;

        if (!had_heard_from_all && have_heard_from_all) {
                /* This is the first event which completes aggregation
                   of events from all subvolumes. If at least one subvol
                   had come up, propagate CHILD_UP, but only this time
                */
                event = GF_EVENT_CHILD_DOWN;

                LOCK (&priv->lock);
                {
                        up_children = AFR_COUNT (priv->child_up, priv->child_count);
                        for (i = 0; i < priv->child_count; i++) {
                                if (priv->last_event[i] == GF_EVENT_CHILD_UP) {
                                        event = GF_EVENT_CHILD_UP;
                                        break;
                                }

                                if (priv->last_event[i] ==
                                                GF_EVENT_CHILD_CONNECTING) {
                                        event = GF_EVENT_CHILD_CONNECTING;
                                        /* continue to check other events for CHILD_UP */
                                }
                        }
                }
                UNLOCK (&priv->lock);
        }

        ret = 0;
        if (propagate)
                ret = default_notify (this, event, data);

        if (!had_heard_from_all && have_heard_from_all && priv->shd.iamshd) {
                /*
                 * Since self-heal is supposed to be launched only after
                 * the responses from all the bricks are collected,
                 * launch self-heals now on all up subvols.
                 */
                for (i = 0; i < priv->child_count; i++)
                        if (priv->child_up[i])
                                afr_selfheal_childup (this, i);
        } else if (have_heard_from_all && call_psh && priv->shd.iamshd) {
                /*
                 * Already heard from everyone. Just launch heal on now up
                 * subvolume.
                 */
                 afr_selfheal_childup (this, up_child);
	}
out:
        return ret;
}


int
afr_local_init (afr_local_t *local, afr_private_t *priv, int32_t *op_errno)
{
        local->op_ret = -1;
        local->op_errno = EUCLEAN;

	syncbarrier_init (&local->barrier);

        local->child_up = GF_CALLOC (priv->child_count,
                                     sizeof (*local->child_up),
                                     gf_afr_mt_char);
        if (!local->child_up) {
                if (op_errno)
                        *op_errno = ENOMEM;
                goto out;
        }

        memcpy (local->child_up, priv->child_up,
                sizeof (*local->child_up) * priv->child_count);
        local->call_count = AFR_COUNT (local->child_up, priv->child_count);
        if (local->call_count == 0) {
                gf_log (THIS->name, GF_LOG_INFO, "no subvolumes up");
                if (op_errno)
                        *op_errno = ENOTCONN;
                goto out;
        }
	local->event_generation = priv->event_generation;

	local->read_attempted = GF_CALLOC (priv->child_count, sizeof (char),
					   gf_afr_mt_char);
	if (!local->read_attempted) {
		if (op_errno)
			*op_errno = ENOMEM;
		goto out;
	}

	local->readable = GF_CALLOC (priv->child_count, sizeof (char),
				     gf_afr_mt_char);
	if (!local->readable) {
		if (op_errno)
			*op_errno = ENOMEM;
		goto out;
	}

	local->replies = GF_CALLOC(priv->child_count, sizeof(*local->replies),
				   gf_afr_mt_reply_t);
	if (!local->replies) {
		if (op_errno)
			*op_errno = ENOMEM;
		goto out;
	}

	return 0;
out:
        return -1;
}

int
afr_internal_lock_init (afr_internal_lock_t *lk, size_t child_count,
                        transaction_lk_type_t lk_type)
{
        int             ret = -ENOMEM;

        lk->locked_nodes = GF_CALLOC (sizeof (*lk->locked_nodes),
                                      child_count, gf_afr_mt_char);
        if (NULL == lk->locked_nodes)
                goto out;

        lk->lower_locked_nodes = GF_CALLOC (sizeof (*lk->lower_locked_nodes),
                                            child_count, gf_afr_mt_char);
        if (NULL == lk->lower_locked_nodes)
                goto out;

        lk->lock_op_ret   = -1;
        lk->lock_op_errno = EUCLEAN;
        lk->transaction_lk_type = lk_type;

        ret = 0;
out:
        return ret;
}

void
afr_matrix_cleanup (int32_t **matrix, unsigned int m)
{
        int             i         = 0;

        if (!matrix)
                goto out;
        for (i = 0; i < m; i++) {
                GF_FREE (matrix[i]);
        }

        GF_FREE (matrix);
out:
        return;
}

int32_t**
afr_matrix_create (unsigned int m, unsigned int n)
{
        int32_t         **matrix = NULL;
        int             i       = 0;

        matrix = GF_CALLOC (sizeof (*matrix), m, gf_afr_mt_int32_t);
        if (!matrix)
                goto out;

        for (i = 0; i < m; i++) {
                matrix[i] = GF_CALLOC (sizeof (*matrix[i]), n,
                                       gf_afr_mt_int32_t);
                if (!matrix[i])
                        goto out;
        }
        return matrix;
out:
        afr_matrix_cleanup (matrix, m);
        return NULL;
}

int
afr_inodelk_init (afr_inodelk_t *lk, char *dom, size_t child_count)
{
        int             ret = -ENOMEM;

        lk->domain = dom;
        lk->locked_nodes = GF_CALLOC (sizeof (*lk->locked_nodes),
                                      child_count, gf_afr_mt_char);
        if (NULL == lk->locked_nodes)
                goto out;
        ret = 0;
out:
        return ret;
}

int
afr_transaction_local_init (afr_local_t *local, xlator_t *this)
{
        int            child_up_count = 0;
        int            ret = -ENOMEM;
        afr_private_t *priv = NULL;

        priv = this->private;
        ret = afr_internal_lock_init (&local->internal_lock, priv->child_count,
                                      AFR_TRANSACTION_LK);
        if (ret < 0)
                goto out;

        if ((local->transaction.type == AFR_DATA_TRANSACTION) ||
            (local->transaction.type == AFR_METADATA_TRANSACTION)) {
                ret = afr_inodelk_init (&local->internal_lock.inodelk[0],
                                        this->name, priv->child_count);
                if (ret < 0)
                        goto out;
        }

        ret = -ENOMEM;
        child_up_count = AFR_COUNT (local->child_up, priv->child_count);
        if (priv->optimistic_change_log && child_up_count == priv->child_count)
                local->optimistic_change_log = 1;

	local->pre_op_compat = priv->pre_op_compat;

        local->transaction.eager_lock =
                GF_CALLOC (sizeof (*local->transaction.eager_lock),
                           priv->child_count,
                           gf_afr_mt_int32_t);

        if (!local->transaction.eager_lock)
                goto out;

        local->transaction.pre_op = GF_CALLOC (sizeof (*local->transaction.pre_op),
                                               priv->child_count,
                                               gf_afr_mt_char);
        if (!local->transaction.pre_op)
                goto out;

        local->transaction.fop_subvols = GF_CALLOC (sizeof (*local->transaction.fop_subvols),
						    priv->child_count,
						    gf_afr_mt_char);
        if (!local->transaction.fop_subvols)
                goto out;

        local->transaction.failed_subvols = GF_CALLOC (sizeof (*local->transaction.failed_subvols),
						       priv->child_count,
						       gf_afr_mt_char);
        if (!local->transaction.failed_subvols)
                goto out;

        local->pending = afr_matrix_create (priv->child_count,
                                            AFR_NUM_CHANGE_LOGS);
        if (!local->pending)
                goto out;

	INIT_LIST_HEAD (&local->transaction.eager_locked);

        ret = 0;
out:
        return ret;
}


void
afr_set_low_priority (call_frame_t *frame)
{
        frame->root->pid = LOW_PRIO_PROC_PID;
}


gf_boolean_t
afr_have_quorum (char *logname, afr_private_t *priv)
{
        unsigned int        quorum = 0;

        GF_VALIDATE_OR_GOTO(logname,priv,out);

        quorum = priv->quorum_count;
        if (quorum != AFR_QUORUM_AUTO) {
                return (priv->up_count >= (priv->down_count + quorum));
        }

        quorum = priv->child_count / 2 + 1;
        if (priv->up_count >= (priv->down_count + quorum)) {
                return _gf_true;
        }

        /*
         * Special case for even numbers of nodes: if we have exactly half
         * and that includes the first ("senior-most") node, then that counts
         * as quorum even if it wouldn't otherwise.  This supports e.g. N=2
         * while preserving the critical property that there can only be one
         * such group.
         */
        if ((priv->child_count % 2) == 0) {
                quorum = priv->child_count / 2;
                if (priv->up_count >= (priv->down_count + quorum)) {
                        if (priv->child_up[0]) {
                                return _gf_true;
                        }
                }
        }

out:
        return _gf_false;
}

void
afr_priv_destroy (afr_private_t *priv)
{
        int            i           = 0;

        if (!priv)
                goto out;
        inode_unref (priv->root_inode);
        GF_FREE (priv->last_event);
        if (priv->pending_key) {
                for (i = 0; i < priv->child_count; i++)
                        GF_FREE (priv->pending_key[i]);
        }
        GF_FREE (priv->pending_key);
        GF_FREE (priv->children);
        GF_FREE (priv->child_up);
        LOCK_DESTROY (&priv->lock);

        GF_FREE (priv);
out:
        return;
}

int
xlator_subvolume_count (xlator_t *this)
{
        int i = 0;
        xlator_list_t *list = NULL;

        for (list = this->children; list; list = list->next)
                i++;
        return i;
}


void
afr_handle_open_fd_count (call_frame_t *frame, xlator_t *this)
{
        afr_local_t     *local = NULL;
        afr_fd_ctx_t    *fd_ctx   = NULL;

        local = frame->local;

        if (!local->fd)
		return;

	fd_ctx = afr_fd_ctx_get (local->fd, this);
	if (!fd_ctx)
		return;

	fd_ctx->open_fd_count = local->open_fd_count;
}

int**
afr_mark_pending_changelog (afr_private_t *priv, unsigned char *pending,
                            dict_t *xattr, ia_type_t iat)
{
       int i = 0;
       int **changelog = NULL;
       int idx = -1;
       int m_idx = 0;
       int ret = 0;

       m_idx = afr_index_for_transaction_type (AFR_METADATA_TRANSACTION);

       idx = afr_index_from_ia_type (iat);

       changelog = afr_matrix_create (priv->child_count, AFR_NUM_CHANGE_LOGS);
       if (!changelog)
                goto out;

       for (i = 0; i < priv->child_count; i++) {
               if (!pending[i])
                       continue;

               changelog[i][m_idx] = hton32(1);
               if (idx != -1)
                       changelog[i][idx] = hton32(1);
       }
       ret = afr_set_pending_dict (priv, xattr, changelog);
       if (ret < 0) {
               afr_matrix_cleanup (changelog, priv->child_count);
               return NULL;
       }
out:
       return changelog;
}
