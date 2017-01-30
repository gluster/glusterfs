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
#include "events.h"
#include "upcall-utils.h"
#include "fd.h"

#include "afr-inode-read.h"
#include "afr-inode-write.h"
#include "afr-dir-read.h"
#include "afr-dir-write.h"
#include "afr-transaction.h"
#include "afr-self-heal.h"
#include "afr-self-heald.h"
#include "afr-messages.h"
#include "compound-fop-utils.h"

int32_t
afr_quorum_errno (afr_private_t *priv)
{
        if (priv->quorum_reads)
                return ENOTCONN;
        return EROFS;
}

int
afr_fav_child_reset_sink_xattrs (void *opaque);

int
afr_fav_child_reset_sink_xattrs_cbk (int ret, call_frame_t *frame,
                                     void *opaque);

gf_boolean_t
afr_is_consistent_io_possible (afr_local_t *local, afr_private_t *priv,
                               int32_t *op_errno)
{
        if (priv->consistent_io && local->call_count != priv->child_count) {
                gf_msg (THIS->name, GF_LOG_INFO, 0,
                        AFR_MSG_SUBVOLS_DOWN, "All subvolumes are not up");
                if (op_errno)
                        *op_errno = ENOTCONN;
                return _gf_false;
        }
        return _gf_true;
}

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

/* Check if an entry or inode could be undergoing a transaction. */
gf_boolean_t
afr_is_possibly_under_txn (afr_transaction_type type, afr_local_t *local,
                           xlator_t *this)
{
        int i = 0;
        int tmp = 0;
	afr_private_t *priv = NULL;
        GF_UNUSED char *key = NULL;

        priv = this->private;

        if (type == AFR_ENTRY_TRANSACTION)
                key = GLUSTERFS_PARENT_ENTRYLK;
        else if (type == AFR_DATA_TRANSACTION)
                /*FIXME: Use GLUSTERFS_INODELK_DOM_COUNT etc. once
                 * pl_inodelk_xattr_fill supports separate keys for different
                 * domains.*/
                key = GLUSTERFS_INODELK_COUNT;

        for (i = 0; i < priv->child_count; i++) {
		if (!local->replies[i].xdata)
			continue;
		if (dict_get_int32 (local->replies[i].xdata, key, &tmp) == 0)
			if (tmp)
				return _gf_true;
	}

        return _gf_false;
}

int
__afr_inode_ctx_get (xlator_t *this, inode_t *inode, afr_inode_ctx_t **ctx)
{
        uint64_t                ctx_int = 0;
        int                     ret     = -1;
        afr_inode_ctx_t        *tmp_ctx = NULL;

        ret = __inode_ctx_get (inode, this, &ctx_int);
        if (ret) {
                tmp_ctx = GF_CALLOC (1, sizeof (afr_inode_ctx_t),
                                     gf_afr_mt_inode_ctx_t);
                if (!tmp_ctx)
                        goto out;

                ctx_int = (long) tmp_ctx;
                ret = __inode_ctx_set (inode, this, &ctx_int);
                if (ret) {
                        GF_FREE (tmp_ctx);
                        goto out;
                }
                tmp_ctx->spb_choice = -1;
                tmp_ctx->read_subvol = 0;
        } else {
                tmp_ctx = (afr_inode_ctx_t *) ctx_int;
        }

        *ctx = tmp_ctx;
        ret = 0;
out:
        return ret;
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
__afr_set_in_flight_sb_status (xlator_t *this, afr_local_t *local,
                               inode_t *inode)
{
        int                 i               = 0;
        int                 ret             = -1;
        int                 txn_type        = 0;
        int                 count           = 0;
        int                 index           = -1;
        uint16_t            datamap_old     = 0;
        uint16_t            metadatamap_old = 0;
        uint16_t            datamap         = 0;
        uint16_t            metadatamap     = 0;
        uint16_t            tmp_map         = 0;
        uint16_t            mask            = 0;
        uint32_t            event           = 0;
        uint64_t            val             = 0;
        afr_private_t      *priv            = NULL;
        afr_inode_ctx_t    *ctx             = NULL;

        priv = this->private;
        txn_type = local->transaction.type;

        ret = __afr_inode_ctx_get (this, inode, &ctx);
        if (ret < 0)
                return ret;

        val = ctx->read_subvol;

        metadatamap_old = metadatamap = (val & 0x000000000000ffff);
        datamap_old = datamap = (val & 0x00000000ffff0000) >> 16;
        event = (val & 0xffffffff00000000) >> 32;

        if (txn_type == AFR_DATA_TRANSACTION)
                tmp_map = datamap;
        else if (txn_type == AFR_METADATA_TRANSACTION)
                tmp_map = metadatamap;

        count = gf_bits_count (tmp_map);

        if (count == 1)
                index = gf_bits_index (tmp_map);

        for (i = 0; i < priv->child_count; i++) {
                mask = 0;
                if (!local->transaction.failed_subvols[i])
                        continue;

                mask = 1 << i;
                if (txn_type == AFR_METADATA_TRANSACTION)
                        metadatamap &= ~mask;
                else if (txn_type == AFR_DATA_TRANSACTION)
                        datamap &= ~mask;
        }

        switch (txn_type) {
        case AFR_METADATA_TRANSACTION:
                if ((metadatamap_old != 0) && (metadatamap == 0) &&
                    (count == 1)) {
                        local->transaction.in_flight_sb_errno =
                                                local->replies[index].op_errno;
                        local->transaction.in_flight_sb = _gf_true;
                        metadatamap |= (1 << index);
                }
                if (metadatamap_old != metadatamap)
                        event = 0;
                break;

        case AFR_DATA_TRANSACTION:
                if ((datamap_old != 0) && (datamap == 0) && (count == 1)) {
                        local->transaction.in_flight_sb_errno =
                                                local->replies[index].op_errno;
                        local->transaction.in_flight_sb = _gf_true;
                        datamap |= (1 << index);
                }
                if (datamap_old != datamap)
                        event = 0;
                break;

        default:
                break;
        }

        val = ((uint64_t) metadatamap) |
                (((uint64_t) datamap) << 16) |
                (((uint64_t) event) << 32);

        ctx->read_subvol = val;

        return ret;
}

int
afr_set_in_flight_sb_status (xlator_t *this, afr_local_t *local, inode_t *inode)
{
        int            ret  = -1;
        afr_private_t *priv = NULL;

        priv = this->private;

        /* If this transaction saw no failures, then exit. */
        if (AFR_COUNT (local->transaction.failed_subvols,
                       priv->child_count) == 0)
                return 0;

        LOCK (&inode->lock);
        {
                ret = __afr_set_in_flight_sb_status (this, local, inode);
        }
        UNLOCK (&inode->lock);

        return ret;
}

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
        afr_inode_ctx_t *ctx = NULL;

	priv = this->private;

	ret = __afr_inode_ctx_get (this, inode, &ctx);
	if (ret < 0)
		return ret;

        val = ctx->read_subvol;

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
        int ret = -1;
        afr_inode_ctx_t *ctx = NULL;

	priv = this->private;

        ret = __afr_inode_ctx_get (this, inode, &ctx);
        if (ret)
                goto out;

	for (i = 0; i < priv->child_count; i++) {
		if (data[i])
			datamap |= (1 << i);
		if (metadata[i])
			metadatamap |= (1 << i);
	}

	val = ((uint64_t) metadatamap) |
		(((uint64_t) datamap) << 16) |
		(((uint64_t) event) << 32);

        ctx->read_subvol = val;

        ret = 0;
out:
        return ret;
}

int
__afr_inode_event_gen_reset_small (inode_t *inode, xlator_t *this)
{
	int               ret         = -1;
	uint16_t          datamap     = 0;
	uint16_t          metadatamap = 0;
	uint32_t          event       = 0;
	uint64_t          val         = 0;
        afr_inode_ctx_t  *ctx         = NULL;

	ret = __afr_inode_ctx_get (this, inode, &ctx);
        if (ret)
                return ret;

        val = ctx->read_subvol;

	metadatamap = (val & 0x000000000000ffff) >> 0;
	datamap =     (val & 0x00000000ffff0000) >> 16;
	event = 0;

	val = ((uint64_t) metadatamap) |
		(((uint64_t) datamap) << 16) |
		(((uint64_t) event) << 32);

        ctx->read_subvol = val;

        return ret;
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
__afr_inode_split_brain_choice_get (inode_t *inode, xlator_t *this,
			            int *spb_choice)
{
        afr_inode_ctx_t *ctx = NULL;
        int ret = -1;

        ret = __afr_inode_ctx_get (this, inode, &ctx);
        if (ret < 0)
                return ret;

        *spb_choice = ctx->spb_choice;
        return 0;
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
__afr_inode_split_brain_choice_set (inode_t *inode, xlator_t *this,
                                    int spb_choice)
{
        afr_inode_ctx_t *ctx = NULL;
	int ret = -1;

	ret = __afr_inode_ctx_get (this, inode, &ctx);
        if (ret)
                goto out;

        ctx->spb_choice = spb_choice;

        ret = 0;
out:
        return ret;
}

int
__afr_inode_event_gen_reset (inode_t *inode, xlator_t *this)
{
	afr_private_t *priv = NULL;
	int ret = -1;

	priv = this->private;

	if (priv->child_count <= 16)
		ret = __afr_inode_event_gen_reset_small (inode, this);
	else
		ret = -1;

	return ret;
}


int
afr_inode_read_subvol_get (inode_t *inode, xlator_t *this, unsigned char *data,
			   unsigned char *metadata, int *event_p)
{
	int ret = -1;

        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        LOCK(&inode->lock);
	{
		ret = __afr_inode_read_subvol_get (inode, this, data,
						   metadata, event_p);
	}
	UNLOCK(&inode->lock);
out:
        return ret;
}

int
afr_inode_get_readable (call_frame_t *frame, inode_t *inode, xlator_t *this,
                       unsigned char *readable, int *event_p, int type)
{

        afr_private_t *priv = this->private;
        afr_local_t *local = frame->local;
        unsigned char *data = alloca0 (priv->child_count);
        unsigned char *metadata = alloca0 (priv->child_count);
        int data_count = 0;
        int metadata_count = 0;
        int event_generation = 0;
        int ret = 0;

        ret = afr_inode_read_subvol_get (inode, this, data, metadata,
                                         &event_generation);
        if (ret == -1)
                return -EIO;

        data_count = AFR_COUNT (data, priv->child_count);
        metadata_count = AFR_COUNT (metadata, priv->child_count);

        if (inode->ia_type == IA_IFDIR) {
                /* For directories, allow even if it is in data split-brain. */
                if (type == AFR_METADATA_TRANSACTION ||
                    local->op == GF_FOP_STAT || local->op == GF_FOP_FSTAT) {
                        if (!metadata_count)
                                return -EIO;
                }
        } else {
                /* For files, abort in case of data/metadata split-brain. */
                if (!data_count || !metadata_count)
                        return -EIO;
        }

        if (type == AFR_METADATA_TRANSACTION && readable)
                memcpy (readable, metadata, priv->child_count * sizeof *metadata);
        if (type == AFR_DATA_TRANSACTION && readable) {
                if (!data_count)
                        memcpy (readable, local->child_up,
                                priv->child_count * sizeof *readable);
                else
                        memcpy (readable, data, priv->child_count * sizeof *data);
        }
        if (event_p)
                *event_p = event_generation;
        return 0;
}

int
afr_inode_split_brain_choice_get (inode_t *inode, xlator_t *this,
                                  int *spb_choice)
{
	int ret = -1;

        GF_VALIDATE_OR_GOTO (this->name, inode, out);

	LOCK(&inode->lock);
	{
		ret = __afr_inode_split_brain_choice_get (inode, this,
                                                          spb_choice);
	}
	UNLOCK(&inode->lock);
out:
        return ret;
}


int
afr_inode_read_subvol_set (inode_t *inode, xlator_t *this, unsigned char *data,
			   unsigned char *metadata, int event)
{
	int ret = -1;

        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        LOCK(&inode->lock);
	{
		ret = __afr_inode_read_subvol_set (inode, this, data, metadata,
						   event);
	}
	UNLOCK(&inode->lock);
out:
        return ret;
}


int
afr_inode_split_brain_choice_set (inode_t *inode, xlator_t *this,
                                  int spb_choice)
{
	int ret = -1;

        GF_VALIDATE_OR_GOTO (this->name, inode, out);

	LOCK(&inode->lock);
	{
		ret = __afr_inode_split_brain_choice_set (inode, this,
                                                          spb_choice);
	}
	UNLOCK(&inode->lock);
out:
	return ret;
}


/* The caller of this should perform afr_inode_refresh, if this function
 * returns _gf_true
 */
gf_boolean_t
afr_is_inode_refresh_reqd (inode_t *inode, xlator_t *this,
                           int event_gen1, int event_gen2)
{
        gf_boolean_t     need_refresh = _gf_false;
        afr_inode_ctx_t  *ctx         = NULL;
        int              ret          = -1;

        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        LOCK(&inode->lock);
        {
                ret = __afr_inode_ctx_get (this, inode, &ctx);
                if (ret)
                        goto unlock;

                need_refresh = ctx->need_refresh;
                /* Hoping that the caller will do inode_refresh followed by
                 * this, hence setting the need_refresh to false */
                ctx->need_refresh = _gf_false;
        }
unlock:
        UNLOCK(&inode->lock);

        if (event_gen1 != event_gen2)
                need_refresh = _gf_true;
out:
        return need_refresh;
}


static int
afr_inode_need_refresh_set (inode_t *inode, xlator_t *this)
{
        int               ret         = -1;
        afr_inode_ctx_t  *ctx         = NULL;

        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        LOCK(&inode->lock);
        {
                ret = __afr_inode_ctx_get (this, inode, &ctx);
                if (ret)
                        goto unlock;

                ctx->need_refresh = _gf_true;
        }
unlock:
        UNLOCK(&inode->lock);
out:
        return ret;
}

int
afr_inode_event_gen_reset (inode_t *inode, xlator_t *this)
{
	int ret = -1;

        GF_VALIDATE_OR_GOTO (this->name, inode, out);

        LOCK(&inode->lock);
        {
                ret = __afr_inode_event_gen_reset (inode, this);
        }
        UNLOCK(&inode->lock);
out:
	return ret;
}

int
afr_spb_choice_timeout_cancel (xlator_t *this, inode_t *inode)
{
        afr_inode_ctx_t *ctx    = NULL;
        int              ret    = -1;

        if (!inode)
                return ret;

        LOCK(&inode->lock);
        {
                ret = __afr_inode_ctx_get (this, inode, &ctx);
                if (ret < 0 || !ctx) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                AFR_MSG_SPLIT_BRAIN_CHOICE_ERROR,
                                "Failed to cancel split-brain choice timer.");
                        goto out;
                }
                ctx->spb_choice = -1;
                if (ctx->timer) {
                        gf_timer_call_cancel (this->ctx, ctx->timer);
                        ctx->timer = NULL;
                }
                ret = 0;
        }
out:
        UNLOCK(&inode->lock);
        return ret;
}

