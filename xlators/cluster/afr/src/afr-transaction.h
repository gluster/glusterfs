/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __TRANSACTION_H__
#define __TRANSACTION_H__

#include "afr.h"

void
afr_transaction_fop_failed (call_frame_t *frame, xlator_t *this,
			    int child_index);

int32_t
afr_transaction (call_frame_t *frame, xlator_t *this, afr_transaction_type type);

int
afr_set_pending_dict (afr_private_t *priv, dict_t *xattr, int32_t **pending);

void
afr_delayed_changelog_wake_up (xlator_t *this, fd_t *fd);

void
__mark_all_success (call_frame_t *frame, xlator_t *this);

gf_boolean_t
afr_txn_nothing_failed (call_frame_t *frame, xlator_t *this);

int afr_read_txn (call_frame_t *frame, xlator_t *this, inode_t *inode,
		  afr_read_txn_wind_t readfn, afr_transaction_type type);

int afr_read_txn_continue (call_frame_t *frame, xlator_t *this, int subvol);

void
afr_pending_read_increment (afr_private_t *priv, int child_index);

void
afr_pending_read_decrement (afr_private_t *priv, int child_index);

call_frame_t *afr_transaction_detach_fop_frame (call_frame_t *frame);
gf_boolean_t afr_has_quorum (unsigned char *subvols, xlator_t *this);
gf_boolean_t afr_needs_changelog_update (afr_local_t *local);
void afr_zero_fill_stat (afr_local_t *local);

void
afr_pick_error_xdata (afr_local_t *local, afr_private_t *priv,
                      inode_t *inode1, unsigned char *readable1,
                      inode_t *inode2, unsigned char *readable2);
int
afr_transaction_resume (call_frame_t *frame, xlator_t *this);

int
afr_lock (call_frame_t *frame, xlator_t *this);

void
afr_delayed_changelog_wake_up_cbk (void *data);

int
afr_fill_ta_loc (xlator_t *this, loc_t *loc);
#endif /* __TRANSACTION_H__ */
