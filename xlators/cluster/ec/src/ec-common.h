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

#include "glusterfs/compat-errno.h"  // for ENODATA on BSD
#include "ec-data.h"

typedef enum { EC_DATA_TXN, EC_METADATA_TXN } ec_txn_t;

#define EC_FOP_HEAL -1
#define EC_FOP_FHEAL -2

#define EC_CONFIG_VERSION 0

#define EC_CONFIG_ALGORITHM 0

#define EC_FLAG_LOCK_SHARED 0x0001

#define QUORUM_CBK(fn, fop, frame, cookie, this, op_ret, op_errno, params...)  \
    do {                                                                       \
        ec_t *__ec = fop->xl->private;                                         \
        int32_t __op_ret = 0;                                                  \
        int32_t __op_errno = 0;                                                \
        int32_t __success_count = gf_bits_count(fop->good);                    \
                                                                               \
        __op_ret = op_ret;                                                     \
        __op_errno = op_errno;                                                 \
        if (!fop->parent && frame &&                                           \
            (GF_CLIENT_PID_SELF_HEALD != frame->root->pid) &&                  \
            __ec->quorum_count && (__success_count < __ec->quorum_count) &&    \
            op_ret >= 0) {                                                     \
            __op_ret = -1;                                                     \
            __op_errno = EIO;                                                  \
            gf_msg(__ec->xl->name, GF_LOG_ERROR, 0,                            \
                   EC_MSG_CHILDS_INSUFFICIENT,                                 \
                   "Insufficient available children for this request "         \
                   "(have %d, need %d). %s",                                   \
                   __success_count, __ec->quorum_count, ec_msg_str(fop));      \
        }                                                                      \
        fn(frame, cookie, this, __op_ret, __op_errno, params);                 \
    } while (0)

enum _ec_xattrop_flags {
    EC_FLAG_XATTROP,
    EC_FLAG_DATA_DIRTY,
    EC_FLAG_METADATA_DIRTY,

    /* Add any new flag here, before EC_FLAG_MAX. The maximum number of
     * flags that can be defined is 16. */

    EC_FLAG_MAX
};

/* We keep two sets of flags. One to determine what's really providing the
 * current xattrop and the other to know what the parent fop of the xattrop
 * needs to proceed. It might happen that a fop needs some information that
 * is being already requested by a previous fop. The two sets are stored
 * contiguously. */

#define EC_FLAG_NEEDS(_flag) (1 << (_flag))
#define EC_FLAG_PROVIDES(_flag) (1 << ((_flag) + EC_FLAG_MAX))

#define EC_NEEDED_FLAGS(_flags) ((_flags) & ((1 << EC_FLAG_MAX) - 1))

#define EC_PROVIDED_FLAGS(_flags) EC_NEEDED_FLAGS((_flags) >> EC_FLAG_MAX)

#define EC_FLAGS_HAVE(_flags, _flag) (((_flags) & (1 << (_flag))) != 0)

#define EC_SELFHEAL_BIT 62

#define EC_MINIMUM_ONE (1 << 6)
#define EC_MINIMUM_MIN (2 << 6)
#define EC_MINIMUM_ALL (3 << 6)
#define EC_FOP_NO_PROPAGATE_ERROR (1 << 8)
#define EC_FOP_MINIMUM(_flags) ((_flags)&255)
#define EC_FOP_FLAGS(_flags) ((_flags) & ~255)

#define EC_UPDATE_DATA 1
#define EC_UPDATE_META 2
#define EC_QUERY_INFO 4
#define EC_INODE_SIZE 8

#define EC_STATE_START 0
#define EC_STATE_END 0
#define EC_STATE_INIT 1
#define EC_STATE_LOCK 2
#define EC_STATE_DISPATCH 3
#define EC_STATE_PREPARE_ANSWER 4
#define EC_STATE_REPORT 5
#define EC_STATE_LOCK_REUSE 6
#define EC_STATE_UNLOCK 7

#define EC_STATE_DELAYED_START 100

#define EC_STATE_HEAL_ENTRY_LOOKUP 200
#define EC_STATE_HEAL_ENTRY_PREPARE 201
#define EC_STATE_HEAL_PRE_INODELK_LOCK 202
#define EC_STATE_HEAL_PRE_INODE_LOOKUP 203
#define EC_STATE_HEAL_XATTRIBUTES_REMOVE 204
#define EC_STATE_HEAL_XATTRIBUTES_SET 205
#define EC_STATE_HEAL_ATTRIBUTES 206
#define EC_STATE_HEAL_OPEN 207
#define EC_STATE_HEAL_REOPEN_FD 208
#define EC_STATE_HEAL_UNLOCK 209
#define EC_STATE_HEAL_UNLOCK_ENTRY 210
#define EC_STATE_HEAL_DATA_LOCK 211
#define EC_STATE_HEAL_DATA_COPY 212
#define EC_STATE_HEAL_DATA_UNLOCK 213
#define EC_STATE_HEAL_POST_INODELK_LOCK 214
#define EC_STATE_HEAL_POST_INODE_LOOKUP 215
#define EC_STATE_HEAL_SETATTR 216
#define EC_STATE_HEAL_POST_INODELK_UNLOCK 217
#define EC_STATE_HEAL_DISPATCH 218

