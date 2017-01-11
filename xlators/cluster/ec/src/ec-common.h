/*
  Copyright (c) 2012-2014 DataLab, s.l. <http://www.datalab.es>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef __EC_COMMON_H__
#define __EC_COMMON_H__

#include "xlator.h"

#include "ec-data.h"

typedef enum {
        EC_DATA_TXN,
        EC_METADATA_TXN
} ec_txn_t;

#define EC_FOP_HEAL     -1
#define EC_FOP_FHEAL    -2

#define EC_CONFIG_VERSION 0

#define EC_CONFIG_ALGORITHM 0

#define EC_FLAG_LOCK_SHARED       0x0001
#define EC_FLAG_WAITING_XATTROP   0x0002
#define EC_FLAG_QUERY_METADATA    0x0004

#define EC_SELFHEAL_BIT 62

#define EC_MINIMUM_ONE   -1
#define EC_MINIMUM_MIN   -2
#define EC_MINIMUM_ALL   -3

#define EC_UPDATE_DATA   1
#define EC_UPDATE_META   2
#define EC_QUERY_INFO    4
#define EC_INODE_SIZE    8

#define EC_STATE_START                        0
#define EC_STATE_END                          0
#define EC_STATE_INIT                         1
#define EC_STATE_LOCK                         2
#define EC_STATE_DISPATCH                     3
#define EC_STATE_PREPARE_ANSWER               4
#define EC_STATE_REPORT                       5
#define EC_STATE_LOCK_REUSE                   6
#define EC_STATE_UNLOCK                       7

#define EC_STATE_DELAYED_START              100

#define EC_STATE_HEAL_ENTRY_LOOKUP          200
#define EC_STATE_HEAL_ENTRY_PREPARE         201
#define EC_STATE_HEAL_PRE_INODELK_LOCK      202
#define EC_STATE_HEAL_PRE_INODE_LOOKUP      203
#define EC_STATE_HEAL_XATTRIBUTES_REMOVE    204
#define EC_STATE_HEAL_XATTRIBUTES_SET       205
#define EC_STATE_HEAL_ATTRIBUTES            206
#define EC_STATE_HEAL_OPEN                  207
#define EC_STATE_HEAL_REOPEN_FD             208
#define EC_STATE_HEAL_UNLOCK                209
#define EC_STATE_HEAL_UNLOCK_ENTRY          210
#define EC_STATE_HEAL_DATA_LOCK             211
#define EC_STATE_HEAL_DATA_COPY             212
#define EC_STATE_HEAL_DATA_UNLOCK           213
#define EC_STATE_HEAL_POST_INODELK_LOCK     214
#define EC_STATE_HEAL_POST_INODE_LOOKUP     215
#define EC_STATE_HEAL_SETATTR               216
#define EC_STATE_HEAL_POST_INODELK_UNLOCK   217
#define EC_STATE_HEAL_DISPATCH              218

gf_boolean_t ec_dispatch_one_retry (ec_fop_data_t *fop, ec_cbk_data_t **cbk);
int32_t ec_dispatch_next(ec_fop_data_t * fop, int32_t idx);

void ec_complete(ec_fop_data_t *fop);

void ec_update_good(ec_fop_data_t *fop, uintptr_t good);

void ec_fop_set_error(ec_fop_data_t *fop, int32_t error);

ec_cbk_data_t *
ec_fop_prepare_answer(ec_fop_data_t *fop, gf_boolean_t ro);

gf_boolean_t
ec_cbk_set_error(ec_cbk_data_t *cbk, int32_t error, gf_boolean_t ro);

void ec_lock_prepare_inode(ec_fop_data_t *fop, loc_t *loc, uint32_t flags);
void ec_lock_prepare_parent_inode(ec_fop_data_t *fop, loc_t *loc, loc_t *base,
                                  uint32_t flags);
void ec_lock_prepare_fd(ec_fop_data_t *fop, fd_t *fd, uint32_t flags);
void ec_lock(ec_fop_data_t * fop);
void ec_lock_reuse(ec_fop_data_t *fop);
void ec_unlock(ec_fop_data_t * fop);

gf_boolean_t ec_get_inode_size(ec_fop_data_t *fop, inode_t *inode,
                               uint64_t *size);
gf_boolean_t ec_set_inode_size(ec_fop_data_t *fop, inode_t *inode,
                               uint64_t size);
void ec_clear_inode_info(ec_fop_data_t *fop, inode_t *inode);

void ec_flush_size_version(ec_fop_data_t * fop);

void ec_dispatch_all(ec_fop_data_t * fop);
void ec_dispatch_inc(ec_fop_data_t * fop);
void ec_dispatch_min(ec_fop_data_t * fop);
void ec_dispatch_one(ec_fop_data_t * fop);

void ec_sleep(ec_fop_data_t *fop);
void ec_resume(ec_fop_data_t * fop, int32_t error);
void ec_resume_parent(ec_fop_data_t * fop, int32_t error);

void ec_manager(ec_fop_data_t * fop, int32_t error);
gf_boolean_t ec_is_recoverable_error (int32_t op_errno);
void ec_handle_healers_done (ec_fop_data_t *fop);

int32_t
ec_heal_inspect (call_frame_t *frame, ec_t *ec,
                 inode_t *inode, unsigned char *locked_on,
                 gf_boolean_t *need_heal);
int32_t
ec_get_heal_info (xlator_t *this, loc_t *loc, dict_t **dict);

#endif /* __EC_COMMON_H__ */
