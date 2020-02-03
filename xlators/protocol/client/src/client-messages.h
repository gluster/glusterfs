/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _PC_MESSAGES_H__
#define _PC_MESSAGES_H__

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
    PC, PC_MSG_TIMER_EXPIRED, PC_MSG_DIR_OP_FAILED, PC_MSG_FILE_OP_FAILED,
    PC_MSG_TIMER_REG, PC_MSG_GRACE_TIMER_CANCELLED, PC_MSG_DICT_SET_FAILED,
    PC_MSG_DICT_GET_FAILED, PC_MSG_NO_MEMORY, PC_MSG_RPC_CBK_FAILED,
    PC_MSG_FUNCTION_CALL_ERROR, PC_MSG_RPC_INITED_ALREADY, PC_MSG_RPC_INIT,
    PC_MSG_RPC_DESTROY, PC_MSG_RPC_INVALID_CALL, PC_MSG_INVALID_ENTRY,
    PC_MSG_HANDSHAKE_RETURN, PC_MSG_CHILD_UP_NOTIFY_FAILED,
    PC_MSG_CLIENT_DISCONNECTED, PC_MSG_CHILD_DOWN_NOTIFY_FAILED,
    PC_MSG_PARENT_UP, PC_MSG_PARENT_DOWN, PC_MSG_RPC_INIT_FAILED,
    PC_MSG_RPC_NOTIFY_FAILED, PC_MSG_FD_DUPLICATE_TRY, PC_MSG_FD_SET_FAIL,
    PC_MSG_DICT_UNSERIALIZE_FAIL, PC_MSG_FD_GET_FAIL, PC_MSG_FD_CTX_INVALID,
    PC_MSG_FOP_SEND_FAILED, PC_MSG_XDR_DECODING_FAILED, PC_MSG_REMOTE_OP_FAILED,
    PC_MSG_RPC_STATUS_ERROR, PC_MSG_VOL_FILE_NOT_FOUND, PC_MSG_SEND_REQ_FAIL,
    PC_MSG_LOCK_VERSION_SERVER, PC_MSG_SET_LK_VERSION_ERROR,
    PC_MSG_LOCK_REQ_FAIL, PC_MSG_CLIENT_REQ_FAIL, PC_MSG_LOCK_ERROR,
    PC_MSG_LOCK_REACQUIRE, PC_MSG_CHILD_UP_NOTIFY, PC_MSG_CHILD_UP_NOTIFY_DELAY,
    PC_MSG_VOL_SET_FAIL, PC_MSG_SETVOLUME_FAIL, PC_MSG_VOLFILE_NOTIFY_FAILED,
    PC_MSG_REMOTE_VOL_CONNECTED, PC_MSG_LOCK_MISMATCH, PC_MSG_LOCK_MATCH,
    PC_MSG_AUTH_FAILED, PC_MSG_AUTH_FAILED_NOTIFY_FAILED,
    PC_MSG_CHILD_CONNECTING_EVENT, PC_MSG_CHILD_CONNECTING_NOTIFY_FAILED,
    PC_MSG_PROCESS_UUID_SET_FAIL, PC_MSG_DICT_ERROR, PC_MSG_DICT_SERIALIZE_FAIL,
    PC_MSG_PGM_NOT_FOUND, PC_MSG_VERSION_INFO, PC_MSG_PORT_NUM_ERROR,
    PC_MSG_VERSION_ERROR, PC_MSG_DIR_OP_SUCCESS, PC_MSG_BAD_FD,
    PC_MSG_CLIENT_LOCK_INFO, PC_MSG_CACHE_INVALIDATION_FAIL,
    PC_MSG_CHILD_STATUS, PC_MSG_GFID_NULL, PC_MSG_RECALL_LEASE_FAIL,
    PC_MSG_INODELK_CONTENTION_FAIL, PC_MSG_ENTRYLK_CONTENTION_FAIL,
    PC_MSG_BIGGER_SIZE, PC_MSG_CLIENT_DUMP_LOCKS_FAILED, PC_MSG_UNKNOWN_CMD,
    PC_MSG_REOPEN_FAILED, PC_MSG_FIND_KEY_FAILED, PC_MSG_VOL_ID_CHANGED,
    PC_MSG_GETHOSTNAME_FAILED, PC_MSG_VOLFILE_KEY_SET_FAILED,
    PC_MSG_VOLFILE_CHECKSUM_FAILED, PC_MSG_FRAME_NOT_FOUND,
    PC_MSG_REMOTE_SUBVOL_SET_FAIL, PC_MSG_HANDSHAKE_PGM_NOT_FOUND,
    PC_MSG_MERGE_IOBREF_FAILED, PC_MSG_ADD_IOBUF_FAILED,
    PC_MSG_RELEASE_DIR_OP_FAILED, PC_MSG_REMOTE_HOST_SET_FAILED,
    PC_MSG_REMOTE_PORT_SET_FAILED, PC_MSG_REMOTE_HOST_NOT_SET,
    PC_MSG_NOREMOTE_HOST, PC_MSG_REMOTE_SUBVOL_NOT_GIVEN,
    PC_MSG_FATAL_CLIENT_PROTOCOL, PC_MSG_VOL_DANGLING,
    PC_MSG_CREATE_MEM_POOL_FAILED, PC_MSG_PVT_XLATOR_NULL, PC_MSG_XLATOR_NULL,
    PC_MSG_LEASE_FOP_FAILED, PC_MSG_DICT_SET_FAIL, PC_MSG_NO_MEM,
    PC_MSG_UNKNOWN_LOCK_TYPE, PC_MSG_CLIENT_UID_ALLOC_FAILED);

