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

GLFS_MSGID(PC,
        PC_MSG_TIMER_EXPIRED,
        PC_MSG_DIR_OP_FAILED,
        PC_MSG_FILE_OP_FAILED,
        PC_MSG_TIMER_REG,
        PC_MSG_GRACE_TIMER_CANCELLED,
        PC_MSG_DICT_SET_FAILED,
        PC_MSG_DICT_GET_FAILED,
        PC_MSG_NO_MEMORY,
        PC_MSG_RPC_CBK_FAILED,
        PC_MSG_FUNCTION_CALL_ERROR,
        PC_MSG_RPC_INITED_ALREADY,
        PC_MSG_RPC_INIT,
        PC_MSG_RPC_DESTROY,
        PC_MSG_RPC_INVALID_CALL,
        PC_MSG_INVALID_ENTRY,
        PC_MSG_HANDSHAKE_RETURN,
        PC_MSG_CHILD_UP_NOTIFY_FAILED,
        PC_MSG_CLIENT_DISCONNECTED,
        PC_MSG_CHILD_DOWN_NOTIFY_FAILED,
        PC_MSG_PARENT_UP,
        PC_MSG_PARENT_DOWN,
        PC_MSG_RPC_INIT_FAILED,
        PC_MSG_RPC_NOTIFY_FAILED,
        PC_MSG_FD_DUPLICATE_TRY,
        PC_MSG_FD_SET_FAIL,
        PC_MSG_DICT_UNSERIALIZE_FAIL,
        PC_MSG_FD_GET_FAIL,
        PC_MSG_FD_CTX_INVALID,
        PC_MSG_FOP_SEND_FAILED,
        PC_MSG_XDR_DECODING_FAILED,
        PC_MSG_REMOTE_OP_FAILED,
        PC_MSG_RPC_STATUS_ERROR,
        PC_MSG_VOL_FILE_NOT_FOUND,
        PC_MSG_SEND_REQ_FAIL,
        PC_MSG_LOCK_VERSION_SERVER,
        PC_MSG_SET_LK_VERSION_ERROR,
        PC_MSG_LOCK_REQ_FAIL,
        PC_MSG_CLIENT_REQ_FAIL,
        PC_MSG_LOCK_ERROR,
        PC_MSG_LOCK_REACQUIRE,
        PC_MSG_CHILD_UP_NOTIFY,
        PC_MSG_CHILD_UP_NOTIFY_DELAY,
        PC_MSG_VOL_SET_FAIL,
        PC_MSG_SETVOLUME_FAIL,
        PC_MSG_VOLFILE_NOTIFY_FAILED,
        PC_MSG_REMOTE_VOL_CONNECTED,
        PC_MSG_LOCK_MISMATCH,
        PC_MSG_LOCK_MATCH,
        PC_MSG_AUTH_FAILED,
        PC_MSG_AUTH_FAILED_NOTIFY_FAILED,
        PC_MSG_CHILD_CONNECTING_EVENT,
        PC_MSG_CHILD_CONNECTING_NOTIFY_FAILED,
        PC_MSG_PROCESS_UUID_SET_FAIL,
        PC_MSG_DICT_ERROR,
        PC_MSG_DICT_SERIALIZE_FAIL,
        PC_MSG_PGM_NOT_FOUND,
        PC_MSG_VERSION_INFO,
        PC_MSG_PORT_NUM_ERROR,
        PC_MSG_VERSION_ERROR,
        PC_MSG_DIR_OP_SUCCESS,
        PC_MSG_BAD_FD,
        PC_MSG_CLIENT_LOCK_INFO,
        PC_MSG_CACHE_INVALIDATION_FAIL,
        PC_MSG_CHILD_STATUS,
        PC_MSG_GFID_NULL,
        PC_MSG_RECALL_LEASE_FAIL,
        PC_MSG_INODELK_CONTENTION_FAIL,
        PC_MSG_ENTRYLK_CONTENTION_FAIL
);

#endif /* !_PC_MESSAGES_H__ */
