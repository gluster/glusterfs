/*
  Copyright (c) 2012 DataLab, s.l. <http://www.datalab.es>

  This file is part of the cluster/ec translator for GlusterFS.

  The cluster/ec translator for GlusterFS is free software: you can
  redistribute it and/or modify it under the terms of the GNU General
  Public License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.

  The cluster/ec translator for GlusterFS is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE. See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with the cluster/ec translator for GlusterFS. If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef __EC_COMMON_H__
#define __EC_COMMON_H__

#include "xlator.h"

#include "ec-data.h"

#define EC_CONFIG_VERSION 0

#define EC_CONFIG_ALGORITHM 0

#define EC_FLAG_UPDATE_LOC_PARENT 0x0001
#define EC_FLAG_UPDATE_LOC_INODE  0x0002
#define EC_FLAG_UPDATE_FD         0x0004
#define EC_FLAG_UPDATE_FD_INODE   0x0008

#define EC_FLAG_WAITING_WINDS     0x0010

#define EC_MINIMUM_ONE   -1
#define EC_MINIMUM_MIN   -2
#define EC_MINIMUM_ALL   -3

#define EC_LOCK_ENTRY   0
#define EC_LOCK_INODE   1

#define EC_STATE_START                        0
#define EC_STATE_END                          0
#define EC_STATE_INIT                         1
#define EC_STATE_LOCK                         2
#define EC_STATE_GET_SIZE_AND_VERSION         3
#define EC_STATE_DISPATCH                     4
#define EC_STATE_PREPARE_ANSWER               5
#define EC_STATE_REPORT                       6
#define EC_STATE_LOCK_REUSE                   7
#define EC_STATE_UNLOCK                       8

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

int32_t ec_dispatch_one_retry(ec_fop_data_t * fop, int32_t idx, int32_t op_ret,
                              int32_t op_errno);
int32_t ec_dispatch_next(ec_fop_data_t * fop, int32_t idx);

void ec_complete(ec_fop_data_t * fop);

void ec_update_bad(ec_fop_data_t * fop, uintptr_t good);

void ec_fop_set_error(ec_fop_data_t * fop, int32_t error);

void ec_lock_prepare_inode(ec_fop_data_t *fop, loc_t *loc, int32_t update);
void ec_lock_prepare_entry(ec_fop_data_t *fop, loc_t *loc, int32_t update);
void ec_lock_prepare_fd(ec_fop_data_t *fop, fd_t *fd, int32_t update);
void ec_lock(ec_fop_data_t * fop);
void ec_lock_reuse(ec_fop_data_t *fop);
void ec_unlock(ec_fop_data_t * fop);

void ec_get_size_version(ec_fop_data_t * fop);
void ec_flush_size_version(ec_fop_data_t * fop);

void ec_dispatch_all(ec_fop_data_t * fop);
void ec_dispatch_inc(ec_fop_data_t * fop);
void ec_dispatch_min(ec_fop_data_t * fop);
void ec_dispatch_one(ec_fop_data_t * fop);

void ec_wait_winds(ec_fop_data_t * fop);

void ec_resume(ec_fop_data_t * fop, int32_t error);
void ec_resume_parent(ec_fop_data_t * fop, int32_t error);

void ec_manager(ec_fop_data_t * fop, int32_t error);

#endif /* __EC_COMMON_H__ */
