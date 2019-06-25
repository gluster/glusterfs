/*
 Copyright (c) 2018 Red Hat, Inc. <http://www.redhat.com>
 This file is part of GlusterFS.

 This file is licensed to you under your choice of the GNU Lesser
 General Public License, version 3 or any later version (LGPLv3 or
 later), or the GNU General Public License, version 2 (GPLv2), in all
 cases as published by the Free Software Foundation.
 */

#ifndef _SNAPVIEW_SERVER_MESSAGES_H_
#define _SNAPVIEW_SERVER_MESSAGES_H_

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

GLFS_MSGID(SNAPVIEW_SERVER, SVS_MSG_NO_MEMORY, SVS_MSG_MEM_ACNT_FAILED,
           SVS_MSG_NULL_GFID, SVS_MSG_GET_LATEST_SNAP_FAILED,
           SVS_MSG_INVALID_GLFS_CTX, SVS_MSG_LOCK_DESTROY_FAILED,
           SVS_MSG_SNAPSHOT_LIST_CHANGED, SVS_MSG_MGMT_INIT_FAILED,
           SVS_MSG_GET_SNAPSHOT_LIST_FAILED, SVS_MSG_GET_GLFS_H_OBJECT_FAILED,
           SVS_MSG_PARENT_CTX_OR_NAME_NULL, SVS_MSG_SET_INODE_CONTEXT_FAILED,
           SVS_MSG_GET_INODE_CONTEXT_FAILED, SVS_MSG_NEW_INODE_CTX_FAILED,
           SVS_MSG_DELETE_INODE_CONTEXT_FAILED, SVS_MSG_SET_FD_CONTEXT_FAILED,
           SVS_MSG_NEW_FD_CTX_FAILED, SVS_MSG_DELETE_FD_CTX_FAILED,
           SVS_MSG_GETXATTR_FAILED, SVS_MSG_LISTXATTR_FAILED,
           SVS_MSG_RELEASEDIR_FAILED, SVS_MSG_RELEASE_FAILED,
           SVS_MSG_TELLDIR_FAILED, SVS_MSG_STAT_FAILED, SVS_MSG_STATFS_FAILED,
           SVS_MSG_OPEN_FAILED, SVS_MSG_READ_FAILED, SVS_MSG_READLINK_FAILED,
           SVS_MSG_ACCESS_FAILED, SVS_MSG_GET_FD_CONTEXT_FAILED,
           SVS_MSG_DICT_SET_FAILED, SVS_MSG_OPENDIR_FAILED,
           SVS_MSG_FS_INSTANCE_INVALID, SVS_MSG_SETFSUID_FAIL,
           SVS_MSG_SETFSGID_FAIL, SVS_MSG_SETFSGRPS_FAIL,
           SVS_MSG_BUILD_TRNSPRT_OPT_FAILED, SVS_MSG_RPC_INIT_FAILED,
           SVS_MSG_REG_NOTIFY_FAILED, SVS_MSG_REG_CBK_PRGM_FAILED,
           SVS_MSG_RPC_CLNT_START_FAILED, SVS_MSG_XDR_PAYLOAD_FAILED,
           SVS_MSG_NULL_CTX, SVS_MSG_RPC_CALL_FAILED, SVS_MSG_XDR_DECODE_FAILED,
           SVS_MSG_RSP_DICT_EMPTY, SVS_MSG_DICT_GET_FAILED,
           SVS_MSG_SNAP_LIST_REFRESH_FAILED, SVS_MSG_RPC_REQ_FAILED,
           SVS_MSG_CLOSEDIR_FAILED, SVS_MSG_CLOSE_FAILED,
           SVS_MSG_GFID_GEN_FAILED, SVS_MSG_GLFS_NEW_FAILED,
           SVS_MSG_SET_VOLFILE_SERVR_FAILED, SVS_MSG_SET_LOGGING_FAILED,
           SVS_MSG_VOLFILE_SERVER_GET_FAIL, SVS_MSG_GLFS_INIT_FAILED);

#endif /* !_SNAPVIEW_CLIENT_MESSAGES_H_ */