void
afr_set_split_brain_choice_cbk (void *data)
{
        inode_t      *inode     = data;
        xlator_t     *this      = THIS;

        afr_spb_choice_timeout_cancel (this, inode);
        inode_unref (inode);
        return;
}


int
afr_set_split_brain_choice (int ret, call_frame_t *frame, void *opaque)
{
        int                 op_errno         = ENOMEM;
        afr_private_t      *priv             = NULL;
        afr_inode_ctx_t    *ctx              = NULL;
        inode_t            *inode            = NULL;
        loc_t              *loc              = NULL;
        xlator_t           *this             = NULL;
        afr_spbc_timeout_t *data             = opaque;
        struct              timespec delta   = {0, };
        gf_boolean_t        timer_set        = _gf_false;
        gf_boolean_t        timer_cancelled  = _gf_false;
        gf_boolean_t        timer_reset      = _gf_false;
        int                 old_spb_choice   = -1;

        frame = data->frame;
        loc = data->loc;
        this = frame->this;
        priv = this->private;

        if (ret) {
                op_errno = -ret;
                ret = -1;
                goto out;
        }

        delta.tv_sec = priv->spb_choice_timeout;
        delta.tv_nsec = 0;

        if (!loc->inode) {
                ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        if (!(data->d_spb || data->m_spb)) {
                gf_msg (this->name, GF_LOG_WARNING, 0,
                        AFR_MSG_SPLIT_BRAIN_CHOICE_ERROR, "Cannot set "
                        "replica.split-brain-choice on %s. File is"
                        " not in data/metadata split-brain.",
                        uuid_utoa (loc->gfid));
                ret = -1;
                op_errno = EINVAL;
                goto out;
        }

        /*
         * we're ref'ing the inode before LOCK like it is done elsewhere in the
         * code. If we ref after LOCK, coverity complains of possible deadlocks.
         */
        inode = inode_ref (loc->inode);

        LOCK(&inode->lock);
        {
                ret = __afr_inode_ctx_get (this, inode, &ctx);
                if (ret) {
                        gf_msg (this->name, GF_LOG_ERROR, 0,
                                AFR_MSG_SPLIT_BRAIN_CHOICE_ERROR,
                                "Failed to get inode_ctx for %s", loc->name);
                        goto unlock;
                }

                old_spb_choice = ctx->spb_choice;
                ctx->spb_choice = data->spb_child_index;

                /* Possible changes in spb-choice :
                 *         valid to -1    : cancel timer and unref
                 *         valid to valid : cancel timer and inject new one
                 *         -1    to -1    : unref and do not do anything
                 *         -1 to valid    : inject timer
                 */

                /* ctx->timer is NULL iff previous value of
                 * ctx->spb_choice is -1
                 */
                if (ctx->timer) {
                        if (ctx->spb_choice == -1) {
                                if (!gf_timer_call_cancel (this->ctx,
                                                            ctx->timer)) {
                                        ctx->timer = NULL;
                                        timer_cancelled = _gf_true;
                                }
                                /* If timer cancel failed here it means that the
                                *  previous cbk will be executed which will set
                                *  spb_choice to -1. So we can consider the
                                *  'valid to -1' case to be a sucess
                                *  (i.e. ret = 0) and goto unlock.
                                */
                                goto unlock;
                        }
                        goto reset_timer;
                } else {
                        if (ctx->spb_choice == -1)
                                goto unlock;
                        goto set_timer;
                }

reset_timer:
                ret = gf_timer_call_cancel (this->ctx, ctx->timer);
                if (ret != 0) {
                        /* We need to bail out now instead of launching a new
                         * timer. Otherwise the cbk of the previous timer event
                         * will cancel the new ctx->timer.
                         */
                        ctx->spb_choice = old_spb_choice;
                        ret = -1;
                        op_errno = EAGAIN;
                        goto unlock;
                }
                ctx->timer = NULL;
                timer_reset = _gf_true;

set_timer:
                ctx->timer = gf_timer_call_after (this->ctx, delta,
                                                  afr_set_split_brain_choice_cbk,
                                                  inode);
                if (!ctx->timer) {
                        ctx->spb_choice = old_spb_choice;
                        ret = -1;
                        op_errno = ENOMEM;
                }
                if (!timer_reset && ctx->timer)
                        timer_set = _gf_true;
                if (timer_reset && !ctx->timer)
                        timer_cancelled = _gf_true;
        }
unlock:
        UNLOCK(&inode->lock);
        if (!timer_set)
                inode_unref (inode);
        if (timer_cancelled)
                inode_unref (inode);
        /*
         * We need to invalidate the inode to prevent the kernel from serving
         * reads from an older cached value despite a change in spb_choice to
         * a new value.
         */
        inode_invalidate (inode);
out:
        if (data)
                GF_FREE (data);
        AFR_STACK_UNWIND (setxattr, frame, ret, op_errno, NULL);
        return 0;
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
                if (replies[i].valid && replies[i].xdata &&
                    dict_get (replies[i].xdata, GLUSTERFS_BAD_INODE))
                        continue;
		if (data_accused[i])
			continue;
		if (replies[i].poststat.ia_size > maxsize)
			maxsize = replies[i].poststat.ia_size;
	}

	for (i = 0; i < priv->child_count; i++) {
		if (data_accused[i])
			continue;
                if (AFR_IS_ARBITER_BRICK(priv, i))
                        continue;
		if (replies[i].poststat.ia_size < maxsize)
			data_accused[i] = 1;
	}

	return 0;
}

int
afr_replies_interpret (call_frame_t *frame, xlator_t *this, inode_t *inode,
                       gf_boolean_t *start_heal)
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
        if (AFR_IS_ARBITER_BRICK (priv, ARBITER_BRICK_INDEX)) {
                data_readable[ARBITER_BRICK_INDEX] =  0;
                metadata_readable[ARBITER_BRICK_INDEX] = 0;
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

                if (replies[i].xdata &&
                    dict_get (replies[i].xdata, GLUSTERFS_BAD_INODE)) {
			data_readable[i] = 0;
			metadata_readable[i] = 0;
			continue;
                }

		afr_accused_fill (this, replies[i].xdata, data_accused,
				  (replies[i].poststat.ia_type == IA_IFDIR) ?
				   AFR_ENTRY_TRANSACTION : AFR_DATA_TRANSACTION);

		afr_accused_fill (this, replies[i].xdata,
				  metadata_accused, AFR_METADATA_TRANSACTION);

	}

	if ((inode->ia_type != IA_IFDIR) &&
            /* We want to accuse small files only when we know for sure that
             * there is no IO happening. Otherwise, the ia_sizes obtained in
             * post-refresh replies may  mismatch due to a race between inode-
             * refresh and ongoing writes, causing spurious heal launches*/
            !afr_is_possibly_under_txn (AFR_DATA_TRANSACTION, local, this))
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

	for (i = 0; i < priv->child_count; i++) {
                if (start_heal && priv->child_up[i] &&
                    (data_accused[i] || metadata_accused[i])) {
                        *start_heal = _gf_true;
                        break;
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

gf_boolean_t
afr_selfheal_enabled (xlator_t *this)
{
	afr_private_t *priv = NULL;
	gf_boolean_t data = _gf_false;
        int ret = 0;

	priv = this->private;

	ret = gf_string2boolean (priv->data_self_heal, &data);
        GF_ASSERT (!ret);

	return data || priv->metadata_self_heal || priv->entry_self_heal;
}


int
afr_txn_refresh_done (call_frame_t *frame, xlator_t *this, int err)
{

        call_frame_t *heal_frame = NULL;
        afr_local_t *heal_local = NULL;
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        inode_t *inode = NULL;
        int event_generation = 0;
        int read_subvol = -1;
        int op_errno = ENOMEM;
        int ret = 0;

        local = frame->local;
        inode = local->inode;
        priv = this->private;

        if (err)
                goto refresh_done;

        if (local->op == GF_FOP_LOOKUP)
                goto refresh_done;

        ret = afr_inode_get_readable (frame, inode, this, local->readable,
                                      &event_generation,
                                      local->transaction.type);

        if (ret == -EIO || (local->is_read_txn && !event_generation)) {
                /* No readable subvolume even after refresh ==> splitbrain.*/
                if (!priv->fav_child_policy) {
                        err = -EIO;
                        goto refresh_done;
                }
                read_subvol = afr_sh_get_fav_by_policy (this, local->replies,
                                                        inode, NULL);
                if (read_subvol == -1) {
                        err = -EIO;
                        goto refresh_done;
                }

                heal_frame = copy_frame (frame);
                if (!heal_frame) {
                        err = -EIO;
                        goto refresh_done;
                }
                heal_frame->root->pid = GF_CLIENT_PID_SELF_HEALD;
                heal_local = AFR_FRAME_INIT (heal_frame, op_errno);
                if (!heal_local) {
                        err = -EIO;
                        AFR_STACK_DESTROY (heal_frame);
                        goto refresh_done;
                }
                heal_local->xdata_req = dict_new();
                if (!heal_local->xdata_req) {
                        err = -EIO;
                        AFR_STACK_DESTROY (heal_frame);
                        goto refresh_done;
                }
                heal_local->heal_frame = frame;
                ret = synctask_new (this->ctx->env,
                                    afr_fav_child_reset_sink_xattrs,
                                    afr_fav_child_reset_sink_xattrs_cbk,
                                    heal_frame,
                                    heal_frame);
                return 0;
        }

refresh_done:
        afr_local_replies_wipe (local, this->private);
        local->refreshfn (frame, this, err);

        return 0;
}

int
afr_inode_refresh_done (call_frame_t *frame, xlator_t *this)
{
	call_frame_t *heal_frame = NULL;
	afr_local_t *local = NULL;
        gf_boolean_t start_heal = _gf_false;
        afr_local_t *heal_local = NULL;
        int op_errno = ENOMEM;
	int ret = 0;
	int err = 0;

	local = frame->local;

	ret = afr_replies_interpret (frame, this, local->refreshinode,
                                     &start_heal);

	err = afr_inode_refresh_err (frame, this);

	if (ret && afr_selfheal_enabled (this) && start_heal) {
                heal_frame = copy_frame (frame);
                if (!heal_frame)
                        goto refresh_done;
                heal_frame->root->pid = GF_CLIENT_PID_SELF_HEALD;
                heal_local = AFR_FRAME_INIT (heal_frame, op_errno);
                if (!heal_local) {
                        AFR_STACK_DESTROY (heal_frame);
                        goto refresh_done;
                }
                heal_local->refreshinode = inode_ref (local->refreshinode);
                heal_local->heal_frame = heal_frame;
                if (!afr_throttled_selfheal (heal_frame, this)) {
                        AFR_STACK_DESTROY (heal_frame);
                        goto refresh_done;
                }
        }

refresh_done:
        afr_txn_refresh_done (frame, this, err);

	return 0;
}

void
afr_inode_refresh_subvol_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                              int op_ret, int op_errno, struct iatt *buf,
                              dict_t *xdata, struct iatt *par)
{
        afr_local_t *local = NULL;
        int call_child = (long) cookie;
        int8_t need_heal = 1;
        int call_count = 0;
        GF_UNUSED int ret = 0;

        local = frame->local;
        local->replies[call_child].valid = 1;
        local->replies[call_child].op_ret = op_ret;
        local->replies[call_child].op_errno = op_errno;
	if (op_ret != -1) {
		local->replies[call_child].poststat = *buf;
		if (par)
                        local->replies[call_child].postparent = *par;
                if (xdata)
		        local->replies[call_child].xdata = dict_ref (xdata);
	}
        if (xdata) {
                ret = dict_get_int8 (xdata, "link-count", &need_heal);
                local->replies[call_child].need_heal = need_heal;
        } else {
                local->replies[call_child].need_heal = need_heal;
        }

        call_count = afr_frame_return (frame);
        if (call_count == 0) {
                afr_set_need_heal (this, local);
		afr_inode_refresh_done (frame, this);
        }

}

int
afr_inode_refresh_subvol_with_lookup_cbk (call_frame_t *frame, void *cookie,
                                          xlator_t *this, int op_ret,
                                          int op_errno, inode_t *inode,
                                          struct iatt *buf, dict_t *xdata,
                                          struct iatt *par)
{
        afr_inode_refresh_subvol_cbk (frame, cookie, this, op_ret, op_errno,
                                      buf, xdata, par);
	return 0;
}


int
afr_inode_refresh_subvol_with_lookup (call_frame_t *frame, xlator_t *this,
                                      int i, inode_t *inode, uuid_t gfid,
                                      dict_t *xdata)
{
	loc_t loc = {0, };
	afr_private_t *priv = NULL;

	priv = this->private;

	loc.inode = inode;
        if (gf_uuid_is_null (inode->gfid) && gfid) {
                /* To handle setattr/setxattr on yet to be linked inode from
                 * dht */
                gf_uuid_copy (loc.gfid, gfid);
        } else {
                gf_uuid_copy (loc.gfid, inode->gfid);
        }

	STACK_WIND_COOKIE (frame, afr_inode_refresh_subvol_with_lookup_cbk,
			   (void *) (long) i, priv->children[i],
			   priv->children[i]->fops->lookup, &loc, xdata);
	return 0;
}

int
afr_inode_refresh_subvol_with_fstat_cbk (call_frame_t *frame,
                                         void *cookie, xlator_t *this,
                                         int32_t op_ret, int32_t op_errno,
                                         struct iatt *buf, dict_t *xdata)
{
        afr_inode_refresh_subvol_cbk (frame, cookie, this, op_ret, op_errno,
                                      buf, xdata, NULL);
        return 0;
}

int
afr_inode_refresh_subvol_with_fstat (call_frame_t *frame, xlator_t *this, int i,
			             dict_t *xdata)
{
        afr_private_t *priv = NULL;
        afr_local_t *local = NULL;

        priv = this->private;
        local = frame->local;

        STACK_WIND_COOKIE (frame, afr_inode_refresh_subvol_with_fstat_cbk,
                           (void *) (long) i, priv->children[i],
                           priv->children[i]->fops->fstat, local->fd, xdata);
        return 0;
}

int
afr_inode_refresh_do (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int call_count = 0;
	int i = 0;
        int ret = 0;
	dict_t *xdata = NULL;
        afr_fd_ctx_t  *fd_ctx = NULL;
        unsigned char *wind_subvols = NULL;

	priv = this->private;
	local = frame->local;
        wind_subvols = alloca0 (priv->child_count);

        afr_local_replies_wipe (local, priv);

        if (local->fd) {
                fd_ctx = afr_fd_ctx_get (local->fd, this);
                if (!fd_ctx) {
                        afr_inode_refresh_done (frame, this);
                        return 0;
                }
        }

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

        ret = dict_set_str (xdata, "link-count", GF_XATTROP_INDEX_COUNT);
        if (ret) {
                gf_msg_debug (this->name, -ret,
                              "Unable to set link-count in dict ");
        }

        ret = dict_set_str (xdata, GLUSTERFS_INODELK_DOM_COUNT, this->name);
        if (ret) {
                gf_msg_debug (this->name, -ret,
                              "Unable to set inodelk-dom-count in dict ");

        }

        if (local->fd) {
                for (i = 0; i < priv->child_count; i++) {
                        if (local->child_up[i] &&
                            fd_ctx->opened_on[i] == AFR_FD_OPENED)
                                wind_subvols[i] = 1;
                }
        } else {
                memcpy (wind_subvols, local->child_up,
                        sizeof (*local->child_up) * priv->child_count);
        }

	local->call_count = AFR_COUNT (wind_subvols, priv->child_count);

	call_count = local->call_count;
        if (!call_count) {
                dict_unref (xdata);
                afr_inode_refresh_done (frame, this);
                return 0;
        }
	for (i = 0; i < priv->child_count; i++) {
		if (!wind_subvols[i])
			continue;

                if (local->fd)
                        afr_inode_refresh_subvol_with_fstat (frame, this, i,
                                                             xdata);
                else
                        afr_inode_refresh_subvol_with_lookup (frame, this, i,
                                                    local->refreshinode,
                                                    local->refreshgfid, xdata);

		if (!--call_count)
			break;
	}

	dict_unref (xdata);

	return 0;
}


int
afr_inode_refresh (call_frame_t *frame, xlator_t *this, inode_t *inode,
                   uuid_t gfid, afr_inode_refresh_cbk_t refreshfn)
{
	afr_local_t *local = NULL;

	local = frame->local;

	local->refreshfn = refreshfn;

	if (local->refreshinode) {
		inode_unref (local->refreshinode);
		local->refreshinode = NULL;
	}

	local->refreshinode = inode_ref (inode);

        if (gfid)
                gf_uuid_copy (local->refreshgfid, gfid);
        else
                gf_uuid_clear (local->refreshgfid);

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
                        gf_msg (this->name, GF_LOG_WARNING,
                                -ret, AFR_MSG_DICT_SET_FAILED,
                                "Unable to set dict value for %s",
                                priv->pending_key[i]);
                /* 3 = data+metadata+entry */
        }
        ret = dict_set_uint64 (xattr_req, AFR_DIRTY,
			       AFR_NUM_CHANGE_LOGS * sizeof(int));
        if (ret) {
                gf_msg_debug (this->name, -ret, "failed to set dirty "
                              "query flag");
        }

        ret = dict_set_int32 (xattr_req, "list-xattr", 1);
        if (ret) {
                gf_msg_debug (this->name, -ret,
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

        if (xattr_req && (xattr_req != local->xattr_req))
                dict_copy (xattr_req, local->xattr_req);

        ret = afr_xattr_req_prepare (this, local->xattr_req);

        ret = dict_set_uint64 (local->xattr_req, GLUSTERFS_INODELK_COUNT, 0);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING,
                        -ret, AFR_MSG_DICT_SET_FAILED,
                        "%s: Unable to set dict value for %s",
                        loc->path, GLUSTERFS_INODELK_COUNT);
        }
        ret = dict_set_uint64 (local->xattr_req, GLUSTERFS_ENTRYLK_COUNT, 0);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING,
                        -ret, AFR_MSG_DICT_SET_FAILED,
                        "%s: Unable to set dict value for %s",
                        loc->path, GLUSTERFS_ENTRYLK_COUNT);
        }

        ret = dict_set_uint32 (local->xattr_req, GLUSTERFS_PARENT_ENTRYLK, 0);
        if (ret < 0) {
                gf_msg (this->name, GF_LOG_WARNING,
                        -ret, AFR_MSG_DICT_SET_FAILED,
                        "%s: Unable to set dict value for %s",
                        loc->path, GLUSTERFS_PARENT_ENTRYLK);
        }

        ret = dict_set_str (local->xattr_req, "link-count",
                            GF_XATTROP_INDEX_COUNT);
        if (ret) {
                gf_msg_debug (this->name, -ret,
                              "Unable to set link-count in dict ");
        }

        ret = 0;
