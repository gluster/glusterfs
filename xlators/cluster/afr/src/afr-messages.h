/*
 Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _AFR_MESSAGES_H_
#define _AFR_MESSAGES_H_

#include <glusterfs/glfs-message-id.h>

/* To add new message IDs, append new identifiers at the end of the list.
 *
 * Never remove a message ID. If it's not used anymore, you can rename it or
 * leave it as it is, but not delete it. This is to prevent reutilization of
 * IDs by other messages.
 *
 * The component name must match one of the entries defined in
 * glfs-message-id.h.
 */

GLFS_MSGID(
    AFR, AFR_MSG_QUORUM_FAIL, AFR_MSG_QUORUM_MET, AFR_MSG_QUORUM_OVERRIDE,
    AFR_MSG_INVALID_CHILD_UP, AFR_MSG_SUBVOL_UP, AFR_MSG_SUBVOLS_DOWN,
    AFR_MSG_ENTRY_UNLOCK_FAIL, AFR_MSG_SPLIT_BRAIN, AFR_MSG_OPEN_FAIL,
    AFR_MSG_UNLOCK_FAIL, AFR_MSG_REPLACE_BRICK_STATUS, AFR_MSG_GFID_NULL,
    AFR_MSG_FD_CREATE_FAILED, AFR_MSG_DICT_SET_FAILED,
    AFR_MSG_EXPUNGING_FILE_OR_DIR, AFR_MSG_MIGRATION_IN_PROGRESS,
    AFR_MSG_CHILD_MISCONFIGURED, AFR_MSG_VOL_MISCONFIGURED,
    AFR_MSG_INTERNAL_LKS_FAILED, AFR_MSG_INVALID_FD, AFR_MSG_LOCK_INFO,
    AFR_MSG_LOCK_XLATOR_NOT_LOADED, AFR_MSG_FD_CTX_GET_FAILED,
    AFR_MSG_INVALID_SUBVOL, AFR_MSG_PUMP_XLATOR_ERROR, AFR_MSG_SELF_HEAL_INFO,
    AFR_MSG_READ_SUBVOL_ERROR, AFR_MSG_DICT_GET_FAILED, AFR_MSG_INFO_COMMON,
    AFR_MSG_SPLIT_BRAIN_CHOICE_ERROR, AFR_MSG_LOCAL_CHILD, AFR_MSG_INVALID_DATA,
    AFR_MSG_INVALID_ARG, AFR_MSG_INDEX_DIR_GET_FAILED, AFR_MSG_FSYNC_FAILED,
    AFR_MSG_FAVORITE_CHILD, AFR_MSG_SELF_HEAL_FAILED,
    AFR_MSG_SPLIT_BRAIN_STATUS, AFR_MSG_ADD_BRICK_STATUS, AFR_MSG_NO_CHANGELOG,
    AFR_MSG_TIMER_CREATE_FAIL, AFR_MSG_SBRAIN_FAV_CHILD_POLICY,
    AFR_MSG_INODE_CTX_GET_FAILED, AFR_MSG_THIN_ARB,
    AFR_MSG_THIN_ARB_XATTROP_FAILED, AFR_MSG_THIN_ARB_LOC_POP_FAILED,
    AFR_MSG_GET_PEND_VAL, AFR_MSG_THIN_ARB_SKIP_SHD, AFR_MSG_UNKNOWN_SET,
    AFR_MSG_NO_XL_ID, AFR_MSG_SELF_HEAL_INFO_START,
    AFR_MSG_SELF_HEAL_INFO_FINISH, AFR_MSG_INCRE_COUNT,
    AFR_MSG_ADD_TO_OUTPUT_FAILED, AFR_MSG_SET_TIME_FAILED,
    AFR_MSG_GFID_MISMATCH_DETECTED, AFR_MSG_GFID_HEAL_MSG,
    AFR_MSG_THIN_ARB_LOOKUP_FAILED, AFR_MSG_DICT_CREATE_FAILED,
    AFR_MSG_NO_MAJORITY_TO_RESOLVE, AFR_MSG_TYPE_MISMATCH,
    AFR_MSG_SIZE_POLICY_NOT_APPLICABLE, AFR_MSG_NO_CHILD_SELECTED,
    AFR_MSG_INVALID_CHILD, AFR_MSG_RESOLVE_CONFLICTING_DATA,
    SERROR_GETTING_SRC_BRICK, SNO_DIFF_IN_MTIME, SNO_BIGGER_FILE,
    SALL_BRICKS_UP_TO_RESOLVE, AFR_MSG_UNLOCK_FAILED, AFR_MSG_POST_OP_FAILED,
    AFR_MSG_TA_FRAME_CREATE_FAILED, AFR_MSG_SET_KEY_XATTROP_FAILED,
    AFR_MSG_BLOCKING_ENTRYLKS_FAILED, AFR_MSG_FOP_FAILED,
    AFR_MSG_CLEAN_UP_FAILED, AFR_MSG_UNABLE_TO_FETCH, AFR_MSG_XATTR_SET_FAILED,
    AFR_MSG_SPLIT_BRAIN_REPLICA, AFR_MSG_INODE_CTX_FAILED,
    AFR_MSG_LOOKUP_FAILED, AFR_MSG_ALL_SUBVOLS_DOWN,
    AFR_MSG_RELEASE_LOCK_FAILED, AFR_MSG_CLEAR_TIME_SPLIT_BRAIN,
    AFR_MSG_READ_FAILED, AFR_MSG_LAUNCH_FAILED, AFR_MSG_READ_SUBVOL_NOT_UP,
    AFR_MSG_LK_HEAL_DOM, AFR_MSG_NEW_BRICK, AFR_MSG_SPLIT_BRAIN_SET_FAILED,
    AFR_MSG_SPLIT_BRAIN_DETERMINE_FAILED, AFR_MSG_HEALER_SPAWN_FAILED,
    AFR_MSG_ADD_CRAWL_EVENT_FAILED, AFR_MSG_NULL_DEREF, AFR_MSG_SET_PEND_XATTR,
    AFR_MSG_INTERNAL_ATTR);

