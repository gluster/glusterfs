/*
 *   Copyright (c) 2015-2018 Red Hat, Inc. <http://www.redhat.com>
 *   This file is part of GlusterFS.
 *
 *   This file is licensed to you under your choice of the GNU Lesser
 *   General Public License, version 3 or any later version (LGPLv3 or
 *   later), or the GNU General Public License, version 2 (GPLv2), in all
 *   cases as published by the Free Software Foundation.
 *   */

#ifndef _GFAPI_MESSAGES_H__
#define _GFAPI_MESSAGES_H__

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

GLFS_MSGID(API, API_MSG_MEM_ACCT_INIT_FAILED,
           API_MSG_PRIMARY_XLATOR_INIT_FAILED, API_MSG_GFAPI_XLATOR_INIT_FAILED,
           API_MSG_VOLFILE_OPEN_FAILED, API_MSG_VOL_SPEC_FILE_ERROR,
           API_MSG_GLFS_FSOBJ_NULL, API_MSG_INVALID_ENTRY,
           API_MSG_FSMUTEX_LOCK_FAILED, API_MSG_COND_WAIT_FAILED,
           API_MSG_FSMUTEX_UNLOCK_FAILED, API_MSG_INODE_REFRESH_FAILED,
           API_MSG_GRAPH_CONSTRUCT_FAILED, API_MSG_API_XLATOR_ERROR,
           API_MSG_XDR_PAYLOAD_FAILED, API_MSG_GET_VOLINFO_CBK_FAILED,
           API_MSG_FETCH_VOLUUID_FAILED, API_MSG_INSUFF_SIZE,
           API_MSG_FRAME_CREAT_FAILED, API_MSG_DICT_SET_FAILED,
           API_MSG_XDR_DECODE_FAILED, API_MSG_GET_VOLFILE_FAILED,
           API_MSG_WRONG_OPVERSION, API_MSG_DICT_SERIALIZE_FAILED,
           API_MSG_REMOTE_HOST_CONN_FAILED, API_MSG_VOLFILE_SERVER_EXHAUST,
           API_MSG_CREATE_RPC_CLIENT_FAILED, API_MSG_REG_NOTIFY_FUNC_FAILED,
           API_MSG_REG_CBK_FUNC_FAILED, API_MSG_GET_CWD_FAILED,
           API_MSG_FGETXATTR_FAILED, API_MSG_LOCKINFO_KEY_MISSING,
           API_MSG_FSETXATTR_FAILED, API_MSG_FSYNC_FAILED,
           API_MSG_FDCREATE_FAILED, API_MSG_INODE_PATH_FAILED,
           API_MSG_SYNCOP_OPEN_FAILED, API_MSG_LOCK_MIGRATE_FAILED,
           API_MSG_OPENFD_SKIPPED, API_MSG_FIRST_LOOKUP_GRAPH_FAILED,
           API_MSG_CWD_GRAPH_REF_FAILED, API_MSG_SWITCHED_GRAPH,
           API_MSG_XDR_RESPONSE_DECODE_FAILED, API_MSG_VOLFILE_INFO,
           API_MSG_VOLFILE_CONNECTING, API_MSG_NEW_GRAPH, API_MSG_ALLOC_FAILED,
           API_MSG_CREATE_HANDLE_FAILED, API_MSG_INODE_LINK_FAILED,
           API_MSG_STATEDUMP_FAILED, API_MSG_XREADDIRP_R_FAILED,
           API_MSG_LOCK_INSERT_MERGE_FAILED, API_MSG_SETTING_LOCK_TYPE_FAILED,
           API_MSG_INODE_FIND_FAILED, API_MSG_FDCTX_SET_FAILED,
           API_MSG_UPCALL_SYNCOP_FAILED, API_MSG_INVALID_ARG,
           API_MSG_UPCALL_EVENT_NULL_RECEIVED, API_MSG_FLAGS_HANDLE,
           API_MSG_FDCREATE_FAILED_ON_GRAPH, API_MSG_TRANS_RDMA_DEP,
           API_MSG_TRANS_NOT_SUPPORTED, API_MSG_FS_NOT_INIT,
           API_MSG_INVALID_SYSRQ, API_MSG_DECODE_XDR_FAILED, API_MSG_NULL,
           API_MSG_CALL_NOT_SUCCESSFUL, API_MSG_CALL_NOT_VALID,
           API_MSG_UNABLE_TO_DEL, API_MSG_REMOTE_HOST_DISCONN,
           API_MSG_HANDLE_NOT_SET);

