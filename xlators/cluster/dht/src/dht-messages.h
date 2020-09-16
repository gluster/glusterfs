/*Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _DHT_MESSAGES_H_
#define _DHT_MESSAGES_H_

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
    DHT, DHT_MSG_CACHED_SUBVOL_GET_FAILED, DHT_MSG_CREATE_LINK_FAILED,
    DHT_MSG_DICT_SET_FAILED, DHT_MSG_DIR_ATTR_HEAL_FAILED,
    DHT_MSG_DIR_SELFHEAL_FAILED, DHT_MSG_DIR_SELFHEAL_XATTR_FAILED,
    DHT_MSG_FILE_ON_MULT_SUBVOL, DHT_MSG_FILE_TYPE_MISMATCH,
    DHT_MSG_GFID_MISMATCH, DHT_MSG_GFID_NULL, DHT_MSG_HASHED_SUBVOL_GET_FAILED,
    DHT_MSG_INIT_FAILED, DHT_MSG_INVALID_CONFIGURATION,
    DHT_MSG_INVALID_DISK_LAYOUT, DHT_MSG_INVALID_OPTION,
    DHT_MSG_LAYOUT_FIX_FAILED, DHT_MSG_LAYOUT_MERGE_FAILED,
    DHT_MSG_LAYOUT_MISMATCH, DHT_MSG_LAYOUT_NULL, DHT_MSG_MIGRATE_DATA_COMPLETE,
    DHT_MSG_MIGRATE_DATA_FAILED, DHT_MSG_MIGRATE_FILE_COMPLETE,
    DHT_MSG_MIGRATE_FILE_FAILED, DHT_MSG_NO_MEMORY, DHT_MSG_OPENDIR_FAILED,
    DHT_MSG_REBALANCE_FAILED, DHT_MSG_REBALANCE_START_FAILED,
    DHT_MSG_REBALANCE_STATUS, DHT_MSG_REBALANCE_STOPPED, DHT_MSG_RENAME_FAILED,
    DHT_MSG_SETATTR_FAILED, DHT_MSG_SUBVOL_INSUFF_INODES,
    DHT_MSG_SUBVOL_INSUFF_SPACE, DHT_MSG_UNLINK_FAILED,
    DHT_MSG_LAYOUT_SET_FAILED, DHT_MSG_LOG_FIXED_LAYOUT,
    DHT_MSG_GET_XATTR_FAILED, DHT_MSG_FILE_LOOKUP_FAILED,
    DHT_MSG_OPEN_FD_FAILED, DHT_MSG_SET_INODE_CTX_FAILED,
    DHT_MSG_UNLOCKING_FAILED, DHT_MSG_DISK_LAYOUT_NULL, DHT_MSG_SUBVOL_INFO,
    DHT_MSG_CHUNK_SIZE_INFO, DHT_MSG_LAYOUT_FORM_FAILED, DHT_MSG_SUBVOL_ERROR,
    DHT_MSG_LAYOUT_SORT_FAILED, DHT_MSG_REGEX_INFO, DHT_MSG_FOPEN_FAILED,
    DHT_MSG_SET_HOSTNAME_FAILED, DHT_MSG_BRICK_ERROR, DHT_MSG_SYNCOP_FAILED,
    DHT_MSG_MIGRATE_INFO, DHT_MSG_SOCKET_ERROR, DHT_MSG_CREATE_FD_FAILED,
    DHT_MSG_READDIR_ERROR, DHT_MSG_CHILD_LOC_BUILD_FAILED,
    DHT_MSG_SET_SWITCH_PATTERN_ERROR, DHT_MSG_COMPUTE_HASH_FAILED,
    DHT_MSG_FIND_LAYOUT_ANOMALIES_ERROR, DHT_MSG_ANOMALIES_INFO,
    DHT_MSG_LAYOUT_INFO, DHT_MSG_INODE_LK_ERROR, DHT_MSG_RENAME_INFO,
    DHT_MSG_DATA_NULL, DHT_MSG_AGGREGATE_QUOTA_XATTR_FAILED,
    DHT_MSG_UNLINK_LOOKUP_INFO, DHT_MSG_LINK_FILE_LOOKUP_INFO,
    DHT_MSG_OPERATION_NOT_SUP, DHT_MSG_NOT_LINK_FILE_ERROR, DHT_MSG_CHILD_DOWN,
    DHT_MSG_UUID_PARSE_ERROR, DHT_MSG_GET_DISK_INFO_ERROR,
    DHT_MSG_INVALID_VALUE, DHT_MSG_SWITCH_PATTERN_INFO,
    DHT_MSG_SUBVOL_OP_FAILED, DHT_MSG_LAYOUT_PRESET_FAILED,
    DHT_MSG_INVALID_LINKFILE, DHT_MSG_FIX_LAYOUT_INFO,
    DHT_MSG_GET_HOSTNAME_FAILED, DHT_MSG_WRITE_FAILED,
    DHT_MSG_MIGRATE_HARDLINK_FILE_FAILED, DHT_MSG_FSYNC_FAILED,
    DHT_MSG_SUBVOL_DECOMMISSION_INFO, DHT_MSG_BRICK_QUERY_FAILED,
    DHT_MSG_SUBVOL_NO_LAYOUT_INFO, DHT_MSG_OPEN_FD_ON_DST_FAILED,
    DHT_MSG_SUBVOL_NOT_FOUND, DHT_MSG_FILE_LOOKUP_ON_DST_FAILED,
    DHT_MSG_DISK_LAYOUT_MISSING, DHT_MSG_DICT_GET_FAILED,
    DHT_MSG_REVALIDATE_CBK_INFO, DHT_MSG_UPGRADE_BRICKS, DHT_MSG_LK_ARRAY_INFO,
    DHT_MSG_RENAME_NOT_LOCAL, DHT_MSG_RECONFIGURE_INFO,
    DHT_MSG_INIT_LOCAL_SUBVOL_FAILED, DHT_MSG_SYS_CALL_GET_TIME_FAILED,
    DHT_MSG_NO_DISK_USAGE_STATUS, DHT_MSG_SUBVOL_DOWN_ERROR,
    DHT_MSG_REBAL_THROTTLE_INFO, DHT_MSG_COMMIT_HASH_INFO,
    DHT_MSG_REBAL_STRUCT_SET, DHT_MSG_HAS_MIGINFO, DHT_MSG_SETTLE_HASH_FAILED,
    DHT_MSG_DEFRAG_PROCESS_DIR_FAILED, DHT_MSG_FD_CTX_SET_FAILED,
    DHT_MSG_STALE_LOOKUP, DHT_MSG_PARENT_LAYOUT_CHANGED,
    DHT_MSG_LOCK_MIGRATION_FAILED, DHT_MSG_LOCK_INODE_UNREF_FAILED,
    DHT_MSG_ASPRINTF_FAILED, DHT_MSG_DIR_LOOKUP_FAILED, DHT_MSG_INODELK_FAILED,
    DHT_MSG_LOCK_FRAME_FAILED, DHT_MSG_LOCAL_LOCK_INIT_FAILED,
    DHT_MSG_ENTRYLK_ERROR, DHT_MSG_INODELK_ERROR, DHT_MSG_LOC_FAILED,
    DHT_MSG_UNKNOWN_FOP, DHT_MSG_MIGRATE_FILE_SKIPPED,
    DHT_MSG_DIR_XATTR_HEAL_FAILED, DHT_MSG_HASHED_SUBVOL_DOWN,
    DHT_MSG_NON_HASHED_SUBVOL_DOWN, DHT_MSG_SYNCTASK_CREATE_FAILED,
    DHT_MSG_DIR_HEAL_ABORT, DHT_MSG_MIGRATE_SKIP, DHT_MSG_FD_CREATE_FAILED,
    DHT_MSG_DICT_NEW_FAILED, DHT_MSG_FAILED_TO_OPEN, DHT_MSG_CREATE_FAILED,
    DHT_MSG_FILE_NOT_EXIST, DHT_MSG_CHOWN_FAILED, DHT_MSG_FALLOCATE_FAILED,
    DHT_MSG_FTRUNCATE_FAILED, DHT_MSG_STATFS_FAILED, DHT_MSG_WRITE_CROSS,
    DHT_MSG_NEW_TARGET_FOUND, DHT_MSG_INSUFF_MEMORY, DHT_MSG_SET_XATTR_FAILED,
    DHT_MSG_SET_MODE_FAILED, DHT_MSG_FILE_EXISTS_IN_DEST,
    DHT_MSG_SYMLINK_FAILED, DHT_MSG_LINKFILE_DEL_FAILED, DHT_MSG_MKNOD_FAILED,
    DHT_MSG_MIGRATE_CLEANUP_FAILED, DHT_MSG_LOCK_MIGRATE,
    DHT_MSG_PARENT_BUILD_FAILED, DHT_MSG_HASHED_SUBVOL_NOT_FOUND,
    DHT_MSG_ACQUIRE_ENTRYLK_FAILED, DHT_MSG_CREATE_DST_FAILED,
    DHT_MSG_MIGRATION_EXIT, DHT_MSG_CHANGED_DST, DHT_MSG_TRACE_FAILED,
    DHT_MSG_WRITE_LOCK_FAILED, DHT_MSG_GETACTIVELK_FAILED, DHT_MSG_STAT_FAILED,
    DHT_MSG_UNLINK_PERFORM_FAILED, DHT_MSG_CLANUP_SOURCE_FILE_FAILED,
    DHT_MSG_UNLOCK_FILE_FAILED, DHT_MSG_REMOVE_XATTR_FAILED,
    DHT_MSG_DATA_MIGRATE_ABORT, DHT_MSG_DEFRAG_NULL, DHT_MSG_PARENT_NULL,
    DHT_MSG_GFID_NOT_PRESENT, DHT_MSG_CHILD_LOC_FAILED,
    DHT_MSG_SET_LOOKUP_FAILED, DHT_MSG_DIR_REMOVED, DHT_MSG_FIX_NOT_COMP,
    DHT_MSG_SUBVOL_DETER_FAILED, DHT_MSG_LOCAL_SUBVOL, DHT_MSG_NODE_UUID,
    DHT_MSG_SIZE_FILE, DHT_MSG_GET_DATA_SIZE_FAILED,
    DHT_MSG_PTHREAD_JOIN_FAILED, DHT_MSG_COUNTER_THREAD_CREATE_FAILED,
    DHT_MSG_MIGRATION_INIT_QUEUE_FAILED, DHT_MSG_PAUSED_TIMEOUT, DHT_MSG_WOKE,
    DHT_MSG_ABORT_REBALANCE, DHT_MSG_CREATE_TASK_REBAL_FAILED,
    DHT_MSG_REBAL_ESTIMATE_NOT_AVAIL, DHT_MSG_ADD_CHOICES_ERROR,
    DHT_MSG_GET_CHOICES_ERROR, DHT_MSG_PREPARE_STATUS_ERROR,
    DHT_MSG_SET_CHOICE_FAILED, DHT_MSG_SET_HASHED_SUBVOL_FAILED,
    DHT_MSG_XATTR_HEAL_NOT_POSS, DHT_MSG_LINKTO_FILE_FAILED,
    DHT_MSG_STALE_LINKFILE_DELETE, DHT_MSG_NO_SUBVOL_FOR_LINKTO,
    DHT_MSG_SUBVOL_RETURNED, DHT_MSG_UNKNOWN_LOCAL_XSEL, DHT_MSG_GET_XATTR_ERR,
    DHT_MSG_ALLOC_OR_FILL_FAILED, DHT_MSG_GET_REAL_NAME_FAILED,
    DHT_MSG_COPY_UUID_FAILED, DHT_MSG_MDS_DETER_FAILED,
    DHT_MSG_CREATE_REBAL_FAILED, DHT_MSG_LINK_LAYOUT_FAILED,
    DHT_MSG_NO_SUBVOL_IN_LAYOUT, DHT_MSG_MEM_ALLOC_FAILED,
    DHT_MSG_SET_IN_PARAMS_DICT_FAILED, DHT_MSG_LOC_COPY_FAILED,
    DHT_MSG_PARENT_LOC_FAILED, DHT_MSG_CREATE_LOCK_FAILED,
    DHT_MSG_PREV_ATTEMPT_FAILED, DHT_MSG_REFRESH_ATTEMPT,
    DHT_MSG_ACQUIRE_LOCK_FAILED, DHT_MSG_CREATE_STUB_FAILED,
    DHT_MSG_WIND_LOCK_REQ_FAILED, DHT_MSG_REFRESH_FAILED,
    DHT_MSG_CACHED_SUBVOL_ERROR, DHT_MSG_NO_LINK_SUBVOL, DHT_MSG_SET_KEY_FAILED,
    DHT_MSG_REMOVE_LINKTO_FAILED, DHT_MSG_LAYOUT_DICT_SET_FAILED,
    DHT_MSG_XATTR_DICT_NULL, DHT_MSG_DUMMY_ALLOC_FAILED, DHT_MSG_DICT_IS_NULL,
    DHT_MSG_LINK_INODE_FAILED, DHT_MSG_SELFHEAL_FAILED, DHT_MSG_NO_MDS_SUBVOL,
    DHT_MSG_LIST_XATTRS_FAILED, DHT_MSG_RESET_INTER_XATTR_FAILED,
    DHT_MSG_MDS_DOWN_UNABLE_TO_SET, DHT_MSG_WIND_UNLOCK_FAILED,
    DHT_MSG_COMMIT_HASH_FAILED, DHT_MSG_UNLOCK_GFID_FAILED,
    DHT_MSG_UNLOCK_FOLLOW_ENTRYLK, DHT_MSG_COPY_FRAME_FAILED,
    DHT_MSG_UNLOCK_FOLLOW_LOCKS, DHT_MSG_ENTRYLK_FAILED_AFT_INODELK,
    DHT_MSG_CALLOC_FAILED, DHT_MSG_LOCK_ALLOC_FAILED,
    DHT_MSG_BLOCK_INODELK_FAILED,
    DHT_MSG_LOCAL_LOCKS_STORE_FAILED_UNLOCKING_FOLLOWING_ENTRYLK,
    DHT_MSG_ALLOC_FRAME_FAILED_NOT_UNLOCKING_FOLLOWING_ENTRYLKS,
    DHT_MSG_DST_NULL_SET_FAILED);

#define DHT_MSG_FD_CTX_SET_FAILED_STR "Failed to set fd ctx"
#define DHT_MSG_INVALID_VALUE_STR "Different dst found in the fd ctx"
#define DHT_MSG_UNKNOWN_FOP_STR "Unknown FOP on file"
#define DHT_MSG_OPEN_FD_ON_DST_FAILED_STR "Failed to open the fd on file"
#define DHT_MSG_SYNCTASK_CREATE_FAILED_STR "Failed to create synctask"
#define DHT_MSG_ASPRINTF_FAILED_STR                                            \
    "asprintf failed while fetching subvol from the id"
#define DHT_MSG_HAS_MIGINFO_STR "Found miginfo in the inode ctx"
#define DHT_MSG_FILE_LOOKUP_FAILED_STR "failed to lookup the file"
#define DHT_MSG_INVALID_LINKFILE_STR                                           \
    "linkto target is different from cached-subvol. treating as destination "  \
    "subvol"
#define DHT_MSG_GFID_MISMATCH_STR "gfid different on the target file"
#define DHT_MSG_GET_XATTR_FAILED_STR "failed to get 'linkto' xattr"
#define DHT_MSG_SET_INODE_CTX_FAILED_STR "failed to set inode-ctx target file"
#define DHT_MSG_DIR_SELFHEAL_FAILED_STR "Healing of path failed"
#define DHT_MSG_DIR_HEAL_ABORT_STR                                             \
    "Failed to get path from subvol. Aborting directory healing"
#define DHT_MSG_DIR_XATTR_HEAL_FAILED_STR "xattr heal failed for directory"
#define DHT_MSG_LOCK_INODE_UNREF_FAILED_STR                                    \
    "Found a NULL inode. Failed to unref the inode"
#define DHT_MSG_DICT_SET_FAILED_STR "Failed to set dictionary value"
#define DHT_MSG_NOT_LINK_FILE_ERROR_STR "got non-linkfile"
#define DHT_MSG_CREATE_LINK_FAILED_STR "failed to initialize linkfile data"
#define DHT_MSG_UNLINK_FAILED_STR "Unlinking linkfile on subvolume failed"
#define DHT_MSG_MIGRATE_FILE_FAILED_STR "Migrate file failed"
#define DHT_MSG_NO_MEMORY_STR "could not allocate memory for dict"
#define DHT_MSG_SUBVOL_ERROR_STR "Failed to get linkto subvol"
#define DHT_MSG_MIGRATE_HARDLINK_FILE_FAILED_STR "link failed on subvol"
#define DHT_MSG_MIGRATE_FILE_SKIPPED_STR "Migration skipped"
#define DHT_MSG_FD_CREATE_FAILED_STR "fd create failed"
#define DHT_MSG_DICT_NEW_FAILED_STR "dict_new failed"
#define DHT_MSG_FAILED_TO_OPEN_STR "failed to open"
#define DHT_MSG_CREATE_FAILED_STR "failed to create"
#define DHT_MSG_FILE_NOT_EXIST_STR "file does not exist"
#define DHT_MSG_CHOWN_FAILED_STR "chown failed"
#define DHT_MSG_FALLOCATE_FAILED_STR "fallocate failed"
#define DHT_MSG_FTRUNCATE_FAILED_STR "ftruncate failed"
#define DHT_MSG_STATFS_FAILED_STR "failed to get statfs"
#define DHT_MSG_WRITE_CROSS_STR                                                \
    "write will cross min-fre-disk for file on subvol. looking for new subvol"
#define DHT_MSG_SUBVOL_INSUFF_SPACE_STR                                        \
    "Could not find any subvol with space accommodating the file. Cosider "    \
    "adding bricks"
#define DHT_MSG_NEW_TARGET_FOUND_STR "New target found for file"
#define DHT_MSG_INSUFF_MEMORY_STR "insufficient memory"
#define DHT_MSG_SET_XATTR_FAILED_STR "failed to set xattr"
#define DHT_MSG_SET_MODE_FAILED_STR "failed to set mode"
#define DHT_MSG_FILE_EXISTS_IN_DEST_STR "file exists in destination"
#define DHT_MSG_LINKFILE_DEL_FAILED_STR "failed to delete the linkfile"
#define DHT_MSG_SYMLINK_FAILED_STR "symlink failed"
#define DHT_MSG_MKNOD_FAILED_STR "mknod failed"
#define DHT_MSG_SETATTR_FAILED_STR "failed to perform setattr"
#define DHT_MSG_MIGRATE_CLEANUP_FAILED_STR                                     \
    "Migrate file cleanup failed: failed to fstat file"
#define DHT_MSG_LOCK_MIGRATE_STR "locks will be migrated for file"
#define DHT_MSG_PARENT_BUILD_FAILED_STR                                        \
    "failed to build parent loc, which is needed to acquire entrylk to "       \
    "synchronize with renames on this path. Skipping migration"
#define DHT_MSG_HASHED_SUBVOL_NOT_FOUND_STR                                    \
    "cannot find hashed subvol which is needed to synchronize with renames "   \
    "on this path. Skipping migration"
#define DHT_MSG_ACQUIRE_ENTRYLK_FAILED_STR "failed to acquire entrylk on subvol"
#define DHT_MSG_CREATE_DST_FAILED_STR "create dst failed for file"
#define DHT_MSG_MIGRATION_EXIT_STR "Exiting migration"
#define DHT_MSG_CHANGED_DST_STR "destination changed fo file"
#define DHT_MSG_TRACE_FAILED_STR "Trace failed"
#define DHT_MSG_WRITE_LOCK_FAILED_STR "write lock failed"
#define DHT_MSG_GETACTIVELK_FAILED_STR "getactivelk failed for file"
#define DHT_MSG_STAT_FAILED_STR "failed to do a stat"
#define DHT_MSG_UNLINK_PERFORM_FAILED_STR "failed to perform unlink"
#define DHT_MSG_MIGRATE_FILE_COMPLETE_STR "completed migration"
#define DHT_MSG_CLANUP_SOURCE_FILE_FAILED_STR "failed to cleanup source file"
#define DHT_MSG_UNLOCK_FILE_FAILED_STR "failed to unlock file"
#define DHT_MSG_REMOVE_XATTR_FAILED_STR "remove xattr failed"
#define DHT_MSG_SOCKET_ERROR_STR "Failed to unlink listener socket"
#define DHT_MSG_HASHED_SUBVOL_GET_FAILED_STR "Failed to get hashed subvolume"
#define DHT_MSG_CACHED_SUBVOL_GET_FAILED_STR "Failed to get cached subvolume"
#define DHT_MSG_MIGRATE_DATA_FAILED_STR "migrate-data failed"
#define DHT_MSG_DEFRAG_NULL_STR "defrag is NULL"
#define DHT_MSG_DATA_MIGRATE_ABORT_STR                                         \
    "Readdirp failed. Aborting data migration for dict"
#define DHT_MSG_LAYOUT_FIX_FAILED_STR "fix layout failed"
#define DHT_MSG_PARENT_NULL_STR "parent is NULL"
#define DHT_MSG_GFID_NOT_PRESENT_STR "gfid not present"
#define DHT_MSG_CHILD_LOC_FAILED_STR "Child loc build failed"
#define DHT_MSG_SET_LOOKUP_FAILED_STR "Failed to set lookup"
#define DHT_MSG_DIR_LOOKUP_FAILED_STR "lookup failed"
#define DHT_MSG_DIR_REMOVED_STR "Dir renamed or removed. Skipping"
#define DHT_MSG_READDIR_ERROR_STR "readdir failed, Aborting fix-layout"
#define DHT_MSG_SETTLE_HASH_FAILED_STR "Settle hash failed"
#define DHT_MSG_DEFRAG_PROCESS_DIR_FAILED_STR "gf_defrag_process_dir failed"
#define DHT_MSG_FIX_NOT_COMP_STR                                               \
    "Unable to retrieve fixlayout xattr. Assume background fix layout not "    \
    "complete"
#define DHT_MSG_SUBVOL_DETER_FAILED_STR                                        \
    "local subvolume determination failed with error"
#define DHT_MSG_LOCAL_SUBVOL_STR "local subvol"
#define DHT_MSG_NODE_UUID_STR "node uuid"
#define DHT_MSG_SIZE_FILE_STR "Total size files"
#define DHT_MSG_GET_DATA_SIZE_FAILED_STR                                       \
    "Failed to get the total data size. Unable to estimate time to complete "  \
    "rebalance"
#define DHT_MSG_PTHREAD_JOIN_FAILED_STR                                        \
    "file_counter_thread: pthread_join failed"
#define DHT_MSG_COUNTER_THREAD_CREATE_FAILED_STR                               \
    "Failed to create the file counter thread"
#define DHT_MSG_MIGRATION_INIT_QUEUE_FAILED_STR                                \
    "Failed to initialise migration queue"
#define DHT_MSG_REBALANCE_STOPPED_STR "Received stop command on rebalance"
#define DHT_MSG_PAUSED_TIMEOUT_STR "Request pause timer timeout"
#define DHT_MSG_WOKE_STR "woken"
#define DHT_MSG_ABORT_REBALANCE_STR "Aborting rebalance"
#define DHT_MSG_REBALANCE_START_FAILED_STR                                     \
    "Failed to start rebalance: look up on / failed"
#define DHT_MSG_CREATE_TASK_REBAL_FAILED_STR                                   \
    "Could not create task for rebalance"
#define DHT_MSG_REBAL_ESTIMATE_NOT_AVAIL_STR                                   \
    "Rebalance estimates will not be available"
#define DHT_MSG_REBALANCE_STATUS_STR "Rebalance status"
#define DHT_MSG_DATA_NULL_STR "data value is NULL"
#define DHT_MSG_ADD_CHOICES_ERROR_STR "Error to add choices in buffer"
#define DHT_MSG_GET_CHOICES_ERROR_STR "Error to get choices"
#define DHT_MSG_PREPARE_STATUS_ERROR_STR "Error to prepare status"
#define DHT_MSG_SET_CHOICE_FAILED_STR "Failed to set full choice"
#define DHT_MSG_AGGREGATE_QUOTA_XATTR_FAILED_STR                               \
    "Failed to aggregate quota xattr"
#define DHT_MSG_FILE_TYPE_MISMATCH_STR                                         \
    "path exists as a file on one subvolume and directory on another. Please " \
    "fix it manually"
#define DHT_MSG_LAYOUT_SET_FAILED_STR "failed to set layout for subvolume"
#define DHT_MSG_LAYOUT_MERGE_FAILED_STR "failed to merge layouts for subvolume"
#define DHT_MSG_SET_HASHED_SUBVOL_FAILED_STR "Failed to set hashed subvolume"
#define DHT_MSG_XATTR_HEAL_NOT_POSS_STR                                        \
    "No gfid exists for path. so healing xattr is not possible"
#define DHT_MSG_REVALIDATE_CBK_INFO_STR "Revalidate: subvolume returned -1"
#define DHT_MSG_LAYOUT_MISMATCH_STR "Mismatching layouts"
#define DHT_MSG_UNLINK_LOOKUP_INFO_STR "lookup_unlink retuened"
#define DHT_MSG_LINKTO_FILE_FAILED_STR                                         \
    "Could not unlink the linkto file as either fd is open and/or linkto "     \
    "xattr is set"
#define DHT_MSG_LAYOUT_PRESET_FAILED_STR                                       \
    "Could not set pre-set layout for subvolume"
#define DHT_MSG_FILE_ON_MULT_SUBVOL_STR                                        \
    "multiple subvolumes have file (preferably rename the file in the "        \
    "backend, and do a fresh lookup"
#define DHT_MSG_STALE_LINKFILE_DELETE_STR                                      \
    "attempting deletion of stale linkfile"
#define DHT_MSG_LINK_FILE_LOOKUP_INFO_STR "Lookup on following linkfile"
#define DHT_MSG_NO_SUBVOL_FOR_LINKTO_STR "No link subvolume for linkto"
#define DHT_MSG_SUBVOL_RETURNED_STR "Subvolume returned -1"
#define DHT_MSG_UNKNOWN_LOCAL_XSEL_STR "Unknown local->xsel"
#define DHT_MSG_DICT_GET_FAILED_STR "Failed to get"
#define DHT_MSG_UUID_PARSE_ERROR_STR "Failed to parse uuid"
#define DHT_MSG_GET_XATTR_ERR_STR "getxattr err for dir"
#define DHT_MSG_ALLOC_OR_FILL_FAILED_STR "alloc or fill failed"
#define DHT_MSG_UPGRADE_BRICKS_STR                                             \
    "At least one of the bricks does not support this operation. Please "      \
    "upgrade all bricks"
#define DHT_MSG_GET_REAL_NAME_FAILED_STR "Failed to get real filename"
#define DHT_MSG_LAYOUT_NULL_STR "Layout is NULL"
#define DHT_MSG_COPY_UUID_FAILED_STR "Failed to copy node uuid key"
#define DHT_MSG_MDS_DETER_FAILED_STR                                           \
    "Cannot determine MDS, fetching xattr randomly from a subvol"
#define DHT_MSG_HASHED_SUBVOL_DOWN_STR                                         \
    "MDS is down for path, so fetching xattr randomly from subvol"
#define DHT_MSG_CREATE_REBAL_FAILED_STR                                        \
    "failed to create a new rebalance synctask"
#define DHT_MSG_FIX_LAYOUT_INFO_STR "fixing the layout"
#define DHT_MSG_OPERATION_NOT_SUP_STR "wrong directory-spread-count value"
#define DHT_MSG_LINK_LAYOUT_FAILED_STR "failed to link the layout in inode"
#define DHT_MSG_NO_SUBVOL_IN_LAYOUT_STR "no subvolume in layout for path"
#define DHT_MSG_INODE_LK_ERROR_STR "mknod lock failed for file"
#define DHT_MSG_MEM_ALLOC_FAILED_STR "mem allocation failed"
#define DHT_MSG_PARENT_LAYOUT_CHANGED_STR                                      \
    "extracting in-memory layout of parent failed"
#define DHT_MSG_SET_IN_PARAMS_DICT_FAILED_STR                                  \
    "setting in params dictionary failed"
#define DHT_MSG_LOC_COPY_FAILED_STR "loc_copy failed"
#define DHT_MSG_LOC_FAILED_STR "parent loc build failed"
#define DHT_MSG_PARENT_LOC_FAILED_STR "locking parent failed"
#define DHT_MSG_CREATE_LOCK_FAILED_STR "Create lock failed"
#define DHT_MSG_PREV_ATTEMPT_FAILED_STR                                        \
    "mkdir loop detected. parent layout didn't change even though previous "   \
    "attempt of mkdir failed because of in-memory layout not matching with "   \
    "that on disk."
#define DHT_MSG_REFRESH_ATTEMPT_STR                                            \
    "mkdir parent layout changed. Attempting a refresh and then a retry"
#define DHT_MSG_ACQUIRE_LOCK_FAILED_STR                                        \
    "Acquiring lock on parent to guard against layout-change failed"
#define DHT_MSG_CREATE_STUB_FAILED_STR "creating stub failed"
#define DHT_MSG_WIND_LOCK_REQ_FAILED_STR                                       \
    "cannot wind lock request to guard parent layout"
#define DHT_MSG_REFRESH_FAILED_STR "refreshing parent layout failed."
#define DHT_MSG_CACHED_SUBVOL_ERROR_STR "On cached subvol"
#define DHT_MSG_NO_LINK_SUBVOL_STR "Linkfile does not have link subvolume"
#define DHT_MSG_SET_KEY_FAILED_STR "failed to set key"
#define DHT_MSG_CHILD_DOWN_STR "Received CHILD_DOWN. Exiting"
#define DHT_MSG_LOG_FIXED_LAYOUT_STR "log layout fixed"
#define DHT_MSG_REBAL_STRUCT_SET_STR "local->rebalance already set"
#define DHT_MSG_REMOVE_LINKTO_FAILED_STR "Removal of linkto failed at subvol"
#define DHT_MSG_LAYOUT_DICT_SET_FAILED_STR "dht layout dict set failed"
#define DHT_MSG_SUBVOL_INFO_STR "creating subvolume"
#define DHT_MSG_COMPUTE_HASH_FAILED_STR "hash computation failed"
#define DHT_MSG_INVALID_DISK_LAYOUT_STR                                        \
    "Invalid disk layout: Catastrophic error layout with unknown type found"
#define DHT_MSG_LAYOUT_SORT_FAILED_STR "layout sort failed"
#define DHT_MSG_ANOMALIES_INFO_STR "Found anomalies"
#define DHT_MSG_XATTR_DICT_NULL_STR "xattr dictionary is NULL"
#define DHT_MSG_DISK_LAYOUT_MISSING_STR "Disk layout missing"
#define DHT_MSG_LAYOUT_INFO_STR "layout info"
#define DHT_MSG_SUBVOL_NO_LAYOUT_INFO_STR "no pre-set layout for subvol"
#define DHT_MSG_SELFHEAL_XATTR_FAILED_STR "layout setxattr failed"
#define DHT_MSG_DIR_SELFHEAL_XATTR_FAILED_STR "Directory self heal xattr failed"
#define DHT_MSG_DUMMY_ALLOC_FAILED_STR "failed to allocate dummy layout"
#define DHT_MSG_DICT_IS_NULL_STR                                               \
    "dict is NULL, need to make sure gfids are same"
#define DHT_MSG_ENTRYLK_ERROR_STR "acquiring entrylk after inodelk failed"
#define DHT_MSG_NO_DISK_USAGE_STATUS_STR "no du stats"
#define DHT_MSG_LINK_INODE_FAILED_STR "linking inode failed"
#define DHT_MSG_SELFHEAL_FAILED_STR "Directory selfheal failed"
#define DHT_MSG_NO_MDS_SUBVOL_STR "No mds subvol"
#define DHT_MSG_LIST_XATTRS_FAILED_STR "failed to list xattrs"
#define DHT_MSG_RESET_INTER_XATTR_FAILED_STR "Failed to reset internal xattr"
#define DHT_MSG_MDS_DOWN_UNABLE_TO_SET_STR                                     \
    "mds subvol is down, unable to set xattr"
#define DHT_MSG_DIR_ATTR_HEAL_FAILED_STR                                       \
    "Directory attr heal failed. Failed to set uid/gid"
#define DHT_MSG_WIND_UNLOCK_FAILED_STR                                         \
    "Winding unlock failed: stale locks left on brick"
#define DHT_MSG_COMMIT_HASH_FAILED_STR "Directory commit hash updaten failed"
#define DHT_MSG_LK_ARRAY_INFO_STR "lk info"
#define DHT_MSG_UNLOCK_GFID_FAILED_STR                                         \
    "unlock failed on gfid: stale lock might be left"
#define DHT_MSG_UNLOCKING_FAILED_STR "unlocking failed"
#define DHT_MSG_UNLOCK_FOLLOW_ENTRYLK_STR "not unlocking following entrylks"
#define DHT_MSG_COPY_FRAME_FAILED_STR "copy frame failed"
#define DHT_MSG_UNLOCK_FOLLOW_LOCKS_STR "not unlocking following locks"
#define DHT_MSG_INODELK_FAILED_STR "inodelk failed on subvol"
#define DHT_MSG_LOCK_FRAME_FAILED_STR "memory allocation failed for lock_frame"
#define DHT_MSG_LOCAL_LOCK_INIT_FAILED_STR "dht_local_lock_init failed"
#define DHT_MSG_ENTRYLK_FAILED_AFT_INODELK_STR                                 \
    "dht_blocking_entrylk failed after taking inodelk"
#define DHT_MSG_BLOCK_INODELK_FAILED_STR "dht_blocking_inodelk failed"
#define DHT_MSG_CALLOC_FAILED_STR "calloc failed"
#define DHT_MSG_LOCK_ALLOC_FAILED_STR "lock allocation failed"
#define DHT_MSG_ALLOC_FRAME_FAILED_NOT_UNLOCKING_FOLLOWING_ENTRYLKS_STR        \
    "cannot allocate a frame, not unlocking following entrylks"
#define DHT_MSG_LOCAL_LOCKS_STORE_FAILED_UNLOCKING_FOLLOWING_ENTRYLK_STR       \
    "storing locks in local failed, not unlocking following entrylks"
#define DHT_MSG_DST_NULL_SET_FAILED_STR                                        \
    "src or dst is NULL, Failed to set dictionary value"

#endif /* _DHT_MESSAGES_H_ */