out:
        return ret;
}


int
afr_hash_child (afr_read_subvol_args_t *args, int32_t child_count, int hashmode)
{
        uuid_t gfid_copy = {0,};
        pid_t pid;

        if (!hashmode) {
                return -1;
        }

        gf_uuid_copy (gfid_copy, args->gfid);

        if ((hashmode > 1) && (args->ia_type != IA_IFDIR)) {
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
				  unsigned char *readable,
                                  afr_read_subvol_args_t *args)
{
	int             i           = 0;
	int             read_subvol = -1;
	afr_private_t  *priv        = NULL;
        afr_read_subvol_args_t local_args = {0,};

	priv = this->private;

	/* first preference - explicitly specified or local subvolume */
	if (priv->read_child >= 0 && readable[priv->read_child])
                return priv->read_child;

        if (inode_is_linked (inode)) {
                gf_uuid_copy (local_args.gfid, inode->gfid);
                local_args.ia_type = inode->ia_type;
        } else if (args) {
                local_args = *args;
        }

	/* second preference - use hashed mode */
	read_subvol = afr_hash_child (&local_args, priv->child_count,
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
                     unsigned char *readables,
		     int *event_p, afr_transaction_type type,
                     afr_read_subvol_args_t *args)
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
							   intersection, args);
	else
		subvol = afr_read_subvol_select_by_policy (inode, this,
							   readable, args);
	if (subvol_p)
		*subvol_p = subvol;
	if (event_p)
		*event_p = event;
        if (readables)
                memcpy (readables, readable,
                        sizeof (*readables) * priv->child_count);
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

        GF_FREE (local->transaction.pre_op_sources);
        if (local->transaction.pre_op_xdata) {
                for (i = 0; i < priv->child_count; i++) {
                        if (!local->transaction.pre_op_xdata[i])
                                continue;
                        dict_unref (local->transaction.pre_op_xdata[i]);
                }
                GF_FREE (local->transaction.pre_op_xdata);
        }

        GF_FREE (local->transaction.eager_lock);
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

                if (replies[i].xattr) {
                        dict_unref (replies[i].xattr);
                        replies[i].xattr = NULL;
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

static gf_boolean_t
afr_fop_lock_is_unlock (call_frame_t *frame)
{
        afr_local_t *local = frame->local;
        switch (local->op) {
        case GF_FOP_INODELK:
        case GF_FOP_FINODELK:
                if ((F_UNLCK == local->cont.inodelk.in_flock.l_type) &&
                   (local->cont.inodelk.in_cmd == F_SETLKW ||
                    local->cont.inodelk.in_cmd == F_SETLK))
                        return _gf_true;
                break;
        case GF_FOP_ENTRYLK:
        case GF_FOP_FENTRYLK:
                if (ENTRYLK_UNLOCK == local->cont.entrylk.in_cmd)
                        return _gf_true;
                break;
        default:
                return _gf_false;
        }
        return _gf_false;
}

static gf_boolean_t
afr_lk_is_unlock (int32_t cmd, struct gf_flock *flock)
{
        switch (cmd) {
        case F_RESLK_UNLCK:
                return _gf_true;
                break;

#if F_SETLKW != F_SETLKW64
        case F_SETLKW64:
#endif
        case F_SETLKW:

#if F_SETLK != F_SETLK64
        case F_SETLK64:
#endif
        case F_SETLK:
                if (F_UNLCK == flock->l_type)
                        return _gf_true;
                break;
        default:
                return _gf_false;
        }
        return _gf_false;
}

void
afr_handle_inconsistent_fop (call_frame_t *frame, int32_t *op_ret,
                             int32_t *op_errno)
{
        afr_private_t *priv = NULL;
        afr_local_t   *local = NULL;

        if (!frame || !frame->this || !frame->local || !frame->this->private)
                return;

        if (*op_ret < 0)
                return;

        /* Failing inodelk/entrylk/lk here is not a good idea because we
         * need to cleanup the locks on the other bricks if we choose to fail
         * the fop here. The brick may go down just after unwind happens as well
         * so anyways the fop will fail when the next fop is sent so leaving
         * it like this for now.*/
        local = frame->local;
        switch (local->op) {
        case GF_FOP_LOOKUP:
        case GF_FOP_INODELK:
        case GF_FOP_FINODELK:
        case GF_FOP_ENTRYLK:
        case GF_FOP_FENTRYLK:
        case GF_FOP_LK:
                return;
        default:
                break;
        }

        priv = frame->this->private;
        if (!priv->consistent_io)
                return;

        if (local->event_generation &&
            (local->event_generation != priv->event_generation))
                goto inconsistent;

        return;
inconsistent:
        *op_ret = -1;
        *op_errno = ENOTCONN;
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

        if (local->xattr_rsp)
                dict_unref (local->xattr_rsp);

        if (local->dict)
                dict_unref (local->dict);

        afr_local_replies_wipe (local, priv);
	GF_FREE(local->replies);

        GF_FREE (local->child_up);

        GF_FREE (local->read_attempted);

        GF_FREE (local->readable);
        GF_FREE (local->readable2);

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

        { /* inodelk */
                GF_FREE (local->cont.inodelk.volume);
                if (local->cont.inodelk.xdata)
                        dict_unref (local->cont.inodelk.xdata);
        }

        { /* entrylk */
                GF_FREE (local->cont.entrylk.volume);
                GF_FREE (local->cont.entrylk.basename);
                if (local->cont.entrylk.xdata)
                        dict_unref (local->cont.entrylk.xdata);
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

static char *afr_ignore_xattrs[] = {
        GF_SELINUX_XATTR_KEY,
        QUOTA_SIZE_KEY,
        NULL
};

gf_boolean_t
afr_is_xattr_ignorable (char *key)
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

static gf_boolean_t
afr_xattr_match_needed (dict_t *this, char *key1, data_t *value1, void *data)
{
        /* Ignore all non-disk (i.e. virtual) xattrs right away. */
        if (!gf_is_valid_xattr_namespace (key1))
                return _gf_false;

        /* Ignore on-disk xattrs that AFR doesn't need to heal. */
        if (!afr_is_xattr_ignorable (key1))
                return _gf_true;

        return _gf_false;
}

gf_boolean_t
afr_xattrs_are_equal (dict_t *dict1, dict_t *dict2)
{
        return are_dicts_equal (dict1, dict2, afr_xattr_match_needed, NULL);
}

static int
afr_get_parent_read_subvol (xlator_t *this, inode_t *parent,
                            struct afr_reply *replies, unsigned char *readable)
{
        int             i                    = 0;
        int             par_read_subvol      = -1;
        int             par_read_subvol_iter = -1;
        afr_private_t  *priv                 = NULL;

        priv = this->private;

        if (parent)
                par_read_subvol = afr_data_subvol_get (parent, this, NULL, NULL,
                                                       NULL, NULL);

        for (i = 0; i < priv->child_count; i++) {
                if (!replies[i].valid)
                        continue;

                if (replies[i].op_ret < 0)
                        continue;

                if (par_read_subvol_iter == -1) {
                        par_read_subvol_iter = i;
                        continue;
                }

                if ((par_read_subvol_iter != par_read_subvol) && readable[i])
                        par_read_subvol_iter = i;

                if (i == par_read_subvol)
                        par_read_subvol_iter = i;
        }
        /* At the end of the for-loop, the only reason why @par_read_subvol_iter
         * could be -1 is when this LOOKUP has failed on all sub-volumes.
         * So it is okay to send an arbitrary subvolume (0 in this case)
         * as parent read subvol.
         */
        if (par_read_subvol_iter == -1)
                par_read_subvol_iter = 0;

        return par_read_subvol_iter;

}

int
afr_read_subvol_decide (inode_t *inode, xlator_t *this,
                        afr_read_subvol_args_t *args)
{
        int data_subvol  = -1;
        int mdata_subvol = -1;

        data_subvol = afr_data_subvol_get (inode, this, NULL, NULL, NULL, args);
        mdata_subvol = afr_metadata_subvol_get (inode, this,
                                                NULL, NULL, NULL, args);
        if (data_subvol == -1 || mdata_subvol == -1)
                return -1;

        return data_subvol;
}

static inline int
afr_first_up_child (call_frame_t *frame, xlator_t *this)
{
        afr_private_t       *priv  = NULL;
        afr_local_t         *local = NULL;
        int                  i     = 0;

        local = frame->local;
        priv = this->private;

        for (i = 0; i < priv->child_count; i++)
                if (local->replies[i].valid &&
                    local->replies[i].op_ret == 0)
                        return i;
        return 0;
}

static void
afr_lookup_done (call_frame_t *frame, xlator_t *this)
{
        afr_private_t       *priv  = NULL;
        afr_local_t         *local = NULL;
	int                 i = -1;
	int                 op_errno = 0;
	int                 read_subvol = 0;
        int                 par_read_subvol = 0;
	unsigned char      *readable = NULL;
	int                 event = 0;
	struct afr_reply   *replies = NULL;
	uuid_t              read_gfid = {0, };
	gf_boolean_t        locked_entry = _gf_false;
	gf_boolean_t        can_interpret = _gf_true;
        inode_t            *parent = NULL;
        int                 spb_choice = -1;
        ia_type_t           ia_type = IA_INVAL;
        afr_read_subvol_args_t args = {0,};

        priv  = this->private;
        local = frame->local;
	replies = local->replies;
        parent = local->loc.parent;

	locked_entry = afr_is_possibly_under_txn (AFR_ENTRY_TRANSACTION, local,
                                                  this);

	readable = alloca0 (priv->child_count);

	afr_inode_read_subvol_get (parent, this, readable, NULL, &event);

        afr_inode_split_brain_choice_get (local->inode, this,
                                                &spb_choice);
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
			goto unwind;
		}

		if (replies[i].op_ret == -1)
			continue;

		if (read_subvol == -1 || !readable[read_subvol]) {
			read_subvol = i;
			gf_uuid_copy (read_gfid, replies[i].poststat.ia_gfid);
                        ia_type = replies[i].poststat.ia_type;
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

		if (!gf_uuid_compare (replies[i].poststat.ia_gfid, read_gfid))
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
                gf_uuid_copy (args.gfid, read_gfid);
                args.ia_type = ia_type;
		if (afr_replies_interpret (frame, this, local->inode, NULL)) {
                        read_subvol = afr_read_subvol_decide (local->inode,
                                                              this, &args);
			afr_inode_event_gen_reset (local->inode, this);
			goto cant_interpret;
		} else {
                        read_subvol = afr_data_subvol_get (local->inode, this,
                                                       NULL, NULL, NULL, &args);
		}
	} else {
	cant_interpret:
                if (read_subvol == -1) {
                        if (spb_choice >= 0)
                                read_subvol = spb_choice;
                        else
                                read_subvol = afr_first_up_child (frame, this);
                }
		dict_del (replies[read_subvol].xdata, GF_CONTENT_KEY);
	}

	afr_handle_quota_size (frame, this);

unwind:
        afr_set_need_heal (this, local);
        if (read_subvol == -1) {
                if (spb_choice >= 0)
                        read_subvol = spb_choice;
                else
                        read_subvol = afr_first_up_child (frame, this);

        }
        par_read_subvol = afr_get_parent_read_subvol (this, parent, replies,
                                                      readable);
        if (AFR_IS_ARBITER_BRICK (priv, read_subvol) && local->op_ret == 0) {
                        local->op_ret = -1;
                        local->op_errno = ENOTCONN;
        }

	AFR_STACK_UNWIND (lookup, frame, local->op_ret, local->op_errno,
			  local->inode, &local->replies[read_subvol].poststat,
			  local->replies[read_subvol].xdata,
			  &local->replies[par_read_subvol].postparent);
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
		if (local->replies[i].op_ret >= 0)
			continue;
		tmp_errno = local->replies[i].op_errno;
		op_errno = afr_higher_errno (op_errno, tmp_errno);
	}

	return op_errno;
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

        ret = glusterfs_is_local_pathinfo (pathinfo, &is_local);
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
                priv->local[child_index] = 1;
                /* Don't set arbiter as read child. */
                if (AFR_IS_ARBITER_BRICK(priv, child_index))
                        goto out;
                gf_msg (this->name, GF_LOG_INFO, 0,
                        AFR_MSG_LOCAL_CHILD, "selecting local read_child %s",
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
        int ret = -1;
        dict_t *dict = NULL;

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

        if (afr_selfheal_metadata_by_stbuf (this, &replies[first].poststat))
                goto out;

        afr_local_replies_wipe (local, this->private);

        dict = dict_new ();
        if (!dict)
                goto out;
        ret = dict_set_str (dict, "link-count", GF_XATTROP_INDEX_COUNT);
        if (ret) {
                gf_msg_debug (this->name, -ret,
                              "Unable to set link-count in dict ");
        }

        inode = afr_selfheal_unlocked_lookup_on (frame, local->loc.parent,
                                                 local->loc.name, local->replies,
                                                 local->child_up, dict);
        if (inode)
                inode_unref (inode);
out:
        afr_lookup_done (frame, this);

        if (dict)
                dict_unref (dict);

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

        if (!priv->metadata_self_heal)
                return _gf_false;

        for (i = 0; i < priv->child_count; i++) {
                if(!replies[i].valid || replies[i].op_ret == -1)
                        continue;
                if (first == -1) {
                        first = i;
                        stbuf = replies[i].poststat;
                        continue;
                }

                if (gf_uuid_compare (stbuf.ia_gfid, replies[i].poststat.ia_gfid)) {
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
                heal->root->pid = GF_CLIENT_PID_SELF_HEALD;
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
        uuid_t pargfid = {0,};

	local = frame->local;
	this = frame->this;
        loc_pargfid (&local->loc, pargfid);

	ret = afr_selfheal_name (frame->this, pargfid, local->loc.name,
                                 &local->cont.lookup.gfid_req);
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

		if (gf_uuid_compare (replies[i].poststat.ia_gfid,
				  replies[first].poststat.ia_gfid)) {
			need_heal = _gf_true;
			break;
		}
	}

	if (need_heal) {
		heal = copy_frame (frame);
		if (heal)
			heal->root->pid = GF_CLIENT_PID_SELF_HEALD;
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
        GF_UNUSED int   ret             = 0;
	int8_t need_heal                = 1;

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

        if (xdata) {
                ret = dict_get_int8 (xdata, "link-count", &need_heal);
                local->replies[child_index].need_heal = need_heal;
        } else {
                local->replies[child_index].need_heal = need_heal;
        }
	if (op_ret != -1) {
		local->replies[child_index].poststat = *buf;
		local->replies[child_index].postparent = *postparent;
		if (xdata)
			local->replies[child_index].xdata = dict_ref (xdata);
	}

        call_count = afr_frame_return (frame);
        if (call_count == 0) {
                afr_set_need_heal (this, local);
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
	int                 spb_choice = -1;
	int                 read_subvol = -1;

        priv  = this->private;
        local = frame->local;

        afr_inode_split_brain_choice_get (local->inode, this,
                                          &spb_choice);

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

	afr_replies_interpret (frame, this, local->inode, NULL);

	read_subvol = afr_read_subvol_decide (local->inode, this, NULL);
	if (read_subvol == -1) {
	        gf_msg (this->name, GF_LOG_WARNING, 0,
                        AFR_MSG_READ_SUBVOL_ERROR, "no read subvols for %s",
			local->loc.path);

                if (spb_choice >= 0) {
                        read_subvol = spb_choice;
                } else {
                        read_subvol = afr_first_up_child (frame, this);
                }
	}

unwind:
	if (read_subvol == -1) {
                if (spb_choice >= 0)
                        read_subvol = spb_choice;
                else
                        read_subvol = afr_first_up_child (frame, this);
        }
        if (AFR_IS_ARBITER_BRICK (priv, read_subvol) && local->op_ret == 0) {
                        local->op_ret = -1;
                        local->op_errno = ENOTCONN;
        }

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
        GF_UNUSED int ret               = 0;
	int8_t need_heal                = 1;

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

        if (xdata) {
                ret = dict_get_int8 (xdata, "link-count", &need_heal);
                local->replies[child_index].need_heal = need_heal;
        } else {
                local->replies[child_index].need_heal = need_heal;
        }

        call_count = afr_frame_return (frame);
        if (call_count == 0) {
               afr_set_need_heal (this, local);
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

	if (gf_uuid_is_null (loc->inode->gfid)) {
		afr_discover_do (frame, this, 0);
		return 0;
	}

	afr_read_subvol_get (loc->inode, this, NULL, NULL, &event,
			     AFR_DATA_TRANSACTION, NULL);

	if (afr_is_inode_refresh_reqd (loc->inode, this, event,
                                       local->event_generation))
		afr_inode_refresh (frame, this, loc->inode, NULL,
                                   afr_discover_do);
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

	if (!loc->parent && gf_uuid_is_null (loc->pargfid)) {
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
		local->xattr_req = dict_copy_with_ref (xattr_req, NULL);
		if (!local->xattr_req) {
		        op_errno = ENOMEM;
		        goto out;
                }
                ret = dict_get_ptr (local->xattr_req, "gfid-req", &gfid_req);
                if (ret == 0) {
                        gf_uuid_copy (local->cont.lookup.gfid_req, gfid_req);
                        dict_del (local->xattr_req, "gfid-req");
                }
        }

	afr_read_subvol_get (loc->parent, this, NULL, NULL, &event,
			     AFR_DATA_TRANSACTION, NULL);

	if (afr_is_inode_refresh_reqd (loc->inode, this, event,
                                       local->event_generation))
		afr_inode_refresh (frame, this, loc->parent, NULL,
                                   afr_lookup_do);
	else
		afr_lookup_do (frame, this, 0);

	return 0;
out:
	AFR_STACK_UNWIND (lookup, frame, -1, op_errno, NULL, NULL, NULL, NULL);

        return 0;
}

void
_afr_cleanup_fd_ctx (afr_fd_ctx_t *fd_ctx)
{
        int i = 0;


	for (i = 0; i < AFR_NUM_CHANGE_LOGS; i++)
		GF_FREE (fd_ctx->pre_op_done[i]);

        GF_FREE (fd_ctx->opened_on);

        GF_FREE (fd_ctx->lock_piggyback);

        GF_FREE (fd_ctx->lock_acquired);

	pthread_mutex_destroy (&fd_ctx->delay_lock);

        GF_FREE (fd_ctx);

        return;
}

int
afr_cleanup_fd_ctx (xlator_t *this, fd_t *fd)
{
        uint64_t        ctx = 0;
        afr_fd_ctx_t    *fd_ctx = NULL;
        int             ret = 0;

        ret = fd_ctx_get (fd, this, &ctx);
        if (ret < 0)
                goto out;

        fd_ctx = (afr_fd_ctx_t *)(long) ctx;

        if (fd_ctx) {
                /*no need to take any locks*/
                if (!list_empty (&fd_ctx->eager_locked))
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                AFR_MSG_INVALID_DATA, "%s: Stale "
                                "Eager-lock stubs found",
                                uuid_utoa (fd->inode->gfid));

                _afr_cleanup_fd_ctx (fd_ctx);

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

        ret = pthread_mutex_init (&fd_ctx->delay_lock, NULL);
        if (ret) {
                GF_FREE (fd_ctx);
                fd_ctx = NULL;
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

	fd_ctx->readdir_subvol = -1;

        INIT_LIST_HEAD (&fd_ctx->eager_locked);

        ret = __fd_ctx_set (fd, this, (uint64_t)(long) fd_ctx);
        if (ret)
                gf_msg_debug (this->name, 0,
                              "failed to set fd ctx (%p)", fd);
out:
        if (ret && fd_ctx)
                _afr_cleanup_fd_ctx (fd_ctx);
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

        local->op = GF_FOP_FLUSH;
	if (!afr_is_consistent_io_possible (local, this->private, &op_errno))
		goto out;

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

	read_subvol = afr_data_subvol_get (local->inode, this, NULL, NULL,
                                           NULL, NULL);

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

        local->op = GF_FOP_FSYNC;
	if (!afr_is_consistent_io_possible (local, priv, &op_errno))
		goto out;

        local->fd = fd_ref (fd);

	if (afr_fd_has_witnessed_unstable_write (this, fd)) {
		/* don't care. we only wanted to CLEAR the bit */
	}

	local->inode = inode_ref (fd->inode);

        call_count = local->call_count;
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

        local->op = GF_FOP_FSYNCDIR;
	if (!afr_is_consistent_io_possible (local, priv, &op_errno))
		goto out;

        call_count = local->call_count;
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

static int
afr_serialized_lock_wind (call_frame_t *frame, xlator_t *this);

static gf_boolean_t
afr_is_conflicting_lock_present (int32_t op_ret, int32_t op_errno)
{
        if (op_ret == -1 && op_errno == EAGAIN)
                return _gf_true;
        return _gf_false;
}

static void
afr_fop_lock_unwind (call_frame_t *frame, glusterfs_fop_t op, int32_t op_ret,
                     int32_t op_errno, dict_t *xdata)
{
        switch (op) {
        case GF_FOP_INODELK:
                AFR_STACK_UNWIND (inodelk, frame, op_ret, op_errno, xdata);
                break;
        case GF_FOP_FINODELK:
                AFR_STACK_UNWIND (finodelk, frame, op_ret, op_errno, xdata);
                break;
        case GF_FOP_ENTRYLK:
                AFR_STACK_UNWIND (entrylk, frame, op_ret, op_errno, xdata);
                break;
        case GF_FOP_FENTRYLK:
                AFR_STACK_UNWIND (fentrylk, frame, op_ret, op_errno, xdata);
                break;
        default:
                break;
        }
}

static void
afr_fop_lock_wind (call_frame_t *frame, xlator_t *this, int child_index,
                   int32_t (*lock_cbk) (call_frame_t *, void *, xlator_t *,
                                        int32_t, int32_t, dict_t *))
{
        afr_local_t *local = frame->local;
        afr_private_t *priv = this->private;
        int i = child_index;

        switch (local->op) {
        case GF_FOP_INODELK:
                STACK_WIND_COOKIE (frame, lock_cbk, (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->inodelk,
                                   (const char *)local->cont.inodelk.volume,
                                   &local->loc, local->cont.inodelk.cmd,
                                   &local->cont.inodelk.flock,
                                   local->cont.inodelk.xdata);
                break;
        case GF_FOP_FINODELK:
                STACK_WIND_COOKIE (frame, lock_cbk, (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->finodelk,
                                   (const char *)local->cont.inodelk.volume,
                                   local->fd, local->cont.inodelk.cmd,
                                   &local->cont.inodelk.flock,
                                   local->cont.inodelk.xdata);
                break;
        case GF_FOP_ENTRYLK:
                STACK_WIND_COOKIE (frame, lock_cbk, (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->entrylk,
                                   local->cont.entrylk.volume, &local->loc,
                                   local->cont.entrylk.basename,
                                   local->cont.entrylk.cmd,
                                   local->cont.entrylk.type,
                                   local->cont.entrylk.xdata);
                break;
        case GF_FOP_FENTRYLK:
                STACK_WIND_COOKIE (frame, lock_cbk, (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->fentrylk,
                                   local->cont.entrylk.volume, local->fd,
                                   local->cont.entrylk.basename,
                                   local->cont.entrylk.cmd,
                                   local->cont.entrylk.type,
                                   local->cont.entrylk.xdata);
                break;
        default:
                break;
        }
}

void
afr_fop_lock_proceed (call_frame_t *frame)
{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv = frame->this->private;

        if (local->fop_lock_state != AFR_FOP_LOCK_PARALLEL) {
                afr_fop_lock_unwind (frame, local->op, local->op_ret,
                                     local->op_errno, local->xdata_rsp);
                return;
        }
        /* At least one child is up */
        /*
         * Non-blocking locks also need to be serialized.  Otherwise there is
         * a chance that both the mounts which issued same non-blocking inodelk
         * may endup not acquiring the lock on any-brick.
         * Ex: Mount1 and Mount2
         * request for full length lock on file f1.  Mount1 afr may acquire the
         * partial lock on brick-1 and may not acquire the lock on brick-2
         * because Mount2 already got the lock on brick-2, vice versa.  Since
         * both the mounts only got partial locks, afr treats them as failure in
         * gaining the locks and unwinds with EAGAIN errno.
         */
        local->op_ret = -1;
        local->op_ret = EUCLEAN;
        local->fop_lock_state = AFR_FOP_LOCK_SERIAL;
        afr_local_replies_wipe (local, priv);
        if (local->xdata_rsp)
                dict_unref (local->xdata_rsp);
        local->xdata_rsp = NULL;
        switch (local->op) {
        case GF_FOP_INODELK:
        case GF_FOP_FINODELK:
                local->cont.inodelk.cmd = local->cont.inodelk.in_cmd;
                local->cont.inodelk.flock = local->cont.inodelk.in_flock;
                if (local->cont.inodelk.xdata)
                        dict_unref (local->cont.inodelk.xdata);
                local->cont.inodelk.xdata = NULL;
                if (local->xdata_req)
                        local->cont.inodelk.xdata = dict_ref (local->xdata_req);
                break;
        case GF_FOP_ENTRYLK:
        case GF_FOP_FENTRYLK:
                local->cont.entrylk.cmd = local->cont.entrylk.in_cmd;
                if (local->cont.entrylk.xdata)
                        dict_unref (local->cont.entrylk.xdata);
                local->cont.entrylk.xdata = NULL;
                if (local->xdata_req)
                        local->cont.entrylk.xdata = dict_ref (local->xdata_req);
                break;
        default:
                break;
        }
        afr_serialized_lock_wind (frame, frame->this);
}

static int32_t
afr_unlock_partial_lock_cbk (call_frame_t *frame, void *cookie,
                             xlator_t *this, int32_t op_ret,
                             int32_t op_errno, dict_t *xdata)

{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int call_count = -1;
        int child_index = (long)cookie;
        uuid_t  gfid = {0};

        local = frame->local;
        priv = this->private;

        if (op_ret < 0 && op_errno != ENOTCONN) {
                if (local->fd)
                        gf_uuid_copy (gfid, local->fd->inode->gfid);
                else
                        loc_gfid (&local->loc, gfid);
                gf_msg (this->name, GF_LOG_ERROR, op_errno,
                        AFR_MSG_UNLOCK_FAIL,
                        "%s: Failed to unlock %s on %s "
                        "with lk_owner: %s", uuid_utoa (gfid),
                        gf_fop_list[local->op],
                        priv->children[child_index]->name,
                        lkowner_utoa (&frame->root->lk_owner));
        }

        call_count = afr_frame_return (frame);
        if (call_count == 0)
                afr_fop_lock_proceed (frame);

        return 0;
}

static int32_t
afr_unlock_locks_and_proceed (call_frame_t *frame, xlator_t *this,
                             int call_count)
{
        int i = 0;
        afr_private_t *priv = NULL;
        afr_local_t *local = NULL;

        if (call_count == 0) {
                afr_fop_lock_proceed (frame);
                goto out;
        }

        local = frame->local;
        priv = this->private;
        local->call_count = call_count;
        switch (local->op) {
        case GF_FOP_INODELK:
        case GF_FOP_FINODELK:
                local->cont.inodelk.flock.l_type = F_UNLCK;
                local->cont.inodelk.cmd = F_SETLK;
                if (local->cont.inodelk.xdata)
                        dict_unref (local->cont.inodelk.xdata);
                local->cont.inodelk.xdata = NULL;
                break;
        case GF_FOP_ENTRYLK:
        case GF_FOP_FENTRYLK:
                local->cont.entrylk.cmd = ENTRYLK_UNLOCK;
                if (local->cont.entrylk.xdata)
                        dict_unref (local->cont.entrylk.xdata);
                local->cont.entrylk.xdata = NULL;
                break;
        default:
                break;
        }

        for (i = 0; i < priv->child_count; i++) {
                if (!local->replies[i].valid)
                        continue;

                if (local->replies[i].op_ret == -1)
                        continue;

                afr_fop_lock_wind (frame, this, i, afr_unlock_partial_lock_cbk);

                if (!--call_count)
                        break;
        }

out:
        return 0;
}

int32_t
afr_fop_lock_done (call_frame_t *frame, xlator_t *this)
{
        int i = 0;
        int lock_count = 0;
        unsigned char *success = NULL;

        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;

        local = frame->local;
        priv = this->private;
        success = alloca0(priv->child_count);

        for (i = 0; i < priv->child_count; i++) {
                if (!local->replies[i].valid)
                        continue;

                if (local->replies[i].op_ret == 0) {
                        lock_count++;
                        success[i] = 1;
                }

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

        if (afr_fop_lock_is_unlock (frame))
                goto unwind;

        if (afr_is_conflicting_lock_present (local->op_ret, local->op_errno)) {
                afr_unlock_locks_and_proceed (frame, this, lock_count);
        } else if (priv->quorum_count && !afr_has_quorum (success, this)) {
                local->fop_lock_state = AFR_FOP_LOCK_QUORUM_FAILED;
                local->op_ret = -1;
                local->op_errno = afr_final_errno (local, priv);
                if (local->op_errno == 0)
                        local->op_errno = afr_quorum_errno (priv);
                afr_unlock_locks_and_proceed (frame, this, lock_count);
        } else {
                goto unwind;
        }

        return 0;
unwind:
        afr_fop_lock_unwind (frame, local->op, local->op_ret,
                             local->op_errno, local->xdata_rsp);
        return 0;
}

static int
afr_common_lock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                        int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_local_t *local = NULL;
        int child_index = (long)cookie;

        local = frame->local;

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
        return 0;
}

static int32_t
afr_serialized_lock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                         int32_t op_ret, int32_t op_errno, dict_t *xdata)

{
        afr_local_t *local = NULL;
        afr_private_t *priv = NULL;
        int child_index = (long)cookie;
        int next_child  = 0;

        local = frame->local;
        priv = this->private;

        afr_common_lock_cbk (frame, cookie, this, op_ret, op_errno, xdata);

        for (next_child = child_index + 1; next_child < priv->child_count;
             next_child++) {
                if (local->child_up[next_child])
                        break;
        }

        if (afr_is_conflicting_lock_present (op_ret, op_errno) ||
            (next_child == priv->child_count)) {
                afr_fop_lock_done (frame, this);
        } else {
                afr_fop_lock_wind (frame, this, next_child,
                                   afr_serialized_lock_cbk);
        }

        return 0;
}

static int
afr_serialized_lock_wind (call_frame_t *frame, xlator_t *this)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int i = 0;

        priv = this->private;
        local = frame->local;

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        afr_fop_lock_wind (frame, this, i,
                                           afr_serialized_lock_cbk);
                        break;
                }
        }
        return 0;
}

static int32_t
afr_parallel_lock_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, dict_t *xdata)

{
        int     call_count = 0;

        afr_common_lock_cbk (frame, cookie, this, op_ret, op_errno, xdata);

        call_count = afr_frame_return (frame);
        if (call_count == 0)
                afr_fop_lock_done (frame, this);

        return 0;
}

static int
afr_parallel_lock_wind (call_frame_t *frame, xlator_t *this)
{
        afr_private_t *priv = NULL;
        afr_local_t *local  = NULL;
        int         call_count = 0;
        int i = 0;

        priv = this->private;
        local = frame->local;
        call_count = local->call_count;

        for (i = 0; i < priv->child_count; i++) {
                if (!local->child_up[i])
                        continue;
                afr_fop_lock_wind (frame, this, i, afr_parallel_lock_cbk);
                if (!--call_count)
                        break;
        }
        return 0;
}

static int
afr_fop_handle_lock (call_frame_t *frame, xlator_t *this)
{
        afr_local_t *local = frame->local;
        int op_errno = 0;

        if (!afr_fop_lock_is_unlock (frame)) {
                if (!afr_is_consistent_io_possible (local, this->private,
                                                &op_errno))
                        goto out;

                switch (local->op) {
                case GF_FOP_INODELK:
                case GF_FOP_FINODELK:
                        local->cont.inodelk.cmd = F_SETLK;
                        break;
                case GF_FOP_ENTRYLK:
                case GF_FOP_FENTRYLK:
                        local->cont.entrylk.cmd = ENTRYLK_LOCK_NB;
                        break;
                default:
                        break;
                }
        }

        if (local->xdata_req) {
                switch (local->op) {
                case GF_FOP_INODELK:
                case GF_FOP_FINODELK:
                        local->cont.inodelk.xdata = dict_ref (local->xdata_req);
                        break;
                case GF_FOP_ENTRYLK:
                case GF_FOP_FENTRYLK:
                        local->cont.entrylk.xdata = dict_ref (local->xdata_req);
                        break;
                default:
                        break;
                }
        }

        local->fop_lock_state = AFR_FOP_LOCK_PARALLEL;
        afr_parallel_lock_wind (frame, this);
out:
        return -op_errno;
}

static int32_t
afr_handle_inodelk (call_frame_t *frame, glusterfs_fop_t fop,
                    const char *volume, loc_t *loc, fd_t *fd, int32_t cmd,
                    struct gf_flock *flock, dict_t *xdata)
{
        afr_local_t *local  = NULL;
        int32_t op_errno = ENOMEM;

        local = AFR_FRAME_INIT (frame, op_errno);
        if (!local)
                goto out;

        local->op = fop;
        if (loc)
                loc_copy (&local->loc, loc);
        if (fd)
                local->fd = fd_ref (fd);

        local->cont.inodelk.volume = gf_strdup (volume);
        if (!local->cont.inodelk.volume) {
                op_errno = ENOMEM;
                goto out;
        }

        local->cont.inodelk.in_cmd = cmd;
        local->cont.inodelk.cmd = cmd;
        local->cont.inodelk.in_flock = *flock;
        local->cont.inodelk.flock = *flock;
        if (xdata)
                local->xdata_req = dict_ref (xdata);

        op_errno = -afr_fop_handle_lock (frame, frame->this);
        if (op_errno)
                goto out;
        return 0;
out:
        afr_fop_lock_unwind (frame, fop, -1, op_errno, NULL);

        return 0;
}

int32_t
afr_inodelk (call_frame_t *frame, xlator_t *this,
             const char *volume, loc_t *loc, int32_t cmd,
             struct gf_flock *flock, dict_t *xdata)
{
        afr_handle_inodelk (frame, GF_FOP_INODELK, volume, loc, NULL, cmd,
                            flock, xdata);
        return 0;
}

int32_t
afr_finodelk (call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
	      int32_t cmd, struct gf_flock *flock, dict_t *xdata)
{
        afr_handle_inodelk (frame, GF_FOP_FINODELK, volume, NULL, fd, cmd,
                            flock, xdata);
        return 0;
}

static int
afr_handle_entrylk (call_frame_t *frame, glusterfs_fop_t fop,
                    const char *volume, loc_t *loc, fd_t *fd,
                    const char *basename, entrylk_cmd cmd,
                    entrylk_type type, dict_t *xdata)
{
        afr_local_t *local  = NULL;
        int32_t op_errno = ENOMEM;

        local = AFR_FRAME_INIT (frame, op_errno);
        if (!local)
                goto out;

        local->op = fop;
        if (loc)
                loc_copy (&local->loc, loc);
        if (fd)
                local->fd = fd_ref (fd);
        local->cont.entrylk.cmd = cmd;
        local->cont.entrylk.in_cmd = cmd;
        local->cont.entrylk.type = type;
        local->cont.entrylk.volume = gf_strdup (volume);
        local->cont.entrylk.basename = gf_strdup (basename);
        if (!local->cont.entrylk.volume || !local->cont.entrylk.basename) {
                op_errno = ENOMEM;
                goto out;
        }
        if (xdata)
                local->xdata_req = dict_ref (xdata);
        op_errno = -afr_fop_handle_lock (frame, frame->this);
        if (op_errno)
                goto out;

        return 0;
out:
        afr_fop_lock_unwind (frame, fop, -1, op_errno, NULL);
        return 0;
}

int
afr_entrylk (call_frame_t *frame, xlator_t *this, const char *volume,
	     loc_t *loc, const char *basename, entrylk_cmd cmd,
	     entrylk_type type, dict_t *xdata)
{
        afr_handle_entrylk (frame, GF_FOP_ENTRYLK, volume, loc, NULL, basename,
                            cmd, type, xdata);
        return 0;
}

int
afr_fentrylk (call_frame_t *frame, xlator_t *this, const char *volume, fd_t *fd,
              const char *basename, entrylk_cmd cmd, entrylk_type type,
	      dict_t *xdata)
{
        afr_handle_entrylk (frame, GF_FOP_FENTRYLK, volume, NULL, fd, basename,
                            cmd, type, xdata);
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

        local->op = GF_FOP_STATFS;
	if (!afr_is_consistent_io_possible (local, priv, &op_errno))
		goto out;

        if (priv->arbiter_count == 1 && local->child_up[ARBITER_BRICK_INDEX])
                local->call_count--;
        call_count = local->call_count;
	if (!call_count) {
		op_errno = ENOTCONN;
		goto out;
	}

        for (i = 0; i < priv->child_count; i++) {
                if (local->child_up[i]) {
                        if (AFR_IS_ARBITER_BRICK(priv, i))
                                continue;
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

        local->op = GF_FOP_LK;
        if (!afr_lk_is_unlock (cmd, flock) &&
            !afr_is_consistent_io_possible (local, priv, &op_errno))
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
afr_ipc_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
             int32_t op_ret, int32_t op_errno, dict_t *xdata)
{
        afr_local_t   *local          = NULL;
        int            child_index    = (long)cookie;
        int            call_count     = 0;
        gf_boolean_t   failed         = _gf_false;
        gf_boolean_t   succeded       = _gf_false;
        int            i              = 0;
        afr_private_t *priv           = NULL;

        local = frame->local;
        priv = this->private;

        local->replies[child_index].valid = 1;
        local->replies[child_index].op_ret = op_ret;
        local->replies[child_index].op_errno = op_errno;
        if (xdata)
                local->replies[child_index].xdata = dict_ref (xdata);

        call_count = afr_frame_return (frame);
        if (call_count)
                goto out;
        /* If any of the subvolumes failed with other than ENOTCONN
         * return error else return success unless all the subvolumes
         * failed.
         * TODO: In case of failure, we need to unregister the xattrs
         * from the other subvolumes where it succeded (once upcall
         * fixes the Bz-1371622)*/
        for (i = 0; i < priv->child_count; i++) {
                if (!local->replies[i].valid)
                        continue;
                if (local->replies[i].op_ret < 0 &&
                    local->replies[i].op_errno != ENOTCONN) {
                        local->op_ret = local->replies[i].op_ret;
                        local->op_errno = local->replies[i].op_errno;
                        if (local->xdata_rsp)
                                dict_unref (local->xdata_rsp);
                        local->xdata_rsp = NULL;
                        if (local->replies[i].xdata) {
                                local->xdata_rsp =
                                        dict_ref (local->replies[i].xdata);
                        }
                        failed = _gf_true;
                        break;
                }
                if (local->replies[i].op_ret == 0) {
                        succeded = _gf_true;
                        local->op_ret = 0;
                        local->op_errno = 0;
                        if (!local->xdata_rsp && local->replies[i].xdata) {
                                local->xdata_rsp =
                                        dict_ref (local->replies[i].xdata);
                        }
                }
        }

        if (!succeded && !failed) {
                local->op_ret = -1;
                local->op_errno = ENOTCONN;
        }

        AFR_STACK_UNWIND (ipc, frame, local->op_ret, local->op_errno,
                          local->xdata_rsp);

out:
        return 0;
}

int
afr_ipc (call_frame_t *frame, xlator_t *this, int32_t op, dict_t *xdata)
{
        afr_local_t    *local    = NULL;
        int32_t         op_errno = -1;
        afr_private_t  *priv     = NULL;
        int             i        = 0;
        int             call_cnt = -1;

        VALIDATE_OR_GOTO (frame, err);
        VALIDATE_OR_GOTO (this, err);

        if (op != GF_IPC_TARGET_UPCALL)
                goto wind_default;

        VALIDATE_OR_GOTO (this->private, err);
        priv = this->private;

        local = AFR_FRAME_INIT (frame, op_errno);
        if (!local)
                goto err;

        call_cnt = local->call_count;

        if (xdata) {
                for (i = 0; i < priv->child_count; i++) {
                        if (dict_set_int8 (xdata, priv->pending_key[i], 0) < 0)
                                goto err;
                }
        }

        for (i = 0; i < priv->child_count; i++) {
                if (!local->child_up[i])
                        continue;

                STACK_WIND_COOKIE (frame, afr_ipc_cbk,
                                   (void *) (long) i,
                                   priv->children[i],
                                   priv->children[i]->fops->ipc,
                                   op, xdata);
                if (!--call_cnt)
                        break;
        }
        return 0;

err:
        if (op_errno == -1)
                op_errno = errno;
        AFR_STACK_UNWIND (ipc, frame, -1, op_errno, NULL);

        return 0;

wind_default:
        STACK_WIND (frame, default_ipc_cbk, FIRST_CHILD (this),
                    FIRST_CHILD (this)->fops->ipc, op, xdata);
        return 0;
}

int
afr_forget (xlator_t *this, inode_t *inode)
{
        uint64_t        ctx_int = 0;
        afr_inode_ctx_t *ctx    = NULL;

        afr_spb_choice_timeout_cancel (this, inode);
        inode_ctx_del (inode, this, &ctx_int);
        if (!ctx_int)
                return 0;

        ctx = (afr_inode_ctx_t *)ctx_int;
        GF_FREE (ctx);
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
        gf_proc_dump_write("quorum-reads", "%d", priv->quorum_reads);
        gf_proc_dump_write("heal-wait-queue-length", "%d",
                           priv->heal_wait_qlen);
        gf_proc_dump_write("heal-waiters", "%d", priv->heal_waiters);
        gf_proc_dump_write("background-self-heal-count", "%d",
                           priv->background_self_heal_count);
        gf_proc_dump_write("healers", "%d", priv->healers);

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

static int
__afr_get_up_children_count (afr_private_t *priv)
{
        int             up_children         = 0;
        int             i = 0;

        for (i = 0; i < priv->child_count; i++)
                if (priv->child_up[i] == 1)
                        up_children++;

        return up_children;
}

glusterfs_event_t
__afr_transform_event_from_state (afr_private_t *priv)
{
        int i = 0;
        int up_children = 0;

        if (AFR_COUNT (priv->last_event, priv->child_count) ==
                       priv->child_count)
                /* have_heard_from_all. Let afr_notify() do the propagation. */
                return GF_EVENT_MAXVAL;

        up_children = __afr_get_up_children_count (priv);
        /* Treat the children with pending notification, as having sent a
         * GF_EVENT_CHILD_DOWN. i.e. set the event as GF_EVENT_SOME_DESCENDENT_DOWN,
         * as done in afr_notify() */
        for (i = 0; i < priv->child_count; i++) {
                if (priv->last_event[i])
                        continue;
                priv->last_event[i] = GF_EVENT_SOME_DESCENDENT_DOWN;
                priv->child_up[i] = 0;
        }

        if (up_children)
                /* We received at least one child up */
                return GF_EVENT_CHILD_UP;
        else
                return GF_EVENT_CHILD_DOWN;

        return GF_EVENT_MAXVAL;
}

static void
afr_notify_cbk (void *data)
{
        xlator_t *this = data;
        afr_private_t *priv = this->private;
        glusterfs_event_t event = GF_EVENT_MAXVAL;
        gf_boolean_t propagate = _gf_false;

        LOCK (&priv->lock);
        {
                if (!priv->timer) {
                        /*
                         * Either child_up/child_down is already sent to parent.
                         * This is a spurious wake up.
                         */
                        goto unlock;
                }
                priv->timer = NULL;
                event = __afr_transform_event_from_state (priv);
                if (event != GF_EVENT_MAXVAL)
                        propagate = _gf_true;
        }
unlock:
        UNLOCK (&priv->lock);
        if (propagate)
                default_notify (this, event, NULL);
}

static void
__afr_launch_notify_timer (xlator_t *this, afr_private_t *priv)
{

        struct timespec delay = {0, };

        gf_msg_debug (this->name, 0, "Initiating child-down timer");
        delay.tv_sec = 10;
        delay.tv_nsec = 0;
        priv->timer = gf_timer_call_after (this->ctx, delay,
                                           afr_notify_cbk, this);
        if (priv->timer == NULL) {
                gf_msg (this->name, GF_LOG_ERROR, 0, AFR_MSG_TIMER_CREATE_FAIL,
                        "Cannot create timer for delayed initialization");
        }
}

int
__get_heard_from_all_status (xlator_t *this)
{
        afr_private_t *priv          = this->private;
        int           heard_from_all = 1;
        int           i              = 0;

        for (i = 0; i < priv->child_count; i++) {
                if (!priv->last_event[i]) {
                        heard_from_all = 0;
                        break;
                }
        }
        return heard_from_all;
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
        dict_t          *input              = NULL;
        dict_t          *output             = NULL;
        gf_boolean_t    had_quorum          = _gf_false;
        gf_boolean_t    has_quorum          = _gf_false;
        struct gf_upcall *up_data           = NULL;
        struct gf_upcall_cache_invalidation *up_ci = NULL;
        inode_table_t  *itable              = NULL;
        inode_t        *inode               = NULL;

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
        if (event == GF_EVENT_TRANSLATOR_OP) {
                LOCK (&priv->lock);
                {
                        had_heard_from_all = __get_heard_from_all_status (this);
                }
                UNLOCK (&priv->lock);

                if (!had_heard_from_all) {
                        ret = -1;
                } else {
                        input = data;
                        output = data2;
                        ret = afr_xl_op (this, input, output);
                }
                goto out;
        }

        LOCK (&priv->lock);
        {
                had_heard_from_all = __get_heard_from_all_status (this);
                switch (event) {
                case GF_EVENT_PARENT_UP:
                        __afr_launch_notify_timer (this, priv);
                        propagate = 1;
                        break;
                case GF_EVENT_CHILD_UP:
                        /*
                         * This only really counts if the child was never up
                         * (value = -1) or had been down (value = 0).  See
                         * comment at GF_EVENT_CHILD_DOWN for a more detailed
                         * explanation.
                         */
                        if (priv->child_up[idx] != 1) {
                                priv->event_generation++;
                        }
                        priv->child_up[idx] = 1;

                        call_psh = 1;
                        up_children = __afr_get_up_children_count (priv);
                        if (up_children == 1) {
                                gf_msg (this->name, GF_LOG_INFO, 0,
                                        AFR_MSG_SUBVOL_UP,
                                        "Subvolume '%s' came back up; "
                                     "going online.", ((xlator_t *)data)->name);
                                gf_event (EVENT_AFR_SUBVOL_UP,
                                          "subvol=%s", this->name);

                        } else {
                                event = GF_EVENT_SOME_DESCENDENT_UP;
                        }

                        priv->last_event[idx] = event;

                        break;

                case GF_EVENT_CHILD_DOWN:
                        if (priv->child_up[idx] == 1) {
                                priv->event_generation++;
                        }
                        priv->child_up[idx] = 0;

                        for (i = 0; i < priv->child_count; i++)
                                if (priv->child_up[i] == 0)
                                        down_children++;
                        if (down_children == priv->child_count) {
                                gf_msg (this->name, GF_LOG_ERROR, 0,
                                        AFR_MSG_SUBVOLS_DOWN,
                                       "All subvolumes are down. Going offline "
                                    "until atleast one of them comes back up.");
                                gf_event (EVENT_AFR_SUBVOLS_DOWN,
                                          "subvol=%s", this->name);
                        } else {
                                event = GF_EVENT_SOME_DESCENDENT_DOWN;
                        }

                        priv->last_event[idx] = event;

                        break;

                case GF_EVENT_CHILD_CONNECTING:
                        priv->last_event[idx] = event;

                        break;

                case GF_EVENT_SOME_DESCENDENT_DOWN:
                        priv->last_event[idx] = event;
                        break;
                case GF_EVENT_UPCALL:
                        up_data = (struct gf_upcall *)data;
                        if (up_data->event_type != GF_UPCALL_CACHE_INVALIDATION)
                                break;
                        up_ci = (struct gf_upcall_cache_invalidation *)up_data->data;

                        /* Since md-cache will be aggressively filtering
                         * lookups, the stale read issue will be more
                         * pronounced. Hence when a pending xattr is set notify
                         * all the md-cache clients to invalidate the existing
                         * stat cache and send the lookup next time */
                        if (!up_ci->dict)
                                break;
                        for (i = 0; i < priv->child_count; i++) {
                                if (dict_get (up_ci->dict, priv->pending_key[i])) {
                                         up_ci->flags |= UP_INVAL_ATTR;
                                         itable = ((xlator_t *)this->graph->top)->itable;
                                         /*Internal processes may not have itable for top xlator*/
                                         if (itable)
                                                 inode = inode_find (itable, up_data->gfid);
                                         if (inode)
                                                 afr_inode_need_refresh_set (inode, this);

                                         break;
                                }
                        }
                        break;
                default:
                        propagate = 1;
                        break;
                }
                have_heard_from_all = __get_heard_from_all_status (this);
                if (!had_heard_from_all && have_heard_from_all) {
                        if (priv->timer) {
                                gf_timer_call_cancel (this->ctx, priv->timer);
                                priv->timer = NULL;
                        }
                        /* This is the first event which completes aggregation
                           of events from all subvolumes. If at least one subvol
                           had come up, propagate CHILD_UP, but only this time
                        */
                        event = GF_EVENT_CHILD_DOWN;
                        up_children = __afr_get_up_children_count (priv);
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
        }
        UNLOCK (&priv->lock);

        if (priv->quorum_count) {
                has_quorum = afr_has_quorum (priv->child_up, this);
                if (!had_quorum && has_quorum) {
                        gf_msg (this->name, GF_LOG_INFO, 0, AFR_MSG_QUORUM_MET,
                                "Client-quorum is met");
                        gf_event  (EVENT_AFR_QUORUM_MET,
                                   "subvol=%s", this->name);
                }
                if (had_quorum && !has_quorum) {
                        gf_msg (this->name, GF_LOG_WARNING, 0,
                                AFR_MSG_QUORUM_FAIL,
                                "Client-quorum is not met");
                        gf_event  (EVENT_AFR_QUORUM_FAIL, "subvol=%s",
                                   this->name);
                }
        }

        /* if all subvols have reported status, no need to hide anything
           or wait for anything else. Just propagate blindly */
        if (have_heard_from_all)
                propagate = 1;

        ret = 0;
        if (propagate)
                ret = default_notify (this, event, data);

        if ((!had_heard_from_all) || call_psh) {
                /* Launch self-heal on all local subvolumes if:
                 * a) We have_heard_from_all for the first time
                 * b) Already heard from everyone, but we now got a child-up
                 *    event.
                 */
                if (have_heard_from_all && priv->shd.iamshd) {
                        for (i = 0; i < priv->child_count; i++)
                                if (priv->child_up[i])
                                        afr_selfheal_childup (this, i);
                }
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
                gf_msg (THIS->name, GF_LOG_INFO, 0,
                        AFR_MSG_SUBVOLS_DOWN, "no subvolumes up");
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

        local->readable2 = GF_CALLOC (priv->child_count, sizeof (char),
                                      gf_afr_mt_char);
        if (!local->readable2) {
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

        local->need_full_crawl = _gf_false;

        local->compound = _gf_false;
        INIT_LIST_HEAD (&local->healer);
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

        if (priv->arbiter_count == 1) {
                local->transaction.pre_op_xdata =
                        GF_CALLOC (sizeof (*local->transaction.pre_op_xdata),
                                   priv->child_count, gf_afr_mt_dict_t);
                if (!local->transaction.pre_op_xdata)
                        goto out;

                local->transaction.pre_op_sources =
                        GF_CALLOC (sizeof (*local->transaction.pre_op_sources),
                                   priv->child_count, gf_afr_mt_char);
                if (!local->transaction.pre_op_sources)
                        goto out;
        }

        local->transaction.failed_subvols = GF_CALLOC (sizeof (*local->transaction.failed_subvols),
						       priv->child_count,
						       gf_afr_mt_char);
        if (!local->transaction.failed_subvols)
                goto out;

        local->pending = afr_matrix_create (priv->child_count,
                                            AFR_NUM_CHANGE_LOGS);
        if (!local->pending)
                goto out;

        local->compound = _gf_false;
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


void
afr_priv_destroy (afr_private_t *priv)
{
        int            i           = 0;

        if (!priv)
                goto out;
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
       int d_idx = 0;
       int ret = 0;

       m_idx = afr_index_for_transaction_type (AFR_METADATA_TRANSACTION);
       d_idx = afr_index_for_transaction_type (AFR_DATA_TRANSACTION);

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
                /* If the newentry marking is on a newly created directory,
                 * then mark it with the full-heal indicator.
                 */
                if ((IA_ISDIR (iat)) && (priv->esh_granular))
                        changelog[i][d_idx] = hton32(1);
       }
       ret = afr_set_pending_dict (priv, xattr, changelog);
       if (ret < 0) {
               afr_matrix_cleanup (changelog, priv->child_count);
               return NULL;
       }
out:
       return changelog;
}

gf_boolean_t
afr_decide_heal_info (afr_private_t *priv, unsigned char *sources, int source)
{
        int sources_count = 0;

        if (source < 0)
                goto out;

        sources_count = AFR_COUNT (sources, priv->child_count);
        if (sources_count == priv->child_count)
                return _gf_false;
out:
        return _gf_true;
}

int
afr_selfheal_locked_metadata_inspect (call_frame_t *frame, xlator_t *this,
                                      inode_t *inode, gf_boolean_t *msh,
                                      gf_boolean_t *pending)
{
        int ret = -1;
        unsigned char *locked_on = NULL;
        unsigned char *sources = NULL;
        unsigned char *sinks = NULL;
        unsigned char *healed_sinks = NULL;
        unsigned char *undid_pending = NULL;
        struct afr_reply *locked_replies = NULL;

        afr_private_t *priv = this->private;

        locked_on = alloca0 (priv->child_count);
        sources = alloca0 (priv->child_count);
        sinks = alloca0 (priv->child_count);
        healed_sinks = alloca0 (priv->child_count);
        undid_pending = alloca0 (priv->child_count);

        locked_replies = alloca0 (sizeof (*locked_replies) * priv->child_count);

        ret = afr_selfheal_inodelk (frame, this, inode, this->name,
                                    LLONG_MAX - 1, 0, locked_on);
        {
                if (ret == 0) {
                        /* Not a single lock */
                        ret = -afr_final_errno (frame->local, priv);
                        if (ret == 0)
                                ret = -ENOTCONN;/* all invalid responses */
                        goto out;
                }
                ret = __afr_selfheal_metadata_prepare (frame, this, inode,
                                                       locked_on, sources,
                                                       sinks, healed_sinks,
                                                       undid_pending,
                                                       locked_replies,
                                                       pending);
                *msh = afr_decide_heal_info (priv, sources, ret);
        }
        afr_selfheal_uninodelk (frame, this, inode, this->name,
                                LLONG_MAX - 1, 0, locked_on);
out:
        if (locked_replies)
                afr_replies_wipe (locked_replies, priv->child_count);
        return ret;
}

int
afr_selfheal_locked_data_inspect (call_frame_t *frame, xlator_t *this,
                                  inode_t *inode, gf_boolean_t *dsh,
                                  gf_boolean_t *pflag)
{
        int ret = -1;
        unsigned char *data_lock = NULL;
        unsigned char *sources = NULL;
        unsigned char *sinks = NULL;
        unsigned char *healed_sinks = NULL;
        unsigned char *undid_pending = NULL;
        afr_private_t   *priv = NULL;
        fd_t          *fd = NULL;
        struct afr_reply *locked_replies = NULL;

        priv = this->private;
        data_lock = alloca0 (priv->child_count);
        sources = alloca0 (priv->child_count);
        sinks = alloca0 (priv->child_count);
        healed_sinks = alloca0 (priv->child_count);
        undid_pending = alloca0 (priv->child_count);

        /* Heal-info does an open() on the file being examined so that the
         * current eager-lock holding client, if present, at some point sees
         * open-fd count being > 1 and releases the eager-lock so that heal-info
         * doesn't remain blocked forever until IO completes.
         */
        ret = afr_selfheal_data_open (this, inode, &fd);
        if (ret < 0) {
                gf_msg_debug (this->name, -ret, "%s: Failed to open",
                              uuid_utoa (inode->gfid));
                goto out;
        }

        locked_replies = alloca0 (sizeof (*locked_replies) * priv->child_count);

        ret = afr_selfheal_inodelk (frame, this, inode, this->name,
                                    0, 0, data_lock);
        {
                if (ret == 0) {
                        ret = -afr_final_errno (frame->local, priv);
                        if (ret == 0)
                                ret = -ENOTCONN; /* all invalid responses */
                        goto out;
                }
                ret = __afr_selfheal_data_prepare (frame, this, inode,
                                                   data_lock, sources, sinks,
                                                   healed_sinks, undid_pending,
                                                   locked_replies, pflag);
                *dsh = afr_decide_heal_info (priv, sources, ret);
        }
        afr_selfheal_uninodelk (frame, this, inode, this->name, 0, 0,
                                data_lock);
out:
        if (locked_replies)
                afr_replies_wipe (locked_replies, priv->child_count);
        if (fd)
                fd_unref (fd);
        return ret;
}

int
afr_selfheal_locked_entry_inspect (call_frame_t *frame, xlator_t *this,
                                   inode_t *inode,
                                   gf_boolean_t *esh, gf_boolean_t *pflag)
{
        int ret = -1;
        int source = -1;
        afr_private_t   *priv = NULL;
        unsigned char *locked_on = NULL;
        unsigned char *data_lock = NULL;
        unsigned char *sources = NULL;
        unsigned char *sinks = NULL;
        unsigned char *healed_sinks = NULL;
        struct afr_reply *locked_replies = NULL;
        gf_boolean_t granular_locks = _gf_false;

        priv = this->private;
	if (strcmp ("granular", priv->locking_scheme) == 0)
	        granular_locks = _gf_true;
        locked_on = alloca0 (priv->child_count);
        data_lock = alloca0 (priv->child_count);
        sources = alloca0 (priv->child_count);
        sinks = alloca0 (priv->child_count);
        healed_sinks = alloca0 (priv->child_count);

        locked_replies = alloca0 (sizeof (*locked_replies) * priv->child_count);

        if (!granular_locks) {
                ret = afr_selfheal_tryentrylk (frame, this, inode,
                                              priv->sh_domain, NULL, locked_on);
        }
        {
                if (!granular_locks && ret == 0) {
                        ret = -afr_final_errno (frame->local, priv);
                        if (ret == 0)
                                ret = -ENOTCONN;/* all invalid responses */
                        goto out;
                }

                ret = afr_selfheal_entrylk (frame, this, inode, this->name,
                                            NULL, data_lock);
                {
                        if (ret == 0) {
                                ret = -afr_final_errno (frame->local, priv);
                                if (ret == 0)
                                        ret = -ENOTCONN;
                                /* all invalid responses */
                                goto unlock;
                        }
                        ret = __afr_selfheal_entry_prepare (frame, this, inode,
                                                            data_lock, sources,
                                                            sinks, healed_sinks,
                                                            locked_replies,
                                                            &source, pflag);
                        if ((ret == 0) && source < 0)
                                ret = -EIO;
                        *esh = afr_decide_heal_info (priv, sources, ret);
                }
                afr_selfheal_unentrylk (frame, this, inode, this->name, NULL,
                                        data_lock, NULL);
        }
unlock:
        if (!granular_locks)
                afr_selfheal_unentrylk (frame, this, inode, priv->sh_domain,
                                        NULL, locked_on, NULL);
out:
        if (locked_replies)
                afr_replies_wipe (locked_replies, priv->child_count);
        return ret;
}

int
afr_selfheal_locked_inspect (call_frame_t *frame, xlator_t *this, uuid_t gfid,
                             inode_t **inode,
                             gf_boolean_t *entry_selfheal,
                             gf_boolean_t *data_selfheal,
                             gf_boolean_t *metadata_selfheal,
                             gf_boolean_t *pending)

{
        int ret             = -1;
        gf_boolean_t    dsh = _gf_false;
        gf_boolean_t    msh = _gf_false;
        gf_boolean_t    esh = _gf_false;

        ret = afr_selfheal_unlocked_inspect (frame, this, gfid, inode,
                                             &dsh, &msh, &esh);
        if (ret)
                goto out;

        /* For every heal type hold locks and check if it indeed needs heal */

        if (msh) {
                ret = afr_selfheal_locked_metadata_inspect (frame, this,
                                                            *inode, &msh,
                                                            pending);
                if (ret == -EIO)
                        goto out;
        }

        if (dsh) {
                ret = afr_selfheal_locked_data_inspect (frame, this, *inode,
                                                        &dsh, pending);
                if (ret == -EIO || (ret == -EAGAIN))
                        goto out;
        }

        if (esh) {
                ret = afr_selfheal_locked_entry_inspect (frame, this, *inode,
                                                         &esh, pending);
        }

out:
        *data_selfheal = dsh;
        *entry_selfheal = esh;
        *metadata_selfheal = msh;
        return ret;
}

dict_t*
afr_set_heal_info (char *status)
{
        dict_t *dict = NULL;
        int    ret   = -1;

        dict = dict_new ();
        if (!dict) {
                ret = -ENOMEM;
                goto out;
        }

        ret = dict_set_str (dict, "heal-info", status);
        if (ret)
                gf_msg ("", GF_LOG_WARNING, -ret,
                        AFR_MSG_DICT_SET_FAILED,
                        "Failed to set heal-info key to "
                        "%s", status);
out:
        return dict;
}

int
afr_get_heal_info (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        gf_boolean_t    data_selfheal     = _gf_false;
        gf_boolean_t    metadata_selfheal = _gf_false;
        gf_boolean_t    entry_selfheal    = _gf_false;
        gf_boolean_t    pending           = _gf_false;
        dict_t         *dict              = NULL;
        int             ret               = -1;
        int             op_errno          = 0;
        int             size              = 0;
        inode_t        *inode             = NULL;
        char           *substr            = NULL;
        char           *status            = NULL;

        ret = afr_selfheal_locked_inspect (frame, this, loc->gfid, &inode,
                                           &entry_selfheal,
                                           &data_selfheal, &metadata_selfheal,
                                           &pending);

        if (ret == -ENOMEM) {
                op_errno = -ret;
                ret = -1;
                goto out;
        }

        if (pending) {
                size = strlen ("-pending") + 1;
                gf_asprintf (&substr, "-pending");
                if (!substr)
                        goto out;
        }

        if (ret == -EIO) {
                size += strlen ("split-brain") + 1;
                ret = gf_asprintf (&status, "split-brain%s",
                                   substr? substr : "");
                if (ret < 0)
                        goto out;
                dict = afr_set_heal_info (status);
        } else if (ret == -EAGAIN) {
                size += strlen ("possibly-healing") + 1;
                ret = gf_asprintf (&status, "possibly-healing%s",
                                   substr? substr : "");
                if (ret < 0)
                        goto out;
                dict = afr_set_heal_info (status);
        } else if (ret >= 0) {
                /* value of ret = source index
                 * so ret >= 0 and at least one of the 3 booleans set to
                 * true means a source is identified; heal is required.
                 */
                if (!data_selfheal && !entry_selfheal &&
                    !metadata_selfheal) {
                        dict = afr_set_heal_info ("no-heal");
                } else {
                        size += strlen ("heal") + 1;
                        ret = gf_asprintf (&status, "heal%s",
                                           substr? substr : "");
                        if (ret < 0)
                                goto out;
                        dict = afr_set_heal_info (status);
                }
        } else if (ret < 0) {
                /* Apart from above checked -ve ret values, there are
                 * other possible ret values like ENOTCONN
                 * (returned when number of valid replies received are
                 * less than 2)
                 * in which case heal is required when one of the
                 * selfheal booleans is set.
                 */
                if (data_selfheal || entry_selfheal ||
                    metadata_selfheal) {
                        size += strlen ("heal") + 1;
                        ret = gf_asprintf (&status, "heal%s",
                                           substr? substr : "");
                        if (ret < 0)
                                goto out;
                        dict = afr_set_heal_info (status);
                }
        }
        ret = 0;

out:
        AFR_STACK_UNWIND (getxattr, frame, ret, op_errno, dict, NULL);
        if (dict)
               dict_unref (dict);
        if (inode)
                inode_unref (inode);
        GF_FREE (substr);
        return ret;
}

int
_afr_is_split_brain (call_frame_t *frame, xlator_t *this,
                         struct afr_reply *replies,
                         afr_transaction_type type,
                         gf_boolean_t *spb)
{
        afr_private_t    *priv              = NULL;
        uint64_t         *witness           = NULL;
        unsigned char    *sources           = NULL;
        unsigned char    *sinks             = NULL;
        int               sources_count     = 0;
        int               ret               = 0;

        priv = this->private;

        sources = alloca0 (priv->child_count);
        sinks = alloca0 (priv->child_count);
        witness = alloca0(priv->child_count * sizeof (*witness));

        ret = afr_selfheal_find_direction (frame, this, replies,
					   type, priv->child_up, sources,
                                           sinks, witness, NULL);
        if (ret)
                return ret;

        sources_count = AFR_COUNT (sources, priv->child_count);
        if (!sources_count)
                *spb = _gf_true;

        return ret;
}

int
afr_is_split_brain (call_frame_t *frame, xlator_t *this, inode_t *inode,
                    uuid_t gfid, gf_boolean_t *d_spb, gf_boolean_t *m_spb)
{
        int    ret                          = -1;
        afr_private_t    *priv              = NULL;
        struct afr_reply *replies           = NULL;

        priv = this->private;

        replies = alloca0 (sizeof (*replies) * priv->child_count);

        ret = afr_selfheal_unlocked_discover (frame, inode, gfid, replies);
        if (ret)
                goto out;

        if (!afr_can_decide_split_brain_source_sinks (replies,
                                                      priv->child_count)) {
                ret = -EAGAIN;
                goto out;
        }

        ret = _afr_is_split_brain (frame, this, replies,
                                    AFR_DATA_TRANSACTION, d_spb);
        if (ret)
                goto out;

        ret = _afr_is_split_brain (frame, this, replies,
                                    AFR_METADATA_TRANSACTION, m_spb);
out:
        if (replies) {
                afr_replies_wipe (replies, priv->child_count);
                replies = NULL;
        }
        return ret;
}

int
afr_get_split_brain_status_cbk (int ret, call_frame_t *frame, void *opaque)
{
        GF_FREE (opaque);
        return 0;
}

int
afr_get_split_brain_status (void *opaque)
{
        gf_boolean_t      d_spb             = _gf_false;
        gf_boolean_t      m_spb             = _gf_false;
        int               ret               = -1;
        int               op_errno          = 0;
        int               i                 = 0;
        char             *choices           = NULL;
        char             *status            = NULL;
        dict_t           *dict              = NULL;
        inode_t          *inode             = NULL;
        afr_private_t    *priv              = NULL;
        xlator_t         **children         = NULL;
        call_frame_t     *frame             = NULL;
        xlator_t         *this              = NULL;
        loc_t            *loc               = NULL;
        afr_spb_status_t *data              = NULL;

        data     = opaque;
        frame    = data->frame;
        this     = frame->this;
        loc      = data->loc;
        priv     = this->private;
        children = priv->children;

        inode = afr_inode_find (this, loc->gfid);
        if (!inode)
                goto out;

        dict = dict_new ();
        if (!dict) {
                op_errno = ENOMEM;
                ret = -1;
                goto out;
        }

        /* Calculation for string length :
        * (child_count X length of child-name) + strlen ("    Choices :")
        * child-name consists of :
        * a) 256 = max characters for volname according to GD_VOLUME_NAME_MAX
        * b) strlen ("-client-00,") assuming 16 replicas
        */
        choices = alloca0 (priv->child_count * (256 + strlen ("-client-00,")) +
                           strlen ("    Choices:"));

        ret = afr_is_split_brain (frame, this, inode, loc->gfid, &d_spb,
                                  &m_spb);
        if (ret) {
                op_errno = -ret;
                if (ret == -EAGAIN)
                        ret = dict_set_str (dict, GF_AFR_SBRAIN_STATUS,
                                            SBRAIN_HEAL_NO_GO_MSG);
                ret = -1;
                goto out;
        }

        if (d_spb || m_spb) {
                sprintf (choices, "    Choices:");
                for (i = 0; i < priv->child_count; i++) {
                        strcat (choices, children[i]->name);
                        strcat (choices, ",");
                }
                choices[strlen (choices) - 1] = '\0';

                ret = gf_asprintf (&status, "data-split-brain:%s    "
                                    "metadata-split-brain:%s%s",
                                    (d_spb) ? "yes" : "no",
                                    (m_spb) ? "yes" : "no", choices);

                if (-1 == ret) {
                        op_errno = ENOMEM;
                        goto out;
                }
                ret = dict_set_dynstr (dict, GF_AFR_SBRAIN_STATUS, status);
                if (ret) {
                        op_errno = -ret;
                        ret = -1;
                        goto out;
                }
        } else {
                ret = dict_set_str (dict, GF_AFR_SBRAIN_STATUS,
                                    "The file is not under data or"
                                    " metadata split-brain");
                if (ret) {
                        op_errno = -ret;
                        ret = -1;
                        goto out;
                }
        }

        ret = 0;
out:
        AFR_STACK_UNWIND (getxattr, frame, ret, op_errno, dict, NULL);
        if (dict)
               dict_unref (dict);
        if (inode)
                inode_unref (inode);
        return ret;
}

int32_t
afr_heal_splitbrain_file(call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        int             ret               = 0;
        int             op_errno          = 0;
        dict_t         *dict              = NULL;
        afr_local_t    *local             = NULL;

        local = frame->local;
        dict = dict_new ();
        if (!dict) {
                op_errno = ENOMEM;
                ret = -1;
                goto out;
        }

        ret = afr_selfheal_do (frame, this, loc->gfid);

        if (ret == 1 || ret == 2) {
                ret = dict_set_str (dict, "sh-fail-msg",
                                    "File not in split-brain");
                if (ret)
                        gf_msg (this->name, GF_LOG_WARNING,
                                -ret, AFR_MSG_DICT_SET_FAILED,
                                "Failed to set sh-fail-msg in dict");
                ret = 0;
                goto out;
        } else {
                if (local->xdata_rsp) {
                        /* 'sh-fail-msg' has been set in the dict during self-heal.*/
                        dict_copy (local->xdata_rsp, dict);
                        ret = 0;
                } else if (ret < 0) {
                        op_errno = -ret;
                        ret = -1;
                }
        }

out:
        if (local->op == GF_FOP_GETXATTR)
                AFR_STACK_UNWIND (getxattr, frame, ret, op_errno, dict, NULL);
        else if (local->op == GF_FOP_SETXATTR)
                AFR_STACK_UNWIND (setxattr, frame, ret, op_errno, NULL);
        if (dict)
                dict_unref(dict);
        return ret;
}

int
afr_get_child_index_from_name (xlator_t *this, char *name)
{
        afr_private_t *priv  = this->private;
        int            index = -1;

        for (index = 0; index < priv->child_count; index++) {
                if (!strcmp (priv->children[index]->name, name))
                        goto out;
        }
        index = -1;
out:
        return index;
}

void
afr_priv_need_heal_set (afr_private_t *priv, gf_boolean_t need_heal)
{
        LOCK (&priv->lock);
        {
                priv->need_heal = need_heal;
        }
        UNLOCK (&priv->lock);
}

void
afr_set_need_heal (xlator_t *this, afr_local_t *local)
{
        int             i         = 0;
        afr_private_t  *priv      = this->private;
        gf_boolean_t    need_heal = _gf_false;

        for (i = 0; i < priv->child_count; i++) {
                if (local->replies[i].valid && local->replies[i].need_heal) {
                        need_heal = _gf_true;
                        break;
                }
        }
        afr_priv_need_heal_set (priv, need_heal);
        return;
}

gf_boolean_t
afr_get_need_heal (xlator_t *this)
{
        afr_private_t  *priv      = this->private;
        gf_boolean_t    need_heal = _gf_true;

        LOCK (&priv->lock);
        {
                need_heal = priv->need_heal;
        }
        UNLOCK (&priv->lock);
        return need_heal;
}

int
afr_get_msg_id (char *op_type)
{

        if (!strcmp (op_type, GF_AFR_REPLACE_BRICK))
                return AFR_MSG_REPLACE_BRICK_STATUS;
        else if (!strcmp (op_type, GF_AFR_ADD_BRICK))
                return AFR_MSG_ADD_BRICK_STATUS;
        return -1;
}

gf_boolean_t
afr_can_compound_pre_op_and_op (afr_private_t *priv, glusterfs_fop_t fop)
{
        if (priv->arbiter_count != 0)
                return _gf_false;

        if (!priv->use_compound_fops)
                return _gf_false;

        switch (fop) {
        case GF_FOP_WRITE:
                return _gf_true;
        default:
                return _gf_false;
        }
}

afr_compound_cbk_t
afr_pack_fop_args (call_frame_t *frame, compound_args_t *args,
                   glusterfs_fop_t fop, int index)
{
        afr_local_t     *local  = frame->local;

        switch (fop) {
        case GF_FOP_WRITE:
                COMPOUND_PACK_ARGS (writev, GF_FOP_WRITE,
                                    args, index,
                                    local->fd, local->cont.writev.vector,
                                    local->cont.writev.count,
                                    local->cont.writev.offset,
                                    local->cont.writev.flags,
                                    local->cont.writev.iobref,
                                    local->xdata_req);
                return afr_pre_op_writev_cbk;
        default:
                break;
        }
        return NULL;
}

int
afr_fav_child_reset_sink_xattrs_cbk (int ret, call_frame_t *heal_frame,
                                     void *opaque)
{

        call_frame_t *txn_frame = NULL;
        afr_local_t *local = NULL;
        afr_local_t *heal_local = NULL;
        xlator_t *this = NULL;

        heal_local = heal_frame->local;
        txn_frame = heal_local->heal_frame;
        local = txn_frame->local;
        this = txn_frame->this;

        /* Refresh the inode agan and proceed with the transaction.*/
        afr_inode_refresh (txn_frame, this, local->inode, NULL,
                           local->refreshfn);

        if (heal_frame)
                AFR_STACK_DESTROY (heal_frame);

        return 0;
}

int
afr_fav_child_reset_sink_xattrs (void *opaque)
{
        call_frame_t *heal_frame = NULL;
        call_frame_t *txn_frame = NULL;
        xlator_t *this = NULL;
        gf_boolean_t d_spb = _gf_false;
        gf_boolean_t m_spb = _gf_false;
        afr_local_t *heal_local = NULL;
        afr_local_t *txn_local = NULL;
        afr_private_t *priv = NULL;
        inode_t *inode  = NULL;
        unsigned char *locked_on = NULL;
        unsigned char *sources = NULL;
        unsigned char *sinks = NULL;
        unsigned char *healed_sinks = NULL;
        unsigned char *undid_pending = NULL;
        struct afr_reply *locked_replies = NULL;
        int ret = 0;

        heal_frame = (call_frame_t *) opaque;
        heal_local = heal_frame->local;
        txn_frame = heal_local->heal_frame;
        txn_local = txn_frame->local;
        this = txn_frame->this;
        inode = txn_local->inode;
        priv = this->private;
        locked_on = alloca0 (priv->child_count);
        sources = alloca0 (priv->child_count);
        sinks = alloca0 (priv->child_count);
        healed_sinks = alloca0 (priv->child_count);
        undid_pending = alloca0 (priv->child_count);
        locked_replies = alloca0 (sizeof (*locked_replies) * priv->child_count);

        ret = _afr_is_split_brain (txn_frame, this, txn_local->replies,
                                   AFR_DATA_TRANSACTION, &d_spb);

        ret = _afr_is_split_brain (txn_frame, this, txn_local->replies,
                                   AFR_METADATA_TRANSACTION, &m_spb);

        /* Take appropriate locks and reset sink xattrs. */
        if (d_spb) {
                ret = afr_selfheal_inodelk (heal_frame, this, inode, this->name,
                                            0, 0, locked_on);
                {
                        if (ret < AFR_SH_MIN_PARTICIPANTS)
                                goto data_unlock;
                        ret = __afr_selfheal_data_prepare (heal_frame, this,
                                                           inode, locked_on,
                                                           sources, sinks,
                                                           healed_sinks,
                                                           undid_pending,
                                                           locked_replies,
                                                           NULL);
                }
data_unlock:
                afr_selfheal_uninodelk (heal_frame, this, inode, this->name,
                                        0, 0, locked_on);
        }

        if (m_spb) {
                memset (locked_on, 0, sizeof (*locked_on) * priv->child_count);
                memset (undid_pending, 0,
                        sizeof (*undid_pending) * priv->child_count);
                ret = afr_selfheal_inodelk (heal_frame, this, inode, this->name,
                                            LLONG_MAX-1, 0, locked_on);
                {
                        if (ret < AFR_SH_MIN_PARTICIPANTS)
                                goto mdata_unlock;
                        ret = __afr_selfheal_metadata_prepare (heal_frame, this,
                                                               inode, locked_on,
                                                               sources, sinks,
                                                               healed_sinks,
                                                               undid_pending,
                                                               locked_replies,
                                                               NULL);

                }
mdata_unlock:
                afr_selfheal_uninodelk (heal_frame, this, inode, this->name,
                                        LLONG_MAX-1, 0, locked_on);
        }

        return ret;

}