#define AFR_MSG_DICT_GET_FAILED_STR "Dict get failed"
#define AFR_MSG_DICT_SET_FAILED_STR "Dict set failed"
#define AFR_MSG_HEALER_SPAWN_FAILED_STR "Healer spawn failed"
#define AFR_MSG_ADD_CRAWL_EVENT_FAILED_STR "Adding crawl event failed"
#define AFR_MSG_INVALID_ARG_STR "Invalid argument"
#define AFR_MSG_INDEX_DIR_GET_FAILED_STR "unable to get index-dir on "
#define AFR_MSG_THIN_ARB_LOOKUP_FAILED_STR "Failed lookup on file"
#define AFR_MSG_DICT_CREATE_FAILED_STR "Failed to create dict."
#define AFR_MSG_THIN_ARB_XATTROP_FAILED_STR "Xattrop failed."
#define AFR_MSG_THIN_ARB_LOC_POP_FAILED_STR                                    \
    "Failed to populate loc for thin-arbiter"
#define AFR_MSG_GET_PEND_VAL_STR "Error getting value of pending"
#define AFR_MSG_THIN_ARB_SKIP_SHD_STR "I am not the god shd. skipping."
#define AFR_MSG_UNKNOWN_SET_STR "Unknown set"
#define AFR_MSG_NO_XL_ID_STR "xl does not have id"
#define AFR_MSG_SELF_HEAL_INFO_START_STR "starting full sweep on"
#define AFR_MSG_SELF_HEAL_INFO_FINISH_STR "finished full sweep on"
#define AFR_MSG_INCRE_COUNT_STR "Could not increment the counter."
#define AFR_MSG_ADD_TO_OUTPUT_FAILED_STR "Could not add to output"
#define AFR_MSG_SET_TIME_FAILED_STR "Could not set time"
#define AFR_MSG_GFID_HEAL_MSG_STR "Error setting gfid-heal-msg dict"
#define AFR_MSG_NO_MAJORITY_TO_RESOLVE_STR                                     \
    "No majority to resolve gfid split brain"
#define AFR_MSG_GFID_MISMATCH_DETECTED_STR "Gfid mismatch dectected"
#define AFR_MSG_SELF_HEAL_INFO_STR "performing selfheal"
#define AFR_MSG_TYPE_MISMATCH_STR "TYPE mismatch"
#define AFR_MSG_SIZE_POLICY_NOT_APPLICABLE_STR                                 \
    "Size policy is not applicable to directories."
#define AFR_MSG_NO_CHILD_SELECTED_STR                                          \
    "No child selected by favorite-child policy"
#define AFR_MSG_INVALID_CHILD_STR "Invalid child"
#define AFR_MSG_RESOLVE_CONFLICTING_DATA_STR                                   \
    "selected as authentic to resolve conflicting data"
#define SERROR_GETTING_SRC_BRICK_STR "Error getting the source brick"
#define SNO_DIFF_IN_MTIME_STR "No difference in mtime"
#define SNO_BIGGER_FILE_STR "No bigger file"
#define SALL_BRICKS_UP_TO_RESOLVE_STR                                          \
    "All the bricks should be up to resolve the gfid split brain"
