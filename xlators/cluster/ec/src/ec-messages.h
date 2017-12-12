/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _EC_MESSAGES_H_
#define _EC_MESSAGES_H_

#include "glfs-message-id.h"

/* To add new message IDs, append new identifiers at the end of the list.
 *
 * Never remove a message ID. If it's not used anymore, you can rename it or
 * leave it as it is, but not delete it. This is to prevent reutilization of
 * IDs by other messages.
 *
 * The component name must match one of the entries defined in
 * glfs-message-id.h.
 */

GLFS_MSGID(EC,
        EC_MSG_INVALID_CONFIG,
        EC_MSG_HEAL_FAIL,
        EC_MSG_DICT_COMBINE_FAIL,
        EC_MSG_STIME_COMBINE_FAIL,
        EC_MSG_INVALID_DICT_NUMS,
        EC_MSG_IATT_COMBINE_FAIL,
        EC_MSG_INVALID_FORMAT,
        EC_MSG_DICT_GET_FAILED,
        EC_MSG_UNHANDLED_STATE,
        EC_MSG_FILE_DESC_REF_FAIL,
        EC_MSG_LOC_COPY_FAIL,
        EC_MSG_BUF_REF_FAIL,
        EC_MSG_DICT_REF_FAIL,
        EC_MSG_LK_UNLOCK_FAILED,
        EC_MSG_UNLOCK_FAILED,
        EC_MSG_LOC_PARENT_INODE_MISSING,
        EC_MSG_INVALID_LOC_NAME,
        EC_MSG_NO_MEMORY,
        EC_MSG_GFID_MISMATCH,
        EC_MSG_UNSUPPORTED_VERSION,
        EC_MSG_FD_CREATE_FAIL,
        EC_MSG_READDIRP_REQ_PREP_FAIL,
        EC_MSG_LOOKUP_REQ_PREP_FAIL,
        EC_MSG_INODE_REF_FAIL,
        EC_MSG_LOOKUP_READAHEAD_FAIL,
        EC_MSG_FRAME_MISMATCH,
        EC_MSG_XLATOR_MISMATCH,
        EC_MSG_VECTOR_MISMATCH,
        EC_MSG_IATT_MISMATCH,
        EC_MSG_FD_MISMATCH,
        EC_MSG_DICT_MISMATCH,
        EC_MSG_INDEX_DIR_GET_FAIL,
        EC_MSG_PREOP_LOCK_FAILED,
        EC_MSG_CHILDS_INSUFFICIENT,
        EC_MSG_OP_EXEC_UNAVAIL,
        EC_MSG_UNLOCK_DELAY_FAILED,
        EC_MSG_SIZE_VERS_UPDATE_FAIL,
        EC_MSG_INVALID_REQUEST,
        EC_MSG_INVALID_LOCK_TYPE,
        EC_MSG_SIZE_VERS_GET_FAIL,
        EC_MSG_FILE_SIZE_GET_FAIL,
        EC_MSG_FOP_MISMATCH,
        EC_MSG_SUBVOL_ID_DICT_SET_FAIL,
        EC_MSG_SUBVOL_BUILD_FAIL,
        EC_MSG_XLATOR_INIT_FAIL,
        EC_MSG_NO_PARENTS,
        EC_MSG_TIMER_CREATE_FAIL,
        EC_MSG_TOO_MANY_SUBVOLS,
        EC_MSG_DATA_UNAVAILABLE,
        EC_MSG_INODE_REMOVE_FAIL,
        EC_MSG_INVALID_REDUNDANCY,
        EC_MSG_XLATOR_PARSE_OPT_FAIL,
        EC_MSG_OP_FAIL_ON_SUBVOLS,
        EC_MSG_INVALID_INODE,
        EC_MSG_LOCK_MISMATCH,
        EC_MSG_XDATA_MISMATCH,
        EC_MSG_HEALING_INFO,
        EC_MSG_HEAL_SUCCESS,
        EC_MSG_FULL_SWEEP_START,
        EC_MSG_FULL_SWEEP_STOP,
        EC_MSG_INVALID_FOP,
        EC_MSG_EC_UP,
        EC_MSG_EC_DOWN,
        EC_MSG_SIZE_XATTR_GET_FAIL,
        EC_MSG_VER_XATTR_GET_FAIL,
        EC_MSG_CONFIG_XATTR_GET_FAIL,
        EC_MSG_CONFIG_XATTR_INVALID,
        EC_MSG_EXTENSION,
        EC_MSG_EXTENSION_NONE,
        EC_MSG_EXTENSION_UNKNOWN,
        EC_MSG_EXTENSION_UNSUPPORTED,
        EC_MSG_EXTENSION_FAILED,
        EC_MSG_NO_GF,
        EC_MSG_MATRIX_FAILED,
        EC_MSG_DYN_CREATE_FAILED,
        EC_MSG_DYN_CODEGEN_FAILED
);

#endif /* !_EC_MESSAGES_H_ */
