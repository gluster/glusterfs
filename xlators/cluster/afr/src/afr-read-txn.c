/*
  Copyright (c) 2013 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "afr.h"
#include "afr-transaction.h"

int
afr_read_txn_next_subvol (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;
	int subvol = -1;

	local = frame->local;
	priv = this->private;


	for (i = 0; i < priv->child_count; i++) {
		if (!local->readable[i]) {
			/* don't even bother trying here.
			   just mark as attempted and move on. */
			local->read_attempted[i] = 1;
			continue;
		}

		if (!local->read_attempted[i]) {
			subvol = i;
			break;
		}
	}

	/* If no more subvols were available for reading, we leave
	   @subvol as -1, which is an indication we have run out of
	   readable subvols. */
	if (subvol != -1)
		local->read_attempted[subvol] = 1;
	local->readfn (frame, this, subvol);

	return 0;
}


int
afr_read_txn_refresh_done (call_frame_t *frame, xlator_t *this, int err)
{
	afr_local_t *local = NULL;
	int read_subvol = 0;
	int event_generation = 0;
	inode_t *inode = NULL;
	int ret = -1;

	local = frame->local;
	inode = local->inode;

	if (err) {
		local->op_errno = -err;
		local->op_ret = -1;
		read_subvol = -1;
		goto readfn;
	}

	ret = afr_inode_read_subvol_type_get (inode, this, local->readable,
					      &event_generation,
					      local->transaction.type);

	if (ret == -1 || !event_generation) {
		/* Even after refresh, we don't have a good
		   read subvolume. Time to bail */
		local->op_ret = -1;
		local->op_errno = EIO;
		read_subvol = -1;
		goto readfn;
	}

	read_subvol = afr_read_subvol_select_by_policy (inode, this,
							local->readable);

	if (read_subvol == -1) {
		local->op_ret = -1;
		local->op_errno = EIO;
		goto readfn;
	}

	if (local->read_attempted[read_subvol]) {
		afr_read_txn_next_subvol (frame, this);
		return 0;
	}

	local->read_attempted[read_subvol] = 1;
readfn:
	local->readfn (frame, this, read_subvol);

	return 0;
}


int
afr_read_txn_continue (call_frame_t *frame, xlator_t *this, int subvol)
{
	afr_local_t *local = NULL;

	local = frame->local;

	if (!local->refreshed) {
		local->refreshed = _gf_true;
		afr_inode_refresh (frame, this, local->inode,
				   afr_read_txn_refresh_done);
	} else {
		afr_read_txn_next_subvol (frame, this);
	}

	return 0;
}


/* afr_read_txn_wipe:

   clean internal variables in @local in order to make
   it possible to call afr_read_txn() multiple times from
   the same frame
*/

void
afr_read_txn_wipe (call_frame_t *frame, xlator_t *this)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int i = 0;

	local = frame->local;
	priv = this->private;

	local->readfn = NULL;

	if (local->inode)
		inode_unref (local->inode);

	for (i = 0; i < priv->child_count; i++) {
		local->read_attempted[i] = 0;
		local->readable[i] = 0;
	}
}


/*
  afr_read_txn:

  This is the read transaction function. The way it works:

  - Determine read-subvolume from inode ctx.

  - If read-subvolume's generation was stale, refresh ctx once by
    calling afr_inode_refresh()

    Else make an attempt to read on read-subvolume.

  - If attempted read on read-subvolume fails, refresh ctx once
    by calling afr_inode_refresh()

  - After ctx refresh, query read-subvolume freshly and attempt
    read once.

  - If read fails, try every other readable[] subvolume before
    finally giving up. readable[] elements are set by afr_inode_refresh()
    based on dirty and pending flags.

  - If file is in split brain in the backend, generation will be
    kept 0 by afr_inode_refresh() and readable[] will be set 0 for
    all elements. Therefore reads always fail.
*/

int
afr_read_txn (call_frame_t *frame, xlator_t *this, inode_t *inode,
	      afr_read_txn_wind_t readfn, afr_transaction_type type)
{
	afr_local_t *local = NULL;
	afr_private_t *priv = NULL;
	int read_subvol = -1;
	int event_generation = 0;
	int ret = -1;

	priv = this->private;
	local = frame->local;

	afr_read_txn_wipe (frame, this);

	local->readfn = readfn;
	local->inode = inode_ref (inode);

	local->transaction.type = type;
	ret = afr_inode_read_subvol_type_get (inode, this, local->readable,
					      &event_generation, type);
	if (ret == -1)
		/* very first transaction on this inode */
		goto refresh;

        gf_log (this->name, GF_LOG_DEBUG, "%s: generation now vs cached: %d, "
                "%d", uuid_utoa (inode->gfid), local->event_generation,
                event_generation);
	if (local->event_generation != event_generation)
		/* servers have disconnected / reconnected, and possibly
		   rebooted, very likely changing the state of freshness
		   of copies */
		goto refresh;

	read_subvol = afr_read_subvol_select_by_policy (inode, this,
							local->readable);

	if (read_subvol < 0 || read_subvol > priv->child_count) {
		gf_msg (this->name, GF_LOG_WARNING, 0, AFR_MSG_SPLIT_BRAIN,
                       "Unreadable subvolume %d found with event generation "
                       "%d. (Possible split-brain)",
                        read_subvol, event_generation);
		goto refresh;
	}

	if (!local->child_up[read_subvol]) {
		/* should never happen, just in case */
		gf_log (this->name, GF_LOG_WARNING, "subvolume %d is the "
			"read subvolume in this generation, but is not up",
			read_subvol);
		goto refresh;
	}

	local->read_attempted[read_subvol] = 1;

	local->readfn (frame, this, read_subvol);

	return 0;

refresh:
	afr_inode_refresh (frame, this, inode, afr_read_txn_refresh_done);

	return 0;
}