#define AFR_MSG_UNLOCK_FAILED_STR "Failed to unlock"
#define AFR_MSG_POST_OP_FAILED_STR "Post-op on thin-arbiter failed"
#define AFR_MSG_TA_FRAME_CREATE_FAILED_STR "Failed to create ta_frame"
#define AFR_MSG_SET_KEY_XATTROP_FAILED_STR "Could not set key during xattrop"
#define AFR_MSG_BLOCKING_ENTRYLKS_FAILED_STR "Blocking entrylks failed"
#define AFR_MSG_FSYNC_FAILED_STR "fsync failed"
#define AFR_MSG_QUORUM_FAIL_STR "quorum is not met"
#define AFR_MSG_FOP_FAILED_STR "Failing Fop"
#define AFR_MSG_INVALID_SUBVOL_STR "not a subvolume"
#define AFR_MSG_VOL_MISCONFIGURED_STR "Volume is dangling"
#define AFR_MSG_CHILD_MISCONFIGURED_STR                                        \
    "replicate translator needs more than one subvolume defined"
#define AFR_MSG_CLEAN_UP_FAILED_STR "Failed to clean up healer threads"
#define AFR_MSG_QUORUM_OVERRIDE_STR "overriding quorum-count"
#define AFR_MSG_UNABLE_TO_FETCH_STR                                            \
    "Unable to fetch afr-pending-xattr option from volfile. Falling back to "  \
    "using client translator names"
#define AFR_MSG_NULL_DEREF_STR "possible NULL deref"
#define AFR_MSG_XATTR_SET_FAILED_STR "Cannot set xattr cookie key"
#define AFR_MSG_SPLIT_BRAIN_STATUS_STR "Failed to create synctask"
#define AFR_MSG_SUBVOLS_DOWN_STR "All subvolumes are not up"
#define AFR_MSG_SPLIT_BRAIN_CHOICE_ERROR_STR                                   \
    "Failed to cancel split-brain choice"
#define AFR_MSG_SPLIT_BRAIN_REPLICA_STR                                        \
    "Cannot set replica. File is not in data/metadata split-brain"
#define AFR_MSG_INODE_CTX_FAILED_STR "Failed to get inode_ctx"
#define AFR_MSG_READ_SUBVOL_ERROR_STR "no read subvols"
#define AFR_MSG_LOCAL_CHILD_STR "selecting local read-child"
#define AFR_MSG_LOOKUP_FAILED_STR "Failed to lookup/create thin-arbiter id file"
#define AFR_MSG_TIMER_CREATE_FAIL_STR                                          \
    "Cannot create timer for delayed initialization"
#define AFR_MSG_SUBVOL_UP_STR "Subvolume came back up; going online"
#define AFR_MSG_ALL_SUBVOLS_DOWN_STR                                           \
    "All subvolumes are down. Going offline until atleast one of them is up"
#define AFR_MSG_RELEASE_LOCK_FAILED_STR "Failed to release lock"
#define AFR_MSG_INVALID_CHILD_UP_STR "Received child_up from invalid subvolume"
#define AFR_MSG_QUORUM_MET_STR "Client-quorum is met"
#define AFR_MSG_EXPUNGING_FILE_OR_DIR_STR "expunging file or dir"
#define AFR_MSG_SELF_HEAL_FAILED_STR "Invalid"
#define AFR_MSG_SPLIT_BRAIN_STR "Skipping conservative mergeon the file"
#define AFR_MSG_CLEAR_TIME_SPLIT_BRAIN_STR "clear time split brain"
#define AFR_MSG_READ_FAILED_STR "Failing read since good brick is down"
#define AFR_MSG_LAUNCH_FAILED_STR "Failed to launch synctask"
#define AFR_MSG_READ_SUBVOL_NOT_UP_STR                                         \
    "read subvolume in this generation is not up"
#define AFR_MSG_INTERNAL_LKS_FAILED_STR                                        \
    "Unable to work with lk-owner while attempting fop"
#define AFR_MSG_LOCK_XLATOR_NOT_LOADED_STR                                     \
    "subvolume does not support locking. please load features/locks xlator "   \
    "on server."
#define AFR_MSG_FD_CTX_GET_FAILED_STR "unable to get fd ctx"
#define AFR_MSG_INFO_COMMON_STR "fd not open on any subvolumes, aborting."
#define AFR_MSG_REPLACE_BRICK_STATUS_STR "Couldn't acquire lock on any child."
#define AFR_MSG_NEW_BRICK_STR "New brick"
#define AFR_MSG_SPLIT_BRAIN_SET_FAILED_STR                                     \
    "Failed to set split-brain choice to -1"
#define AFR_MSG_SPLIT_BRAIN_DETERMINE_FAILED_STR                               \
    "Failed to determine split-brain. Aborting split-brain-choice set"
#define AFR_MSG_OPEN_FAIL_STR "Failed to open subvolume"
#define AFR_MSG_SET_PEND_XATTR_STR "Set of pending xattr"
#define AFR_MSG_INTERNAL_ATTR_STR "is an internal extended attribute"
#endif /* !_AFR_MESSAGES_H_ */
