/*
 *   Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
 *   This file is part of GlusterFS.
 *
 *   This file is licensed to you under your choice of the GNU Lesser
 *   General Public License, version 3 or any later version (LGPLv3 or
 *   later), or the GNU General Public License, version 2 (GPLv2), in all
 *   cases as published by the Free Software Foundation.
 *   */

#ifndef _GFAPI_MESSAGES_H__
#define _GFAPI_MESSAGES_H__

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

GLFS_MSGID(API,
        API_MSG_MEM_ACCT_INIT_FAILED,
        API_MSG_MASTER_XLATOR_INIT_FAILED,
        API_MSG_GFAPI_XLATOR_INIT_FAILED,
        API_MSG_VOLFILE_OPEN_FAILED,
        API_MSG_VOL_SPEC_FILE_ERROR,
        API_MSG_GLFS_FSOBJ_NULL,
        API_MSG_INVALID_ENTRY,
        API_MSG_FSMUTEX_LOCK_FAILED,
        API_MSG_COND_WAIT_FAILED,
        API_MSG_FSMUTEX_UNLOCK_FAILED,
        API_MSG_INODE_REFRESH_FAILED,
        API_MSG_GRAPH_CONSTRUCT_FAILED,
        API_MSG_API_XLATOR_ERROR,
        API_MSG_XDR_PAYLOAD_FAILED,
        API_MSG_GET_VOLINFO_CBK_FAILED,
        API_MSG_FETCH_VOLUUID_FAILED,
        API_MSG_INSUFF_SIZE,
        API_MSG_FRAME_CREAT_FAILED,
        API_MSG_DICT_SET_FAILED,
        API_MSG_XDR_DECODE_FAILED,
        API_MSG_GET_VOLFILE_FAILED,
        API_MSG_WRONG_OPVERSION,
        API_MSG_DICT_SERIALIZE_FAILED,
        API_MSG_REMOTE_HOST_CONN_FAILED,
        API_MSG_VOLFILE_SERVER_EXHAUST,
        API_MSG_CREATE_RPC_CLIENT_FAILED,
        API_MSG_REG_NOTIFY_FUNC_FAILED,
        API_MSG_REG_CBK_FUNC_FAILED,
        API_MSG_GET_CWD_FAILED,
        API_MSG_FGETXATTR_FAILED,
        API_MSG_LOCKINFO_KEY_MISSING,
        API_MSG_FSETXATTR_FAILED,
        API_MSG_FSYNC_FAILED,
        API_MSG_FDCREATE_FAILED,
        API_MSG_INODE_PATH_FAILED,
        API_MSG_SYNCOP_OPEN_FAILED,
        API_MSG_LOCK_MIGRATE_FAILED,
        API_MSG_OPENFD_SKIPPED,
        API_MSG_FIRST_LOOKUP_GRAPH_FAILED,
        API_MSG_CWD_GRAPH_REF_FAILED,
        API_MSG_SWITCHED_GRAPH,
        API_MSG_XDR_RESPONSE_DECODE_FAILED,
        API_MSG_VOLFILE_INFO,
        API_MSG_VOLFILE_CONNECTING,
        API_MSG_NEW_GRAPH,
        API_MSG_ALLOC_FAILED,
        API_MSG_CREATE_HANDLE_FAILED,
        API_MSG_INODE_LINK_FAILED,
        API_MSG_STATEDUMP_FAILED,
        API_MSG_XREADDIRP_R_FAILED,
        API_MSG_LOCK_INSERT_MERGE_FAILED,
        API_MSG_SETTING_LOCK_TYPE_FAILED,
        API_MSG_INODE_FIND_FAILED,
        API_MSG_FDCTX_SET_FAILED,
        API_MSG_UPCALL_SYNCOP_FAILED
);

#endif /* !_GFAPI_MESSAGES_H__ */