#define PC_MSG_REMOTE_OP_FAILED_STR "remote operation failed."
#define PC_MSG_XDR_DECODING_FAILED_STR "XDR decoding failed"
#define PC_MSG_FOP_SEND_FAILED_STR "failed to send the fop"
#define PC_MSG_BIGGER_SIZE_STR "read-size is bigger than iobuf isze"
#define PC_MSG_CLIENT_DUMP_LOCKS_FAILED_STR "client dump locks failed"
#define PC_MSG_UNKNOWN_CMD_STR "Unknown cmd"
#define PC_MSG_CHILD_UP_NOTIFY_FAILED_STR "notify of CHILD_UP failed"
#define PC_MSG_CHILD_STATUS_STR                                                \
    "Defering sending CHILD_UP message as the client translators are not yet " \
    "ready to serve"
#define PC_MSG_CHILD_UP_NOTIFY_STR "last fd open'd - notifying CHILD_UP"
#define PC_MSG_RPC_STATUS_ERROR_STR                                            \
    "received RPC status error, returning ENOTCONN"
#define PC_MSG_REOPEN_FAILED_STR "reopen failed"
#define PC_MSG_DIR_OP_SUCCESS_STR "reopen dir succeeded"
#define PC_MSG_DIR_OP_FAILED_STR "failed to send the re-opendir request"
#define PC_MSG_CHILD_UP_NOTIFY_DELAY_STR                                       \
    "fds open - Delaying child_up until they are re-opened"
#define PC_MSG_VOL_SET_FAIL_STR "failed to set the volume"
#define PC_MSG_DICT_UNSERIALIZE_FAIL_STR "failed to unserialize buffer to dict"
#define PC_MSG_DICT_GET_FAILED_STR "failed to get from reply dict"
#define PC_MSG_SETVOLUME_FAIL_STR "SETVOLUME on remote-host failed"
#define PC_MSG_VOLFILE_NOTIFY_FAILED_STR "notify of VOLFILE_MODIFIED failed"
#define PC_MSG_FIND_KEY_FAILED_STR "failed to find key in the options"
#define PC_MSG_VOL_ID_CHANGED_STR                                              \
    "volume-id changed, can't connect to server. Needs remount"
#define PC_MSG_REMOTE_VOL_CONNECTED_STR "Connected, attached to remote volume"
#define PC_MSG_AUTH_FAILED_STR "sending AUTH_FAILED event"
#define PC_MSG_AUTH_FAILED_NOTIFY_FAILED_STR "notify of AUTH_FAILED failed"
#define PC_MSG_CHILD_CONNECTING_EVENT_STR "sending CHILD_CONNECTING event"
#define PC_MSG_CHILD_CONNECTING_NOTIFY_FAILED_STR                              \
    "notify of CHILD_CONNECTING failed"
#define PC_MSG_DICT_SET_FAILED_STR "failed to set in handshake msg"
#define PC_MSG_GETHOSTNAME_FAILED_STR "gethostname: failed"
#define PC_MSG_PROCESS_UUID_SET_FAIL_STR                                       \
    "asprintf failed while setting process_uuid"
#define PC_MSG_VOLFILE_KEY_SET_FAILED_STR "failed to set volfile-key"
#define PC_MSG_VOLFILE_CHECKSUM_FAILED_STR "failed to set volfile-checksum"
#define PC_MSG_DICT_SERIALIZE_FAIL_STR "failed to serialize dictionary"
#define PC_MSG_PGM_NOT_FOUND_STR "xlator not found OR RPC program not found"
#define PC_MSG_VERSION_INFO_STR "Using Program"
#define PC_MSG_FRAME_NOT_FOUND_STR "frame not found with rpc request"
#define PC_MSG_PORT_NUM_ERROR_STR                                              \
    "failed to get the port number for remote subvolume. Please run gluster "  \
    "volume status on server to see if brick process is running"
