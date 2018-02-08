/*
  Copyright (c) 2016 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _DHT_LOCK_H
#define _DHT_LOCK_H

#include "xlator.h"
#include "dht-common.h"

void
dht_lock_array_free (dht_lock_t **lk_array, int count);

int32_t
dht_lock_count (dht_lock_t **lk_array, int lk_count);

dht_lock_t *
dht_lock_new (xlator_t *this, xlator_t *xl, loc_t *loc, short type,
              const char *domain, const char *basename,
              dht_reaction_type_t do_on_failure);

int32_t
dht_unlock_entrylk_wrapper (call_frame_t *, dht_elock_wrap_t *);

void
dht_blocking_entrylk_rec (call_frame_t *frame, int i);

int
dht_blocking_entrylk (call_frame_t *frame, dht_lock_t **lk_array,
                      int lk_count, fop_inodelk_cbk_t entrylk_cbk);

int32_t
dht_unlock_inodelk (call_frame_t *frame, dht_lock_t **lk_array, int lk_count,
                     fop_inodelk_cbk_t inodelk_cbk);

int32_t
dht_unlock_inodelk_wrapper (call_frame_t *, dht_ilock_wrap_t *);

/* Acquire non-blocking inodelk on a list of xlators.
 *
 * @lk_array: array of lock requests lock on.
 *
 * @lk_count: number of locks in @lk_array
 *
 * @inodelk_cbk: will be called after inodelk replies are received
 *
 * @retval: -1 if stack_winding inodelk fails. 0 otherwise.
 *          inodelk_cbk is called with appropriate error on errors.
 *          On failure to acquire lock on all members of list, successful
 *          locks are unlocked before invoking cbk.
 */

int
dht_nonblocking_inodelk (call_frame_t *frame, dht_lock_t **lk_array,
                         int lk_count, fop_inodelk_cbk_t inodelk_cbk);

void
dht_blocking_inodelk_rec (call_frame_t *frame, int i);

/* same as dht_nonblocking_inodelk, but issues sequential blocking locks on
 * @lk_array directly. locks are issued on some order which remains same
 * for a list of xlators (irrespective of order of xlators within list).
 */

int
dht_blocking_inodelk (call_frame_t *frame, dht_lock_t **lk_array,
                      int lk_count, fop_inodelk_cbk_t inodelk_cbk);

int32_t
dht_blocking_entrylk_after_inodelk (call_frame_t *frame, void *cookie,
                                    xlator_t *this, int32_t op_ret,
                                    int32_t op_errno, dict_t *xdata);

int32_t
dht_blocking_entrylk_after_inodelk_rename (call_frame_t *frame, void *cookie,
                                           xlator_t *this, int32_t op_ret,
                                           int32_t op_errno, dict_t *xdata);

void
dht_unlock_namespace (call_frame_t *, dht_dir_transaction_t *);

int
dht_protect_namespace (call_frame_t *frame, loc_t *loc, xlator_t *subvol,
                       struct dht_namespace *ns,
                       fop_entrylk_cbk_t ns_cbk);

#endif   /* _DHT_LOCK_H */