#define API_MSG_ALLOC_FAILED_STR "Upcall allocation failed"
#define API_MSG_LOCK_INSERT_MERGE_FAILED_STR                                   \
    "Lock insertion and splitting/merging failed"
#define API_MSG_SETTING_LOCK_TYPE_FAILED_STR "Setting lock type failed"

#define API_MSG_INVALID_ARG_STR "Invalid"
#define API_MSG_INVALID_ENTRY_STR "Upcall entry validation failed"
#define API_MSG_INODE_FIND_FAILED_STR "Unable to find inode entry"
#define API_MSG_CREATE_HANDLE_FAILED_STR "handle creation failed"
#define API_MSG_UPCALL_EVENT_NULL_RECEIVED_STR                                 \
    "Upcall_EVENT_NULL received. Skipping it"
#define API_MSG_UPCALL_SYNCOP_FAILED_STR "Synctask for upcall failed"
#define API_MSG_FDCREATE_FAILED_STR "Allocating anonymous fd failed"
#define API_MSG_XREADDIRP_R_FAILED_STR "glfs_x_readdirp_r failed"
#define API_MSG_FDCTX_SET_FAILED_STR "Setting fd ctx failed"
#define API_MSG_FLAGS_HANDLE_STR "arg not set. Flags handled are"
#define API_MSG_INODE_REFRESH_FAILED_STR "inode refresh failed"
#define API_MSG_INODE_LINK_FAILED_STR "inode linking failed"
#define API_MSG_GET_CWD_FAILED_STR "Failed to get cwd"
#define API_MSG_FGETXATTR_FAILED_STR "fgetxattr failed"
#define API_MSG_LOCKINFO_KEY_MISSING_STR "missing lockinfo key"
#define API_MSG_FSYNC_FAILED_STR "fsync() failed"
#define API_MSG_FDCREATE_FAILED_ON_GRAPH_STR "fd_create failed on graph"
#define API_MSG_INODE_PATH_FAILED_STR "inode_path failed"
#define API_MSG_SYNCOP_OPEN_FAILED_STR "syncop_open failed"
#define API_MSG_LOCK_MIGRATE_FAILED_STR "lock migration failed on graph"
#define API_MSG_OPENFD_SKIPPED_STR "skipping openfd in graph"
#define API_MSG_FIRST_LOOKUP_GRAPH_FAILED_STR "first lookup on graph failed"
#define API_MSG_CWD_GRAPH_REF_FAILED_STR "cwd refresh of graph failed"
#define API_MSG_SWITCHED_GRAPH_STR "switched to graph"
#define API_MSG_FSETXATTR_FAILED_STR "fsetxattr failed"
#define API_MSG_MEM_ACCT_INIT_FAILED_STR "Memory accounting init failed"
#define API_MSG_PRIMARY_XLATOR_INIT_FAILED_STR                                 \
    "primary xlator for initialization failed"
#define API_MSG_GFAPI_XLATOR_INIT_FAILED_STR                                   \
    "failed to initialize gfapi translator"
#define API_MSG_VOLFILE_OPEN_FAILED_STR "volume file open failed"
#define API_MSG_VOL_SPEC_FILE_ERROR_STR "Cannot reach volume specification file"
#define API_MSG_TRANS_RDMA_DEP_STR                                             \
    "transport RDMA is deprecated, falling back to tcp"