#define PC_MSG_REMOTE_SUBVOL_SET_FAIL_STR "remote-subvolume not set in volfile"
#define PC_MSG_VERSION_ERROR_STR "failed to get the version from server"
#define PC_MSG_NO_VERSION_SUPPORT_STR "server doesn't support the version"
#define PC_MSG_HANDSHAKE_PGM_NOT_FOUND_STR "handshake program not found"
#define PC_MSG_MERGE_IOBREF_FAILED_STR                                         \
    "cannot merge iobref passed from caller into new_iobref"
#define PC_MSG_ADD_IOBUF_FAILED_STR "cannot add iobuf into iobref"
#define PC_MSG_RELEASE_DIR_OP_FAILED_STR "release dir op failed"
#define PC_MSG_FILE_OP_FAILED_STR "release fop failed"
#define PC_MSG_REMOTE_HOST_SET_FAILED_STR "failed to set remote-host"
#define PC_MSG_REMOTE_PORT_SET_FAILED_STR "failed to set remote-port"
#define PC_MSG_RPC_INIT_STR "client rpc init command"
#define PC_MSG_RPC_DESTROY_STR "client rpc destroy command"
#define PC_MSG_HANDSHAKE_RETURN_STR "handshake msg returned"
#define PC_MSG_CLIENT_DISCONNECTED_STR                                         \
    "disconnected from client, process will keep trying to connect glusterd "  \
    "until brick's port is available"
#define PC_MSG_CHILD_DOWN_NOTIFY_FAILED_STR "CHILD_DOWN notify failed"
#define PC_MSG_PARENT_UP_STR                                                   \
    "parent translators are ready, attempting connect on transport"
#define PC_MSG_PARENT_DOWN_STR                                                 \
    "current graph is no longer active, destroying rpc_client"
#define PC_MSG_REMOTE_HOST_NOT_SET_STR                                         \
    "Remote host is not set. Assuming the volfile server as remote host"
#define PC_MSG_NOREMOTE_HOST_STR "No remote host to connect"
#define PC_MSG_REMOTE_SUBVOL_NOT_GIVEN_STR "option 'remote-subvolume' not given"
#define PC_MSG_NO_MEMORY_STR "Memory accounting init failed"
#define PC_MSG_RPC_INVALID_CALL_STR                                            \
    "RPC destroy called on already destroyed connection"
#define PC_MSG_RPC_INITED_ALREADY_STR "client rpc already init'ed"
#define PC_MSG_RPC_INIT_FAILED_STR "failed to initialize RPC"
#define PC_MSG_RPC_NOTIFY_FAILED_STR "failed to register notify"
#define PC_MSG_RPC_CBK_FAILED_STR "failed to reister callback program"
#define PC_MSG_FATAL_CLIENT_PROTOCOL_STR                                       \
    "FATAL: client protocol, translator cannot have any subvolumes"
#define PC_MSG_VOL_DANGLING_STR "Volume is dangling"
#define PC_MSG_CREATE_MEM_POOL_FAILED_STR                                      \
    "failed to create local_t's memory pool"
#define PC_MSG_XLATOR_NULL_STR "xlator is NULL"
#define PC_MSG_PVT_XLATOR_NULL_STR "private structure of the xlator is NULL"
#define PC_MSG_LEASE_FOP_FAILED_STR "Lease fop failed"
#define PC_MSG_LOCK_ERROR_STR                                                  \
    "Unexpected case in subtract_locks. Please send a bug report to "          \
    "gluster-devel@gluster.org"
#define PC_MSG_FD_CTX_INVALID_STR "fdctx not valid"
#define PC_MSG_FD_GET_FAIL_STR "failed to get fd context. sending EBADFD"
#define PC_MSG_DICT_SET_FAIL_STR "could not set dict"
#define PC_MSG_CLIENT_LOCK_INFO_STR "client lock info"
#define PC_MSG_BAD_FD_STR "remote_fd is -1. EBADFD"
#define PC_MSG_FUNCTION_CALL_ERROR_STR "this function should not be called"
#define PC_MSG_RECALL_LEASE_FAIL_STR "XDR decode of recall lease failed"
#define PC_MSG_CACHE_INVALIDATION_FAIL_STR                                     \
    "XDR decode of cache_invalidation failed"
#define PC_MSG_INODELK_CONTENTION_FAIL_STR                                     \
    "XDR decode of inodelk contention failed"
#define PC_MSG_ENTRYLK_CONTENTION_FAIL_STR                                     \
    "XDR decode of entrylk contention failed"
#define PC_MSG_FD_DUPLICATE_TRY_STR "trying duplicate remote fd set"
#define PC_MSG_FD_SET_FAIL_STR "failed to set remote-fd"
#define PC_MSG_NO_MEM_STR "No memory"
#define PC_MSG_UNKNOWN_LOCK_TYPE_STR "Unknown lock type"
#define PC_MSG_CLIENT_UID_ALLOC_FAILED_STR "client-uid could not be allocated"

#endif /* !_PC_MESSAGES_H__ */