/* Value to cover the full range of a file */
#define EC_RANGE_FULL ((uint64_t)LLONG_MAX + 1)

gf_boolean_t
ec_dispatch_one_retry(ec_fop_data_t *fop, ec_cbk_data_t **cbk);
void
ec_dispatch_next(ec_fop_data_t *fop, uint32_t idx);

void
ec_complete(ec_fop_data_t *fop);

void
ec_update_good(ec_fop_data_t *fop, uintptr_t good);

void
ec_fop_set_error(ec_fop_data_t *fop, int32_t error);

void
__ec_fop_set_error(ec_fop_data_t *fop, int32_t error);

ec_cbk_data_t *
ec_fop_prepare_answer(ec_fop_data_t *fop, gf_boolean_t ro);

gf_boolean_t
ec_cbk_set_error(ec_cbk_data_t *cbk, int32_t error, gf_boolean_t ro);

void
ec_lock_prepare_inode(ec_fop_data_t *fop, loc_t *loc, uint32_t flags,
                      off_t fl_start, uint64_t fl_size);
void
ec_lock_prepare_parent_inode(ec_fop_data_t *fop, loc_t *loc, loc_t *base,
                             uint32_t flags);
void
ec_lock_prepare_fd(ec_fop_data_t *fop, fd_t *fd, uint32_t flags, off_t fl_start,
                   uint64_t fl_size);
void
ec_lock(ec_fop_data_t *fop);
void
ec_lock_reuse(ec_fop_data_t *fop);
void
ec_unlock(ec_fop_data_t *fop);
void
ec_lock_release(ec_t *ec, inode_t *inode);

gf_boolean_t
ec_get_inode_size(ec_fop_data_t *fop, inode_t *inode, uint64_t *size);
gf_boolean_t
__ec_get_inode_size(ec_fop_data_t *fop, inode_t *inode, uint64_t *size);
gf_boolean_t
ec_set_inode_size(ec_fop_data_t *fop, inode_t *inode, uint64_t size);
gf_boolean_t
__ec_set_inode_size(ec_fop_data_t *fop, inode_t *inode, uint64_t size);
void
ec_clear_inode_info(ec_fop_data_t *fop, inode_t *inode);

void
ec_flush_size_version(ec_fop_data_t *fop);

void
ec_dispatch_all(ec_fop_data_t *fop);
void
ec_dispatch_inc(ec_fop_data_t *fop);
void
ec_dispatch_min(ec_fop_data_t *fop);
void
ec_dispatch_one(ec_fop_data_t *fop);

void
ec_succeed_all(ec_fop_data_t *fop);

void
ec_sleep(ec_fop_data_t *fop);
void
ec_resume(ec_fop_data_t *fop, int32_t error);
void
ec_resume_parent(ec_fop_data_t *fop);

void
ec_manager(ec_fop_data_t *fop, int32_t error);
gf_boolean_t
ec_is_recoverable_error(int32_t op_errno);
void
ec_handle_healers_done(ec_fop_data_t *fop);

int32_t
ec_heal_inspect(call_frame_t *frame, ec_t *ec, inode_t *inode,
                unsigned char *locked_on, gf_boolean_t self_locked,
                gf_boolean_t thorough, ec_heal_need_t *need_heal);
int32_t
ec_get_heal_info(xlator_t *this, loc_t *loc, dict_t **dict);

int32_t
ec_lock_unlocked(call_frame_t *frame, void *cookie, xlator_t *this,
                 int32_t op_ret, int32_t op_errno, dict_t *xdata);

void
ec_update_fd_status(fd_t *fd, xlator_t *xl, int child_index,
                    int32_t ret_status);
gf_boolean_t
ec_is_entry_healing(ec_fop_data_t *fop);
void
ec_set_entry_healing(ec_fop_data_t *fop);
void
ec_reset_entry_healing(ec_fop_data_t *fop);
char *
ec_msg_str(ec_fop_data_t *fop);
gf_boolean_t
__ec_is_last_fop(ec_t *ec);
void
ec_lock_update_good(ec_lock_t *lock, ec_fop_data_t *fop);
#endif /* __EC_COMMON_H__ */