#define API_MSG_TRANS_NOT_SUPPORTED_STR                                        \
    "transport is not supported, possible values tcp|unix"
#define API_MSG_GLFS_FSOBJ_NULL_STR "fs is NULL"
#define API_MSG_FS_NOT_INIT_STR "fs is not properly initialized"
#define API_MSG_FSMUTEX_LOCK_FAILED_STR                                        \
    "pthread lock on glfs mutex, returned error"
#define API_MSG_FSMUTEX_UNLOCK_FAILED_STR                                      \
    "pthread unlock on glfs mutex, returned error"
#define API_MSG_COND_WAIT_FAILED_STR "cond wait failed"
#define API_MSG_INVALID_SYSRQ_STR "not a valid sysrq"
#define API_MSG_GRAPH_CONSTRUCT_FAILED_STR "failed to construct the graph"
#define API_MSG_API_XLATOR_ERROR_STR                                           \
    "api primary xlator cannot be specified in volume file"
#define API_MSG_STATEDUMP_FAILED_STR "statedump failed"
#define API_MSG_DECODE_XDR_FAILED_STR                                          \
    "Failed to decode xdr response for GF_CBK_STATEDUMP"
#define API_MSG_NULL_STR "NULL"
#define API_MSG_XDR_PAYLOAD_FAILED_STR "failed to create XDR payload"
#define API_MSG_CALL_NOT_SUCCESSFUL_STR                                        \
    "GET_VOLUME_INFO RPC call is not successful"
#define API_MSG_XDR_RESPONSE_DECODE_FAILED_STR                                 \
    "Failed to decode xdr response for GET_VOLUME_INFO"
#define API_MSG_CALL_NOT_VALID_STR                                             \
    "Response received for GET_VOLUME_INFO RPC is not valid"
#define API_MSG_GET_VOLINFO_CBK_FAILED_STR                                     \
    "In GET_VOLUME_INFO cbk, received error"
#define API_MSG_FETCH_VOLUUID_FAILED_STR "Unable to fetch volume UUID"
#define API_MSG_INSUFF_SIZE_STR "Insufficient size passed"
#define API_MSG_FRAME_CREAT_FAILED_STR "failed to create the frame"
#define API_MSG_DICT_SET_FAILED_STR "failed to set"
#define API_MSG_XDR_DECODE_FAILED_STR "XDR decoding error"
#define API_MSG_GET_VOLFILE_FAILED_STR "failed to get the volume file"
#define API_MSG_VOLFILE_INFO_STR "No change in volfile, continuing"
#define API_MSG_UNABLE_TO_DEL_STR "unable to delete file"
#define API_MSG_WRONG_OPVERSION_STR                                            \
    "Server is operating at an op-version which is not supported"
#define API_MSG_DICT_SERIALIZE_FAILED_STR "Failed to serialize dictionary"
#define API_MSG_REMOTE_HOST_CONN_FAILED_STR "Failed to connect to remote-host"
#define API_MSG_REMOTE_HOST_DISCONN_STR "disconnected from remote-host"
#define API_MSG_VOLFILE_SERVER_EXHAUST_STR "Exhausted all volfile servers"
#define API_MSG_VOLFILE_CONNECTING_STR "connecting to next volfile server"
#define API_MSG_CREATE_RPC_CLIENT_FAILED_STR "failed to create rpc clnt"
#define API_MSG_REG_NOTIFY_FUNC_FAILED_STR "failed to register notify function"
#define API_MSG_REG_CBK_FUNC_FAILED_STR "failed to register callback function"
#define API_MSG_NEW_GRAPH_STR "New graph coming up"
#define API_MSG_HANDLE_NOT_SET_STR "handle not set. Flags handled for xstat are"
#endif /* !_GFAPI_MESSAGES_H__ */
