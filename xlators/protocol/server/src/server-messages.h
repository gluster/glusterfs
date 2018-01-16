/*
  Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#ifndef _PS_MESSAGES_H__
#define _PS_MESSAGES_H__

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

GLFS_MSGID(PS,
        PS_MSG_AUTHENTICATE_ERROR,
        PS_MSG_VOL_VALIDATE_FAILED,
        PS_MSG_AUTH_INIT_FAILED,
        PS_MSG_REMOTE_CLIENT_REFUSED,
        PS_MSG_GFID_RESOLVE_FAILED,
        PS_MSG_ANONYMOUS_FD_CREATE_FAILED,
        PS_MSG_NO_MEMORY,
        PS_MSG_FD_NOT_FOUND,
        PS_MSG_INVALID_ENTRY,
        PS_MSG_GET_UID_FAILED,
        PS_MSG_UID_NOT_FOUND,
        PS_MSG_MAPPING_ERROR,
        PS_MSG_FD_CLEANUP,
        PS_MSG_SERVER_CTX_GET_FAILED,
        PS_MSG_FDENTRY_NULL,
        PS_MSG_DIR_NOT_FOUND,
        PS_MSG_SERVER_MSG,
        PS_MSG_DICT_SERIALIZE_FAIL,
        PS_MSG_RW_STAT,
        PS_MSG_DICT_GET_FAILED,
        PS_MSG_LOGIN_ERROR,
        PS_MSG_REMOUNT_CLIENT_REQD,
        PS_MSG_DEFAULTING_FILE,
        PS_MSG_VOL_FILE_OPEN_FAILED,
        PS_MSG_STAT_ERROR,
        PS_MSG_SSL_NAME_SET_FAILED,
        PS_MSG_ASPRINTF_FAILED,
        PS_MSG_CLIENT_VERSION_NOT_SET,
        PS_MSG_CLIENT_ACCEPTED,
        PS_MSG_CLIENT_LK_VERSION_ERROR,
        PS_MSG_GRACE_TIMER_EXPD,
        PS_MSG_SERIALIZE_REPLY_FAILED,
        PS_MSG_AUTH_IP_ERROR,
        PS_MSG_SKIP_FORMAT_CHK,
        PS_MSG_INTERNET_ADDR_ERROR,
        PS_MSG_CLIENT_DISCONNECTING,
        PS_MSG_GRACE_TIMER_START,
        PS_MSG_STATEDUMP_PATH_ERROR,
        PS_MSG_GRP_CACHE_ERROR,
        PS_MSG_RPC_CONF_ERROR,
        PS_MSG_TRANSPORT_ERROR,
        PS_MSG_SUBVOL_NULL,
        PS_MSG_PARENT_VOL_ERROR,
        PS_MSG_RPCSVC_CREATE_FAILED,
        PS_MSG_RPCSVC_LISTENER_CREATE_FAILED,
        PS_MSG_RPCSVC_NOTIFY,
        PS_MSG_PGM_REG_FAILED,
        PS_MSG_ULIMIT_SET_FAILED,
        PS_MSG_STATFS,
        PS_MSG_LOOKUP_INFO,
        PS_MSG_LK_INFO,
        PS_MSG_LOCK_ERROR,
        PS_MSG_INODELK_INFO,
        PS_MSG_ENTRYLK_INFO,
        PS_MSG_ACCESS_INFO,
        PS_MSG_DIR_INFO,
        PS_MSG_MKNOD_INFO,
        PS_MSG_REMOVEXATTR_INFO,
        PS_MSG_GETXATTR_INFO,
        PS_MSG_SETXATTR_INFO,
        PS_MSG_RENAME_INFO,
        PS_MSG_LINK_INFO,
        PS_MSG_TRUNCATE_INFO,
        PS_MSG_FSTAT_INFO,
        PS_MSG_FLUSH_INFO,
        PS_MSG_SYNC_INFO,
        PS_MSG_WRITE_INFO,
        PS_MSG_READ_INFO,
        PS_MSG_CHKSUM_INFO,
        PS_MSG_OPEN_INFO,
        PS_MSG_CREATE_INFO,
        PS_MSG_SETATTR_INFO,
        PS_MSG_XATTROP_INFO,
        PS_MSG_ALLOC_INFO,
        PS_MSG_DISCARD_INFO,
        PS_MSG_ZEROFILL_INFO,
        PS_MSG_FD_CREATE_FAILED,
        PS_MSG_WRONG_STATE,
        PS_MSG_CONF_DIR_INVALID,
        PS_MSG_MOUNT_PT_FAIL,
        PS_MSG_STAT_INFO,
        PS_MSG_FILE_OP_FAILED,
        PS_MSG_GRACE_TIMER_CANCELLED,
        PS_MSG_ENCODE_MSG_FAILED,
        PS_MSG_REPLY_SUBMIT_FAILED,
        PS_MSG_RPC_NOTIFY_ERROR,
        PS_MSG_SERVER_EVENT_UPCALL_FAILED,
        PS_MSG_SERVER_IPC_INFO,
        PS_MSG_SEEK_INFO,
        PS_MSG_COMPOUND_INFO,
        PS_MSG_CLIENT_OPVERSION_GET_FAILED,
        PS_MSG_CHILD_STATUS_FAILED,
        PS_MSG_PUT_INFO
);

#endif /* !_PS_MESSAGES_H__ */

