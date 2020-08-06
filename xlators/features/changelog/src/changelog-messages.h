/*
 Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _CHANGELOG_MESSAGES_H_
#define _CHANGELOG_MESSAGES_H_

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
    CHANGELOG, CHANGELOG_MSG_OPEN_FAILED, CHANGELOG_MSG_BARRIER_FOP_FAILED,
    CHANGELOG_MSG_VOL_MISCONFIGURED, CHANGELOG_MSG_RENAME_ERROR,
    CHANGELOG_MSG_READ_ERROR, CHANGELOG_MSG_HTIME_ERROR,
    CHANGELOG_MSG_PTHREAD_MUTEX_INIT_FAILED,
    CHANGELOG_MSG_PTHREAD_COND_INIT_FAILED, CHANGELOG_MSG_CHILD_MISCONFIGURED,
    CHANGELOG_MSG_DIR_OPTIONS_NOT_SET, CHANGELOG_MSG_CLOSE_ERROR,
    CHANGELOG_MSG_PIPE_CREATION_ERROR, CHANGELOG_MSG_DICT_GET_FAILED,
    CHANGELOG_MSG_BARRIER_INFO, CHANGELOG_MSG_BARRIER_ERROR,
    CHANGELOG_MSG_GET_TIME_OP_FAILED, CHANGELOG_MSG_WRITE_FAILED,
    CHANGELOG_MSG_PTHREAD_ERROR, CHANGELOG_MSG_INODE_NOT_FOUND,
    CHANGELOG_MSG_FSYNC_OP_FAILED, CHANGELOG_MSG_TOTAL_LOG_INFO,
    CHANGELOG_MSG_SNAP_INFO, CHANGELOG_MSG_SELECT_FAILED,
    CHANGELOG_MSG_FCNTL_FAILED, CHANGELOG_MSG_BNOTIFY_INFO,
    CHANGELOG_MSG_ENTRY_BUF_INFO, CHANGELOG_MSG_CHANGELOG_NOT_ACTIVE,
    CHANGELOG_MSG_LOCAL_INIT_FAILED, CHANGELOG_MSG_NOTIFY_REGISTER_FAILED,
    CHANGELOG_MSG_PROGRAM_NAME_REG_FAILED, CHANGELOG_MSG_HANDLE_PROBE_ERROR,
    CHANGELOG_MSG_SET_FD_CONTEXT, CHANGELOG_MSG_FREEUP_FAILED,
    CHANGELOG_MSG_RECONFIGURE, CHANGELOG_MSG_RPC_SUBMIT_REPLY_FAILED,
    CHANGELOG_MSG_RPC_BUILD_ERROR, CHANGELOG_MSG_RPC_CONNECT_ERROR,
    CHANGELOG_MSG_RPC_START_ERROR, CHANGELOG_MSG_BUFFER_STARVATION_ERROR,
    CHANGELOG_MSG_SCAN_DIR_FAILED, CHANGELOG_MSG_FSETXATTR_FAILED,
    CHANGELOG_MSG_FGETXATTR_FAILED, CHANGELOG_MSG_CLEANUP_ON_ACTIVE_REF,
    CHANGELOG_MSG_DISPATCH_EVENT_FAILED, CHANGELOG_MSG_PUT_BUFFER_FAILED,
    CHANGELOG_MSG_PTHREAD_COND_WAIT_FAILED, CHANGELOG_MSG_PTHREAD_CANCEL_FAILED,
    CHANGELOG_MSG_INJECT_FSYNC_FAILED, CHANGELOG_MSG_CREATE_FRAME_FAILED,
    CHANGELOG_MSG_FSTAT_OP_FAILED, CHANGELOG_MSG_LSEEK_OP_FAILED,
    CHANGELOG_MSG_STRSTR_OP_FAILED, CHANGELOG_MSG_UNLINK_OP_FAILED,
    CHANGELOG_MSG_DETECT_EMPTY_CHANGELOG_FAILED,
    CHANGELOG_MSG_READLINK_OP_FAILED, CHANGELOG_MSG_EXPLICIT_ROLLOVER_FAILED,
    CHANGELOG_MSG_RPCSVC_NOTIFY_FAILED, CHANGELOG_MSG_MEMORY_INIT_FAILED,
    CHANGELOG_MSG_NO_MEMORY, CHANGELOG_MSG_HTIME_STAT_ERROR,
    CHANGELOG_MSG_HTIME_CURRENT_ERROR, CHANGELOG_MSG_BNOTIFY_COND_INFO,
    CHANGELOG_MSG_NO_HTIME_CURRENT, CHANGELOG_MSG_HTIME_CURRENT,
    CHANGELOG_MSG_NEW_HTIME_FILE, CHANGELOG_MSG_MKDIR_ERROR,
    CHANGELOG_MSG_PATH_NOT_FOUND, CHANGELOG_MSG_XATTR_INIT_FAILED,
    CHANGELOG_MSG_WROTE_TO_CSNAP, CHANGELOG_MSG_UNUSED_0,
    CHANGELOG_MSG_GET_BUFFER_FAILED, CHANGELOG_MSG_BARRIER_STATE_NOTIFY,
    CHANGELOG_MSG_BARRIER_DISABLED, CHANGELOG_MSG_BARRIER_ALREADY_DISABLED,
    CHANGELOG_MSG_BARRIER_ON_ERROR, CHANGELOG_MSG_BARRIER_ENABLE,
    CHANGELOG_MSG_BARRIER_KEY_NOT_FOUND, CHANGELOG_MSG_ERROR_IN_DICT_GET,
    CHANGELOG_MSG_UNUSED_1, CHANGELOG_MSG_UNUSED_2,
    CHANGELOG_MSG_DEQUEUING_BARRIER_FOPS,
    CHANGELOG_MSG_DEQUEUING_BARRIER_FOPS_FINISHED,
    CHANGELOG_MSG_BARRIER_TIMEOUT, CHANGELOG_MSG_TIMEOUT_ADD_FAILED,
    CHANGELOG_MSG_CLEANUP_ALREADY_SET);

#define CHANGELOG_MSG_BARRIER_FOP_FAILED_STR                                   \
    "failed to barrier FOPs, disabling changelog barrier"
#define CHANGELOG_MSG_MEMORY_INIT_FAILED_STR "memory accounting init failed"
#define CHANGELOG_MSG_NO_MEMORY_STR "failed to create local memory pool"
#define CHANGELOG_MSG_ENTRY_BUF_INFO_STR                                       \
    "Entry cannot be captured for gfid, Capturing DATA entry."
#define CHANGELOG_MSG_PTHREAD_ERROR_STR "pthread error"
#define CHANGELOG_MSG_PTHREAD_MUTEX_INIT_FAILED_STR "pthread_mutex_init failed"
#define CHANGELOG_MSG_PTHREAD_COND_INIT_FAILED_STR "pthread_cond_init failed"
#define CHANGELOG_MSG_HTIME_ERROR_STR "failed to update HTIME file"
#define CHANGELOG_MSG_HTIME_STAT_ERROR_STR "unable to stat htime file"
#define CHANGELOG_MSG_HTIME_CURRENT_ERROR_STR "Error extracting HTIME_CURRENT."
#define CHANGELOG_MSG_UNLINK_OP_FAILED_STR "error unlinking empty changelog"
#define CHANGELOG_MSG_RENAME_ERROR_STR "error renaming"
#define CHANGELOG_MSG_MKDIR_ERROR_STR "unable to create directory"
#define CHANGELOG_MSG_BNOTIFY_INFO_STR                                         \
    "Explicit rollover changelog signaling bnotify"
#define CHANGELOG_MSG_BNOTIFY_COND_INFO_STR "Woke up: bnotify conditional wait"
#define CHANGELOG_MSG_RECONFIGURE_STR "Reconfigure: Changelog Enable"
#define CHANGELOG_MSG_NO_HTIME_CURRENT_STR                                     \
    "HTIME_CURRENT not found. Changelog enabled before init"
#define CHANGELOG_MSG_HTIME_CURRENT_STR "HTIME_CURRENT"
#define CHANGELOG_MSG_NEW_HTIME_FILE_STR                                       \
    "Changelog enable: Creating new HTIME file"
#define CHANGELOG_MSG_FGETXATTR_FAILED_STR "fgetxattr failed"
#define CHANGELOG_MSG_TOTAL_LOG_INFO_STR "changelog info"
#define CHANGELOG_MSG_PTHREAD_COND_WAIT_FAILED_STR "pthread cond wait failed"
#define CHANGELOG_MSG_INODE_NOT_FOUND_STR "inode not found"
#define CHANGELOG_MSG_READLINK_OP_FAILED_STR                                   \
    "could not read the link from the gfid handle"
#define CHANGELOG_MSG_OPEN_FAILED_STR "unable to open file"
#define CHANGELOG_MSG_RPC_CONNECT_ERROR_STR "failed to connect back"
#define CHANGELOG_MSG_BUFFER_STARVATION_ERROR_STR                              \
    "Failed to get buffer for RPC dispatch"
#define CHANGELOG_MSG_PTHREAD_CANCEL_FAILED_STR "could not cancel thread"
#define CHANGELOG_MSG_FSTAT_OP_FAILED_STR "Could not stat (CHANGELOG)"
#define CHANGELOG_MSG_LSEEK_OP_FAILED_STR "Could not lseek (changelog)"
#define CHANGELOG_MSG_PATH_NOT_FOUND_STR                                       \
    "Could not find CHANGELOG in changelog path"
#define CHANGELOG_MSG_FSYNC_OP_FAILED_STR "fsync failed"
#define CHANGELOG_MSG_DETECT_EMPTY_CHANGELOG_FAILED_STR                        \
    "Error detecting empty changelog"
#define CHANGELOG_MSG_EXPLICIT_ROLLOVER_FAILED_STR                             \
    "Fail snapshot because of previous errors"
#define CHANGELOG_MSG_SCAN_DIR_FAILED_STR "scandir failed"
#define CHANGELOG_MSG_FSETXATTR_FAILED_STR "fsetxattr failed"
#define CHANGELOG_MSG_XATTR_INIT_FAILED_STR "Htime xattr initialization failed"
#define CHANGELOG_MSG_SNAP_INFO_STR "log in call path"
#define CHANGELOG_MSG_WRITE_FAILED_STR "error writing to disk"
#define CHANGELOG_MSG_WROTE_TO_CSNAP_STR "Successfully wrote to csnap"
#define CHANGELOG_MSG_GET_TIME_OP_FAILED_STR "Problem rolling over changelog(s)"
#define CHANGELOG_MSG_BARRIER_INFO_STR "Explicit wakeup on barrier notify"
#define CHANGELOG_MSG_SELECT_FAILED_STR "pthread_cond_timedwait failed"
#define CHANGELOG_MSG_INJECT_FSYNC_FAILED_STR "failed to inject fsync event"
#define CHANGELOG_MSG_LOCAL_INIT_FAILED_STR                                    \
    "changelog local initialization failed"
#define CHANGELOG_MSG_GET_BUFFER_FAILED_STR "Failed to get buffer"
#define CHANGELOG_MSG_SET_FD_CONTEXT_STR                                       \
    "could not set fd context(for release cbk)"
#define CHANGELOG_MSG_DICT_GET_FAILED_STR "Barrier failed"
#define CHANGELOG_MSG_BARRIER_STATE_NOTIFY_STR "Barrier notification"
#define CHANGELOG_MSG_BARRIER_ERROR_STR                                        \
    "Received another barrier off notification while already off"
#define CHANGELOG_MSG_BARRIER_DISABLED_STR "disabled changelog barrier"
#define CHANGELOG_MSG_BARRIER_ALREADY_DISABLED_STR                             \
    "Changelog barrier already disabled"
#define CHANGELOG_MSG_BARRIER_ON_ERROR_STR                                     \
    "Received another barrier on notification when last one is not served yet"
#define CHANGELOG_MSG_BARRIER_ENABLE_STR "Enabled changelog barrier"
#define CHANGELOG_MSG_BARRIER_KEY_NOT_FOUND_STR "barrier key not found"
#define CHANGELOG_MSG_ERROR_IN_DICT_GET_STR                                    \
    "Something went wrong in dict_get_str_boolean"
#define CHANGELOG_MSG_DIR_OPTIONS_NOT_SET_STR "changelog-dir option is not set"
#define CHANGELOG_MSG_FREEUP_FAILED_STR "could not cleanup bootstrapper"
#define CHANGELOG_MSG_CHILD_MISCONFIGURED_STR                                  \
    "translator needs a single subvolume"
#define CHANGELOG_MSG_VOL_MISCONFIGURED_STR                                    \
    "dangling volume. please check volfile"
#define CHANGELOG_MSG_DEQUEUING_BARRIER_FOPS_STR                               \
    "Dequeuing all the changelog barriered fops"
#define CHANGELOG_MSG_DEQUEUING_BARRIER_FOPS_FINISHED_STR                      \
    "Dequeuing changelog barriered fops is finished"
#define CHANGELOG_MSG_BARRIER_TIMEOUT_STR                                      \
    "Disabling changelog barrier because of the timeout"
#define CHANGELOG_MSG_TIMEOUT_ADD_FAILED_STR                                   \
    "Couldn't add changelog barrier timeout event"
#define CHANGELOG_MSG_RPC_BUILD_ERROR_STR "failed to build rpc options"
#define CHANGELOG_MSG_NOTIFY_REGISTER_FAILED_STR "failed to register notify"
#define CHANGELOG_MSG_RPC_START_ERROR_STR "failed to start rpc"
#define CHANGELOG_MSG_CREATE_FRAME_FAILED_STR "failed to create frame"
#define CHANGELOG_MSG_RPC_SUBMIT_REPLY_FAILED_STR "failed to serialize reply"
#define CHANGELOG_MSG_PROGRAM_NAME_REG_FAILED_STR "cannot register program"
#define CHANGELOG_MSG_CHANGELOG_NOT_ACTIVE_STR                                 \
    "Changelog is not active, return success"
#define CHANGELOG_MSG_PUT_BUFFER_FAILED_STR                                    \
    "failed to put buffer after consumption"
#define CHANGELOG_MSG_CLEANUP_ALREADY_SET_STR                                  \
    "cleanup_starting flag is already set for xl"
#define CHANGELOG_MSG_HANDLE_PROBE_ERROR_STR "xdr decoding error"
#endif /* !_CHANGELOG_MESSAGES_H_ */
